#include "MediaSession.h"
#include "FfOpen.h"
#include "HlsMaster.h"
#include "HlsVariants.h"
#include "Logger.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <limits>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
}

// Video decode thread: pops packets from videoq, decodes, pushes frames to frameq.
static void video_decode_loop(Decoder& decoder,
                               Demuxer& demuxer,
                               Queue<AVPacket*>& videoq,
                               Queue<AVFrame*>&  frameq,
                               bool              try_hw,
                               std::atomic<double>& decode_time_ms,
                               std::atomic<int>& video_epoch,
                               std::atomic<std::uint64_t>& seek_applied,
                               StageStats&       stats,
                               std::stop_token   st)
{
    AVFrame* frame = av_frame_alloc();
    // A frame goes to the render thread; waits while frameq is full.
    auto queue_frame = [&](AVFrame* out) {
        stats.set(DecodeState::WaitOutput);
        const bool ok = frameq.push(out);
        stats.set(DecodeState::Decoding);
        return ok;
    };
    // The codec produced a frame (counted before any take-over drop).
    auto pull = [&]() {
        if (!decoder.pull(frame)) return false;
        stats.frames.fetch_add(1, std::memory_order_relaxed);
        return true;
    };
    // Frames carry the decoder generation in AVFrame::opaque so the render
    // thread can tell the first frame of a newly switched track, and their
    // stream time base in AVFrame::time_base (variants differ: 1/60000 vs 1/30000).
    int        epoch      = video_epoch.load();
    // The seek the frames come from, and the last seek's show_from taken
    // (Demuxer::video_show_from: once per generation).
    std::uint64_t seek_serial = demuxer.video_seek_serial();
    std::uint64_t show_gen    = demuxer.video_show_from().gen;
    AVRational tb         = demuxer.video_time_base();
    bool       trace_next = false;   // log the first frame after a reopen
    // After a seek or a track switch the stream is decoded from the keyframe
    // (HLS: the segment start) at or before the target / take-over point;
    // frames before that point are not shown.
    double     drop_before = -1.0;
    // The latest frame skipped: shown if the media ends before the target
    // (a seek past the last frame shows the last frame, not nothing).
    AVFrame*   last_skipped = nullptr;
    // True if `f` is before the target (and must not be queued).
    auto skip = [&](const AVFrame* f) {
        if (drop_before < 0.0) return false;
        const int64_t ts = (f->pts != AV_NOPTS_VALUE) ? f->pts : f->best_effort_timestamp;
        if (ts == AV_NOPTS_VALUE) { drop_before = -1.0; return false; }
        // Half a frame of tolerance: the take-over point is the end of the
        // last old frame, which may round to just past the next frame's pts;
        // a seek shows the frame nearest its target from there on. (1 ms
        // without a duration: a keyframe step's target is that frame's pts,
        // possibly rounded differently.)
        const double tol = f->duration > 0 ? 0.5 * f->duration * av_q2d(tb) : 0.001;
        if (ts * av_q2d(tb) + tol < drop_before) {
            av_frame_free(&last_skipped);
            last_skipped = av_frame_clone(f);
            return true;
        }
        drop_before = -1.0;
        av_frame_free(&last_skipped);
        return false;
    };
    int stream = demuxer.video_stream_index();
    // Decode order of a packet (its dts; the first after a Matroska seek
    // has only a pts).
    auto order = [](const AVPacket* p) { return p->dts != AV_NOPTS_VALUE ? p->dts : p->pts; };
    // While frames before a target are dropped (drop_before), decoding only
    // has to start at the last keyframe at or before it: the packets ahead
    // of that keyframe, if it is queued already, are not decoded at all. An
    // HLS seek lands on a segment start — bipbop: 6 s, keyframes every 2 s —
    // and so does a switch's new track: decoding all of it to reach the
    // target froze the picture for about a second (1080p60). Returns that
    // keyframe's decode position, or AV_NOPTS_VALUE if none is queued.
    auto last_key_before = [&](double target) {
        std::int64_t found = AV_NOPTS_VALUE;
        videoq.scan([&](AVPacket* const& p) {
            if (p == nullptr || p == flush_sentinel()) return false;   // a later seek / switch's
            if ((p->flags & AV_PKT_FLAG_KEY) && p->pts != AV_NOPTS_VALUE) {
                const double tol = p->duration > 0 ? 0.5 * p->duration * av_q2d(tb) : 0.001;
                if (p->pts * av_q2d(tb) <= target + tol) found = order(p);
            }
            return true;
        });
        return found;
    };
    int  skipped = 0;          // packets not decoded since the last decode (last_key_before)
    bool skipping_unref = false;   // Decoder::skip_unreferenced is on
    auto tag = [&](AVFrame* out) {
        out->opaque    = reinterpret_cast<void*>(MediaSession::frame_tag(epoch, stream, seek_serial));
        out->time_base = tb;
        if (trace_next) {
            trace_next = false;
            logger::info("switch-trace: first decoded frame pts={:.3f} {}x{} (t={:.3f})",
                         out->pts != AV_NOPTS_VALUE ? out->pts * av_q2d(tb) : -1.0,
                         out->width, out->height, logger::uptime());
        }
    };

    while (!st.stop_requested()) {
        AVPacket* pkt = nullptr;
        stats.set(DecodeState::WaitPacket);
        if (!videoq.pop(pkt)) break;  // shut down or EOF sentinel
        stats.set(DecodeState::Decoding);

        if (pkt == nullptr) {
            // EOF — flush decoder and drain remaining buffered frames.
            decoder.push(nullptr);
            while (pull()) {
                if (skip(frame)) { av_frame_unref(frame); continue; }
                AVFrame* out = av_frame_clone(frame);
                tag(out);
                if (!queue_frame(out)) { av_frame_free(&out); break; }
                av_frame_unref(frame);
            }
            // Every frame since the seek was before its target: show the last.
            if (drop_before >= 0.0 && last_skipped) {
                tag(last_skipped);
                if (queue_frame(last_skipped)) last_skipped = nullptr;
                drop_before = -1.0;
            }
            av_frame_free(&last_skipped);
        } else if (pkt == flush_sentinel()) {
            // Seek or stream-switch sentinel. A seamless track switch keeps
            // what the old decoder still holds (B-frame delay) for display;
            // otherwise flush the codec and drop stale frames.
            const bool reopen = demuxer.consume_video_reopen();
            if (reopen && demuxer.consume_video_seamless()) {
                decoder.push(nullptr);
                while (pull()) {
                    AVFrame* out = av_frame_clone(frame);
                    tag(out);
                    av_frame_unref(frame);
                    if (!queue_frame(out)) { av_frame_free(&out); break; }
                }
            } else {
                decoder.flush();
                AVFrame* stale;
                while (frameq.try_pop(stale)) av_frame_free(&stale);
            }
            // Every sentinel starts a new generation: frames decoded after it
            // belong to the seek / switch that caused it.
            epoch = ++video_epoch;
            skipped = 0;
            seek_serial = demuxer.video_seek_serial();
            seek_applied.store(seek_serial);
            // A seek shows from its target, not from where it landed.
            if (const Demuxer::ShowFrom sf = demuxer.video_show_from(); sf.gen != show_gen) {
                show_gen    = sf.gen;
                drop_before = sf.from;
            }
            av_frame_free(&last_skipped);
            // If this sentinel followed a video stream switch, reopen the decoder.
            if (reopen) {
                (void)decoder.open(demuxer.video_codecpar(), try_hw);
                skipping_unref = false;   // a new codec context
                tb     = demuxer.video_time_base();
                stream = demuxer.video_stream_index();
                drop_before = std::max(drop_before, demuxer.consume_video_resume());
                trace_next = true;
                logger::info("switch-trace: video decoder reopened, new frames from {:.3f} (t={:.3f})",
                             drop_before, logger::uptime());
            }
        } else {
            // Ahead of the last keyframe at or before the target: not needed.
            if (drop_before >= 0.0) {
                const std::int64_t at  = order(pkt);
                const std::int64_t key = last_key_before(drop_before);
                if (at != AV_NOPTS_VALUE && key != AV_NOPTS_VALUE && at < key) {
                    av_packet_free(&pkt);
                    ++skipped;
                    continue;
                }
                if (skipped > 0) {
                    // Decoding resumes at that keyframe: what the decoder
                    // holds of the packets before is of no use.
                    logger::info("switch-trace: skipped {} packets to the keyframe at {:.3f} before {:.3f} (t={:.3f})",
                                     skipped, pkt->pts != AV_NOPTS_VALUE ? pkt->pts * av_q2d(tb) : -1.0,
                                     drop_before, logger::uptime());
                    decoder.flush();
                    skipped = 0;
                }
            }
            // Normal packet — decode and time. Before the target its frame,
            // if nothing refers to it, needs no decoding either.
            if (drop_before >= 0.0 || skipping_unref) {
                const double tol = pkt->duration > 0 ? 0.5 * pkt->duration * av_q2d(tb) : 0.001;
                const bool unseen = drop_before >= 0.0 && pkt->pts != AV_NOPTS_VALUE
                                 && pkt->pts * av_q2d(tb) + tol < drop_before;
                if (unseen != skipping_unref) decoder.skip_unreferenced(skipping_unref = unseen);
            }
            auto t0 = std::chrono::steady_clock::now();
            stats.packets.fetch_add(1, std::memory_order_relaxed);
            decoder.push(pkt);
            av_packet_free(&pkt);

            int pulled = 0;
            while (pull()) {
                ++pulled;
                if (skip(frame)) { av_frame_unref(frame); continue; }
                AVFrame* out = av_frame_clone(frame);
                tag(out);
                if (!queue_frame(out)) {
                    av_frame_free(&out);
                    break;
                }
                av_frame_unref(frame);
            }

            if (pulled > 0) {
                auto t1 = std::chrono::steady_clock::now();
                decode_time_ms.store(
                    std::chrono::duration<double, std::milli>(t1 - t0).count()
                    / static_cast<double>(pulled));
            }
        }
    }

    av_frame_free(&frame);
    av_frame_free(&last_skipped);
    stats.set(DecodeState::Stopped);
}

