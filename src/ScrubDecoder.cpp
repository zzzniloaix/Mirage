#include "ScrubDecoder.h"
#include "Logger.h"
#include "FfOpen.h"
#include "KeyframeIndex.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

namespace {

using Clock = std::chrono::steady_clock;

double ms_between(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// What frames are decoded from: one video stream of a context that is
// either one HLS variant's own media playlist (direct) or opened the way
// playback was (stream indices are playback's).
struct Source {
    AVFormatContext* fmt    = nullptr;
    AVCodecContext*  codec  = nullptr;
    int              idx    = -1;      // the stream in fmt
    bool             direct = false;

    void close()
    {
        avcodec_free_context(&codec);
        if (fmt) avformat_close_input(&fmt);
        idx    = -1;
        direct = false;
    }
};

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

AVCodecContext* open_codec(const AVStream* stream)
{
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return nullptr;
    AVCodecContext* c = avcodec_alloc_context3(codec);
    if (!c) return nullptr;
    if (avcodec_parameters_to_context(c, stream->codecpar) < 0) {
        avcodec_free_context(&c);
        return nullptr;
    }
    c->thread_count = 2;   // don't starve the playback decoder
    if (avcodec_open2(c, codec, nullptr) < 0) avcodec_free_context(&c);
    return c;
}

// Opens playback stream `stream` into `out`: through `variant`'s own media
// playlist when given and it checks out (HLS — far cheaper than the master,
// see HlsVariants.h), else `url` as playback opened it (`master`: the master
// copy playback opened instead). False, `out` empty, if neither works.
// Network inputs time out, and a stop request aborts blocking I/O so
// close() can join promptly even while the URL is unreachable.
bool open_source(Source& out, int stream, const ffopen::VariantInput& variant,
                 const std::string& url, const std::string& master, const std::stop_token& st,
                 double& open_ms, double& probe_ms)
{
    out = {};
    open_ms = probe_ms = 0.0;
    const auto t_open = Clock::now();
    if (!variant.url.empty()) {
        AVFormatContext* fmt = nullptr;
        int idx = -1;
        if (ffopen::open_variant(&fmt, &idx, variant, st, NetInitiator::Scrub) == 0) {
            out.fmt    = fmt;
            out.idx    = idx;
            out.direct = true;
        }
        if (st.stop_requested()) { out.close(); return false; }
    }
    if (!out.fmt) {
        AVFormatContext* fmt = nullptr;
        if (ffopen::open_input(&fmt, url, st, NetInitiator::Scrub, master) < 0) {
            if (!st.stop_requested())
                logger::error("ScrubDecoder: failed to open '{}'", url);
            return false;
        }
        // Only the video stream is needed. Discard the rest before probing so
        // HLS doesn't fetch every variant's segments (stream indices match the
        // playback Demuxer's: same URL or master copy, same demuxer).
        if (stream >= 0 && stream < static_cast<int>(fmt->nb_streams)) keep_only(fmt, stream);
        const auto t_probe = Clock::now();
        open_ms = ms_between(t_open, t_probe);
        if (avformat_find_stream_info(fmt, nullptr) < 0 || !is_video(fmt, stream)) {
            avformat_close_input(&fmt);
            return false;
        }
        probe_ms = ms_between(t_probe, Clock::now());
        out.fmt = fmt;
        out.idx = stream;
    } else {
        open_ms = ms_between(t_open, Clock::now());
    }
    out.codec = open_codec(out.fmt->streams[out.idx]);
    if (!out.codec) {
        out.close();
        return false;
    }
    return true;
}

// A context opened like playback's has every stream: switch to `stream`
// without reopening anything but the decoder. The next request's seek
// resets the demuxer, so it reads the new stream from there (HLS: the old
// variant's playlist is dropped at its next segment).
bool switch_in_place(Source& s, int stream)
{
    if (s.direct || !is_video(s.fmt, stream)) return false;
    AVCodecContext* c = open_codec(s.fmt->streams[stream]);
    if (!c) return false;
    avcodec_free_context(&s.codec);
    s.codec = c;
    s.idx   = stream;
    keep_only(s.fmt, stream);
    return true;
}

} // namespace

ScrubDecoder::~ScrubDecoder() { close(); }

void ScrubDecoder::open(const std::string& url,
                        int video_stream_idx,
                        AVRational time_base,
                        const ffopen::VariantInput& variant,
                        const std::string& master)
{
    {
        std::lock_guard lk(req_mtx_);
        target_ = { video_stream_idx, time_base, variant };
        latest_gen_.store(++target_gen_);
        latest_stream_.store(video_stream_idx);
    }
    stopped_.store(false);
    thread_ = std::jthread([this, url, master](std::stop_token st) {
        decode_loop(st, url, master);
        stopped_.store(true);
    });
}

void ScrubDecoder::retarget(int video_stream_idx, AVRational time_base, const ffopen::VariantInput& variant)
{
    {
        std::lock_guard lk(req_mtx_);
        if (video_stream_idx == target_.stream) return;
        target_ = { video_stream_idx, time_base, variant };
        latest_gen_.store(++target_gen_);
        latest_stream_.store(video_stream_idx);
    }
    req_cv_.notify_one();
}

void ScrubDecoder::close()
{
    thread_ = {};   // request_stop() then join

    std::lock_guard lk(result_mtx_);
    if (result_) { av_frame_free(&result_); result_ = nullptr; }
}

std::uint64_t ScrubDecoder::request(double target_pts)
{
    std::uint64_t serial = 0;
    {
        std::lock_guard lk(req_mtx_);
        req_pts_     = target_pts;
        req_pending_ = true;
        serial       = ++req_serial_;
    }
    req_cv_.notify_one();
    return serial;
}

std::uint64_t ScrubDecoder::probe(int stream, const KeyframeIndex::Probe& p)
{
    std::uint64_t serial = 0;
    {
        std::lock_guard lk(req_mtx_);
        probe_req_     = p;
        probe_stream_  = stream;
        probe_pending_ = true;
        serial         = ++probe_serial_;
    }
    req_cv_.notify_one();
    return serial;
}

ScrubDecoder::ProbeState ScrubDecoder::probe_state(std::uint64_t serial) const
{
    const std::uint64_t r = probe_result_.load();
    if ((r >> 1) == serial) return (r & 1) ? ProbeState::Done : ProbeState::Failed;
    if ((r >> 1) > serial || stopped_.load()) return ProbeState::Failed;
    return ProbeState::Pending;
}

void ScrubDecoder::cancel_probe()
{
    std::lock_guard lk(req_mtx_);
    probe_pending_ = false;
    ++probe_serial_;   // the probe being read, if any, is stale now
}

AVFrame* ScrubDecoder::poll_frame(std::uint64_t min_serial, int* stream)
{
    std::lock_guard lk(result_mtx_);
    AVFrame* f = result_;
    if (!f) return nullptr;
    result_ = nullptr;
    // Decoded from a stream since replaced, or for a request the caller
    // is no longer waiting for.
    if (result_gen_ != latest_gen_.load() || result_serial_ < min_serial) {
        av_frame_free(&f);
        return nullptr;
    }
    if (result_serial_ > consumed_serial_.load()) consumed_serial_.store(result_serial_);
    if (stream) *stream = result_stream_;
    return f;
}

void ScrubDecoder::decode_loop(std::stop_token st, std::string url, std::string master)
{
    Target        tgt;
    std::uint64_t gen = 0;
    {
        std::lock_guard lk(req_mtx_);
        tgt = target_;
        gen = target_gen_;
    }

    Source src;
    double open_ms = 0.0, probe_ms = 0.0;
    if (!open_source(src, tgt.stream, tgt.variant, url, master, st, open_ms, probe_ms))
        return;

    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();
    AVFrame*  last  = av_frame_alloc();   // a request's latest frame before its target
    bool      seek_warned = false;        // a refused seek is logged once

    // Wake the wait() below whenever stop is requested.
    // Lock-then-notify so a wait() that has just checked its predicate
    // can't miss the wake-up (close() would then join forever).
    std::stop_callback stop_wake(st, [this] {
        { std::lock_guard lk(req_mtx_); }
        req_cv_.notify_all();
    });

    if (src.direct)
        logger::info("ScrubDecoder: ready — variant playlist {} (open + probe {:.0f} ms) (t={:.3f})",
                     tgt.variant.url, open_ms, logger::uptime());
    else
        logger::info("ScrubDecoder: ready (open {:.0f} ms, probe {:.0f} ms) (t={:.3f})",
                     open_ms, probe_ms, logger::uptime());

    // Follow a retarget(): the playback stream `nt.stream` from now on.
    auto follow = [&](const Target& nt) {
        const auto t0 = Clock::now();
        if (nt.stream == tgt.stream) {               // switched away and back meanwhile
            tgt = nt;
            return;
        }
        if (switch_in_place(src, nt.stream)) {
            tgt = nt;
            const AVCodecParameters* par = src.fmt->streams[src.idx]->codecpar;
            logger::info("ScrubDecoder: following stream {} ({}x{}) — same context (t={:.3f})",
                         nt.stream, par->width, par->height, logger::uptime());
            return;
        }
        Source next;
        double o = 0.0, p = 0.0;
        if (open_source(next, nt.stream, nt.variant, url, master, st, o, p)) {
            src.close();
            src = next;
            tgt = nt;
            const AVCodecParameters* par = src.fmt->streams[src.idx]->codecpar;
            if (src.direct)
                logger::info("ScrubDecoder: following stream {} ({}x{}) — variant playlist {} ({:.0f} ms) (t={:.3f})",
                             nt.stream, par->width, par->height, nt.variant.url, ms_between(t0, Clock::now()),
                             logger::uptime());
            else
                logger::info("ScrubDecoder: following stream {} ({}x{}) — reopened (open {:.0f} ms, probe {:.0f} ms) (t={:.3f})",
                             nt.stream, par->width, par->height, o, p, logger::uptime());
            return;
        }
        if (!st.stop_requested())
            logger::warn("ScrubDecoder: could not open stream {} — scrubbing keeps decoding stream {}",
                         nt.stream, tgt.stream);
        // Same timeline, other rendition: posted under the new generation.
        const int kept = tgt.stream;
        tgt = nt;
        tgt.stream = kept;
    };

    // Every packet of the decoded stream read goes to the keyframe index as
    // it is read, one run per seek; a backward seek on HLS lands on a
    // segment start.
    std::uint32_t run       = 0;
    bool          run_first = true, run_seg = false;
    auto start_run = [&](bool backward) {
        if (!keyframes_) return;
        run       = keyframes_->new_run();
        run_first = true;
        run_seg   = backward && keyframes_->hls();
    };
    // Reports `p` (time base `tb`); returns its pts / dts / key flag.
    struct Read { bool ok = false; double pts = 0.0, dts = 0.0; bool key = false; };
    auto report = [&](const AVPacket* p, AVRational tb) {
        Read r;
        const bool first = run_first;
        run_first = false;
        const int64_t pts_ts = p->pts != AV_NOPTS_VALUE ? p->pts : p->dts;
        if (pts_ts == AV_NOPTS_VALUE) return r;
        const int64_t dts_ts = p->dts != AV_NOPTS_VALUE ? p->dts : pts_ts;
        r = { true, pts_ts * av_q2d(tb), dts_ts * av_q2d(tb), (p->flags & AV_PKT_FLAG_KEY) != 0 };
        if (keyframes_) keyframes_->observe(tgt.stream, r.pts, r.dts, r.key, run, first && run_seg);
        return r;
    };

    // A probe (see probe()): seek as `p` says, then read packets of the
    // stream — no decoding — reporting each, up to p.until. Stepping back
    // (!p.forward): until dts >= until; when the seek lands on the frame
    // stepped from (it starts its segment), once more from the segment
    // before. Forward: up to the first keyframe after `until`.
    struct ProbeRun { bool ok = false, backed_up = false; int packets = 0, keys = 0; double from = -1.0, to = -1.0; };
    auto run_probe = [&](const KeyframeIndex::Probe& p, int stream, std::uint64_t pserial) {
        ProbeRun out;
        if (!keyframes_ || stream != tgt.stream) return out;
        static constexpr double kReach = 30.0;   // forward: no keyframe this far past `until` — give up
        const AVRational tb    = src.fmt->streams[src.idx]->time_base;
        const double     frame = p.frame_s > 0.0 ? p.frame_s : 1.0 / 30.0;
        const double     tol   = KeyframeIndex::tolerance(p.frame_s);
        auto stale = [&] {
            std::lock_guard lk(req_mtx_);
            return req_pending_ || target_gen_ != gen || probe_serial_ != pserial;
        };
        double seek_s = p.seek_s;
        bool   after  = p.at_or_after;
        bool   seg    = p.segment_start;
        for (int pass = 0; pass < 2 && !st.stop_requested(); ++pass) {
            if (av_seek_frame(src.fmt, src.idx, std::llround(seek_s / av_q2d(tb)),
                              after ? 0 : AVSEEK_FLAG_BACKWARD) < 0)
                return out;
            start_run(!after);
            run_seg = seg;
            bool   landed      = false;
            double landing_dts = 0.0;
            bool   back_up     = false;
            while (!st.stop_requested() && !stale()) {
                const int ret = av_read_frame(src.fmt, pkt);
                if (ret == AVERROR(EAGAIN)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
                if (ret < 0) {   // the end of the media: read through
                    out.ok = ret == AVERROR_EOF && out.packets > 0;
                    return out;
                }
                if (pkt->stream_index != src.idx) {
                    av_packet_unref(pkt);
                    continue;
                }
                const Read r = report(pkt, tb);
                av_packet_unref(pkt);
                if (!r.ok) continue;
                ++out.packets;
                out.keys += r.key;
                if (!landed) {
                    landed      = true;
                    landing_dts = r.dts;
                    out.from    = r.pts;
                    if (!p.forward && r.pts >= p.until - 0.001) {
                        back_up = true;
                        break;
                    }
                }
                out.to = r.dts;
                if (p.forward ? r.key && r.pts > p.until + 0.001 : r.dts >= p.until - tol) {
                    out.ok = true;
                    return out;
                }
                if (r.dts > p.until + kReach) {
                    out.ok = true;
                    return out;
                }
            }
            if (!back_up || pass > 0) return out;   // stale, stopped, or no segment before
            out.backed_up = true;
            seek_s = landing_dts - 0.5 * frame;
            after  = false;
            seg    = keyframes_->hls();
        }
        return out;
    };

    while (!st.stop_requested()) {
        // Block until a request, a probe or a retarget arrives (or stop).
        // Requests come before probes.
        double               target_pts = -1.0;
        std::uint64_t        serial     = 0;
        bool                 probing    = false;
        KeyframeIndex::Probe probe_req;
        int                  probe_stream = -1;
        {
            std::unique_lock lk(req_mtx_);
            req_cv_.wait(lk, [&] {
                return req_pending_ || probe_pending_ || target_gen_ != gen || st.stop_requested();
            });
            if (st.stop_requested()) break;
            if (target_gen_ != gen) {
                const Target nt = target_;
                gen = target_gen_;
                // A request whose frame was not polled yet is served again
                // from the new stream (a frame-back step waits for it).
                if (req_serial_ > consumed_serial_.load() && req_pts_ >= 0.0) req_pending_ = true;
                lk.unlock();
                follow(nt);
                continue;
            }
            if (req_pending_) {
                target_pts   = req_pts_;
                serial       = req_serial_;
                req_pending_ = false;
            } else {
                probing        = true;
                probe_req      = probe_req_;
                probe_stream   = probe_stream_;
                serial         = probe_serial_;
                probe_pending_ = false;
            }
        }

        if (probing) {
            const auto     t0 = Clock::now();
            const ProbeRun r  = run_probe(probe_req, probe_stream, serial);
            probe_result_.store(serial << 1 | (r.ok ? 1u : 0u));
            if (r.ok)
                logger::info("ScrubDecoder: read stream {} for keyframes {}{:.3f} .. {:.3f} — {} packets, {} keyframes, "
                             "{:.0f} ms (t={:.3f})",
                             probe_stream, r.backed_up ? "(from the segment before) " : "", r.from, r.to,
                             r.packets, r.keys, ms_between(t0, Clock::now()), logger::uptime());
            else
                logger::info("ScrubDecoder: keyframe read of stream {} abandoned / failed after {} packets "
                             "({:.0f} ms) (t={:.3f})",
                             probe_stream, r.packets, ms_between(t0, Clock::now()), logger::uptime());
            continue;
        }

        // Seek targets are in this context's stream time base; posted frames
        // carry pts in tgt.time_base (the playback stream's), which the caller
        // converts with. Same media playlist, so normally the same value.
        const AVRational src_tb = src.fmt->streams[src.idx]->time_base;
        // The seek stays inside the media, as playback's do (Demuxer): FFmpeg's
        // hls demuxer refuses (EIO) one before its first segment or past its
        // last, and the seek bar spans 0..duration while HLS timestamps can
        // start later (bipbop: 10 s). Before the start, the first frame is
        // posted; past the last frame, the last one (the end of the media,
        // below).
        const double media_start = src.fmt->start_time != AV_NOPTS_VALUE
                                 ? static_cast<double>(src.fmt->start_time) / AV_TIME_BASE : 0.0;
        const double media_dur   = src.fmt->duration > 0
                                 ? static_cast<double>(src.fmt->duration) / AV_TIME_BASE : 0.0;
        double seek_pts = std::max(target_pts, media_start);
        if (media_dur > 0.0) seek_pts = std::min(seek_pts, std::max(media_start, media_start + media_dur - 0.5));
        // Back to the keyframe at or before seek_pts (HLS: the start of its
        // segment). The hls demuxer's own start (its first packet's dts) can
        // lie just after start_time: a second try inside the first segment.
        auto seek_back = [&] {
            auto at = [&](double secs) {
                const int64_t ts = static_cast<int64_t>(secs / av_q2d(src_tb));
                return avformat_seek_file(src.fmt, src.idx, INT64_MIN, ts, ts, 0);
            };
            int r = at(seek_pts);
            if (r < 0 && seek_pts < media_start + 1.0) r = at(media_start + 1.0);
            if (r < 0 && !seek_warned) {
                seek_warned = true;
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(r, errbuf, sizeof(errbuf));
                logger::warn("ScrubDecoder: seek to {:.3f} failed: {} — decoding on from where it was",
                             seek_pts, errbuf);
            }
            return r;
        };

        // Seek to the nearest keyframe at or before the target: a container
        // index has it; HLS goes to the start of the segment holding the
        // target instead, unless a keyframe read since (KeyframeIndex) is
        // closer — reached by a forward seek to its dts.
        // Only when it is in the target's segment: certainly so while the
        // index lists every segment start, else when it is recent.
        static constexpr double kRecentKey = 2.5;   // seconds
        std::optional<KeyframeIndex::Keyframe> from;
        if (keyframes_) from = keyframes_->at_or_before(tgt.stream, target_pts);
        bool from_key = from && from->at_or_after
                     && (target_pts - from->pts <= kRecentKey || keyframes_->status(tgt.stream).trusted);
        if (from_key && av_seek_frame(src.fmt, src.idx, std::llround(from->seek_s / av_q2d(src_tb)), 0) < 0)
            from_key = false;
        // A refused seek reads on from where the last request left off: that
        // is no landing on a segment start.
        const bool landed = from_key || seek_back() >= 0;
        avcodec_flush_buffers(src.codec);
        start_run(landed && !from_key);
        ++(from_key ? n_from_key_ : n_from_seek_);

        // Posts `f` as the answer, replacing any frame not yet consumed.
        auto post = [&](const AVFrame* f) {
            AVFrame* out = av_frame_clone(f);
            if (out) {
                if (out->pts != AV_NOPTS_VALUE && av_cmp_q(src_tb, tgt.time_base) != 0)
                    out->pts = av_rescale_q(out->pts, src_tb, tgt.time_base);
                out->time_base = tgt.time_base;
            }
            std::lock_guard rlk(result_mtx_);
            if (result_) av_frame_free(&result_);
            result_        = out;
            result_gen_    = gen;
            result_serial_ = serial;
            result_stream_ = tgt.stream;
        };

        // Decode until we get a frame at or past the target. The end of the
        // media first: the decoder's frames are drained, and the last frame
        // of all is posted (the seek bar's end can lie past it).
        // Abort early when a newer request or a retarget has arrived.
        bool got = false, at_end = false;
        av_frame_unref(last);
        while (!got && !st.stop_requested()) {
            {
                std::lock_guard lk(req_mtx_);
                if (req_pending_ || target_gen_ != gen) break;   // stale
            }

            if (!at_end) {
                const int ret = av_read_frame(src.fmt, pkt);
                if (ret == AVERROR_EOF) {
                    at_end = true;
                    avcodec_send_packet(src.codec, nullptr);
                } else if (ret < 0) {
                    break;   // read error
                } else if (pkt->stream_index != src.idx) {
                    av_packet_unref(pkt);
                    continue;
                } else {
                    (void)report(pkt, src_tb);
                    avcodec_send_packet(src.codec, pkt);
                    av_packet_unref(pkt);
                }
            }

            bool restart = false;
            while (avcodec_receive_frame(src.codec, frame) == 0) {
                double pts = (frame->pts != AV_NOPTS_VALUE)
                             ? frame->pts * av_q2d(src_tb)
                             : target_pts;
                if (from_key) {
                    // The first frame from the keyframe: past the target
                    // means the seek missed it — start from the segment.
                    from_key = false;
                    if (pts > target_pts + 0.001) {
                        av_frame_unref(frame);
                        const bool ok = seek_back() >= 0;
                        avcodec_flush_buffers(src.codec);
                        start_run(ok);
                        ++n_missed_;
                        restart = true;
                        break;
                    }
                }
                if (pts >= target_pts - 0.001) {
                    post(frame);
                    av_frame_unref(frame);
                    got = true;
                    break;
                }
                av_frame_unref(last);
                av_frame_move_ref(last, frame);   // the latest frame before the target
            }
            if (restart) {
                at_end = false;
                av_frame_unref(last);
                continue;
            }
            if (at_end && !got) {   // drained: every frame is before the target
                if (last->buf[0]) post(last);
                break;
            }
        }
        av_frame_unref(last);
    }

    av_frame_free(&last);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    src.close();

    const Counters c = counters();
    logger::info("ScrubDecoder: stopped ({} requests decoded from a keyframe read{}, {} from a seek)",
                 c.from_key, c.missed ? std::format(" — {} missed it", c.missed) : std::string(), c.from_seek);
}
