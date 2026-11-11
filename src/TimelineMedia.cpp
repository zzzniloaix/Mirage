#include "TimelineMedia.h"
#include "VMAFLadder.h"
#include "VMAFNet.h"
#include "VMAFSegments.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <map>
#include <string_view>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}

namespace timeline {

namespace {

std::string text(AVRational q) { return std::format("{}/{}", q.num, q.den); }

// What follows the last '/' of a URL or path: the name a playlist gives a
// segment and the URL its demuxer opens agree on, however each resolved it.
std::string_view file_name(std::string_view url)
{
    const std::size_t slash = url.rfind('/');
    return slash == std::string_view::npos ? url : url.substr(slash + 1);
}

// Whether FFmpeg's hls demuxer reads `url` over HTTP, as its open_url()
// tells (hls.c:660-684 of FFmpeg 8.1.1): the protocol under a "crypto" or
// "data" prefix, else the URL's, whose name starts with "http". It asks an
// HTTP server for a byte range's bytes, and seeks to the range's first byte
// after the open of any other URL (open_input(), hls.c:1439).
bool over_http(const std::string& url)
{
    std::string_view under = url;
    if (under.starts_with("crypto") && under.size() > 6 && (under[6] == '+' || under[6] == ':'))
        under.remove_prefix(7);
    else if (under.starts_with("data") && under.size() > 4 && (under[4] == '+' || under[4] == ':'))
        under.remove_prefix(5);
    const char* proto = avio_find_protocol_name(std::string(under).c_str());
    if (!proto) proto = avio_find_protocol_name(url.c_str());
    return proto && std::string_view(proto).starts_with("http");
}

// The files a demuxer opens for one input, through the io_open and io_close2
// callbacks of its context (P-F): each open that succeeded, in open order,
// with the bytes the demuxer took from the file by its close and its
// context's bytes_read, and, through a vmafsegments::SegmentWatch told of
// each open and close, the first file the demuxer went on without (D-055).
class Recorder {
public:
    Recorder() = default;
    Recorder(const Recorder&)            = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Makes `ctx` tell this recorder of each file it opens and closes, as
    // SegmentWatch::watch() does: call it once, before avformat_open_input();
    // the recorder takes the context's `opaque`, and must outlive it.
    void watch(AVFormatContext* ctx)
    {
        io_open_       = ctx->io_open;
        io_close2_     = ctx->io_close2;
        ctx->opaque    = this;
        ctx->io_open   = &Recorder::on_open;
        ctx->io_close2 = &Recorder::on_close2;
    }

    const std::vector<MediaFile>&     files() const { return files_; }
    vmafsegments::SegmentWatch&       segments()    { return segments_; }

private:
    static int on_open(AVFormatContext* s, AVIOContext** pb, const char* url, int flags,
                       AVDictionary** options)
    {
        auto* r = static_cast<Recorder*>(s->opaque);
        // A byte range's first byte comes as an option of the open, which the
        // open may consume (FFmpeg's http protocol does).
        std::optional<std::int64_t> offset;
        if (const AVDictionaryEntry* e =
                options ? av_dict_get(*options, "offset", nullptr, 0) : nullptr) {
            const std::string_view v = e->value;
            std::int64_t n = 0;
            const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
            if (ec == std::errc() && end == v.data() + v.size()) offset = n;
        }
        std::string name = url;   // as SegmentWatch names a byte range
        if (offset) name += std::format(" (from byte {})", *offset);
        const int ret = r->io_open_(s, pb, url, flags, options);
        r->segments_.opened(name, ret);
        if (ret >= 0 && *pb) {
            r->open_[*pb] = { r->files_.size(), std::move(name) };
            r->files_.push_back({ .url = url, .offset = offset, .bytes = 0 });
        }
        return ret;
    }

    static int on_close2(AVFormatContext* s, AVIOContext* pb)
    {
        auto* r = static_cast<Recorder*>(s->opaque);
        if (const auto it = r->open_.find(pb); it != r->open_.end()) {
            MediaFile& f = r->files_[it->second.first];
            // The bytes taken: the position the demuxer's reads reached, from
            // where they started, the range's first byte when the demuxer
            // seeked there (over_http()). This count differs from P-F's text,
            // AVIOContext::bytes_read at the close, on purpose: bytes_read,
            // kept beside it, also counts what the context read ahead past a
            // byte range's end, and what a short forward seek read instead of
            // seeking. Over HTTP the demuxer asks the server for the range and
            // doesn't seek, so the reads start at the context's position 0
            // (from = 0): that branch is tested by the case "TimelineMedia:
            // EXT-X-BYTERANGE segments of one file over HTTP, each range a
            // request answered with its bytes alone" of
            // tests/test_timeline_media.cpp, whose server answers each
            // range's Range header with 206 (HttpFileRule::ranges,
            // tests/HttpFileServer.h).
            const std::int64_t from = f.offset && !over_http(f.url) ? *f.offset : 0;
            f.bytes      = avio_tell(pb) - from;
            f.bytes_read = pb->bytes_read;
            r->segments_.closed(it->second.second, pb->error);
            r->open_.erase(it);
        }
        return r->io_close2_(s, pb);
    }