// Audio decode thread: pops packets from audioq, decodes into AudioPlayer ring buffer.
// (A wait for ring space inside decode() shows as AudioPlayer::push_waiting().)
static void audio_decode_loop(AudioDecoder&     audio_decoder,
                               AudioPlayer&      audio_player,
                               Demuxer&          demuxer,
                               Queue<AVPacket*>& audioq,
                               std::atomic<int>& audio_stream,
                               const std::atomic<std::uint64_t>& drop_until_seek,
                               StageStats&       stats,
                               std::stop_token   st)
{
    int reopened_stream = -1;   // set by a switch until its first packet is decoded
    std::uint64_t seek_seen = 0;   // serial of the latest seek whose sentinel was popped
    std::uint64_t start_gen = demuxer.audio_start().gen;   // Demuxer::audio_start taken
    while (!st.stop_requested()) {
        AVPacket* pkt = nullptr;
        stats.set(DecodeState::WaitPacket);
        if (!audioq.pop(pkt)) break;
        stats.set(DecodeState::Decoding);
        if (pkt == nullptr) continue;  // end of media: the demuxer may still serve a seek / switch
        if (pkt == flush_sentinel()) {
            seek_seen = demuxer.audio_seek_serial();
            // A seamless track switch keeps the audio already queued for the
            // device; a seek (or a paused switch) discards it.
            const bool reopen = demuxer.consume_audio_reopen();
            if (!(reopen && demuxer.consume_audio_seamless())) {
                audio_decoder.flush();
                audio_player.flush();
            }
            // If this sentinel followed an audio stream switch, reopen the decoder.
            if (reopen) {
                (void)audio_decoder.reopen(demuxer.audio_codecpar(), audio_player,
                                           demuxer.audio_time_base());
                reopened_stream = demuxer.audio_stream_index();
                logger::info("switch-trace: audio decoder reopened (t={:.3f})", logger::uptime());
            }
            // Where the audio after this sentinel starts, to the sample.
            if (const Demuxer::ShowFrom as = demuxer.audio_start(); as.gen != start_gen) {
                start_gen = as.gen;
                audio_decoder.start_at(as.from);
            }
            continue;
        }
        if (seek_seen < drop_until_seek.load()) {   // from before a seek still on its way
            av_packet_free(&pkt);
            continue;
        }
        stats.packets.fetch_add(1, std::memory_order_relaxed);
        audio_decoder.decode(pkt, audio_player);
        av_packet_free(&pkt);
        // First packet of a switched track decoded: its audio is queued.
        if (reopened_stream >= 0) {
            audio_stream.store(reopened_stream);
            reopened_stream = -1;
        }
    }
    stats.set(DecodeState::Stopped);
}

