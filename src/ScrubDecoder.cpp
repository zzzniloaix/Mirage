#include "ScrubDecoder.h"
#include "Logger.h"
#include "FfOpen.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

ScrubDecoder::~ScrubDecoder() { close(); }

void ScrubDecoder::open(const std::string& url,
                        int video_stream_idx,
                        AVRational time_base)
{
    thread_ = std::jthread([this, url, video_stream_idx, time_base]
                           (std::stop_token st) {
        decode_loop(st, url, video_stream_idx, time_base);
    });
}

void ScrubDecoder::close()
{
    thread_ = {};   // request_stop() then join

    std::lock_guard lk(result_mtx_);
    if (result_) { av_frame_free(&result_); result_ = nullptr; }
}

void ScrubDecoder::request(double target_pts)
{
    {
        std::lock_guard lk(req_mtx_);
        req_pts_     = target_pts;
        req_pending_ = true;
    }
    req_cv_.notify_one();
}

AVFrame* ScrubDecoder::poll_frame()
{
    std::lock_guard lk(result_mtx_);
    AVFrame* f = result_;
    result_    = nullptr;
    return f;
}

void ScrubDecoder::decode_loop(std::stop_token st, std::string url,
                                int video_stream_idx, AVRational time_base)
{
    // Network inputs time out, and a stop request aborts blocking I/O so
    // close() can join promptly even while the URL is unreachable.
    AVFormatContext* fmt_ctx = nullptr;
    if (ffopen::open_input(&fmt_ctx, url, st, NetInitiator::Scrub) < 0) {
        if (!st.stop_requested())
            logger::error("ScrubDecoder: failed to open '{}'", url);
        return;
    }
    // Only the video stream is needed. Discard the rest before probing so
    // HLS doesn't fetch every variant's segments (stream indices match the
    // playback Demuxer's: same URL, same demuxer).
    for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i)
        if (static_cast<int>(i) != video_stream_idx)
            fmt_ctx->streams[i]->discard = AVDISCARD_ALL;
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0
        || video_stream_idx < 0
        || video_stream_idx >= static_cast<int>(fmt_ctx->nb_streams)) {
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVStream*       stream    = fmt_ctx->streams[video_stream_idx];
    const AVCodec*  codec     = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, stream->codecpar);
    codec_ctx->thread_count = 2;   // don't starve the playback decoder
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();

    // Wake the wait() below whenever stop is requested.
    // Lock-then-notify so a wait() that has just checked its predicate
    // can't miss the wake-up (close() would then join forever).
    std::stop_callback stop_wake(st, [this] {
        { std::lock_guard lk(req_mtx_); }
        req_cv_.notify_all();
    });

    logger::info("ScrubDecoder: ready");

    while (!st.stop_requested()) {
        // Block until a request arrives (or stop).
        double target_pts;
        {
            std::unique_lock lk(req_mtx_);
            req_cv_.wait(lk, [&] { return req_pending_ || st.stop_requested(); });
            if (st.stop_requested()) break;
            target_pts   = req_pts_;
            req_pending_ = false;
        }

        const int64_t target_ts =
            static_cast<int64_t>(target_pts / av_q2d(time_base));

        // Seek to the nearest keyframe at or before the target.
        avformat_seek_file(fmt_ctx, video_stream_idx,
                           INT64_MIN, target_ts, target_ts, 0);
        avcodec_flush_buffers(codec_ctx);

        // Decode until we get a frame at or past the target.
        // Abort early when a newer request has already arrived.
        bool got = false;
        while (!got && !st.stop_requested()) {
            {
                std::lock_guard lk(req_mtx_);
                if (req_pending_) break;   // stale — newer request waiting
            }

            int ret = av_read_frame(fmt_ctx, pkt);
            if (ret < 0) break;   // EOF or read error

            if (pkt->stream_index == video_stream_idx) {
                avcodec_send_packet(codec_ctx, pkt);
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    double pts = (frame->pts != AV_NOPTS_VALUE)
                                 ? frame->pts * av_q2d(time_base)
                                 : target_pts;
                    if (pts >= target_pts - 0.001) {
                        // Post the result, replacing any frame not yet consumed.
                        AVFrame* out = av_frame_clone(frame);
                        {
                            std::lock_guard rlk(result_mtx_);
                            if (result_) av_frame_free(&result_);
                            result_ = out;
                        }
                        av_frame_unref(frame);
                        got = true;
                        break;
                    }
                    av_frame_unref(frame);
                }
            }
            av_packet_unref(pkt);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    logger::info("ScrubDecoder: stopped");
}
