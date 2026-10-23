#include "Demuxer.h"
#include "FfOpen.h"
#include "Logger.h"
#include "DemuxRetry.h"
#include "NetworkLogger.h"
#include "KeyframeIndex.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <format>
#include <string_view>
#include <thread>
#include <chrono>
#include <utility>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libavutil/display.h>
#include <libavutil/opt.h>
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

static std::int64_t steady_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int Demuxer::interrupt_cb(void* opaque)
{
    auto* d = static_cast<Demuxer*>(opaque);
    // A stall, once found, stays: the hls demuxer takes an interrupted
    // segment read as that playlist's end and goes on with what the others
    // still have buffered — every read after must fail too, so that
    // read_loop gets to the error and stops.
    if (d->abort_io_.load() || d->read_stalled_.load()) return 1;
    if (d->is_network_ && d->in_read_.load(std::memory_order_relaxed)
        && steady_us() - d->read_streak_us_.load(std::memory_order_relaxed)
               > static_cast<std::int64_t>(kReadStallSecs) * 1'000'000) {
        d->read_stalled_.store(true);
        return 1;
    }
    return 0;
}

std::uint64_t Demuxer::request_seek(double seconds, bool key_at_or_after, double show_from)
{
    // Target and mode change together: read_loop takes them as one request.
    std::lock_guard lk(seek_mtx_);
    seek_key_after_ = key_at_or_after;
    seek_show_from_ = show_from;
    seek_target_.store(seconds);
    return ++seek_serial_;
}

void Demuxer::request_audio_switch(int stream_idx, double pos, bool exact, double keep_until)
{
    switch_pos_.store(pos);
    audio_keep_until_.store(keep_until);
    audio_switch_exact_.store(exact);
    pending_audio_switch_.store(stream_idx);
    ++switch_req_;   // after the request: read_loop loads the serial first
}

void Demuxer::request_video_switch(int stream_idx, double pos, bool exact, double keep_until)
{
    switch_pos_.store(pos);
    video_keep_until_.store(keep_until);
    video_switch_exact_.store(exact);
    pending_video_switch_.store(stream_idx);
    ++switch_req_;
}

void Demuxer::request_subtitle_switch(int stream_idx, double pos)
{
    switch_pos_.store(pos);
    pending_subtitle_switch_.store(stream_idx);
    ++switch_req_;
}

void Demuxer::preselect_subtitle(int stream_idx)
{
    if (!fmt_ctx_ || stream_idx >= static_cast<int>(fmt_ctx_->nb_streams)
        || (stream_idx >= 0 && fmt_ctx_->streams[stream_idx]->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE))
        return;
    subtitle_idx_.store(std::max(stream_idx, -1));
    apply_stream_discard();
}

double Demuxer::audio_preroll_s() const
{
    // Three frames (AAC 64 ms, AC-3 96 ms); without a fixed frame size 64 ms.
    const int ai = audio_idx_.load();
    if (!fmt_ctx_ || ai < 0) return 0.064;
    const AVCodecParameters* par = fmt_ctx_->streams[ai]->codecpar;
    return par->frame_size > 0 && par->sample_rate > 0
         ? 3.0 * par->frame_size / par->sample_rate : 0.064;
}