// QA-only: --test-bitmap-sub seeds a synthetic event with three rects
// that exercise the GL render path independent of any codec/demuxer.
// Authored on a 1920x1080 canvas; the render path scales into the video
// viewport via authored_w/h.
static void inject_test_bitmap_sub(SubtitleDecoder& subtitle_decoder)
{
    auto fill = [](BitmapSubRect& r, std::uint8_t R, std::uint8_t G,
                   std::uint8_t B, std::uint8_t A) {
        r.rgba.assign(static_cast<std::size_t>(r.w) * r.h * 4, 0);
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x) {
                std::uint8_t* p = r.rgba.data() + (y * r.w + x) * 4;
                p[0] = R; p[1] = G; p[2] = B; p[3] = A;
            }
    };
    auto stroke_border = [](BitmapSubRect& r, int t, std::uint8_t R,
                            std::uint8_t G, std::uint8_t B) {
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x) {
                if (x >= t && x < r.w - t && y >= t && y < r.h - t) continue;
                std::uint8_t* p = r.rgba.data() + (y * r.w + x) * 4;
                p[0] = R; p[1] = G; p[2] = B; p[3] = 255;
            }
    };

    BitmapSubEvent ev;
    ev.start_pts  = 0.0;
    ev.end_pts    = 1e9;          // visible for entire playback
    ev.authored_w = 1920;
    ev.authored_h = 1080;

    // Rect 1: top-left R/G/B/W swatch — verifies channel ordering.
    // Four 50px stripes side by side, opaque.
    {
        BitmapSubRect r;
        r.x = 40; r.y = 40; r.w = 200; r.h = 60;
        r.rgba.assign(static_cast<std::size_t>(r.w) * r.h * 4, 0);
        const std::uint8_t cols[4][4] = {
            {255,   0,   0, 255},   // red
            {  0, 255,   0, 255},   // green
            {  0,   0, 255, 255},   // blue
            {255, 255, 255, 255},   // white
        };
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x) {
                int band = std::min(3, x / 50);
                std::uint8_t* p = r.rgba.data() + (y * r.w + x) * 4;
                p[0] = cols[band][0]; p[1] = cols[band][1];
                p[2] = cols[band][2]; p[3] = cols[band][3];
            }
        ev.rects.push_back(std::move(r));
    }

    // Rect 2: center semi-transparent yellow box with opaque white border —
    // verifies alpha blend against underlying video.
    {
        BitmapSubRect r;
        r.x = 560; r.y = 480; r.w = 800; r.h = 120;
        fill(r, 255, 220, 0, 110);          // ~43% alpha
        stroke_border(r, 6, 255, 255, 255);
        ev.rects.push_back(std::move(r));
    }

    // Rect 3: bottom banner — horizontal alpha gradient on cyan to verify
    // per-pixel alpha across the rect; opaque white border.
    {
        BitmapSubRect r;
        r.x = 360; r.y = 920; r.w = 1200; r.h = 120;
        r.rgba.assign(static_cast<std::size_t>(r.w) * r.h * 4, 0);
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x) {
                std::uint8_t* p = r.rgba.data() + (y * r.w + x) * 4;
                p[0] = 0;
                p[1] = 200;
                p[2] = 255;
                p[3] = static_cast<std::uint8_t>(255 * x / (r.w - 1));
            }
        stroke_border(r, 6, 255, 255, 255);
        ev.rects.push_back(std::move(r));
    }

    subtitle_decoder.inject_test_bitmap(std::move(ev));
    logger::info("--test-bitmap-sub: injected synthetic 3-rect event");
}