    using OpenFn  = int (*)(AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**);
    using CloseFn = int (*)(AVFormatContext*, AVIOContext*);

    OpenFn  io_open_   = nullptr;   // the context's own
    CloseFn io_close2_ = nullptr;
    std::vector<MediaFile> files_;
    std::map<const AVIOContext*, std::pair<std::size_t, std::string>> open_;   // index into
                                                                               // files_, name
    vmafsegments::SegmentWatch segments_;
};

// An input's demuxer, closed when it goes out of scope, before the recorder
// it tells of its closes.
struct Input {
    AVFormatContext* fmt = nullptr;
    AVPacket*        pkt = nullptr;

    Input() = default;
    ~Input()
    {
        av_packet_free(&pkt);
        avformat_close_input(&fmt);
    }
    Input(const Input&)            = delete;
    Input& operator=(const Input&) = delete;
};

// A VariantMedia without a segment map, for `why` (D-081 (c)): the reason
// and nothing else, no keyframe, corrupt packet or PTS (the owner's answer to
// Q45 (D-086)).
VariantMedia refused(std::string why)
{
    VariantMedia m;
    m.refused = std::move(why);
    return m;
}

const char* ordinal_suffix(std::size_t n)
{
    if (n % 100 >= 11 && n % 100 <= 13) return "th";
    switch (n % 10) {
    case 1:  return "st";
    case 2:  return "nd";
    case 3:  return "rd";
    default: return "th";
    }
}

// "the 3rd", for a position from 0.
std::string nth(std::size_t k) { return std::format("the {}{}", k + 1, ordinal_suffix(k + 1)); }

} // namespace

PlaylistText read_playlist(const std::string& url, std::stop_token cancel)
{
    vmafladder::Fetched f = vmafladder::read_master(url, std::move(cancel));
    return {
        .url      = url,
        .location = std::move(f.location),
        .sha256   = std::move(f.sha256),
        .text     = std::move(f.text),
        .error    = std::move(f.error),
    };
}

std::vector<ScoredFrames> reference_frames(const LadderScores& scores)
{
    std::vector<ScoredFrames> out;
    out.reserve(scores.pairs.size());
    for (const PairScores& p : scores.pairs)
        out.push_back({ .pair = p.index, .time_base = p.ref_time_base, .index = p.ref_index,
                        .pts = p.ref_pts });
    return out;
}

ScoredFrames distorted_frames(const PairScores& pair)
{
    return { .pair = pair.index, .time_base = pair.dis_time_base, .index = pair.dis_index,
             .pts = pair.dis_pts };
}

Demuxed demux(const std::string& url, std::stop_token cancel, std::string& err)
{
    err.clear();
    Recorder rec;   // outlives `in`, whose close tells it of the last closes
    Input    in;
    const auto fail = [&](std::string why) {
        err = cancel.stop_requested() ? "cancelled" : std::move(why);
        return Demuxed{};
    };

    in.fmt = avformat_alloc_context();
    if (!in.fmt) return fail("out of memory");
    // As DecodeCtx::open() (src/VMAFAnalyzer.cpp): a network read waits at
    // most 5 s, a stop on `cancel` interrupts a blocking call, and an HLS
    // input's segments are tried again as the player tries them, each file on
    // a connection of its own (src/VMAFNet.h, src/VMAFSegments.h).
    in.fmt->interrupt_callback.callback = vmafnet::stop_requested;
    in.fmt->interrupt_callback.opaque   = &cancel;
    rec.watch(in.fmt);
    AVDictionary* opts = nullptr;
    vmafnet::set_io_limits(&opts, url);
    vmafsegments::set_hls_options(&opts);
    // On failure avformat_open_input frees the context and nulls the pointer.
    int r = avformat_open_input(&in.fmt, url.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (r < 0)
        return fail(std::format("can't open {}: {}", url, vmafsegments::error_text(r)));
    if ((r = avformat_find_stream_info(in.fmt, nullptr)) < 0)
        return fail(std::format("can't read the streams of {}: {}", url,
                                vmafsegments::error_text(r)));
    // The stream the analyzer decodes: the best video stream with a decoder.
    const AVCodec* codec = nullptr;
    const int      video = av_find_best_stream(in.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (video < 0 || !codec) return fail(std::format("{} has no video stream to decode", url));

    Demuxed d;
    d.hls       = in.fmt->iformat && std::string_view(in.fmt->iformat->name) == "hls";
    d.time_base = in.fmt->streams[video]->time_base;
    in.pkt = av_packet_alloc();
    if (!in.pkt) return fail("out of memory");
    while ((r = av_read_frame(in.fmt, in.pkt)) >= 0) {
        if (in.pkt->stream_index == video)
            d.packets.push_back({
                .pts     = in.pkt->pts == AV_NOPTS_VALUE ? std::nullopt
                                                         : std::optional<std::int64_t>(in.pkt->pts),
                .pos     = in.pkt->pos,
                .key     = (in.pkt->flags & AV_PKT_FLAG_KEY) != 0,
                .corrupt = (in.pkt->flags & AV_PKT_FLAG_CORRUPT) != 0,
            });
        av_packet_unref(in.pkt);
    }
    if (r != AVERROR_EOF)
        return fail(std::format("reading {} failed: {}", url, vmafsegments::error_text(r)));
    av_packet_free(&in.pkt);
    avformat_close_input(&in.fmt);   // its last closes reach `rec`

    // A file the demuxer went on without, as it ends the analyzer's reading
    // of an input (D-055).
    rec.segments().ended();
    if (!rec.segments().lost().empty()) return fail(rec.segments().lost());
    if (rec.files().empty()) return fail(std::format("{} was read from no file", url));
    // The input's own open, the playlist's for an HLS input, comes first
    // (avformat_open_input()); the files after it are its segments.
    d.files.assign(rec.files().begin() + 1, rec.files().end());
    return d;
}

VariantMedia map_frames(const Demuxed& demuxed, const hls::MediaTimeline* playlist,
                        const std::vector<ScoredFrames>& scored)
{
    const std::vector<MediaPacket>& packets = demuxed.packets;
    for (std::size_t i = 0; i < packets.size(); ++i)
        if (!packets[i].pts)
            return refused(std::format("{} video packet read has no PTS, so its frame can't be "
                                       "ordered",
                                       nth(i)));

    // Each segment's packets in PTS order, the segments in read order (P-G);
    // an input that isn't a playlist's is one run.
    std::vector<std::size_t> order;   // packets, in frame order
    order.reserve(packets.size());
    const auto by_pts = [&](std::size_t a, std::size_t b) {
        return *packets[a].pts < *packets[b].pts;
    };
    VariantMedia m;
    if (!playlist) {
        if (demuxed.hls)
            return refused("it was read as an HLS playlist, and no playlist was given for its "
                           "segments");
        for (std::size_t i = 0; i < packets.size(); ++i) order.push_back(i);
        std::ranges::stable_sort(order, by_pts);
    } else {
        if (!demuxed.hls) return refused("it wasn't read by FFmpeg's hls demuxer");
        const std::vector<hls::Segment>& segs = playlist->segments;
        const std::vector<MediaFile>&    files = demuxed.files;
        // Segment k holds the positions [end[k - 1], end[k]) of the stream
        // the demuxer reads (end[-1] = 0).
        std::vector<std::int64_t> end(segs.size());
        std::int64_t at = 0;
        for (std::size_t k = 0; k < std::min(segs.size(), files.size()); ++k) {
            const hls::Segment& s = segs[k];
            const MediaFile&    f = files[k];
            if (file_name(f.url) != file_name(s.uri))
                return refused(std::format("{} file FFmpeg's hls demuxer read after the playlist, "
                                           "{}, isn't its segment {}, {}",
                                           nth(k), f.url, s.seq, s.uri));
            if (!s.range.whole()) {
                if (f.offset != s.range.offset)
                    return refused(std::format("FFmpeg's hls demuxer read segment {} from byte {}, "
                                               "where its EXT-X-BYTERANGE starts at byte {}",
                                               s.seq, f.offset.value_or(0), s.range.offset));
                if (f.bytes != s.range.length)
                    return refused(std::format("FFmpeg's hls demuxer took {} bytes of segment {}, "
                                               "whose EXT-X-BYTERANGE has {}",
                                               f.bytes, s.seq, s.range.length));
            } else if (f.offset) {
                return refused(std::format("FFmpeg's hls demuxer read segment {}, which has no "
                                           "EXT-X-BYTERANGE, from byte {}",
                                           s.seq, *f.offset));
            }
            at += f.bytes;
            end[k] = at;
        }
        if (files.size() != segs.size())
            return refused(std::format("FFmpeg's hls demuxer read {} {} after the playlist, for "
                                       "its {} {}",
                                       files.size(), files.size() == 1 ? "file" : "files",
                                       segs.size(), segs.size() == 1 ? "segment" : "segments"));
        std::vector<std::vector<std::size_t>> in_segment(segs.size());
        for (std::size_t i = 0; i < packets.size(); ++i) {
            const std::int64_t pos = packets[i].pos;
            if (pos < 0)
                return refused(std::format("{} video packet read has no byte position", nth(i)));
            const auto k = std::ranges::upper_bound(end, pos) - end.begin();
            if (k == static_cast<std::ptrdiff_t>(end.size()))
                return refused(std::format("{} video packet read, at byte {}, lies past the "
                                           "segments, which end at byte {}",
                                           nth(i), pos, at));
            in_segment[static_cast<std::size_t>(k)].push_back(i);
        }
        m.segments.reserve(segs.size());
        for (std::size_t k = 0; k < segs.size(); ++k) {
            std::vector<std::size_t>& run = in_segment[k];
            std::ranges::stable_sort(run, by_pts);
            m.segments.push_back({
                .seq         = segs[k].seq,
                .first_index = static_cast<std::int64_t>(order.size()),
                .frames      = static_cast<std::int64_t>(run.size()),
                .bytes       = files[k].bytes,
            });
            order.insert(order.end(), run.begin(), run.end());
        }
    }

    m.pts.reserve(order.size());
    for (std::size_t f = 0; f < order.size(); ++f) {
        const MediaPacket& p = packets[order[f]];
        m.pts.push_back(*p.pts);
        if (p.key) m.keyframes.push_back(static_cast<std::int64_t>(f));
        if (p.corrupt) m.corrupt.push_back(static_cast<std::int64_t>(f));
    }

    // Every scored frame's PTS, to the tick (P-G; D-081 (c)).
    const auto frames = static_cast<std::int64_t>(m.pts.size());
    for (const ScoredFrames& s : scored) {
        if (s.index.empty() && s.pts.empty()) continue;   // nothing scored, nothing to check
        if (av_cmp_q(s.time_base, demuxed.time_base) != 0)
            return refused(std::format("pair {}'s report gives the input's time base as {}, the "
                                       "demux pass read it in {}",
                                       s.pair, text(s.time_base), text(demuxed.time_base)));
        if (s.index.size() != s.pts.size())
            return refused(std::format("pair {}'s report gives {} positions for {} PTS", s.pair,
                                       s.index.size(), s.pts.size()));
        for (std::size_t k = 0; k < s.index.size(); ++k) {
            const std::int64_t i = s.index[k];
            if (i < 0 || i >= frames)
                return refused(std::format("pair {}'s report scores frame {} of the input, which "
                                           "the demux pass doesn't have: it read {} frames",
                                           s.pair, i, frames));
            if (m.pts[static_cast<std::size_t>(i)] != s.pts[k])
                return refused(std::format("frame {} of the input has PTS {} in pair {}'s report "
                                           "and {} in the demux pass, in ticks of {}",
                                           i, s.pts[k], s.pair, m.pts[static_cast<std::size_t>(i)],
                                           text(demuxed.time_base)));
        }
    }
    return m;
}

VariantMedia read_media(const std::string& url, const hls::MediaTimeline* playlist,
                        const std::vector<ScoredFrames>& scored, std::stop_token cancel,
                        std::string& err)
{
    err.clear();
    // FFmpeg's hls demuxer would wait for a live playlist's next segments;
    // the timeline build refuses one (D-081 (a)).
    if (playlist && playlist->live()) {
        err = std::format("{} is a live playlist, without EXT-X-ENDLIST or "
                          "EXT-X-PLAYLIST-TYPE:VOD, which the timeline doesn't read (D-081 (a))",
                          url);
        return {};
    }
    const Demuxed d = demux(url, std::move(cancel), err);
    if (!err.empty()) return {};
    return map_frames(d, playlist, scored);
}

std::optional<SegmentBoundary> next_boundary(const VariantMedia& media,
                                             const hls::MediaTimeline& playlist,
                                             std::int64_t frame)
{
    const std::vector<SegmentFrames>& segs = media.segments;
    for (std::size_t k = 0; k < segs.size(); ++k) {
        if (frame < segs[k].first_index || frame >= segs[k].first_index + segs[k].frames)
            continue;
        if (k + 1 == segs.size()) return std::nullopt;
        const SegmentFrames& next = segs[k + 1];
        const int            at   = playlist.index_of(next.seq);
        const bool disc = at >= 0 && playlist.segments[static_cast<std::size_t>(at)].discontinuity;
        return SegmentBoundary{ .seq           = next.seq,
                                .first_index   = next.first_index,
                                .discontinuity = disc };
    }
    return std::nullopt;
}

} // namespace timeline
