#include "Demuxer.h"
#include "Logger.h"
#include "DemuxRetry.h"

#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libavutil/display.h>
#include <libavcodec/packet.h>
}

// AVIOInterruptCB: non-zero aborts the blocking FFmpeg call in progress.
// av_get_*_fmt_name() return nullptr for *_NONE (e.g. a stream whose
// parameters avformat_find_stream_info could not determine); passing that
// to std::format as const char* dereferences null.
static const char* pix_fmt_name(AVPixelFormat f)
{
    const char* n = av_get_pix_fmt_name(f);
    return n ? n : "none";
}
static const char* sample_fmt_name(AVSampleFormat f)
{
    const char* n = av_get_sample_fmt_name(f);
    return n ? n : "none";
}

static int demux_interrupt_cb(void* opaque)
{
    return static_cast<const std::atomic<bool>*>(opaque)->load() ? 1 : 0;
}

void Demuxer::request_seek(double seconds)
{
    seek_target_.store(seconds);
}

void Demuxer::request_audio_switch(int stream_idx)
{
    pending_audio_switch_.store(stream_idx);
}

void Demuxer::request_video_switch(int stream_idx)
{
    pending_video_switch_.store(stream_idx);
}

void Demuxer::request_subtitle_switch(int stream_idx)
{
    pending_subtitle_switch_.store(stream_idx);
}

bool Demuxer::consume_audio_reopen()
{
    return audio_reopen_.exchange(false);
}

bool Demuxer::consume_video_reopen()
{
    return video_reopen_.exchange(false);
}

bool Demuxer::consume_subtitle_reopen()
{
    return subtitle_reopen_.exchange(false);
}