// ── Open ──────────────────────────────────────────────────────────────────────

bool MediaSession::open(const std::string& open_path, const MediaSessionOptions& opt, std::string& err)
{
    return open_media(open_path, opt, err) && start(opt, err);
}

void MediaSession::cancel_open()
{
    cancelled_.store(true);
    manifest_abort_.store(true);
    demuxer.abort_io();
}

bool MediaSession::open_media(const std::string& open_path, const MediaSessionOptions& opt, std::string& err)
{
    path    = open_path;
    try_hw_ = opt.try_hw;
    auto cancelled = [&] {
        if (!cancelled_.load()) return false;
        err = "Open cancelled";
        return true;
    };

    // ── Manifest parser (HLS / DASH only — non-fatal) ─────────────────────────
    // Fetched beside the container open (one small request against seconds
    // of HLS probing); the helpers need it once open. An HLS master over
    // HTTP(S) is waited for first, briefly: if it lists variant playlists
    // more than once, playback opens a copy that lists each once
    // (HlsMaster.h) — FFmpeg would fetch and probe every repeat. A slow
    // fetch (stalled or unreachable server) doesn't hold the open up: past
    // kMasterWait the master is opened as served while the fetch goes on.
    static constexpr auto kMasterWait = std::chrono::milliseconds(1500);
    std::string      master_copy;
    std::vector<int> kept;
    std::mutex              fetch_mtx;      // fetch_done / fetch_ok, before manifest_fetch:
    std::condition_variable fetch_cv;       // the thread is joined before these go
    bool                    fetch_done = false, fetch_ok = false;
    std::jthread     manifest_fetch;
    if (ManifestParser::is_manifest(open_path)) {
        manifest_fetch = std::jthread([&, open_path] {
            const bool ok = manifest.parse(open_path, &manifest_abort_);
            if (!ok && !manifest_abort_.load())
                logger::warn("ManifestParser: failed to parse {}", open_path);
            {
                std::lock_guard lk(fetch_mtx);
                fetch_done = true;
                fetch_ok   = ok;
            }
            fetch_cv.notify_all();
        });
    }
    const bool http_hls = !ManifestParser::is_dash(open_path)
                       && (open_path.starts_with("http://") || open_path.starts_with("https://"));
    if (manifest_fetch.joinable() && http_hls && opt.hls_dedupe) {
        // Copied under the lock: past the wait the fetch thread may still be
        // writing them.
        bool fetched = false, ok = false;
        {
            std::unique_lock lk(fetch_mtx);
            fetched = fetch_cv.wait_for(lk, kMasterWait, [&] { return fetch_done; });
            ok      = fetched && fetch_ok;
        }
        if (cancelled()) {
            manifest_abort_.store(true);
            return false;   // manifest_fetch joins on the way out
        }
        // `manifest` is complete here when fetched (fetch_done was set
        // after parse()).
        if (!ok) {
            if (!fetched)
                logger::info("HLS: master not fetched within {} ms — opening it as served", kMasterWait.count());
        } else if (manifest.set_cookies()) {
            logger::info("HLS: the master response sets cookies — opening it as served");
        } else if (auto copy = hls::dedupe_master(manifest.text(), manifest.final_url())) {
            std::vector<std::string> uris;
            for (const auto& v : manifest.variants())
                if (std::find(uris.begin(), uris.end(), v.url) == uris.end()) uris.push_back(v.url);
            logger::info("HLS: master lists {} variants over {} playlists — opening a copy with {} of them",
                         manifest.variants().size(), uris.size(), copy->kept.size());
            master_copy = std::move(copy->text);
            kept        = std::move(copy->kept);
        }
    }
    // The manifest is only needed by a session that opened.
    auto drop_manifest = [&] {
        manifest_abort_.store(true);
        if (manifest_fetch.joinable()) manifest_fetch.join();
    };

    if (!demuxer.open(open_path, master_copy)) {
        err = cancelled() ? std::string("Open cancelled") : "Failed to open: " + open_path;
        drop_manifest();
        return false;
    }
    if (cancelled()) { drop_manifest(); return false; }
    if (demuxer.video_stream_index() < 0) {
        logger::error("No video stream — nothing to display");
        err = "No video stream in: " + open_path;
        drop_manifest();
        return false;
    }
    if (manifest_fetch.joinable()) manifest_fetch.join();
    if (demuxer.opened_master_copy()) {
        hls_kept    = std::move(kept);
        hls_master_ = std::move(master_copy);
    }

    if (cancelled()) return false;

    // ── Decoders ──────────────────────────────────────────────────────────────
    if (!video_decoder.open(demuxer.video_codecpar(), try_hw_)) {
        err = "Unsupported video codec in: " + open_path;
        return false;
    }

    has_audio = demuxer.audio_stream_index() >= 0;
    if (has_audio) {
        if (!audio_player.init(48000)) {
            err = "Audio output failed for: " + open_path;
            return false;
        }
        if (!audio_decoder.open(demuxer.audio_codecpar(), audio_player,
                                demuxer.audio_time_base())) {
            err = "Unsupported audio codec in: " + open_path;
            return false;
        }
    }

    video_tb = demuxer.video_time_base();
    duration = demuxer.duration();  // seconds, or -1 for live
    return !cancelled();
}

