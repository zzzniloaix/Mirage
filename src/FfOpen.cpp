#include "FfOpen.h"
#include "Logger.h"

#include <cstring>
#include <string_view>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/base64.h>
#include <libavutil/dict.h>
}

namespace ffopen {

bool is_network_url(const std::string& url)
{
    return url.starts_with("http://")  ||
           url.starts_with("https://") ||
           url.starts_with("rtmp://")  ||
           url.starts_with("rtsp://");
}

static int interrupt_cb(void* opaque)
{
    return static_cast<const std::stop_token*>(opaque)->stop_requested() ? 1 : 0;
}

std::string hls_data_url(const std::string& playlist)
{
    const int n = static_cast<int>(playlist.size());
    std::string b64(static_cast<std::size_t>(AV_BASE64_SIZE(n)), '\0');
    if (!av_base64_encode(b64.data(), static_cast<int>(b64.size()),
                          reinterpret_cast<const uint8_t*>(playlist.data()), n))
        return {};
    b64.resize(std::strlen(b64.c_str()));
    return "data:application/vnd.apple.mpegurl;base64," + b64;
}

int open_input(AVFormatContext** out, const std::string& url, const std::stop_token& st,
               NetInitiator who, const std::string& master)
{
    *out = nullptr;
    const std::string data_url = master.empty() ? std::string() : hls_data_url(master);
    if (!master.empty() && data_url.empty()) return AVERROR(ENOMEM);
    AVFormatContext* ctx = avformat_alloc_context();
    if (!ctx) return AVERROR(ENOMEM);
    ctx->interrupt_callback.callback = interrupt_cb;
    ctx->interrupt_callback.opaque   = const_cast<std::stop_token*>(&st);
    NetworkLogger::hook_format_context(ctx, who);

    AVDictionary* opts = nullptr;
    if (is_network_url(url)) {
        av_dict_set(&opts, "timeout",    "5000000", 0);   // 5 s socket connect/recv
        av_dict_set(&opts, "rw_timeout", "5000000", 0);   // 5 s cap on any single I/O
        av_dict_set(&opts, "allowed_segment_extensions", "ALL", 0);
        av_dict_set(&opts, "extension_picky",            "0",   0);
        // HLS: no second connection prefetching the next segment. Helpers
        // seek all the time (thumbnail sampling, scrubbing), so that segment
        // is almost never the one read next.
        av_dict_set(&opts, "http_multiple",              "0",   0);
    }
    // On failure avformat_open_input frees ctx and nulls the pointer.
    const int ret = data_url.empty()
        ? avformat_open_input(&ctx, url.c_str(), nullptr, &opts)
        : avformat_open_input(&ctx, data_url.c_str(), av_find_input_format("hls"), &opts);
    av_dict_free(&opts);
    if (ret < 0) return ret;
    *out = ctx;
    return 0;
}

int open_variant(AVFormatContext** out, int* video_idx, const VariantInput& v,
                 const std::stop_token& st, NetInitiator who)
{
    *out = nullptr;
    *video_idx = -1;
    if (v.url.empty()) return AVERROR(EINVAL);

    AVFormatContext* ctx = nullptr;
    int ret = open_input(&ctx, v.url, st, who);
    if (ret < 0) return ret;
    auto reject = [&](const char* why) {
        if (!st.stop_requested())
            logger::warn("{}: variant playlist {} not usable ({}) — opening the master instead",
                         net_initiator_name(who), v.url, why);
        avformat_close_input(&ctx);
        return AVERROR(EINVAL);
    };
    if (!ctx->iformat || !ctx->iformat->name || std::string_view(ctx->iformat->name) != "hls")
        return reject("not an HLS playlist");

    int idx = -1;
    for (unsigned i = 0; i < ctx->nb_streams; ++i) {
        if (ctx->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) continue;
        if (idx >= 0) return reject("several video streams");
        idx = static_cast<int>(i);
    }
    if (idx < 0) return reject("no video stream");

    // One media playlist: every stream comes out of the same segments, so
    // probing them all fetches nothing extra (and a discarded stream would
    // keep the probe reading until its limits, waiting for its packets).
    if ((ret = avformat_find_stream_info(ctx, nullptr)) < 0) {
        if (st.stop_requested()) { avformat_close_input(&ctx); return ret; }
        return reject("probe failed");
    }

    const AVCodecParameters* par = ctx->streams[idx]->codecpar;
    if (static_cast<int>(par->codec_id) != v.codec_id) return reject("different codec");
    if ((v.width > 0 && par->width != v.width) || (v.height > 0 && par->height != v.height))
        return reject("different frame size");

    // From here on only the video is read.
    for (unsigned i = 0; i < ctx->nb_streams; ++i)
        if (static_cast<int>(i) != idx) ctx->streams[i]->discard = AVDISCARD_ALL;
    *out = ctx;
    *video_idx = idx;
    return 0;
}

} // namespace ffopen