void Demuxer::read_loop(std::stop_token st,
                        Queue<AVPacket*>& videoq,
                        Queue<AVPacket*>& audioq,
                        Queue<AVPacket*>* subq)
{
    AVPacket* pkt = av_packet_alloc();
    // Consecutive transient network read errors without forward progress.
    // Reset only when a new packet is delivered (or on a seek), never by the
    // duplicates re-read and dropped after a resync, so a failure that
    // recurs at the same position still exhausts the retry budget.
    int read_failures = 0;
    // End time (seconds) of the furthest packet delivered on any routed
    // stream. Used to tell a short read before the media's end (dropped
    // connection) from a real EOF; unlike read_ahead_pts_ it covers audio
    // too, so a video track shorter than the container is not mistaken
    // for a truncated download.
    double media_end = -1.0;
    // Byte-stream (non-HLS) network recovery: dts of the last packet routed
    // per stream; after a failed read the demuxer is re-seeked to it and
    // packets at or before resync_dts[stream] are dropped as duplicates.
    std::vector<int64_t> last_dts(fmt_ctx_->nb_streams, AV_NOPTS_VALUE);
    std::vector<int64_t> resync_dts(fmt_ctx_->nb_streams, AV_NOPTS_VALUE);
    bool resync_pending = false;
    // A stop request aborts an av_read_frame() stuck on the network (the
    // HTTP layer can otherwise block for rw_timeout plus reconnects).
    std::stop_callback abort_on_stop(st, [this] { abort_io_.store(true); });

    while (!st.stop_requested()) {
        // ── Handle pending stream switches ────────────────────────────────
        int audio_sw = pending_audio_switch_.exchange(-1);
        if (audio_sw >= 0 && audio_sw != audio_idx_) {
            AVPacket* stale;
            while (audioq.try_pop(stale))
                if (stale && stale != flush_sentinel()) av_packet_free(&stale);
            audio_idx_ = audio_sw;
            audioq.push(flush_sentinel());
            audio_reopen_.store(true);
            logger::info("Audio stream switched to index {}", audio_idx_);
        }

        int video_sw = pending_video_switch_.exchange(-1);
        if (video_sw >= 0 && video_sw != video_idx_) {
            AVPacket* stale;
            while (videoq.try_pop(stale))
                if (stale && stale != flush_sentinel()) av_packet_free(&stale);
            video_idx_ = video_sw;
            videoq.push(flush_sentinel());
            video_reopen_.store(true);
            logger::info("Video stream switched to index {}", video_idx_);
        }

        int sub_sw = pending_subtitle_switch_.exchange(-2);    // -2 = no request
        if (sub_sw != -2 && sub_sw != subtitle_idx_) {
            if (subq) {
                AVPacket* stale;
                while (subq->try_pop(stale))
                    if (stale && stale != flush_sentinel()) av_packet_free(&stale);
                subq->push(flush_sentinel());
            }
            // Reopen flag before publishing the index: the subtitle thread
            // reads codecpar via subtitle_idx_ once it sees the sentinel.
            if (sub_sw >= 0) subtitle_reopen_.store(true);
            subtitle_idx_.store(sub_sw);
            logger::info("Subtitle stream switched to index {}", sub_sw < 0 ? -1 : sub_sw);
        }

        if (audio_sw >= 0 || video_sw >= 0 || sub_sw != -2)
            apply_stream_discard();

        // ── Handle a pending seek request ─────────────────────────────────
        double seek_secs = seek_target_.exchange(-1.0);
        if (seek_secs >= 0.0) {
            int64_t ts = static_cast<int64_t>(seek_secs * AV_TIME_BASE);
            av_seek_frame(fmt_ctx_, -1, ts, AVSEEK_FLAG_BACKWARD);
            read_ahead_pts_.store(-1.0, std::memory_order_relaxed);
            read_failures  = 0;   // a new position gets a fresh retry budget
            media_end      = -1.0;
            resync_pending = false;
            std::fill(last_dts.begin(),   last_dts.end(),   AV_NOPTS_VALUE);
            std::fill(resync_dts.begin(), resync_dts.end(), AV_NOPTS_VALUE);

            // Drain and free all stale packets still sitting in the queues
            AVPacket* stale;
            while (videoq.try_pop(stale))
                if (stale && stale != flush_sentinel()) av_packet_free(&stale);
            while (audioq.try_pop(stale))
                if (stale && stale != flush_sentinel()) av_packet_free(&stale);
            if (subq)
                while (subq->try_pop(stale))
                    if (stale && stale != flush_sentinel()) av_packet_free(&stale);

            // Tell decode threads to flush their codec buffers
            videoq.push(flush_sentinel());
            if (audio_idx_ >= 0)            audioq.push(flush_sentinel());
            if (subq && subtitle_idx_ >= 0) subq->push(flush_sentinel());
        }

        // ── Resume a byte stream after a failed read ──────────────────────
        // Once the HTTP layer has given up on a connection it just reports
        // EOF; a demuxer seek makes it reconnect at the new offset. Seek back
        // to the last routed packet (keyframe at or before it) and drop what
        // was already delivered, so the decoders see a seamless stream.
        if (resync_pending) {
            resync_pending = false;
            AVIOContext* pb = fmt_ctx_->pb;
            pb->eof_reached = 0;
            pb->error       = 0;
            const int ref = (video_idx_ >= 0 && last_dts[video_idx_] != AV_NOPTS_VALUE)
                                ? video_idx_ : audio_idx_;
            if (ref >= 0 && last_dts[ref] != AV_NOPTS_VALUE) {
                resync_dts = last_dts;
                av_seek_frame(fmt_ctx_, ref, last_dts[ref], AVSEEK_FLAG_BACKWARD);
            }
        }

        int ret = av_read_frame(fmt_ctx_, pkt);
        if (ret < 0 && st.stop_requested()) break;   // aborted by the stop callback
        if (ret < 0) {
            AVIOContext* pb = fmt_ctx_->pb;   // nullptr for HLS (AVFMT_NOFILE)
            const bool avio_eof = pb && avio_feof(pb);
            // A byte-layer error, or bytes that stopped short of the known
            // size before the media's end (a dead HTTP connection reads as
            // plain EOF), means the connection failed — not end of file.
            const bool avio_error = pb && pb->error < 0;
            bool avio_short = false;
            if (pb && is_network_ && !avio_error) {
                const int64_t size = avio_size(pb);
                const double  dur  = duration();
                // Packet timestamps include the container's start offset.
                const double  start = fmt_ctx_->start_time != AV_NOPTS_VALUE
                                    ? static_cast<double>(fmt_ctx_->start_time) / AV_TIME_BASE
                                    : 0.0;
                avio_short = size > 0 && avio_tell(pb) < size
                          && (dur <= 0.0 || media_end < start + dur - 1.0);
            }
            const demux::ReadError kind = demux::classify_read_error(
                ret, is_network_, avio_eof, avio_error, avio_short, read_failures);

            if (kind == demux::ReadError::Wait) {
                // Network stream not ready yet (e.g. waiting for next HLS segment).
                // Yield briefly and retry — do NOT treat this as EOF.
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            if (kind == demux::ReadError::Retry) {
                // Transient network failure (dropped TLS/HTTP connection, a
                // truncated HLS segment). HLS moves on to the next segment by
                // itself; a plain byte stream is re-seeked (resync_pending).
                ++read_failures;
                const auto delay = demux::retry_backoff(read_failures);
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror((pb && pb->error < 0) ? pb->error : ret, errbuf, sizeof(errbuf));
                logger::warn("Demuxer: read error '{}' — retry {}/{} in {} ms",
                             errbuf, read_failures,
                             avio_short ? demux::kMaxShortReadRetries : demux::kMaxReadRetries,
                             delay.count());
                if (pb) resync_pending = true;
                // Sleep in slices so quit, seek and track switches stay responsive.
                const auto until = std::chrono::steady_clock::now() + delay;
                while (std::chrono::steady_clock::now() < until && !st.stop_requested()
                       && seek_target_.load() < 0.0
                       && pending_audio_switch_.load() < 0
                       && pending_video_switch_.load() < 0
                       && pending_subtitle_switch_.load() == -2)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (is_network_ && read_failures > 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror((pb && pb->error < 0) ? pb->error : ret, errbuf, sizeof(errbuf));
                logger::error("Demuxer: giving up after {} failed reads: {}",
                              read_failures, errbuf);
            } else if (ret != AVERROR_EOF) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                logger::info("Demuxer: read ended: {}", errbuf);
            }
            // True EOF or unrecoverable error — signal decoders to flush and stop.
            videoq.push(nullptr);
            if (audio_idx_ >= 0)            audioq.push(nullptr);
            if (subq && subtitle_idx_ >= 0) subq->push(nullptr);
            break;
        }

        // After a resync seek: drop packets the decoders already have.
        // Re-reading them is not progress, so it leaves read_failures alone.
        if (const int si = pkt->stream_index;
            si < static_cast<int>(resync_dts.size()) && resync_dts[si] != AV_NOPTS_VALUE) {
            if (pkt->dts != AV_NOPTS_VALUE && pkt->dts <= resync_dts[si]) {
                av_packet_unref(pkt);
                continue;
            }
            resync_dts[si] = AV_NOPTS_VALUE;   // caught up on this stream
        }
        read_failures = 0;   // a new packet: forward progress, fresh budget

        if (const int si = pkt->stream_index;
            si == video_idx_ || si == audio_idx_ || (subq && si == subtitle_idx_)) {
            const int64_t ts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts : pkt->dts;
            if (ts != AV_NOPTS_VALUE) {
                const double end = static_cast<double>(ts + std::max<int64_t>(pkt->duration, 0))
                                 * av_q2d(fmt_ctx_->streams[si]->time_base);
                media_end = std::max(media_end, end);
            }
        }
        if (pkt->stream_index < static_cast<int>(last_dts.size()) && pkt->dts != AV_NOPTS_VALUE)
            last_dts[pkt->stream_index] = pkt->dts;

        if (pkt->stream_index == video_idx_) {
            const int64_t ts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts : pkt->dts;
            if (ts != AV_NOPTS_VALUE) {
                const double end = static_cast<double>(ts + std::max<int64_t>(pkt->duration, 0))
                                 * av_q2d(fmt_ctx_->streams[video_idx_]->time_base);
                if (end > read_ahead_pts_.load(std::memory_order_relaxed))
                    read_ahead_pts_.store(end, std::memory_order_relaxed);
            }
            AVPacket* copy = av_packet_clone(pkt);
            if (!videoq.push(copy))
                av_packet_free(&copy);
        } else if (pkt->stream_index == audio_idx_) {
            AVPacket* copy = av_packet_clone(pkt);
            if (!audioq.push(copy))
                av_packet_free(&copy);
        } else if (subq && pkt->stream_index == subtitle_idx_) {
            AVPacket* copy = av_packet_clone(pkt);
            if (!subq->push(copy))
                av_packet_free(&copy);
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
}

Demuxer::~Demuxer()
{
    close();
}

bool Demuxer::open(const std::string& url_or_path)
{
    abort_io_.store(false);
    is_network_ = is_network_url(url_or_path);
    if (is_network_)
        avformat_network_init();

    AVDictionary* opts = nullptr;
    if (is_network_) {
        av_dict_set(&opts, "timeout",             "5000000", 0);   // 5 s socket connect/recv
        // rw_timeout: hard cap on any single I/O operation (applies both during
        // avformat_open_input and later reads). Prevents a bad URL from taking
        // 5–10 s to fail because reconnect=1 was retrying with backoff up to
        // the default reconnect_delay_max of 120 s.
        av_dict_set(&opts, "rw_timeout",          "8000000", 0);   // 8 s
        av_dict_set(&opts, "reconnect",           "1",       0);
        av_dict_set(&opts, "reconnect_streamed",  "1",       0);
        av_dict_set(&opts, "reconnect_delay_max", "2",       0);   // cap backoff at 2 s
        av_dict_set(&opts, "buffer_size",         "1048576", 0);
        // Allow non-standard segment extensions (e.g. .mp4a used by Disney+/some HLS streams).
        // FFmpeg 8 uses allowed_segment_extensions; extension_picky=0 disables the check entirely.
        av_dict_set(&opts, "allowed_segment_extensions", "ALL", 0);
        av_dict_set(&opts, "extension_picky",            "0",   0);
    }

    fmt_ctx_ = avformat_alloc_context();
    if (fmt_ctx_) {
        fmt_ctx_->interrupt_callback.callback = demux_interrupt_cb;
        fmt_ctx_->interrupt_callback.opaque   = &abort_io_;
    }
    // On failure avformat_open_input frees the context and nulls fmt_ctx_.
    int ret = avformat_open_input(&fmt_ctx_, url_or_path.c_str(), nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        logger::error("avformat_open_input failed: {}", errbuf);
        return false;
    }

    ret = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        logger::error("avformat_find_stream_info failed: {}", errbuf);
        return false;
    }

    // Find best video stream
    const AVCodec* video_codec = nullptr;
    video_idx_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, &video_codec, 0);
    if (video_idx_ < 0)
        logger::warn("No video stream found");

    // Find best audio stream (prefer one linked to the video stream)
    const AVCodec* audio_codec = nullptr;
    audio_idx_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, video_idx_, &audio_codec, 0);
    if (audio_idx_ < 0)
        logger::warn("No audio stream found");

    // No subtitle stream routed until main applies its selection policy
    // (SubtitleSelect::choose_initial) via request_subtitle_switch.
    subtitle_idx_ = -1;

    apply_stream_discard();
    build_track_lists();
    print_stream_info();
    return true;
}