bool MediaSession::start(const MediaSessionOptions& opt, std::string& err)
{
    const std::string& open_path = path;
    if (!renderer.init(video_decoder.width(), video_decoder.height(),
                       video_decoder.pixel_format(),
                       video_decoder.colorspace(),
                       video_decoder.color_range(),
                       video_decoder.color_transfer(),
                       video_decoder.color_primaries())) {
        err = "Video renderer init failed for: " + open_path;
        return false;
    }

    // HLS: the helpers open just the media playlist they decode (thumbnails:
    // the cheapest variant of the playing codec, scrubbing: the playing
    // variant) when the parsed master says which one carries the stream;
    // otherwise they open `open_path` like playback did.
    const AVFormatContext* fc = demuxer.fmt_ctx();
    const int vidx = demuxer.video_stream_index();
    // The variant structure (also for the network sidebar), read now: once
    // the demux thread runs it may rewrite stream metadata.
    hls_programs_ = hls::programs_of(fc);
    hls_video_variant_.assign(fc->nb_streams, -1);
    for (unsigned i = 0; i < fc->nb_streams; ++i)
        hls_video_variant_[i] = hls::variant_index_for(fc, static_cast<int>(i), manifest, hls_kept);
    auto variant_input = [&](int idx) -> ffopen::VariantInput {
        const int v = idx >= 0 && idx < static_cast<int>(hls_video_variant_.size())
                    ? hls_video_variant_[static_cast<std::size_t>(idx)] : -1;
        if (v < 0) return {};
        const AVCodecParameters* par = fc->streams[idx]->codecpar;
        return { manifest.variants()[static_cast<std::size_t>(v)].url,
                 static_cast<int>(par->codec_id), par->width, par->height };
    };
    // Per video stream: what a helper following it later needs (retargets
    // happen while the demux thread runs), and where its keyframes come from.
    is_hls_        = demuxer.is_hls();
    initial_video_ = vidx;
    keyframes.set_hls(is_hls_);
    video_inputs_.assign(fc->nb_streams, {});
    stream_tb_.assign(fc->nb_streams, AVRational{ 1, 1 });
    playlist_url_.assign(fc->nb_streams, {});
    pl_asked_.assign(fc->nb_streams, 0);
    int n_video = 0;
    for (unsigned i = 0; i < fc->nb_streams; ++i)
        n_video += fc->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
    const bool video_renditions = std::any_of(manifest.renditions().begin(), manifest.renditions().end(),
        [](const MediaRendition& r) { return r.type == "VIDEO" && !r.url.empty(); });
    for (unsigned i = 0; i < fc->nb_streams; ++i) {
        const AVStream* s = fc->streams[i];
        stream_tb_[i] = s->time_base;
        if (s->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) continue;
        const int idx = static_cast<int>(i);
        video_inputs_[i] = variant_input(idx);
        AVRational fr = s->avg_frame_rate;
        if (fr.num <= 0 || fr.den <= 0) fr = s->r_frame_rate;
        keyframes.set_stream(idx,
                             s->start_time != AV_NOPTS_VALUE ? s->start_time * av_q2d(s->time_base)
                                                             : std::numeric_limits<double>::quiet_NaN(),
                             fr.num > 0 && fr.den > 0 ? 1.0 / av_q2d(fr) : 0.0,
                             !container_keyframes(idx));   // that one's come from ThumbnailStrip
        if (!is_hls_) continue;
        // Its media playlist: the variant's own (several variants), the only
        // variant's, or — a media playlist was opened — that one.
        if (const int v = hls_video_variant_[i]; v >= 0)
            playlist_url_[i] = manifest.variants()[static_cast<std::size_t>(v)].url;
        else if (n_video == 1 && manifest.variants().size() == 1 && !video_renditions)
            playlist_url_[i] = manifest.variants().front().url;
        else if (n_video == 1 && manifest.variants().empty() && !manifest.text().empty()) {
            pl_asked_[i] = 1;
            use_media_playlist(idx, manifest.text(), manifest.final_url());
        }
    }
    demuxer.set_keyframe_index(&keyframes);
    if (has_audio) demuxer.set_on_audio_flush([this] { audio_player.drop_blocked_push(); });
    if (is_hls_) {
        playlist_thread_ = std::jthread([this](std::stop_token st) { playlist_loop(st); });
        prepare_video_stream(vidx);
    }

    ThumbnailStrip::VariantSource thumb_src;
    const int thumb_idx = ThumbnailStrip::pick_hls_source(fc, vidx);
    thumb_src.input   = variant_input(thumb_idx);
    thumb_src.playing = thumb_idx == vidx;
    const ffopen::VariantInput scrub_src = variant_input(vidx);
    if (!scrub_src.url.empty() || !thumb_src.input.url.empty())
        logger::info("MediaSession: helpers open variant playlists — scrub {}, thumbnails {} (stream {})",
                     scrub_src.url.empty() ? "<master>" : scrub_src.url,
                     thumb_src.input.url.empty() ? "<master>" : thumb_src.input.url, thumb_idx);

    // Non-fatal: live streams or formats without a keyframe index won't have thumbnails.
    (void)thumb_strip.init(open_path.c_str(),
                     vidx,
                     demuxer.video_time_base(),
                     duration,
                     thumb_src,
                     hls_master_);

    // Dedicated single-frame decoder for smooth scrubbing.
    // Opened once; receives request() calls during mouse drags. It follows
    // video switches (follow_video_stream) and starts decoding at keyframes
    // the demuxer has read where that is closer than its own seek lands.
    scrub_decoder.set_keyframes(&keyframes);
    scrub_decoder.open(open_path.c_str(),
                       vidx,
                       demuxer.video_time_base(),
                       scrub_src,
                       hls_master_);

    build_info();

    if (opt.test_bitmap_sub) inject_test_bitmap_sub(subtitle_decoder);

    if (preselected_subtitle_ >= 0) {
        demuxer.preselect_subtitle(preselected_subtitle_);
        if (demuxer.subtitle_stream_index() == preselected_subtitle_) {
            if (subtitle_decoder.reopen(demuxer.subtitle_codecpar(), demuxer.subtitle_time_base()))
                logger::info("Subtitles: stream {} read from the start", preselected_subtitle_);
            else
                demuxer.preselect_subtitle(-1);   // then selected the usual way (a switch)
        }
    }

    decoded_audio_stream.store(demuxer.audio_stream_index());
    started_at = std::chrono::steady_clock::now();
    start_threads();
    return true;
}

