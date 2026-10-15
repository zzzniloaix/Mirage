#include "MediaSession.h"
#include "FfOpen.h"
#include "Logger.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>

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
                               std::stop_token   st)
{
    AVFrame* frame = av_frame_alloc();
    // Frames carry the decoder generation in AVFrame::opaque so the render
    // thread can tell the first frame of a newly switched track, and their
    // stream time base in AVFrame::time_base (variants differ: 1/60000 vs 1/30000).
    int        epoch      = video_epoch.load();
    AVRational tb         = demuxer.video_time_base();
    bool       trace_next = false;   // log the first frame after a reopen
    // After a track switch the new track is decoded from the keyframe at or
    // before its take-over point; frames before that point are not shown.
    double     drop_before = -1.0;
    // True if `f` is before the take-over point (and must not be queued).
    auto skip = [&](const AVFrame* f) {
        if (drop_before < 0.0) return false;
        const int64_t ts = (f->pts != AV_NOPTS_VALUE) ? f->pts : f->best_effort_timestamp;
        if (ts == AV_NOPTS_VALUE) { drop_before = -1.0; return false; }
        // Half a frame of tolerance: the take-over point is the end of the
        // last old frame, which may round to just past the next frame's pts.
        const double dur = f->duration > 0 ? f->duration * av_q2d(tb) : 0.0;
        if (ts * av_q2d(tb) + 0.5 * dur < drop_before) return true;
        drop_before = -1.0;
        return false;
    };
    int stream = demuxer.video_stream_index();
    auto tag = [&](AVFrame* out) {
        out->opaque    = reinterpret_cast<void*>(MediaSession::frame_tag(epoch, stream));
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
        if (!videoq.pop(pkt)) break;  // shut down or EOF sentinel

        if (pkt == nullptr) {
            // EOF — flush decoder and drain remaining buffered frames.
            decoder.push(nullptr);
            while (decoder.pull(frame)) {
                if (skip(frame)) { av_frame_unref(frame); continue; }
                AVFrame* out = av_frame_clone(frame);
                tag(out);
                if (!frameq.push(out)) { av_frame_free(&out); break; }
                av_frame_unref(frame);
            }
        } else if (pkt == flush_sentinel()) {
            // Seek or stream-switch sentinel. A seamless track switch keeps
            // what the old decoder still holds (B-frame delay) for display;
            // otherwise flush the codec and drop stale frames.
            const bool reopen = demuxer.consume_video_reopen();
            if (reopen && demuxer.consume_video_seamless()) {
                decoder.push(nullptr);
                while (decoder.pull(frame)) {
                    AVFrame* out = av_frame_clone(frame);
                    tag(out);
                    av_frame_unref(frame);
                    if (!frameq.push(out)) { av_frame_free(&out); break; }
                }
            } else {
                decoder.flush();
                AVFrame* stale;
                while (frameq.try_pop(stale)) av_frame_free(&stale);
            }
            // Every sentinel starts a new generation: frames decoded after it
            // belong to the seek / switch that caused it.
            epoch = ++video_epoch;
            // If this sentinel followed a video stream switch, reopen the decoder.
            if (reopen) {
                (void)decoder.open(demuxer.video_codecpar(), try_hw);
                tb     = demuxer.video_time_base();
                stream = demuxer.video_stream_index();
                drop_before = demuxer.consume_video_resume();
                trace_next = true;
                logger::info("switch-trace: video decoder reopened, new frames from {:.3f} (t={:.3f})",
                             drop_before, logger::uptime());
            }
        } else {
            // Normal packet — decode and time.
            auto t0 = std::chrono::steady_clock::now();
            decoder.push(pkt);
            av_packet_free(&pkt);

            int pulled = 0;
            while (decoder.pull(frame)) {
                ++pulled;
                if (skip(frame)) { av_frame_unref(frame); continue; }
                AVFrame* out = av_frame_clone(frame);
                tag(out);
                if (!frameq.push(out)) {
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
}

// Audio decode thread: pops packets from audioq, decodes into AudioPlayer ring buffer.
static void audio_decode_loop(AudioDecoder&     audio_decoder,
                               AudioPlayer&      audio_player,
                               Demuxer&          demuxer,
                               Queue<AVPacket*>& audioq,
                               std::atomic<int>& audio_stream,
                               std::stop_token   st)
{
    int reopened_stream = -1;   // set by a switch until its first packet is decoded
    while (!st.stop_requested()) {
        AVPacket* pkt = nullptr;
        if (!audioq.pop(pkt)) break;
        if (pkt == nullptr) continue;  // end of media: the demuxer may still serve a seek / switch
        if (pkt == flush_sentinel()) {
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
            continue;
        }
        audio_decoder.decode(pkt, audio_player);
        av_packet_free(&pkt);
        // First packet of a switched track decoded: its audio is queued.
        if (reopened_stream >= 0) {
            audio_stream.store(reopened_stream);
            reopened_stream = -1;
        }
    }
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

    if (!demuxer.open(open_path)) {
        err = cancelled() ? std::string("Open cancelled") : "Failed to open: " + open_path;
        return false;
    }
    if (cancelled()) return false;
    if (demuxer.video_stream_index() < 0) {
        logger::error("No video stream — nothing to display");
        err = "No video stream in: " + open_path;
        return false;
    }

    // ── Manifest parser (HLS / DASH only — non-fatal) ─────────────────────────
    if (ManifestParser::is_manifest(open_path.c_str())) {
        if (!manifest.parse(open_path.c_str()))
            logger::warn("ManifestParser: failed to parse {}", open_path);
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

    // Non-fatal: live streams or formats without a keyframe index won't have thumbnails.
    (void)thumb_strip.init(open_path.c_str(),
                     demuxer.video_stream_index(),
                     demuxer.video_time_base(),
                     duration);

    // Dedicated single-frame decoder for smooth scrubbing.
    // Opened once; receives request() calls during mouse drags.
    scrub_decoder.open(open_path.c_str(),
                       demuxer.video_stream_index(),
                       demuxer.video_time_base());

    build_info();

    if (opt.test_bitmap_sub) inject_test_bitmap_sub(subtitle_decoder);

    decoded_audio_stream.store(demuxer.audio_stream_index());
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
        video_decode_loop(video_decoder, demuxer, videoq, frameq, try_hw_, decode_time_ms, video_epoch, st);
        frameq.shutdown();
    });

    audio_thread_ = std::jthread([this](std::stop_token st) {
        if (has_audio)
            audio_decode_loop(audio_decoder, audio_player, demuxer, audioq, decoded_audio_stream, st);
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

// ── Teardown ──────────────────────────────────────────────────────────────────

void MediaSession::stop()
{
    // 1. Ask every thread to stop. The demuxer's stop callback also aborts a
    //    blocking network read, so a stalled HLS fetch doesn't hold the join.
    demux_thread_.request_stop();
    video_thread_.request_stop();
    audio_thread_.request_stop();
    subtitle_thread_.request_stop();

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
    // thread, then frees its FFmpeg context / GL textures), the renderer (GL
    // textures + its ref on the last VideoToolbox frame), the decoders, the
    // audio device, VMAF (cancels + joins its worker), and finally the
    // demuxer's AVFormatContext. All of it runs on the main thread with the
    // GL context current.
    if (!path.empty())
        logger::info("MediaSession: threads stopped ({:.0f} ms)",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}