// Discard every stream we are not routing. HLS/DASH expose each variant as
// its own stream, and the demuxer downloads segments for every non-discarded
// one in parallel — without this a 24-variant master playlist fetches all
// renditions at once and starves the selected one. The HLS demuxer re-checks
// discard flags on each read, so updating them after a track switch is enough.
void Demuxer::apply_stream_discard()
{
    if (!fmt_ctx_) return;
    const int sub = subtitle_idx_.load();
    for (unsigned i = 0; i < fmt_ctx_->nb_streams; ++i) {
        const int idx = static_cast<int>(i);
        const bool keep = idx == video_idx_ || idx == audio_idx_ || idx == sub;
        fmt_ctx_->streams[i]->discard = keep ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    }
}

void Demuxer::close()
{
    if (fmt_ctx_) {
        avformat_close_input(&fmt_ctx_);
        fmt_ctx_ = nullptr;
    }
    video_idx_    = -1;
    audio_idx_    = -1;
    subtitle_idx_ = -1;
}

AVCodecParameters* Demuxer::video_codecpar() const
{
    if (video_idx_ < 0) return nullptr;
    return fmt_ctx_->streams[video_idx_]->codecpar;
}

AVCodecParameters* Demuxer::audio_codecpar() const
{
    if (audio_idx_ < 0) return nullptr;
    return fmt_ctx_->streams[audio_idx_]->codecpar;
}