// Inspector lines, track items and display geometry — static per media.
void MediaSession::build_info()
{
    // ── Stream inspector panel content (static, built once) ──────────────────
    {
        AVFormatContext* fc = demuxer.fmt_ctx();

        // Container section
        inspector_lines.push_back({ "CONTAINER", true });
        inspector_lines.push_back({ std::format("  Format   {}",
            (fc->iformat && fc->iformat->name) ? fc->iformat->name : "?") });
        double dur = demuxer.duration();
        if (dur > 0.0) {
            int d_s = static_cast<int>(dur);
            inspector_lines.push_back({ std::format("  Duration {:02d}:{:02d}:{:02d}",
                d_s / 3600, (d_s % 3600) / 60, d_s % 60) });
        } else {
            inspector_lines.push_back({ "  Duration live" });
        }

        // Video section
        inspector_lines.push_back({ " ", false });  // blank spacer
        inspector_lines.push_back({ "VIDEO", true });
        AVCodecParameters* vpar = demuxer.video_codecpar();
        inspector_lines.push_back({ std::format("  Codec    {}",
            avcodec_get_name(vpar->codec_id)) });
        inspector_lines.push_back({ std::format("  Size     {}x{}",
            video_decoder.width(), video_decoder.height()) });
        {
            AVStream* vs = fc->streams[demuxer.video_stream_index()];
            double fps = (vs->avg_frame_rate.den > 0)
                ? av_q2d(vs->avg_frame_rate) : 0.0;
            inspector_lines.push_back({ std::format("  FPS      {:.4g}", fps) });
        }
        {
            const char* pf = av_get_pix_fmt_name(video_decoder.pixel_format());
            inspector_lines.push_back({ std::format("  Pix fmt  {}", pf ? pf : "?") });
        }
        // Decode path — text is refreshed each frame in the render loop so it
        // flips from "Software" to "VideoToolbox" once the first HW frame lands.
        decode_line_idx = inspector_lines.size();
        inspector_lines.push_back({ std::format("  Decode   {}",
            try_hw_ ? "VideoToolbox (requested)" : "Software") });
        {
            const char* cs = av_color_space_name(video_decoder.colorspace());
            const char* cr = av_color_range_name(video_decoder.color_range());
            inspector_lines.push_back({ std::format("  Color    {} / {}",
                cs ? cs : "?", cr ? cr : "?") });
            const char* trc_name = av_color_transfer_name(video_decoder.color_transfer());
            const char* dynrange =
                renderer.hdr_mode() == 0 ? "HDR (PQ → tonemap)"  :
                renderer.hdr_mode() == 1 ? "HDR (HLG → tonemap)" : "SDR";
            inspector_lines.push_back({ std::format("  Transfer {}", trc_name ? trc_name : "?") });
            inspector_lines.push_back({ std::format("  Dynamic  {}", dynrange) });
        }

        // Audio section
        if (has_audio) {
            inspector_lines.push_back({ " ", false });  // blank spacer
            inspector_lines.push_back({ "AUDIO", true });
            AVCodecParameters* apar = demuxer.audio_codecpar();
            inspector_lines.push_back({ std::format("  Codec    {}",
                avcodec_get_name(apar->codec_id)) });
            inspector_lines.push_back({ std::format("  Rate     {} Hz",
                apar->sample_rate) });
            inspector_lines.push_back({ std::format("  Channels {}",
                apar->ch_layout.nb_channels) });
            const char* sf = av_get_sample_fmt_name(
                static_cast<AVSampleFormat>(apar->format));
            inspector_lines.push_back({ std::format("  Format   {}", sf ? sf : "?") });
        }
    }

    coded_w = video_decoder.width();
    coded_h = video_decoder.height();

    // Compute display dimensions applying SAR and rotation metadata.
    // Priority: stream-level SAR > codec-level SAR > 1:1 (square pixels).
    AVRational sar = demuxer.video_sar();
    if (sar.num <= 0 || sar.den <= 0) sar = video_decoder.sample_aspect_ratio();
    if (sar.num <= 0 || sar.den <= 0) sar = {1, 1};

    // Apply SAR to get the "natural" display width (height is unchanged).
    int disp_w = static_cast<int>(coded_w * static_cast<double>(sar.num) / sar.den + 0.5);
    int disp_h = coded_h;
    if (disp_w <= 0) disp_w = coded_w;  // clamp in case of bad metadata

    // Swap display dimensions if the stream carries a 90° or 270° rotation tag.
    // (Common for videos recorded on phones stored landscape with a rotation flag.)
    const double rotation = demuxer.video_rotation();
    const bool transposed = (rotation > 45.0 && rotation < 135.0)   // 90°
                         || (rotation > 225.0 && rotation < 315.0); // 270°
    vid_w = transposed ? disp_h : disp_w;
    vid_h = transposed ? disp_w : disp_h;

    local_media = !ffopen::is_network_url(path);
    // Network media: buffer several seconds of packets ahead (64 video
    // packets is only ~1 s at 60 fps). A seamless track switch keeps playing
    // the old track from this buffer while the new variant's segment
    // downloads, and a repositioning fetch does not drain audio.
    if (!local_media) {
        videoq.set_capacity(kNetVideoPackets);
        audioq.set_capacity(kNetAudioPackets);
    }

    // Sidecar cue times are file-relative; the player clock is stream PTS.
    {
        const AVStream* vs = demuxer.fmt_ctx()->streams[demuxer.video_stream_index()];
        if (vs->start_time != AV_NOPTS_VALUE) video_start_s = vs->start_time * av_q2d(vs->time_base);
    }

    // Estimate one-frame duration from the video stream's average frame rate.
    // Falls back to ~16.67ms (60fps) if the container doesn't declare it.
    {
        AVRational afr = demuxer.fmt_ctx()->streams[demuxer.video_stream_index()]->avg_frame_rate;
        if (afr.num > 0 && afr.den > 0) frame_dt = static_cast<double>(afr.den) / afr.num;
    }
}

