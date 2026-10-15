#include "FfOpen.h"

extern "C" {
#include <libavformat/avformat.h>
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

int open_input(AVFormatContext** out, const std::string& url, const std::stop_token& st,
               NetInitiator who)
{
    *out = nullptr;
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
    }
    // On failure avformat_open_input frees ctx and nulls the pointer.
    const int ret = avformat_open_input(&ctx, url.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0) return ret;
    *out = ctx;
    return 0;
}

} // namespace ffopen