AVCodecParameters* Demuxer::subtitle_codecpar() const
{
    const int idx = subtitle_idx_.load();
    if (idx < 0) return nullptr;
    return fmt_ctx_->streams[idx]->codecpar;
}

AVRational Demuxer::video_time_base() const
{
    if (video_idx_ < 0) return {1, 1};
    return fmt_ctx_->streams[video_idx_]->time_base;
}

AVRational Demuxer::audio_time_base() const
{
    if (audio_idx_ < 0) return {1, 1};
    return fmt_ctx_->streams[audio_idx_]->time_base;
}

AVRational Demuxer::subtitle_time_base() const
{
    const int idx = subtitle_idx_.load();
    if (idx < 0) return {1, 1};
    return fmt_ctx_->streams[idx]->time_base;
}

double Demuxer::duration() const
{
    if (!fmt_ctx_ || fmt_ctx_->duration == AV_NOPTS_VALUE) return -1.0;
    return static_cast<double>(fmt_ctx_->duration) / AV_TIME_BASE;
}

double Demuxer::video_rotation() const
{
    if (video_idx_ < 0 || !fmt_ctx_) return 0.0;
    AVStream* s = fmt_ctx_->streams[video_idx_];
    // FFmpeg 8: display matrix is in codecpar->coded_side_data.
    const AVPacketSideData* sd = av_packet_side_data_get(
        s->codecpar->coded_side_data,
        s->codecpar->nb_coded_side_data,
        AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd || sd->size < static_cast<int>(9 * sizeof(int32_t))) return 0.0;
    // av_display_rotation_get returns CCW degrees; negate for CW.
    double rot = -av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
    // Normalise to [0, 360).
    while (rot < 0.0)    rot += 360.0;
    while (rot >= 360.0) rot -= 360.0;
    return rot;
}