// ── Threads ───────────────────────────────────────────────────────────────────

void MediaSession::start_threads()
{
    demux_thread_ = std::jthread([this](std::stop_token st) {
        demuxer.read_loop(st, videoq, audioq, &subq);
        videoq.shutdown();
        audioq.shutdown();
        subq.shutdown();
    });

    video_thread_ = std::jthread([this](std::stop_token st) {
        video_decode_loop(video_decoder, demuxer, videoq, frameq, try_hw_, decode_time_ms, video_epoch,
                          video_seek_applied, video_stats, st);
        frameq.shutdown();
    });

    audio_thread_ = std::jthread([this](std::stop_token st) {
        if (has_audio)
            audio_decode_loop(audio_decoder, audio_player, demuxer, audioq, decoded_audio_stream,
                              audio_drop_until_seek, audio_stats, st);
    });

    // Subtitle decode thread idles until a track is selected (subq receives
    // no packets while subtitle_idx_ < 0). On a track switch the demuxer
    // pushes a flush sentinel into subq; the thread sees the reopen flag and
    // reinitialises the codec.
    subtitle_thread_ = std::jthread([this](std::stop_token st) {
        while (!st.stop_requested()) {
            AVPacket* pkt = nullptr;
            if (!subq.pop(pkt)) break;
            if (pkt == nullptr) continue;                // end of media (a seek may follow)
            if (pkt == flush_sentinel()) {
                subtitle_decoder.flush();
                if (demuxer.consume_subtitle_reopen())
                    (void)subtitle_decoder.reopen(demuxer.subtitle_codecpar(),
                                                  demuxer.subtitle_time_base());
                continue;
            }
            subtitle_decoder.decode_packet(pkt);
            av_packet_free(&pkt);
        }
    });
}

void MediaSession::prepare_video_stream(int stream)
{
    if (!is_hls_ || stream < 0 || stream >= static_cast<int>(playlist_url_.size())
        || playlist_url_[static_cast<std::size_t>(stream)].empty())
        return;
    {
        std::lock_guard lk(pl_mtx_);
        if (pl_asked_[static_cast<std::size_t>(stream)]) return;
        pl_asked_[static_cast<std::size_t>(stream)] = 1;
        pl_queue_.push_back(stream);
    }
    pl_cv_.notify_one();
}

void MediaSession::follow_video_stream(int stream)
{
    if (stream < 0 || stream >= static_cast<int>(stream_tb_.size())) return;
    prepare_video_stream(stream);
    scrub_decoder.retarget(stream, stream_tb_[static_cast<std::size_t>(stream)],
                           video_inputs_[static_cast<std::size_t>(stream)]);
}

void MediaSession::playlist_loop(std::stop_token st)
{
    std::atomic<bool> abort{ false };   // aborts a fetch in progress on stop
    std::stop_callback on_stop(st, [&] { abort.store(true); });
    while (!st.stop_requested()) {
        int stream = -1;
        {
            std::unique_lock lk(pl_mtx_);
            if (!pl_cv_.wait(lk, st, [&] { return !pl_queue_.empty(); })) break;
            stream = pl_queue_.front();
            pl_queue_.erase(pl_queue_.begin());
        }
        const std::string& url = playlist_url_[static_cast<std::size_t>(stream)];
        std::string text;
        if (!ManifestParser::fetch(url, text, &abort)) {
            if (!abort.load())
                logger::warn("Keyframes: stream {} — variant playlist {} not fetched: only keyframes read are listed",
                             stream, url);
            continue;
        }
        use_media_playlist(stream, text, url);
    }
}

