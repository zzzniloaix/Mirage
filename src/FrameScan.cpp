#include "FrameScan.h"
#include "FfOpen.h"
#include "Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <string_view>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

// ── Pure helpers ──────────────────────────────────────────────────────────────

namespace framescan {

namespace {

// A frame's time for bitrate windows: pts, else dts.
double time_of(const Frame& f)
{
    return std::isfinite(f.pts) ? f.pts : f.dts;
}

// A frame exactly `window` before another falls outside that one's window
// whatever the rounding of the two timestamps (frames are milliseconds
// apart: no other frame is this close to a window's edge).
constexpr double kEps = 1e-6;

using Keyed = std::vector<std::pair<double, std::uint32_t>>;   // (time, index)

// The frames that have a time, by time; ties in index order.
Keyed by_time(std::span<const Frame> frames)
{
    Keyed v;
    v.reserve(frames.size());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const double t = time_of(frames[i]);
        if (std::isfinite(t)) v.emplace_back(t, static_cast<std::uint32_t>(i));
    }
    std::sort(v.begin(), v.end());
    return v;
}

} // namespace

std::vector<Frame> display_order(std::span<const Frame> frames)
{
    Keyed key;
    key.reserve(frames.size());
    double last = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (std::isfinite(frames[i].pts)) last = frames[i].pts;
        key.emplace_back(last, static_cast<std::uint32_t>(i));
    }
    std::sort(key.begin(), key.end());
    std::vector<Frame> out;
    out.reserve(frames.size());
    for (const auto& k : key) out.push_back(frames[k.second]);
    return out;
}

std::vector<double> rolling_bitrate(std::span<const Frame> frames, double window)
{
    std::vector<double> out(frames.size(), kNaN);
    if (!(window > 0.0)) return out;
    const Keyed order = by_time(frames);
    std::uint64_t bytes = 0;
    std::size_t   lo    = 0;   // the oldest frame in the window
    for (std::size_t j = 0; j < order.size(); ++j) {
        bytes += frames[order[j].second].size;
        const double from = order[j].first - window + kEps;
        while (lo < j && order[lo].first <= from) {
            bytes -= frames[order[lo].second].size;
            ++lo;
        }
        out[order[j].second] = static_cast<double>(bytes) * 8.0 / window;
    }
    return out;
}

double peak_bitrate(std::span<const Frame> frames, double window)
{
    double peak = 0.0;
    for (const double r : rolling_bitrate(frames, window))
        if (std::isfinite(r)) peak = std::max(peak, r);
    return peak;
}

GopSummary summarize(std::span<const Frame> frames, double peak_window)
{
    GopSummary g;
    g.frames = frames.size();
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    double hi_duration = 0.0;   // of the frame shown last
    std::size_t timed = 0;
    for (const Frame& f : frames) {
        switch (f.type) {
        case 'I': ++g.i; break;
        case 'P': ++g.p; break;
        case 'B': ++g.b; break;
        case 'S': ++g.s; break;
        default:  ++g.unknown; break;
        }
        g.keyframes += f.key ? 1 : 0;
        g.bytes     += f.size;
        const double t = time_of(f);
        if (!std::isfinite(t)) continue;
        ++timed;
        lo = std::min(lo, t);
        if (t >= hi) {
            hi          = t;
            hi_duration = f.duration;
        }
    }

    // Keyframe to keyframe in display order. Seconds pts to pts, else dts to
    // dts (AVI with B-frames: its keyframes come without a pts).
    const std::vector<Frame> shown = display_order(frames);
    std::size_t prev = 0, timed_intervals = 0;
    bool   have_prev = false;
    double sum_frames = 0.0, sum_s = 0.0;
    for (std::size_t k = 0; k < shown.size(); ++k) {
        if (!shown[k].key) continue;
        if (have_prev) {
            const int n = static_cast<int>(k - prev);
            g.min_frames = g.intervals == 0 ? n : std::min(g.min_frames, n);
            g.max_frames = g.intervals == 0 ? n : std::max(g.max_frames, n);
            sum_frames += n;
            ++g.intervals;
            const Frame& a = shown[prev];
            const Frame& b = shown[k];
            const double s = std::isfinite(a.pts) && std::isfinite(b.pts) ? b.pts - a.pts : b.dts - a.dts;
            if (std::isfinite(s)) {
                g.min_s = timed_intervals == 0 ? s : std::min(g.min_s, s);
                g.max_s = timed_intervals == 0 ? s : std::max(g.max_s, s);
                sum_s += s;
                ++timed_intervals;
            }
        }
        prev      = k;
        have_prev = true;
    }
    if (g.intervals > 0) g.avg_frames = sum_frames / static_cast<double>(g.intervals);
    if (timed_intervals > 0) g.avg_s = sum_s / static_cast<double>(timed_intervals);

    // First to last frame time plus the last one's duration — unknown, the
    // average frame interval.
    if (timed >= 2 && hi > lo)
        g.seconds = hi_duration > 0.0 ? hi - lo + hi_duration
                                      : (hi - lo) * static_cast<double>(timed) / static_cast<double>(timed - 1);
    else if (timed >= 1)
        g.seconds = hi_duration;
    if (g.seconds > 0.0) g.mean_bps = static_cast<double>(g.bytes) * 8.0 / g.seconds;
    g.peak_bps = peak_bitrate(frames, peak_window);
    return g;
}

