#include "SidecarSubtitles.h"
#include "FfOpen.h"
#include "Logger.h"

extern "C" {
#include <libavformat/avformat.h>
}

SidecarSubtitles::~SidecarSubtitles()
{
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
}

void SidecarSubtitles::load(const std::string& path, double video_start_s)
{
    if (worker_.joinable()) { worker_.request_stop(); worker_.join(); }
    path_          = path;
    video_start_s_ = video_start_s;
    ready_.store(false);
    failed_.store(false);
    decoder_.set_persistent(true);

    worker_ = std::jthread([this](std::stop_token st) {
        if (decode_file(path_, decoder_, st)) {
            logger::info("Sidecar subtitles: {} cue(s) from {}", decoder_.event_count(), path_);
            ready_.store(true);
        } else {
            failed_.store(true);
        }
    });
}

std::string SidecarSubtitles::at(double pts) const
{
    if (!ready_.load()) return {};
    return decoder_.at(pts - video_start_s_);
}

bool SidecarSubtitles::decode_file(const std::string& path, SubtitleDecoder& dec,
                                   const std::stop_token& st)
{
    AVFormatContext* fmt = nullptr;
    if (ffopen::open_input(&fmt, path, st) < 0 || !fmt) {
        logger::warn("Sidecar subtitles: cannot open {}", path);
        return false;
    }
    bool ok = false;
    const int idx = (avformat_find_stream_info(fmt, nullptr) >= 0)
        ? av_find_best_stream(fmt, AVMEDIA_TYPE_SUBTITLE, -1, -1, nullptr, 0) : -1;
    if (idx < 0) {
        logger::warn("Sidecar subtitles: no subtitle stream in {}", path);
    } else if (!dec.open(fmt->streams[idx]->codecpar, fmt->streams[idx]->time_base)) {
        logger::warn("Sidecar subtitles: no decoder for {}", path);
    } else {
        AVPacket* pkt = av_packet_alloc();
        while (!st.stop_requested() && av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == idx) dec.decode_packet(pkt);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
        dec.decode_packet(nullptr);   // drain
        ok = !st.stop_requested();
    }
    avformat_close_input(&fmt);
    return ok;
}
