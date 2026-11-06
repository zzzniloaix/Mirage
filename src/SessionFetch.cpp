#include "SessionFetch.h"

#include "NetworkLogger.h"
#include "VMAFNet.h"

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <vector>

namespace timeline {

namespace {

std::string error_text(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return buf;
}

bool is_http(const std::string& url) { return url.starts_with("http://") || url.starts_with("https://"); }

// NetworkLogger's row as the session keeps it, its times from `t0`, the
// session's start on now_us()'s clock.
RequestRow row_of(const NetRequest& r, std::int64_t t0)
{
    const auto from_start = [t0](std::int64_t t) { return t < 0 ? std::int64_t{ -1 } : t - t0; };
    RequestRow row;
    row.url            = r.url;
    row.t_start        = from_start(r.t_start);
    row.t_sent         = from_start(r.t_sent);
    row.t_first_byte   = from_start(r.t_first_byte);
    row.t_end          = from_start(r.t_end);
    row.bytes          = r.bytes;
    row.status         = r.status;
    row.status_text    = r.status_text;
    row.failed         = r.failed;
    row.error          = r.error;
    row.content_length = r.content_length;
    row.initiator      = net_initiator_name(r.initiator);
    return row;
}

// Reads the open `pb` to its end, or `want` bytes of it (-1: all): the
// fetch's error, or empty.
std::string read_all(AVIOContext* pb, std::int64_t want)
{
    std::vector<unsigned char> buf(64 * 1024);
    std::int64_t got = 0;
    int          n   = 0;
    while (want < 0 || got < want) {
        const std::int64_t ask = want < 0 ? static_cast<std::int64_t>(buf.size())
                                          : std::min<std::int64_t>(static_cast<std::int64_t>(buf.size()), want - got);
        n = avio_read(pb, buf.data(), static_cast<int>(ask));
        if (n <= 0) break;
        got += n;
    }
    // A read's error, which a body cut short gives, and not the end of the
    // file, which AVIOContext::error doesn't hold.
    const int err = pb->error < 0 ? pb->error : n < 0 && n != AVERROR_EOF ? n : 0;
    if (err < 0) return std::format("{} after {} bytes", error_text(err), got);
    if (want >= 0 && got < want)
        return std::format("the file ended after {} of its byte range's {} bytes", got, want);
    return {};
}

// Fetches `r` once, the logger installed: the fetch's error, or empty.
std::string fetch_one(NetworkLogger& logger, const Request& r, std::stop_token& cancel)
{
    const bool ranged = r.range_offset >= 0 && r.range_length >= 0;
    const bool http   = is_http(r.url);
    AVDictionary* opts = nullptr;
    vmafnet::set_io_limits(&opts, r.url);
    if (ranged && http) {
        // http.c asks for bytes=offset-(end_offset - 1).
        av_dict_set_int(&opts, "offset", r.range_offset, 0);
        if (r.range_length > 0) av_dict_set_int(&opts, "end_offset", r.range_offset + r.range_length, 0);
    }
    // A local file's range is given to the logger, which has no Range
    // header to take it from (src/NetworkLogger.h). Its row is NetworkLogger's
    // as it makes it: ended once the file is open (ev_open_end(),
    // src/NetworkLogger.cpp), with the bytes the read below gets counted by
    // poll() and at the close, so that its event lasts the open, not the
    // read. What a fetch of local files measures is pending the owner
    // (src/SessionFetch.h).
    const bool local_range = ranged && !http && r.range_length > 0;
    NetworkLogger::track_open_begin(NetInitiator::Playback, r.url.c_str(), local_range ? r.range_offset : -1,
                                    local_range ? r.range_offset + r.range_length - 1 : -1);
    const AVIOInterruptCB cb{ vmafnet::stop_requested, &cancel };
    AVIOContext* pb  = nullptr;
    const int    ret = avio_open2(&pb, r.url.c_str(), AVIO_FLAG_READ, &cb, &opts);
    av_dict_free(&opts);
    NetworkLogger::track_open_end(ret, pb);
    if (ret < 0) return error_text(ret);

    std::string err;
    if (ranged && !http && r.range_offset > 0) {
        if (const std::int64_t at = avio_seek(pb, r.range_offset, SEEK_SET); at < 0)
            err = std::format("the seek to its byte range's first byte, {}, failed: {}", r.range_offset,
                              error_text(static_cast<int>(at)));
    }
    if (err.empty()) err = read_all(pb, ranged ? r.range_length : -1);
    logger.poll();   // the body's end, before the close (see the header)
    NetworkLogger::track_close(pb);
    avio_closep(&pb);
    return err;
}

} // namespace

FetchResult fetch_session(std::vector<Request> files, std::stop_token cancel)
{
    FetchResult out;
    if (NetworkLogger::instance()) {
        out.error = "another NetworkLogger is installed: the session fetch installs one of its own for its "
                    "files alone (the design's P-N), and FFmpeg's log has one callback for the process";
        return out;
    }
    NetworkLogger logger;
    logger.install();
    // The session's start: NetworkLogger's clock, and the UTC anchor sampled
    // with it (D-073), written as RFC 3339 UTC to the microsecond with the
    // zone Z, a form pending the owner (FetchResult::anchor_utc).
    const std::int64_t t0 = NetworkLogger::now_us();
    out.anchor_utc = std::format(
        "{:%FT%T}Z", std::chrono::floor<std::chrono::microseconds>(std::chrono::system_clock::now()));
    const std::size_t total = files.size();
    for (std::size_t k = 0; k < total; ++k) {
        if (cancel.stop_requested()) {
            out.error = std::format("a stop was requested: the fetch ended before its file {} of {}", k + 1,
                                    total);
            break;
        }
        Request& r = files[k];
        r.chain.clear();
        r.error = fetch_one(logger, r, cancel);
        // The rows of this file alone: the logger was cleared after the one
        // before it, and of the rows it holds only those tagged
        // NetInitiator::Playback are the fetch's. Every row of the fetch's
        // own open is, its redirects', a failed open's and a local file's
        // included (track_open_begin()); a request that another thread makes
        // through FFmpeg meanwhile, on a context nothing hooks or tracks, is
        // tagged Other (src/NetworkLogger.cpp) and left out, FFmpeg's log
        // having one callback for the process (design §15, R4).
        std::vector<NetRequest> rows;
        std::uint64_t           oldest = 0;
        logger.copy_changed_since(0, rows, oldest);
        logger.clear();
        for (const NetRequest& row : rows)
            if (row.initiator == NetInitiator::Playback) r.chain.push_back(row_of(row, t0));
        out.files.push_back(std::move(r));
        if (cancel.stop_requested()) {
            out.error = std::format("a stop was requested: the fetch ended at its file {} of {}", k + 1, total);
            break;
        }
    }
    logger.uninstall();
    return out;
}

} // namespace timeline