int h264_ref_idc(const std::uint8_t* data, std::size_t size, int nal_length_size)
{
    if (!data) return -1;
    auto slice_ref = [](std::uint8_t header) {   // -1: not a slice of the primary picture
        const int type = header & 0x1F;
        return (type == 1 || type == 5) ? (header >> 5) & 3 : -1;
    };
    if (nal_length_size >= 1 && nal_length_size <= 4) {
        const auto n = static_cast<std::size_t>(nal_length_size);
        std::size_t i = 0;
        bool framed = true;
        while (i + n < size) {
            std::size_t len = 0;
            for (std::size_t k = 0; k < n; ++k) len = len << 8 | data[i + k];
            i += n;
            if (len == 0 || len > size - i) {   // not length-prefixed after all
                framed = false;
                break;
            }
            if (const int r = slice_ref(data[i]); r >= 0) return r;
            i += len;
        }
        if (framed) return -1;
    }
    // Annex B: 00 00 01 before every NAL unit (emulation prevention keeps the
    // sequence out of their payloads).
    for (std::size_t i = 0; i + 3 < size; ++i) {
        if (data[i] != 0 || data[i + 1] != 0 || data[i + 2] != 1) continue;
        if (const int r = slice_ref(data[i + 3]); r >= 0) return r;
        i += 2;
    }
    return -1;
}

} // namespace framescan

// ── FrameScan ─────────────────────────────────────────────────────────────────