std::string Demuxer::last_error() const
{
    std::lock_guard lk(err_mtx_);
    return last_error_;
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
    // Highest switch request serial handled; published to switch_done_ once a
    // packet has been read after it.
    std::uint64_t switch_handled = 0;
    // End time (seconds) of the furthest audio packet queued since the last
    // seek — where a seamless audio switch continues. -1 = none.
    double audio_ahead = -1.0;
    // A seek's show_from: audio packets that end before it are not queued
    // (the seek lands on a keyframe / HLS segment start before it). -1 = none.
    double audio_drop_before = -1.0;
    // Serial of the latest seek request taken (published with its audio sentinel).
    std::uint64_t seek_served = 0;
    // Fetch latency of the last audio/video switch: stream awaited (-1 none).
    int fetch_stream = -1;
    std::chrono::steady_clock::time_point fetch_since{};
    // Keyframe reporting (kf_index_): every video packet read, in runs of
    // consecutive packets; a run restarts with every repositioning. One
    // that began on an HLS segment start (the open, a backward seek, a
    // switch's reposition) says so with its first packet.
    std::uint32_t kf_run     = 0;
    bool          kf_run_new = true;
    bool          kf_run_seg = is_hls_;   // the open reads from segment 0
    auto restart_run = [&](bool at_segment_start) {
        kf_run_new = true;
        kf_run_seg = is_hls_ && at_segment_start;
    };
    bool read_streak = false;   // reads since the last packet (stall timing, interrupt_cb)
    // A stop request aborts an av_read_frame() stuck on the network (the
    // HTTP layer can otherwise block for rw_timeout plus reconnects).
    std::stop_callback abort_on_stop(st, [this] { abort_io_.store(true); });
    // Push that may wait for room; the wait is published (startup watchdog).
    auto push_to = [this](Queue<AVPacket*>& q, AVPacket* p, DemuxState waiting) {
        set_state(waiting);
        const bool ok = q.push(p);
        set_state(DemuxState::Reading);
        return ok;
    };
    // Packets read wait for room in their queue — but not while a seek or
    // track switch is waiting: paused, nothing drains the queues, and the
    // request would wait for playback to resume (a paused audio switch hung
    // until then). The packet is held back and the request served first; a
    // held packet is queued before anything else is read. A request that
    // repositions the container frees it instead — it is read again, as
    // last_dts (what the re-read drops) counts only packets queued.
    enum class Route { Video, Audio, Subtitle };
    AVPacket* held       = nullptr;
    Route     held_route = Route::Video;
    auto request_waiting = [&] {
        return seek_target_.load() >= 0.0 || switch_req_.load() != switch_handled;
    };
    static constexpr auto kRequestPoll = std::chrono::milliseconds(10);
    // Queue `p` (a packet the queue then owns) for its decoder; false: held.
    auto deliver = [&](AVPacket* p, Route route) {
        Queue<AVPacket*>& q   = route == Route::Video ? videoq : route == Route::Audio ? audioq : *subq;
        const int         si  = p->stream_index;
        const int64_t     dts = p->dts;   // the decoder may free p once it is queued
        set_state(route == Route::Video ? DemuxState::PushVideo
                : route == Route::Audio ? DemuxState::PushAudio : DemuxState::PushSubtitle);
        const auto r = q.push_or_give_up(p, request_waiting, kRequestPoll);
        set_state(DemuxState::Reading);
        if (r == Queue<AVPacket*>::PushResult::GaveUp) {
            held       = p;
            held_route = route;
            return false;
        }
        if (r == Queue<AVPacket*>::PushResult::Closed) {
            av_packet_free(&p);
            return true;
        }
        if (si < static_cast<int>(last_dts.size()) && dts != AV_NOPTS_VALUE) last_dts[si] = dts;
        (route == Route::Video ? routed_video_ : route == Route::Audio ? routed_audio_ : routed_sub_)
            .fetch_add(1, std::memory_order_relaxed);
        return true;
    };
    // End of media: the decoders drain what they hold on an end marker
    // (nullptr) each, queued like packets, then read_loop idles until a seek
    // or track switch. Markers not queued yet (a request came first): bits.
    static constexpr int kEndVideo = 1, kEndAudio = 2, kEndSubtitle = 4;
    int  end_markers = 0;
    bool at_end      = false;
    auto push_end_markers = [&] {
        for (const int bit : { kEndVideo, kEndAudio, kEndSubtitle }) {
            if (!(end_markers & bit)) continue;
            Queue<AVPacket*>& q = bit == kEndVideo ? videoq : bit == kEndAudio ? audioq : *subq;
            set_state(bit == kEndVideo ? DemuxState::PushVideo
                    : bit == kEndAudio ? DemuxState::PushAudio : DemuxState::PushSubtitle);
            const auto r = q.push_or_give_up(nullptr, request_waiting, kRequestPoll);
            set_state(DemuxState::Reading);
            if (r == Queue<AVPacket*>::PushResult::GaveUp) return false;
            end_markers &= ~bit;
        }
        return true;
    };
    set_state(DemuxState::Reading);

    while (!st.stop_requested()) {
        // The container was repositioned this iteration (a seek, or a track
        // switch's reposition): reading goes on from somewhere else.
        bool moved = false;
        // ── Handle pending stream switches ────────────────────────────────
        // Read the request serial first: a request arriving after this point
        // keeps its own (higher) serial and is handled next iteration.
        const std::uint64_t req_serial = switch_req_.load();
        const int  audio_sw  = pending_audio_switch_.exchange(-1);
        const int  video_sw  = pending_video_switch_.exchange(-1);
        const int  sub_sw    = pending_subtitle_switch_.exchange(-2);    // -2 = no request
        const bool new_audio = audio_sw >= 0 && audio_sw != audio_idx_;
        const bool new_video = video_sw >= 0 && video_sw != video_idx_;
        const bool new_sub   = sub_sw != -2 && sub_sw != subtitle_idx_;
        double exact_seek = -1.0;   // paused video switch: full seek to this position
        if (new_audio || new_video || new_sub) {
            const double pos = switch_pos_.load();
            // Where the container is repositioned (seconds), and per stream
            // what the decoders already have.
            double repo = std::numeric_limits<double>::infinity();
            double audio_from = -1.0;   // new audio track: drop packets before this
            // A stream routed from now on starts afresh: what it delivered
            // when routed before (and a re-read drop point left from a
            // reposition) would drop its packets after the next one.
            auto start_fresh = [&](int si) {
                if (si < 0 || si >= static_cast<int>(last_dts.size())) return;
                last_dts[si]   = AV_NOPTS_VALUE;
                resync_dts[si] = AV_NOPTS_VALUE;
            };

            // Seamless switches keep the old track's queued packets that start
            // before `until` (seconds) and drop the rest; returns where the
            // kept ones end (-1 if none). Sentinels keep their place.
            auto trim_queue = [&](Queue<AVPacket*>& q, int old_stream, double until) {
                std::vector<AVPacket*> items;
                AVPacket* p = nullptr;
                while (q.try_pop(p)) items.push_back(p);
                const AVRational tb = fmt_ctx_->streams[old_stream]->time_base;
                double end = -1.0;
                for (AVPacket* it : items) {
                    if (it == nullptr || it == flush_sentinel()) { q.push(it); continue; }
                    const int64_t ts = it->dts != AV_NOPTS_VALUE ? it->dts : it->pts;
                    if (until >= 0.0 && ts != AV_NOPTS_VALUE && ts * av_q2d(tb) >= until) {
                        av_packet_free(&it);
                        continue;
                    }
                    if (const int64_t pt = it->pts != AV_NOPTS_VALUE ? it->pts : ts; pt != AV_NOPTS_VALUE)
                        end = std::max(end, (pt + std::max<int64_t>(it->duration, 0)) * av_q2d(tb));
                    q.push(it);
                }
                return end;
            };

            if (new_video) {
                const bool exact = video_switch_exact_.load() && pos >= 0.0;
                // Seamless: the new track takes over where the kept packets of
                // the old one end. Exact (paused): drop them, resume at pos.
                double resume = -1.0;
                if (!exact) {
                    resume = trim_queue(videoq, video_idx_, video_keep_until_.load());
                    if (resume < 0.0) resume = std::max(pos, video_keep_until_.load());
                    if (resume >= 0.0) read_ahead_pts_.store(resume, std::memory_order_relaxed);
                } else {
                    resume = pos;
                }
                if (exact) {
                    AVPacket* stale;
                    while (videoq.try_pop(stale))
                        if (stale && stale != flush_sentinel()) av_packet_free(&stale);
                    exact_seek = pos;
                }
                video_idx_ = video_sw;
                start_fresh(video_sw);
                video_resume_s_.store(resume);
                video_seamless_.store(!exact);
                video_reopen_.store(true);
                // Exact: the seek below queues the one sentinel, which then
                // also reopens the decoder. A second one would race the
                // decode thread for the seek's show_from.
                if (!exact) push_to(videoq, flush_sentinel(), DemuxState::PushVideo);
                if (resume >= 0.0) repo = std::min(repo, resume);
                trace_video_pkt_ = trace_video_key_ = true;
                logger::info("Video stream switched to index {} ({}, resume at {:.3f}) (t={:.3f})",
                             video_sw, exact ? "exact" : "seamless", resume, logger::uptime());
            }

            if (new_audio) {
                // Like video: playing — the queued audio of the old track still
                // plays and the new one continues where it ends; paused —
                // drop it and start the new track at the audible position.
                const bool exact = audio_switch_exact_.load() || audio_ahead < 0.0;
                if (exact) {
                    AVPacket* stale;
                    while (audioq.try_pop(stale))
                        if (stale && stale != flush_sentinel()) av_packet_free(&stale);
                    audio_from = pos;
                } else {
                    audio_from = trim_queue(audioq, audio_idx_, audio_keep_until_.load());
                    if (audio_from < 0.0) audio_from = std::max(pos, audio_keep_until_.load());
                }
                audio_ahead = -1.0;
                audio_idx_ = audio_sw;
                start_fresh(audio_sw);
                audio_seamless_.store(!exact);
                audio_reopen_.store(true);
                if (exact && on_audio_flush_) on_audio_flush_();   // (set_on_audio_flush)
                {
                    std::lock_guard lk(show_mtx_);
                    audio_start_ = { audio_start_.gen + 1, audio_from };
                }
                push_to(audioq, flush_sentinel(), DemuxState::PushAudio);
                if (audio_from >= 0.0) repo = std::min(repo, audio_from);
                logger::info("Audio stream switched to index {} ({}, from {:.3f}) (t={:.3f})",
                             audio_sw, exact ? "exact" : "seamless", audio_from, logger::uptime());
            }

            if (new_sub) {
                if (subq) {
                    AVPacket* stale;
                    while (subq->try_pop(stale))
                        if (stale && stale != flush_sentinel()) av_packet_free(&stale);
                    push_to(*subq, flush_sentinel(), DemuxState::PushSubtitle);
                }
                // Reopen flag before publishing the index: the subtitle thread
                // reads codecpar via subtitle_idx_ once it sees the sentinel.
                if (sub_sw >= 0) subtitle_reopen_.store(true);
                subtitle_idx_.store(sub_sw);
                start_fresh(sub_sw);
                // Reading is past the current position: reposition so the
                // cues from there on are read. A newly enabled HLS subtitle
                // playlist would otherwise join at the read position — FFmpeg
                // catches it up to the last packet read, the end of seconds of
                // buffered video — and the cues in between never showed. Costs
                // a re-fetch of what the other streams delivered (dropped).
                if (sub_sw >= 0 && pos >= 0.0 && media_end > pos) repo = std::min(repo, pos);
                logger::info("Subtitle stream switched to index {}", sub_sw < 0 ? -1 : sub_sw);
            }

            apply_stream_discard();

            // Reposition so newly enabled streams start at the current
            // position. Required for HLS, not just faster: a playlist that is
            // re-enabled mid-stream resumes its segment demuxer with stale
            // state (fMP4 byte-range playlists then never deliver a packet and
            // the whole read stalls); FFmpeg's HLS seek resets every playlist.
            // A paused (exact) video switch takes the full seek path below.
            const bool reposition = exact_seek < 0.0 && std::isfinite(repo) && video_idx_ >= 0;
            // The new video stream is read from where that lands: the start
            // of a segment (HLS), unless nothing repositions it.
            if (new_video) restart_run(reposition || exact_seek >= 0.0);
            if (reposition) {
                // Streams that keep playing: drop what they already delivered.
                for (const int si : { video_idx_.load(), audio_idx_.load(), subtitle_idx_.load() }) {
                    if (si < 0 || si >= static_cast<int>(last_dts.size())) continue;
                    const bool is_new = (si == video_idx_ && new_video) || (si == audio_idx_ && new_audio)
                                     || (si == subtitle_idx_ && new_sub);
                    if (!is_new) resync_dts[si] = last_dts[si];
                }
                // New audio: drop what precedes the audible position, but
                // for a few packets that warm its decoder up (start_at
                // trims them away, to the sample).
                if (new_audio && audio_from >= 0.0) {
                    const AVStream* as = fmt_ctx_->streams[audio_idx_];
                    resync_dts[audio_idx_] = static_cast<int64_t>(
                        (audio_from - audio_preroll_s()) / av_q2d(as->time_base)) - 1;
                }
                // Inside the media, as for a seek (the hls demuxer refuses a
                // target before its first segment; its own start can lie just
                // after start_time).
                const double media_start = fmt_ctx_->start_time != AV_NOPTS_VALUE
                                         ? static_cast<double>(fmt_ctx_->start_time) / AV_TIME_BASE : 0.0;
                repo = std::max(repo, media_start);
                const AVStream* vs = fmt_ctx_->streams[video_idx_];
                auto reposition_to = [&](double secs) {
                    return av_seek_frame(fmt_ctx_, video_idx_, static_cast<int64_t>(secs / av_q2d(vs->time_base)),
                                         AVSEEK_FLAG_BACKWARD);
                };
                int r = reposition_to(repo);
                if (r < 0 && repo < media_start + 1.0) r = reposition_to(media_start + 1.0);
                if (r < 0) {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(r, errbuf, sizeof(errbuf));
                    logger::warn("Demuxer: reposition to {:.3f} after track switch failed: {}", repo, errbuf);
                } else {
                    moved = true;
                }
                resync_pending = false;
                logger::info("switch-trace: repositioned to {:.3f} (t={:.3f})", repo, logger::uptime());
            }
            if (new_video || new_audio) {
                fetch_stream = new_video ? video_idx_.load() : audio_idx_.load();
                fetch_since  = std::chrono::steady_clock::now();
            }
            switch_handled = req_serial;
        } else if (switch_handled < req_serial) {
            // Requests that changed nothing (already the current track).
            switch_done_.store(req_serial);
            switch_handled = req_serial;
        }

        // ── Handle a pending seek request ─────────────────────────────────
        // Target and mode are taken together under seek_mtx_, so a newer
        // request can't lend its mode to an older pending target.
        double seek_secs = -1.0;
        bool   key_after = false;
        double show_from = -1.0;
        if (seek_target_.load() >= 0.0) {
            std::lock_guard lk(seek_mtx_);
            seek_secs = seek_target_.exchange(-1.0);
            key_after = seek_secs >= 0.0 && seek_key_after_;
            show_from = seek_secs >= 0.0 ? seek_show_from_ : -1.0;
            if (seek_secs >= 0.0) seek_served = seek_serial_;
        }
        // A paused video switch shows the new track from the paused frame,
        // not from the segment start the seek lands on: the seek's own flush
        // sentinel resets the decode thread's drop point to show_from.
        if (exact_seek >= 0.0) { seek_secs = exact_seek; key_after = false; show_from = exact_seek; }
        if (seek_secs >= 0.0) {
            moved = true;
            // A seek supersedes any seamless take-over point; a paused video
            // switch resumes the new track exactly at the seek target.
            video_resume_s_.store(exact_seek >= 0.0 ? exact_seek : -1.0);
            video_seamless_.store(false);
            // Keep the target inside the media. FFmpeg's hls demuxer refuses
            // (EIO) one before its first segment or past its last, and the
            // seek bar spans 0..duration while HLS timestamps can start later
            // (bipbop: 10 s). Before the start: from the first frame. Past
            // the end: into the last segment — show_from keeps the request,
            // every frame is before it, so the decode thread shows the last.
            const double media_start = fmt_ctx_->start_time != AV_NOPTS_VALUE
                                     ? static_cast<double>(fmt_ctx_->start_time) / AV_TIME_BASE : 0.0;
            const double media_dur   = duration();
            if (seek_secs < media_start) {
                seek_secs = media_start;
                if (show_from < media_start) show_from = -1.0;
            } else if (media_dur > 0.0 && seek_secs > media_start + media_dur - 0.5) {
                seek_secs = std::max(media_start, media_start + media_dur - 0.5);
            }
            auto seek_to = [&](double secs) {
                if (key_after && video_idx_ >= 0) {
                    // Forward on the video stream: the hls demuxer then reads the
                    // segment holding the target and starts at its first keyframe
                    // with dts >= target (a backward seek would start at the
                    // segment's first frame).
                    const AVStream* vs = fmt_ctx_->streams[video_idx_];
                    return av_seek_frame(fmt_ctx_, video_idx_, std::llround(secs / av_q2d(vs->time_base)), 0);
                }
                return av_seek_frame(fmt_ctx_, -1, static_cast<int64_t>(secs * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
            };
            int r = seek_to(seek_secs);
            // The hls demuxer's own start (its first packet's dts) can lie
            // just after start_time: a second try inside the first segment.
            if (r < 0 && seek_secs < media_start + 1.0) r = seek_to(media_start + 1.0);
            if (r < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(r, errbuf, sizeof(errbuf));
                logger::warn("Demuxer: seek to {:.3f} failed: {} — reading on from where it was", seek_secs, errbuf);
                // What is read next follows what was read before: nothing to
                // skip up to a target, and the read stretch goes on.
                show_from = -1.0;
            } else {
                restart_run(!key_after);
            }
            read_ahead_pts_.store(-1.0, std::memory_order_relaxed);
            audio_ahead = -1.0;
            audio_seamless_.store(false);
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

            // Tell decode threads to flush their codec buffers. The video one
            // takes show_from and the serial with this sentinel — after the
            // drain, so no frame of the old position comes after them.
            {
                std::lock_guard lk(show_mtx_);
                show_ = { show_.gen + 1, show_from };
            }
            video_seek_serial_.store(seek_served);
            // The audio starts at show_from too (AudioDecoder::start_at), its
            // packets from a few before it (the decoder's warm-up).
            audio_drop_before = audio_idx_ >= 0 && show_from >= 0.0 ? show_from - audio_preroll_s() : -1.0;
            {
                std::lock_guard lk(show_mtx_);
                audio_start_ = { audio_start_.gen + 1, audio_idx_ >= 0 ? show_from : -1.0 };
            }
            audio_seek_serial_.store(seek_served);
            if (audio_idx_ >= 0 && on_audio_flush_) on_audio_flush_();   // (set_on_audio_flush)
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
            restart_run(false);
        }

        // ── A packet held back for a request (deliver) ────────────────────
        if (held) {
            AVPacket*  p      = std::exchange(held, nullptr);
            const bool wanted = held_route == Route::Video ? p->stream_index == video_idx_
                              : held_route == Route::Audio ? p->stream_index == audio_idx_
                              : p->stream_index == subtitle_idx_;
            if (moved || !wanted) av_packet_free(&p);   // read again from there / not routed any more
            else if (!deliver(p, held_route)) continue; // held again: another request first
        }

        // ── At the end of the media ───────────────────────────────────────
        // Idle (the decode threads stay up) until a seek or a track switch
        // arrives — the queues run the demuxer up to seconds ahead of
        // playback, so the user can still seek back or switch tracks while
        // the tail plays out. One that repositions reads on from there.
        if (at_end) {
            if (!moved) {
                if (!push_end_markers()) continue;   // a request first
                if (switch_done_.load() < switch_handled) switch_done_.store(switch_handled);
                logger::info("Demuxer: end of media — waiting for a seek or track switch (t={:.3f})", logger::uptime());
                set_state(DemuxState::AtEnd);
                while (!st.stop_requested() && !request_waiting()
                       && pending_audio_switch_.load() < 0 && pending_video_switch_.load() < 0
                       && pending_subtitle_switch_.load() == -2)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                if (st.stop_requested()) break;
                set_state(DemuxState::Reading);
            }
            at_end      = false;
            end_markers = 0;
            if (AVIOContext* pb = fmt_ctx_->pb) { pb->eof_reached = 0; pb->error = 0; }
            ended_.store(false);
            if (!moved) continue;   // serve what woke it
        }

        // Stall timing (interrupt_cb): from the first read after a packet.
        if (!read_streak) {
            read_streak = true;
            read_streak_us_.store(steady_us(), std::memory_order_relaxed);
        }
        in_read_.store(true, std::memory_order_relaxed);
        int ret = av_read_frame(fmt_ctx_, pkt);
        in_read_.store(false, std::memory_order_relaxed);
        if (ret < 0 && st.stop_requested()) break;   // aborted by the stop callback
        if (ret < 0 && read_stalled_.load()) {
            // Nothing arrived for kReadStallSecs: the connection is lost.
            logger::error("Demuxer: no data for {} s — giving up", kReadStallSecs);
            {
                std::lock_guard lk(err_mtx_);
                last_error_ = std::format("no data from the network for {} s", kReadStallSecs);
            }
            exited_.store(true);
            ended_.store(true);
            switch_done_.store(switch_req_.load());
            push_to(videoq, nullptr, DemuxState::PushVideo);
            if (audio_idx_ >= 0)            push_to(audioq, nullptr, DemuxState::PushAudio);
            if (subq && subtitle_idx_ >= 0) push_to(*subq, nullptr, DemuxState::PushSubtitle);
            break;
        }
        if (ret >= 0) read_streak = false;   // data: the next read starts a new streak
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
                restart_run(false);   // HLS may resume at a later segment: what is read next is not contiguous
                const auto delay = demux::retry_backoff(read_failures);
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror((pb && pb->error < 0) ? pb->error : ret, errbuf, sizeof(errbuf));
                logger::warn("Demuxer: read error '{}' — retry {}/{} in {} ms",
                             errbuf, read_failures,
                             avio_short ? demux::kMaxShortReadRetries : demux::kMaxReadRetries,
                             delay.count());
                if (pb) resync_pending = true;
                // Sleep in slices so quit, seek and track switches stay responsive.
                set_state(DemuxState::Retrying);
                const auto until = std::chrono::steady_clock::now() + delay;
                while (std::chrono::steady_clock::now() < until && !st.stop_requested()
                       && seek_target_.load() < 0.0
                       && pending_audio_switch_.load() < 0
                       && pending_video_switch_.load() < 0
                       && pending_subtitle_switch_.load() == -2)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                set_state(DemuxState::Reading);
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
            if (ret != AVERROR_EOF || (is_network_ && read_failures > 0)) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror((pb && pb->error < 0) ? pb->error : ret, errbuf, sizeof(errbuf));
                std::lock_guard lk(err_mtx_);
                last_error_ = errbuf;
            }
            const bool fatal = ret != AVERROR_EOF || (is_network_ && read_failures > 0);
            if (fatal) exited_.store(true);
            ended_.store(true);
            switch_done_.store(switch_req_.load());   // nothing more will arrive
            if (fatal) {
                // Unrecoverable error — decoders drain what they hold.
                push_to(videoq, nullptr, DemuxState::PushVideo);
                if (audio_idx_ >= 0)            push_to(audioq, nullptr, DemuxState::PushAudio);
                if (subq && subtitle_idx_ >= 0) push_to(*subq, nullptr, DemuxState::PushSubtitle);
                break;
            }
            // End of media: the end markers, then idle (At the end of the media).
            at_end      = true;
            end_markers = kEndVideo | (audio_idx_ >= 0 ? kEndAudio : 0)
                        | (subq && subtitle_idx_ >= 0 ? kEndSubtitle : 0);
            continue;
        }

        // After a resync seek: drop packets the decoders already have.
        // Re-reading them is not progress, so it leaves read_failures alone.
        // Without a dts (Matroska: the keyframe a seek lands on) its pts:
        // dts <= pts, so one not past the last dts queued was queued.
        if (const int si = pkt->stream_index;
            si < static_cast<int>(resync_dts.size()) && resync_dts[si] != AV_NOPTS_VALUE) {
            const int64_t ts = pkt->dts != AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
            if (ts != AV_NOPTS_VALUE && ts <= resync_dts[si]) {
                av_packet_unref(pkt);
                continue;
            }
            resync_dts[si] = AV_NOPTS_VALUE;   // caught up on this stream
        }
        read_failures = 0;   // a new packet: forward progress, fresh budget
        if (switch_done_.load(std::memory_order_relaxed) < switch_handled)
            switch_done_.store(switch_handled);

        if (const int si = pkt->stream_index;
            si == video_idx_ || si == audio_idx_ || (subq && si == subtitle_idx_)) {
            const int64_t ts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts : pkt->dts;
            if (ts != AV_NOPTS_VALUE) {
                const double end = static_cast<double>(ts + std::max<int64_t>(pkt->duration, 0))
                                 * av_q2d(fmt_ctx_->streams[si]->time_base);
                media_end = std::max(media_end, end);
            }
        }
        if (fetch_stream >= 0 && pkt->stream_index == fetch_stream) {
            switch_fetch_s_.store(std::chrono::duration<double>(std::chrono::steady_clock::now() - fetch_since).count());
            fetch_stream = -1;
        }

        if (pkt->stream_index == video_idx_ && (trace_video_pkt_ || trace_video_key_)) {
            const bool key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
            const double s = (pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts)
                           * av_q2d(fmt_ctx_->streams[video_idx_]->time_base);
            if (trace_video_pkt_)
                logger::info("switch-trace: first video packet pts={:.3f}{} (t={:.3f})",
                             s, key ? " key" : "", logger::uptime());
            if (trace_video_key_ && key)
                logger::info("switch-trace: first video keyframe pts={:.3f} (t={:.3f})", s, logger::uptime());
            trace_video_pkt_ = false;
            if (key) trace_video_key_ = false;
        }
        if (pkt->stream_index == video_idx_) {
            const int64_t ts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts : pkt->dts;
            if (ts != AV_NOPTS_VALUE) {
                const double end = static_cast<double>(ts + std::max<int64_t>(pkt->duration, 0))
                                 * av_q2d(fmt_ctx_->streams[video_idx_]->time_base);
                if (end > read_ahead_pts_.load(std::memory_order_relaxed))
                    read_ahead_pts_.store(end, std::memory_order_relaxed);
            }
            if (kf_index_) {
                const bool first = kf_run_new;
                if (kf_run_new) { kf_run = kf_index_->new_run(); kf_run_new = false; }
                if (ts != AV_NOPTS_VALUE) {
                    const AVRational vtb = fmt_ctx_->streams[video_idx_]->time_base;
                    const int64_t    dts = pkt->dts != AV_NOPTS_VALUE ? pkt->dts : ts;
                    kf_index_->observe(video_idx_, ts * av_q2d(vtb), dts * av_q2d(vtb),
                                       (pkt->flags & AV_PKT_FLAG_KEY) != 0, kf_run, first && kf_run_seg);
                }
            }
            if (AVPacket* copy = av_packet_clone(pkt)) deliver(copy, Route::Video);
        } else if (pkt->stream_index == audio_idx_) {
            // After a seek with show_from: skip the audio before it (the one
            // packet holding it still plays), as the video before it is.
            if (audio_drop_before >= 0.0) {
                const int64_t ts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts : pkt->dts;
                if (ts != AV_NOPTS_VALUE
                    && static_cast<double>(ts + std::max<int64_t>(pkt->duration, 0))
                           * av_q2d(fmt_ctx_->streams[audio_idx_]->time_base) <= audio_drop_before) {
                    av_packet_unref(pkt);
                    continue;
                }
                audio_drop_before = -1.0;
            }
            if (const int64_t ts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts : pkt->dts; ts != AV_NOPTS_VALUE)
                audio_ahead = std::max(audio_ahead,
                    static_cast<double>(ts + std::max<int64_t>(pkt->duration, 0))
                        * av_q2d(fmt_ctx_->streams[audio_idx_]->time_base));
            if (AVPacket* copy = av_packet_clone(pkt)) deliver(copy, Route::Audio);
        } else if (subq && pkt->stream_index == subtitle_idx_) {
            if (AVPacket* copy = av_packet_clone(pkt)) deliver(copy, Route::Subtitle);
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&held);
    av_packet_free(&pkt);
    set_state(DemuxState::Stopped);
}

Demuxer::~Demuxer()
{
    close();
}

bool Demuxer::open(const std::string& url_or_path, const std::string& master)
{
    // abort_io_ is not reset here: abort_io() may be called (from another
    // thread) before open() starts, and a Demuxer is opened only once.
    is_network_ = is_network_url(url_or_path);
    if (is_network_)
        avformat_network_init();

    auto make_opts = [&] {
        AVDictionary* opts = nullptr;
        if (!is_network_) return opts;
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
        // Allow non-standard segment extensions (e.g. .mp4a used by some HLS streams).
        // FFmpeg 8 uses allowed_segment_extensions; extension_picky=0 disables the check entirely.
        av_dict_set(&opts, "allowed_segment_extensions", "ALL", 0);
        av_dict_set(&opts, "extension_picky",            "0",   0);
        // HLS open: the demuxer probes the first segment of every variant and
        // rendition playlist, one after another, and by default opens a second
        // connection for each one's next segment as soon as it starts reading
        // — a TLS handshake per playlist for a segment nobody reads before
        // the variant is picked. Off while opening, back to the default (on
        // for HTTP/1.1+ servers) once playback reads (see below).
        av_dict_set(&opts, "http_multiple",              "0",   0);
        // A segment that fails to open is tried again before the hls demuxer
        // moves on to the next one (default: at once): with the network
        // gone each try takes its timeout, so a lost connection is waited
        // out (or given up on, kReadStallSecs) instead of skipping segment
        // after segment; a missing one (404) is still skipped after these.
        av_dict_set(&opts, "seg_max_retry",              "10",  0);
        return opts;
    };
    // One avformat_open_input attempt; on failure it frees the context and
    // nulls fmt_ctx_.
    auto try_open = [&](const std::string& url, const AVInputFormat* fmt) {
        fmt_ctx_ = avformat_alloc_context();
        if (!fmt_ctx_) return AVERROR(ENOMEM);
        fmt_ctx_->interrupt_callback.callback = interrupt_cb;
        fmt_ctx_->interrupt_callback.opaque   = this;
        NetworkLogger::hook_format_context(fmt_ctx_, NetInitiator::Playback);
        AVDictionary* opts = make_opts();
        const int r = avformat_open_input(&fmt_ctx_, url.c_str(), fmt, &opts);
        av_dict_free(&opts);
        return r;
    };
    using Clock = std::chrono::steady_clock;
    auto ms_since = [](Clock::time_point t) {
        return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
    };
    const auto t_open = Clock::now();

    // HLS: a deduplicated copy of the master (HlsMaster.h), handed to the
    // hls demuxer as a data: URL — its URIs are absolute, and request options
    // (rw_timeout) still reach every playlist / segment request.
    int ret = AVERROR_EXIT;   // what an open aborted before it starts reports
    opened_copy_ = false;
    if (!master.empty()) {
        if (const std::string data_url = ffopen::hls_data_url(master); !data_url.empty())
            ret = try_open(data_url, av_find_input_format("hls"));
        opened_copy_ = ret >= 0;
        if (ret < 0 && !abort_io_.load()) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            logger::warn("Demuxer: deduplicated master failed to open ({}) — opening {} as is", errbuf, url_or_path);
        }
    }
    if (!opened_copy_ && !abort_io_.load())
        ret = try_open(url_or_path, nullptr);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        logger::error("avformat_open_input failed: {}", errbuf);
        return false;
    }
    const double open_ms = ms_since(t_open);
    logger::info("Demuxer: avformat_open_input {:.0f} ms ({} streams, {} programs{}) (t={:.3f})",
                 open_ms, fmt_ctx_->nb_streams, fmt_ctx_->nb_programs,
                 opened_copy_ ? ", deduplicated master" : "", logger::uptime());

    const auto t_probe = Clock::now();
    ret = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        logger::error("avformat_find_stream_info failed: {}", errbuf);
        return false;
    }
    logger::info("Demuxer: avformat_find_stream_info {:.0f} ms (t={:.3f})", ms_since(t_probe), logger::uptime());
    is_hls_ = fmt_ctx_->iformat && fmt_ctx_->iformat->name && std::string_view(fmt_ctx_->iformat->name) == "hls";
    if (is_network_ && is_hls_
        && av_opt_set_int(fmt_ctx_, "http_multiple", -1, AV_OPT_SEARCH_CHILDREN) < 0)   // -1 = auto
        logger::warn("Demuxer: could not re-enable HLS next-segment prefetch (http_multiple)");

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