AVRational Demuxer::video_sar() const
{
    if (video_idx_ < 0 || !fmt_ctx_) return {0, 1};
    return fmt_ctx_->streams[video_idx_]->sample_aspect_ratio;
}

bool Demuxer::is_network_url(const std::string& url) const
{
    return url.starts_with("http://")  ||
           url.starts_with("https://") ||
           url.starts_with("rtmp://")  ||
           url.starts_with("rtsp://");
}

void Demuxer::build_track_lists()
{
    audio_tracks_.clear();
    video_tracks_.clear();
    subtitle_tracks_.clear();

    for (unsigned i = 0; i < fmt_ctx_->nb_streams; ++i) {
        AVStream* s = fmt_ctx_->streams[i];
        AVCodecParameters* par = s->codecpar;

        if (par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            SubtitleTrackInfo info;
            info.stream_idx = static_cast<int>(i);
            info.codec_name = avcodec_get_name(par->codec_id);
            switch (par->codec_id) {
                case AV_CODEC_ID_SUBRIP: case AV_CODEC_ID_TEXT:
                case AV_CODEC_ID_ASS:    case AV_CODEC_ID_SSA:
                case AV_CODEC_ID_WEBVTT: case AV_CODEC_ID_MOV_TEXT:
                    info.is_text = true; break;
                default:
                    info.is_text = false; break;
            }
            auto* lang  = av_dict_get(s->metadata, "language", nullptr, 0);
            auto* title = av_dict_get(s->metadata, "title",    nullptr, 0);
            if (lang && std::strcmp(lang->value, "und") != 0) info.lang = lang->value;
            if (title) info.title = title->value;
            info.is_default = (s->disposition & AV_DISPOSITION_DEFAULT) != 0;
            info.is_forced  = (s->disposition & AV_DISPOSITION_FORCED) != 0;
            info.is_hi      = (s->disposition & AV_DISPOSITION_HEARING_IMPAIRED) != 0;
            if (title && lang)
                info.language = std::string(title->value) + " (" + lang->value + ")";
            else if (title) info.language = title->value;
            else if (lang)  info.language = lang->value;
            subtitle_tracks_.push_back(std::move(info));
            continue;
        }

        if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            AudioTrackInfo info;
            info.stream_idx = static_cast<int>(i);
            info.codec_name = avcodec_get_name(par->codec_id);
            info.sample_rate = par->sample_rate;
            info.channels    = par->ch_layout.nb_channels;
            info.bit_rate    = par->bit_rate;

            // Language/title from stream metadata
            auto* lang  = av_dict_get(s->metadata, "language", nullptr, 0);
            auto* title = av_dict_get(s->metadata, "title",    nullptr, 0);
            if (title && lang)
                info.language = std::string(title->value) + " (" + lang->value + ")";
            else if (title)
                info.language = title->value;
            else if (lang)
                info.language = lang->value;

            audio_tracks_.push_back(std::move(info));

        } else if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            VideoTrackInfo info;
            info.stream_idx = static_cast<int>(i);
            info.codec_name = avcodec_get_name(par->codec_id);
            info.width      = par->width;
            info.height     = par->height;
            info.bit_rate   = par->bit_rate;
            AVRational fps  = s->avg_frame_rate;
            info.fps = (fps.den > 0) ? av_q2d(fps) : 0.0;
            video_tracks_.push_back(std::move(info));
        }
    }
}