namespace {

std::string av_error(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

char type_char(int pict_type)
{
    switch (pict_type) {
    case AV_PICTURE_TYPE_I:  return 'I';
    case AV_PICTURE_TYPE_P:  return 'P';
    case AV_PICTURE_TYPE_B:
    case AV_PICTURE_TYPE_BI: return 'B';
    case AV_PICTURE_TYPE_S:
    case AV_PICTURE_TYPE_SI:
    case AV_PICTURE_TYPE_SP: return 'S';
    default:                 return '?';
    }
}

bool is_video(const AVFormatContext* fmt, int idx)
{
    return fmt && idx >= 0 && idx < static_cast<int>(fmt->nb_streams)
        && fmt->streams[idx]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
}

// Only stream `idx` of `fmt` is read from now on.
void keep_only(AVFormatContext* fmt, int idx)
{
    for (unsigned i = 0; i < fmt->nb_streams; ++i)
        fmt->streams[i]->discard = static_cast<int>(i) == idx ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
}

// The scan thread's FFmpeg objects, freed on every way out.
struct ScanContext {
    AVFormatContext*      fmt    = nullptr;
    AVCodecContext*       codec  = nullptr;   // the stream's parameters for the parser; never opened
    AVCodecParserContext* parser = nullptr;
    AVPacket*             pkt    = nullptr;

    ScanContext() = default;
    ScanContext(const ScanContext&)            = delete;
    ScanContext& operator=(const ScanContext&) = delete;
    ~ScanContext()
    {
        av_packet_free(&pkt);
        if (parser) av_parser_close(parser);
        avcodec_free_context(&codec);
        if (fmt) avformat_close_input(&fmt);
    }
};

} // namespace

FrameScan::~FrameScan() { stop(); }

void FrameScan::start(const std::string& url, int stream, const std::string& master)
{
    stop();
    {
        std::lock_guard lk(mtx_);
        std::deque<framescan::Frame>().swap(frames_);   // frees the memory too
        error_.clear();
        info_.reset();
        count_.store(0);
        progress_.store(0.0);
        state_.store(State::Running);
        ++version_;
    }
    thread_ = std::jthread([this, url, stream, master](std::stop_token st) {
        run(st, url, stream, master);
    });
}

void FrameScan::stop()
{
    thread_ = {};   // request_stop() then join
}

bool FrameScan::done() const
{
    const State s = state_.load();
    return s == State::Done || s == State::Failed || s == State::Stopped;
}

std::string FrameScan::error() const
{
    std::lock_guard lk(mtx_);
    return error_;
}

std::optional<FrameScan::StreamInfo> FrameScan::info() const
{
    std::lock_guard lk(mtx_);
    return info_;
}

std::size_t FrameScan::copy(std::vector<framescan::Frame>& out, std::size_t from) const
{
    std::lock_guard lk(mtx_);
    if (from < frames_.size())
        out.insert(out.end(), frames_.begin() + static_cast<std::ptrdiff_t>(from), frames_.end());
    return frames_.size();
}

void FrameScan::append(std::vector<framescan::Frame>& batch, double progress)
{
    if (batch.empty()) return;
    {
        std::lock_guard lk(mtx_);
        frames_.insert(frames_.end(), batch.begin(), batch.end());
        count_.store(frames_.size());
        if (progress > progress_.load()) progress_.store(std::min(progress, 1.0));
        ++version_;
    }
    batch.clear();
}

void FrameScan::finish(State s, std::string why)
{
    std::lock_guard lk(mtx_);
    error_ = std::move(why);
    if (s == State::Done) progress_.store(1.0);
    state_.store(s);
    ++version_;
}

void FrameScan::run(std::stop_token st, std::string url, int stream, std::string master)
{
    using Clock = std::chrono::steady_clock;
    const auto  t0 = Clock::now();
    ScanContext c;

    auto fail = [&](std::string why) {
        logger::warn("FrameScan: stream {} of '{}': {}", stream, url, why);
        finish(State::Failed, std::move(why));
    };

    int ret = ffopen::open_input(&c.fmt, url, st, NetInitiator::Other, master);
    if (st.stop_requested()) return finish(State::Stopped);
    if (ret < 0) return fail(std::format("cannot open the input: {}", av_error(ret)));

    // Only the stream scanned is read. Discarded before probing, so HLS
    // fetches no other variant's segments; a stream that only appears once
    // packets are read (no header) is probed with the rest.
    const bool listed = stream >= 0 && stream < static_cast<int>(c.fmt->nb_streams);
    if (listed) keep_only(c.fmt, stream);
    ret = avformat_find_stream_info(c.fmt, nullptr);
    if (st.stop_requested()) return finish(State::Stopped);
    if (!is_video(c.fmt, stream))
        return fail(ret < 0 ? std::format("probing the input failed: {}", av_error(ret))
                            : std::format("stream {} is not a video stream", stream));
    if (!listed) keep_only(c.fmt, stream);

    AVStream*                vs  = c.fmt->streams[stream];
    const AVCodecParameters* par = vs->codecpar;
    c.codec = avcodec_alloc_context3(nullptr);
    c.pkt   = av_packet_alloc();
    if (!c.codec || !c.pkt || avcodec_parameters_to_context(c.codec, par) < 0)
        return fail("out of memory");
    c.parser = av_parser_init(par->codec_id);
    if (c.parser) c.parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
    // H.264: the NAL unit length size of avcC packets (0: Annex B).
    const bool h264       = par->codec_id == AV_CODEC_ID_H264;
    const int  nal_length = h264 && par->extradata && par->extradata_size >= 7 && par->extradata[0] == 1
                          ? (par->extradata[4] & 3) + 1 : 0;

    const double tb = av_q2d(vs->time_base);
    StreamInfo info;
    info.stream   = stream;
    info.codec_id = static_cast<int>(par->codec_id);
    info.codec    = avcodec_get_name(par->codec_id);
    info.parser   = c.parser != nullptr;
    const AVCodecDescriptor* desc = avcodec_descriptor_get(par->codec_id);
    info.intra_only = desc && (desc->props & AV_CODEC_PROP_INTRA_ONLY);
    // FFmpeg's own pick (as the ffmpeg tool's): the average rate of a short
    // clip leaves out the last frame's duration.
    const AVRational fr = av_guess_frame_rate(c.fmt, vs, nullptr);
    info.frame_rate = fr.num > 0 && fr.den > 0 ? av_q2d(fr) : 0.0;
    if (vs->start_time != AV_NOPTS_VALUE)
        info.start = static_cast<double>(vs->start_time) * tb;
    else if (c.fmt->start_time != AV_NOPTS_VALUE)
        info.start = static_cast<double>(c.fmt->start_time) / AV_TIME_BASE;
    if (vs->duration != AV_NOPTS_VALUE && vs->duration > 0)
        info.duration = static_cast<double>(vs->duration) * tb;
    else if (c.fmt->duration > 0)
        info.duration = static_cast<double>(c.fmt->duration) / AV_TIME_BASE;
    // HLS / DASH: positions are inside segments, and the size is the playlist's.
    const std::string_view demuxer = c.fmt->iformat->name;
    if (c.fmt->pb && demuxer != "hls" && demuxer != "dash") {
        const std::int64_t size = avio_size(c.fmt->pb);
        if (size > 0) info.file_size = size;
    }
    {
        std::lock_guard lk(mtx_);
        info_ = info;
        ++version_;
    }

    const double t_start = std::isfinite(info.start) ? info.start : 0.0;
    auto progress_at = [&](const framescan::Frame& f) {
        if (info.file_size > 0 && f.pos >= 0)
            return static_cast<double>(f.pos + f.size) / static_cast<double>(info.file_size);
        if (info.duration > 0.0 && std::isfinite(f.pts)) return (f.pts - t_start) / info.duration;
        return 0.0;
    };

    std::vector<framescan::Frame> batch;
    batch.reserve(kBatch);
    auto          flushed = Clock::now();
    std::uint64_t bytes   = 0;
    auto flush = [&] {
        if (!batch.empty()) append(batch, progress_at(batch.back()));
        flushed = Clock::now();
    };

    while (!st.stop_requested()) {
        ret = av_read_frame(c.fmt, c.pkt);
        if (ret == AVERROR(EAGAIN)) {   // nothing yet (live): what was read still goes out in time
            if (Clock::now() - flushed >= std::chrono::milliseconds(kFlushMs)) flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (ret < 0) break;
        if (c.pkt->stream_index == stream) {
            const AVPacket*  p = c.pkt;
            framescan::Frame f;
            if (p->pts != AV_NOPTS_VALUE) f.pts = static_cast<double>(p->pts) * tb;
            if (p->dts != AV_NOPTS_VALUE) f.dts = static_cast<double>(p->dts) * tb;
            if (p->duration > 0) f.duration = static_cast<double>(p->duration) * tb;
            f.pos     = p->pos;
            f.size    = static_cast<std::uint32_t>(std::max(p->size, 0));
            f.key     = (p->flags & AV_PKT_FLAG_KEY) != 0;
            f.discard = (p->flags & AV_PKT_FLAG_DISCARD) != 0;
            if (c.parser && p->size > 0) {   // an empty buffer would flush the parser
                std::uint8_t* out      = nullptr;
                int           out_size = 0;
                // A parser that finds no picture header in the frame leaves
                // these as they are: '?' rather than av_parser_init's I.
                c.parser->pict_type = AV_PICTURE_TYPE_NONE;
                c.parser->key_frame = -1;
                av_parser_parse2(c.parser, c.codec, &out, &out_size, p->data, p->size,
                                 AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
                if (out_size > 0) {   // always, complete frames being passed through at once
                    f.type = type_char(c.parser->pict_type);
                    if (c.parser->key_frame == 1) f.key = true;
                }
            }
            if (h264 && p->size > 0) {
                const int r = framescan::h264_ref_idc(p->data, static_cast<std::size_t>(p->size), nal_length);
                f.ref = static_cast<std::int8_t>(r < 0 ? -1 : r > 0 ? 1 : 0);
            }
            batch.push_back(f);
            bytes += f.size;
        }
        av_packet_unref(c.pkt);
        if (batch.size() >= kBatch || Clock::now() - flushed >= std::chrono::milliseconds(kFlushMs)) flush();
    }
    flush();

    const double      secs = std::chrono::duration<double>(Clock::now() - t0).count();
    const std::size_t n    = count_.load();
    // A stop first: an interrupted read can end as an end of file.
    if (st.stop_requested()) {
        logger::info("FrameScan: stream {} — stopped after {} frames ({:.2f} s)", stream, n, secs);
        return finish(State::Stopped);
    }
    // An I/O error can end as an end of file too.
    if (ret == AVERROR_EOF && c.fmt->pb && c.fmt->pb->error < 0) ret = c.fmt->pb->error;
    if (ret != AVERROR_EOF) return fail(std::format("read error after {} frames: {}", n, av_error(ret)));
    logger::info("FrameScan: stream {} ({}) — {} frames, {:.1f} MB in {:.2f} s", stream, info.codec, n,
                 static_cast<double>(bytes) / 1e6, secs);
    finish(State::Done);
}