void MediaSession::use_media_playlist(int stream, const std::string& text, const std::string& origin)
{
    const hls::MediaPlaylist pl = hls::parse_media_playlist(text);
    const char* why = pl.durations.empty() ? "not a media playlist"
                    : !pl.vod              ? "a live playlist"
                    : pl.discontinuity     ? "it has discontinuities"
                    : pl.gap               ? "it has gaps"
                    : nullptr;
    if (why) {
        logger::info("Keyframes: stream {} — {}: {}, segment starts are not predicted", stream, origin, why);
        return;
    }
    double total = 0.0;
    for (const double d : pl.durations) total += d;
    logger::info("Keyframes: stream {} — {} segments ({:.1f} s{}) in {}", stream, pl.durations.size(), total,
                 pl.independent ? ", independent" : "", origin);
    keyframes.set_segments(stream, pl.durations);
}

int MediaSession::playing_hls_variant(int video_stream, int audio_stream) const
{
    return hls::current_variant(hls_programs_, hls_video_variant_, video_stream, audio_stream,
                                manifest.variants(), hls_kept);
}

PipelineSnapshot MediaSession::pipeline_snapshot() const
{
    PipelineSnapshot s;
    s.videoq = videoq.size();  s.videoq_cap = videoq.capacity();
    s.audioq = audioq.size();  s.audioq_cap = audioq.capacity();
    s.subq   = subq.size();
    s.frameq = frameq.size();  s.frameq_cap = frameq.capacity();

    s.demux       = demuxer.state();
    s.video_pkts  = demuxer.routed_video();
    s.audio_pkts  = demuxer.routed_audio();
    s.sub_pkts    = demuxer.routed_subtitle();
    s.demux_ended = demuxer.ended();
    s.demux_error = demuxer.last_error();

    s.video     = video_stats.get();
    s.video_in  = video_stats.packets.load(std::memory_order_relaxed);
    s.video_out = video_stats.frames.load(std::memory_order_relaxed);
    s.hw        = video_decoder.is_hw_active();

    s.has_audio = has_audio;
    if (has_audio) {
        s.audio            = audio_stats.get();
        if (s.audio == DecodeState::Decoding && audio_player.push_waiting()) s.audio = DecodeState::WaitOutput;
        s.audio_in         = audio_stats.packets.load(std::memory_order_relaxed);
        s.audio_device     = audio_player.device_running();
        s.audio_callbacks  = audio_player.callback_count();
        s.audio_buffered_s = audio_player.buffered_seconds();
        s.audio_clock      = audio_player.clock();
    }
    return s;
}

// ── Teardown ──────────────────────────────────────────────────────────────────

void MediaSession::stop()
{
    // 1. Ask every thread to stop. The demuxer's stop callback also aborts a
    //    blocking network read, so a stalled HLS fetch doesn't hold the join.
    demux_thread_.request_stop();
    video_thread_.request_stop();
    audio_thread_.request_stop();
    subtitle_thread_.request_stop();
    playlist_thread_.request_stop();   // its stop callback aborts a fetch in progress

    // 2. Unblock anything waiting on a queue or on a full audio ring.
    videoq.shutdown();
    audioq.shutdown();
    subq.shutdown();
    frameq.shutdown();
    audio_player.interrupt();   // wake an audio thread blocked on a full ring

    // 3. Join. Nothing touches the queues, decoders or demuxer after this.
    if (demux_thread_.joinable())    demux_thread_.join();
    if (video_thread_.joinable())    video_thread_.join();
    if (audio_thread_.joinable())    audio_thread_.join();
    if (subtitle_thread_.joinable()) subtitle_thread_.join();
    if (playlist_thread_.joinable()) playlist_thread_.join();

    // 4. Free what is still queued (sentinels are not heap packets). Frames
    //    may reference VideoToolbox pixel buffers from the decoder's pool, so
    //    they go before the decoder does (member destruction, below).
    AVPacket* pkt = nullptr;
    for (Queue<AVPacket*>* q : { &videoq, &audioq, &subq })
        while (q->try_pop(pkt))
            if (pkt && pkt != flush_sentinel()) av_packet_free(&pkt);
    AVFrame* f = nullptr;
    while (frameq.try_pop(f)) av_frame_free(&f);

    // 5. Audio device off before the decoder that feeds it goes away.
    audio_player.stop();
}

MediaSession::~MediaSession()
{
    const auto t0 = std::chrono::steady_clock::now();
    if (!path.empty()) logger::info("MediaSession: closing {}", path);
    stop();
    // Remaining members are destroyed in reverse declaration order:
    // subtitle decoder, scrub decoder and thumbnail strip (each joins its own
    // thread, then frees its FFmpeg context / GL textures), the keyframe
    // index (the scrub decoder's thread read it), the renderer (GL
    // textures + its ref on the last VideoToolbox frame), the decoders, the
    // audio device, VMAF (cancels + joins its worker), and finally the
    // demuxer's AVFormatContext. All of it runs on the main thread with the
    // GL context current.
    if (!path.empty())
        logger::info("MediaSession: threads stopped ({:.0f} ms)",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}