void Demuxer::print_stream_info() const
{
    logger::info("Container: {}", fmt_ctx_->iformat->long_name);

    if (int64_t dur = fmt_ctx_->duration; dur != AV_NOPTS_VALUE) {
        int total_s = static_cast<int>(dur / AV_TIME_BASE);
        logger::info("Duration:  {:02d}:{:02d}:{:02d}",
            total_s / 3600, (total_s % 3600) / 60, total_s % 60);
    }

    if (video_idx_ >= 0) {
        AVCodecParameters* par = fmt_ctx_->streams[video_idx_]->codecpar;
        const AVCodecDescriptor* desc = avcodec_descriptor_get(par->codec_id);
        AVRational fps = fmt_ctx_->streams[video_idx_]->avg_frame_rate;

        logger::info("Video:     {} {}×{} @ {:.3f} fps  pixel_fmt={}",
            desc ? desc->name : "unknown",
            par->width, par->height,
            av_q2d(fps),
            pix_fmt_name(static_cast<AVPixelFormat>(par->format)));
    }

    if (audio_idx_ >= 0) {
        AVCodecParameters* par = fmt_ctx_->streams[audio_idx_]->codecpar;
        const AVCodecDescriptor* desc = avcodec_descriptor_get(par->codec_id);

        logger::info("Audio:     {} {} Hz  {} ch  fmt={}",
            desc ? desc->name : "unknown",
            par->sample_rate,
            par->ch_layout.nb_channels,
            sample_fmt_name(static_cast<AVSampleFormat>(par->format)));
    }

    if (!subtitle_tracks_.empty()) {
        logger::info("Subtitles: {} embedded track(s)",
                     subtitle_tracks_.size());
    }
}
