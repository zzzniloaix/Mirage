#pragma once

// The files FFmpeg's hls demuxer reads for an input of the VMAF analyzer
// (DECISIONS.md D-055). The demuxer goes on without a file in two cases, and
// its reader gets no error in either (hls.c of FFmpeg 8.1.1,
// read_data_continuous()): a segment it can't open, once it has tried it
// again seg_max_retry times ("failed too many times, skipping"), and the rest
// of a file whose read fails. A SegmentWatch sees each file an input's
// demuxer opens and closes, through the input's io_open and io_close2
// callbacks (watch()), and keeps the first file the demuxer went on without,
// which ends the analysis's reading of that input as a read error does.
// Header-only, as src/VMAFNet.h.

#include <format>
#include <map>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

namespace vmafsegments {

// How many times the hls demuxer tries a segment again before it goes on
// without it: the player's value (src/Demuxer.cpp). FFmpeg's default is 0.
inline constexpr int kSegMaxRetry = 10;

// Adds the hls demuxer's options to *opts: kSegMaxRetry, and no persistent
// connection or second connection for the next segment, so that the demuxer
// opens each file through io_open when it reads it, one at a time, and
// closes it when its reads end (SegmentWatch::watch()). Other demuxers
// leave them unused.
inline void set_hls_options(AVDictionary** opts)
{
    av_dict_set_int(opts, "seg_max_retry", kSegMaxRetry, 0);
    av_dict_set(opts, "http_persistent", "0", 0);
    av_dict_set(opts, "http_multiple", "0", 0);
}

inline std::string error_text(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return buf;
}

class SegmentWatch {
public:
    SegmentWatch() = default;
    SegmentWatch(const SegmentWatch&)            = delete;
    SegmentWatch& operator=(const SegmentWatch&) = delete;

    // An open of `file` that failed with `err`, or succeeded (err >= 0). A
    // file is gone without once 1 + kSegMaxRetry opens of it have failed
    // with none succeeding, the tries the hls demuxer makes. An open a
    // cancel ended (AVERROR_EXIT) loses nothing.
    void opened(const std::string& file, int err)
    {
        if (err >= 0) {
            failed_.erase(file);
            return;
        }
        if (err == AVERROR_EXIT) return;
        Failures& f = failed_[file];
        ++f.tries;
        f.last = err;
        if (f.tries > kSegMaxRetry) lose(never_opened(file, f));
    }

    // The close of `file`, open, whose reads stopped on `err`, the error its
    // AVIOContext keeps (0: none). A read a cancel ended loses nothing.
    void closed(const std::string& file, int err)
    {
        if (err < 0 && err != AVERROR_EXIT)
            lose(std::format("the hls demuxer went on without the rest of {} after a read "
                             "error: {}",
                             file, error_text(err)));
    }

    // The input's end: a file none of whose opens succeeded was gone without,
    // whatever the number of tries.
    void ended()
    {
        for (const auto& [file, f] : failed_) lose(never_opened(file, f));
    }

    // The first file the demuxer went on without, as an error; empty: none.
    const std::string& lost() const { return lost_; }

    // Makes `ctx` tell this watch of each file it opens and closes. Call it
    // once, on a context of avformat_alloc_context(), before
    // avformat_open_input(); the watch takes the context's `opaque`, and must
    // outlive the context.
    void watch(AVFormatContext* ctx)
    {
        io_open_       = ctx->io_open;
        io_close2_     = ctx->io_close2;
        ctx->opaque    = this;
        ctx->io_open   = &SegmentWatch::on_open;
        ctx->io_close2 = &SegmentWatch::on_close2;
    }

private:
    struct Failures {
        int tries = 0;
        int last  = 0;   // the last open's error
    };

    static std::string never_opened(const std::string& file, const Failures& f)
    {
        return std::format("the hls demuxer went on without {}, which it couldn't open in {} "
                           "{}: {}",
                           file, f.tries, f.tries == 1 ? "try" : "tries", error_text(f.last));
    }

    void lose(std::string why)
    {
        if (lost_.empty()) lost_ = std::move(why);
    }

    static int on_open(AVFormatContext* s, AVIOContext** pb, const char* url, int flags,
                       AVDictionary** options)
    {
        auto* w = static_cast<SegmentWatch*>(s->opaque);
        // A byte range of a file (EXT-X-BYTERANGE) is a file of its own: the
        // demuxer gives its start as an option, which the open consumes.
        std::string file = url;
        if (const AVDictionaryEntry* offset =
                options ? av_dict_get(*options, "offset", nullptr, 0) : nullptr)
            file += std::format(" (from byte {})", offset->value);
        const int r = w->io_open_(s, pb, url, flags, options);
        w->opened(file, r);
        if (r >= 0 && *pb) w->open_[*pb] = std::move(file);
        return r;
    }

    static int on_close2(AVFormatContext* s, AVIOContext* pb)
    {
        auto* w = static_cast<SegmentWatch*>(s->opaque);
        if (const auto it = w->open_.find(pb); it != w->open_.end()) {
            w->closed(it->second, pb->error);
            w->open_.erase(it);
        }
        return w->io_close2_(s, pb);
    }

    using OpenFn  = int (*)(AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**);
    using CloseFn = int (*)(AVFormatContext*, AVIOContext*);

    OpenFn                                    io_open_   = nullptr;   // the context's own
    CloseFn                                   io_close2_ = nullptr;
    std::map<const AVIOContext*, std::string> open_;     // the open files, by their context
    std::map<std::string, Failures>           failed_;   // files no open of has succeeded yet
    std::string                               lost_;
};

} // namespace vmafsegments
