#include "VMAFAnalyzer.h"
#include "ManifestParser.h"
#include "Logger.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <numeric>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/rational.h>
}

#include <libvmaf/libvmaf.h>

// ── Internal decode helper ────────────────────────────────────────────────────

namespace {

enum class DecodeState { Reading, Flushing, Done };

struct DecodeCtx {
    AVFormatContext* fmt_ctx   = nullptr;
    AVCodecContext*  codec_ctx = nullptr;
    AVPacket*        pkt       = nullptr;
    int              vid_idx   = -1;
    DecodeState      state     = DecodeState::Reading;

    [[nodiscard]] bool open(const std::string& url)
    {
        if (avformat_open_input(&fmt_ctx, url.c_str(), nullptr, nullptr) < 0) return false;
        if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) return false;

        const AVCodec* codec = nullptr;
        vid_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (vid_idx < 0 || !codec) return false;

        codec_ctx = avcodec_alloc_context3(codec);
        if (!codec_ctx) return false;
        if (avcodec_parameters_to_context(
                codec_ctx, fmt_ctx->streams[vid_idx]->codecpar) < 0) return false;
        if (avcodec_open2(codec_ctx, codec, nullptr) < 0) return false;

        pkt = av_packet_alloc();
        return pkt != nullptr;
    }

    // Returns the next decoded video frame (caller owns it), or nullptr at EOF/error.
    [[nodiscard]] AVFrame* next_video_frame()
    {
        if (state == DecodeState::Done) return nullptr;

        AVFrame* frame = av_frame_alloc();

        while (true) {
            int r = avcodec_receive_frame(codec_ctx, frame);
            if (r == 0)            return frame;
            if (r == AVERROR_EOF)  { state = DecodeState::Done; break; }
            if (r != AVERROR(EAGAIN)) { state = DecodeState::Done; break; }

            // Codec needs more input
            if (state == DecodeState::Flushing) {
                // Already sent flush packet; keep trying to drain
                continue;
            }

            // Read the next video packet and feed it
            while (true) {
                r = av_read_frame(fmt_ctx, pkt);
                if (r < 0) {
                    avcodec_send_packet(codec_ctx, nullptr);
                    state = DecodeState::Flushing;
                    break;
                }
                if (pkt->stream_index == vid_idx) {
                    avcodec_send_packet(codec_ctx, pkt);
                    av_packet_unref(pkt);
                    break;
                }
                av_packet_unref(pkt);
            }
        }

        av_frame_free(&frame);
        return nullptr;
    }

    void close()
    {
        if (pkt)       av_packet_free(&pkt);
        if (codec_ctx) avcodec_free_context(&codec_ctx);
        if (fmt_ctx)   avformat_close_input(&fmt_ctx);
        vid_idx = -1;
        state   = DecodeState::Reading;
    }

    int           width()   const { return codec_ctx ? codec_ctx->width   : 0; }
    int           height()  const { return codec_ctx ? codec_ctx->height  : 0; }
    AVPixelFormat pix_fmt() const { return codec_ctx ? codec_ctx->pix_fmt : AV_PIX_FMT_NONE; }

    double fps_estimate() const
    {
        if (!fmt_ctx || vid_idx < 0) return 24.0;
        AVRational r = fmt_ctx->streams[vid_idx]->avg_frame_rate;
        return (r.den > 0) ? av_q2d(r) : 24.0;
    }

    double duration_seconds() const
    {
        if (!fmt_ctx || fmt_ctx->duration <= 0) return 0.0;
        return static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE;
    }
};

}  // namespace

// ── analyze_pair ─────────────────────────────────────────────────────────────

void VMAFAnalyzer::analyze_pair(VMAFResult& result, std::stop_token st,
                                 std::atomic<float>& progress_total,
                                 float base, float span)
{
    DecodeCtx ref_ctx, dis_ctx;

    auto fail = [&](const std::string& msg) {
        result.error = msg;
        result.done  = true;
        logger::warn("VMAFAnalyzer: {}", msg);
    };

    if (!ref_ctx.open(result.ref_url))
        return fail("cannot open reference: " + result.ref_url);
    if (!dis_ctx.open(result.dis_url)) {
        ref_ctx.close();
        return fail("cannot open distorted: " + result.dis_url);
    }

    const int ref_w = ref_ctx.width();
    const int ref_h = ref_ctx.height();
    result.width  = ref_w;
    result.height = ref_h;

    // Both streams scale to YUV420P @ reference resolution
    SwsContext* ref_sws = sws_getContext(
        ref_ctx.width(), ref_ctx.height(), ref_ctx.pix_fmt(),
        ref_w, ref_h, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    SwsContext* dis_sws = sws_getContext(
        dis_ctx.width(), dis_ctx.height(), dis_ctx.pix_fmt(),
        ref_w, ref_h, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    // Reusable scaled buffers (output of sws_scale; NOT given directly to vmaf)
    AVFrame* ref_scaled = av_frame_alloc();
    ref_scaled->format = AV_PIX_FMT_YUV420P;
    ref_scaled->width  = ref_w;
    ref_scaled->height = ref_h;
    av_frame_get_buffer(ref_scaled, 32);

    AVFrame* dis_scaled = av_frame_alloc();
    dis_scaled->format = AV_PIX_FMT_YUV420P;
    dis_scaled->width  = ref_w;
    dis_scaled->height = ref_h;
    av_frame_get_buffer(dis_scaled, 32);

    VmafContext* vmaf  = nullptr;
    VmafModel*   model = nullptr;

    auto cleanup = [&] {
        if (model)      vmaf_model_destroy(model);
        if (vmaf)       vmaf_close(vmaf);
        sws_freeContext(ref_sws);
        sws_freeContext(dis_sws);
        if (ref_scaled) av_frame_free(&ref_scaled);
        if (dis_scaled) av_frame_free(&dis_scaled);
        ref_ctx.close();
        dis_ctx.close();
    };

    VmafConfiguration cfg {
        .log_level   = VMAF_LOG_LEVEL_NONE,
        .n_threads   = 4,
        .n_subsample = 1,
        .cpumask     = 0,
        .gpumask     = 0,
    };
    if (vmaf_init(&vmaf, cfg) < 0) {
        cleanup();
        return fail("vmaf_init failed");
    }

    VmafModelConfig model_cfg { .name = "vmaf", .flags = VMAF_MODEL_FLAGS_DEFAULT };
    if (vmaf_model_load(&model, &model_cfg, "vmaf_v0.6.1") < 0) {
        cleanup();
        return fail("vmaf_model_load failed — is vmaf_v0.6.1.json installed?");
    }

    if (vmaf_use_features_from_model(vmaf, model) < 0) {
        cleanup();
        return fail("vmaf_use_features_from_model failed");
    }

    // Estimate total frames for progress reporting
    double total_est = ref_ctx.duration_seconds() * ref_ctx.fps_estimate();
    if (total_est < 1.0) total_est = 1.0;

    unsigned frame_idx = 0;

    while (!st.stop_requested()) {
        AVFrame* ref_frame = ref_ctx.next_video_frame();
        AVFrame* dis_frame = dis_ctx.next_video_frame();

        if (!ref_frame || !dis_frame) {
            if (ref_frame) av_frame_free(&ref_frame);
            if (dis_frame) av_frame_free(&dis_frame);
            break;
        }

        // Scale both → YUV420P @ ref_w × ref_h
        sws_scale(ref_sws,
                  ref_frame->data, ref_frame->linesize, 0, ref_ctx.height(),
                  ref_scaled->data, ref_scaled->linesize);
        sws_scale(dis_sws,
                  dis_frame->data, dis_frame->linesize, 0, dis_ctx.height(),
                  dis_scaled->data, dis_scaled->linesize);

        av_frame_free(&ref_frame);
        av_frame_free(&dis_frame);

        // Allocate VmafPictures and copy planes.
        // vmaf_read_pictures() takes ownership of the backing memory.
        VmafPicture ref_pic{}, dis_pic{};
        vmaf_picture_alloc(&ref_pic, VMAF_PIX_FMT_YUV420P, 8,
                           static_cast<unsigned>(ref_w), static_cast<unsigned>(ref_h));
        vmaf_picture_alloc(&dis_pic, VMAF_PIX_FMT_YUV420P, 8,
                           static_cast<unsigned>(ref_w), static_cast<unsigned>(ref_h));

        for (int p = 0; p < 3; ++p) {
            int pw = (p == 0) ? ref_w : (ref_w + 1) / 2;
            int ph = (p == 0) ? ref_h : (ref_h + 1) / 2;
            const auto* rsrc = ref_scaled->data[p];
            const auto* dsrc = dis_scaled->data[p];
            auto* rdst = static_cast<uint8_t*>(ref_pic.data[p]);
            auto* ddst = static_cast<uint8_t*>(dis_pic.data[p]);
            for (int row = 0; row < ph; ++row) {
                std::memcpy(rdst + row * ref_pic.stride[p],
                            rsrc + row * ref_scaled->linesize[p], pw);
                std::memcpy(ddst + row * dis_pic.stride[p],
                            dsrc + row * dis_scaled->linesize[p], pw);
            }
        }

        // Context takes ownership of ref_pic / dis_pic backing memory.
        vmaf_read_pictures(vmaf, &ref_pic, &dis_pic, frame_idx++);

        float pair_frac = std::min(static_cast<float>(frame_idx) /
                                   static_cast<float>(total_est), 0.99f);
        progress_total.store(base + span * pair_frac);
    }

    // Flush feature extractors
    vmaf_read_pictures(vmaf, nullptr, nullptr, 0);

    // Collect per-frame scores and pooled stats
    if (frame_idx > 0 && !st.stop_requested()) {
        result.per_frame.resize(frame_idx);
        for (unsigned i = 0; i < frame_idx; ++i)
            vmaf_score_at_index(vmaf, model, &result.per_frame[i], i);

        vmaf_score_pooled(vmaf, model, VMAF_POOL_METHOD_MEAN,
                          &result.vmaf_mean, 0, frame_idx - 1);
        vmaf_score_pooled(vmaf, model, VMAF_POOL_METHOD_MIN,
                          &result.vmaf_min, 0, frame_idx - 1);

        // 5th-percentile (manual — no pooling method in libvmaf for percentiles)
        auto sorted = result.per_frame;
        std::sort(sorted.begin(), sorted.end());
        result.vmaf_p5 = sorted[sorted.size() * 5 / 100];

        logger::info("VMAFAnalyzer: {} frames  mean={:.2f}  min={:.2f}  p5={:.2f}  [{}]",
                     frame_idx, result.vmaf_mean, result.vmaf_min, result.vmaf_p5,
                     result.label);
    }

    cleanup();
    result.done = true;
}

// ── Public API ────────────────────────────────────────────────────────────────

void VMAFAnalyzer::start(std::string ref_url, std::string dis_url, int64_t bandwidth_hint)
{
    cancel();
    {
        std::lock_guard g(mtx_);
        results_.clear();
        results_.push_back({
            .ref_url   = ref_url,
            .dis_url   = dis_url,
            .label     = "Analysis",
            .bandwidth = bandwidth_hint,
        });
    }
    running_.store(true);
    progress_.store(0.0f);

    worker_ = std::jthread([this](std::stop_token st) {
        VMAFResult local;
        { std::lock_guard g(mtx_); local = results_[0]; }

        analyze_pair(local, st, progress_, 0.0f, 1.0f);

        { std::lock_guard g(mtx_); results_[0] = std::move(local); }
        progress_.store(1.0f);
        running_.store(false);
    });
}

void VMAFAnalyzer::start_manifest(const std::vector<VariantStream>& variants)
{
    cancel();

    // Sort descending by bandwidth; highest = reference
    auto sorted = variants;
    std::sort(sorted.begin(), sorted.end(),
              [](const VariantStream& a, const VariantStream& b) {
                  return a.bandwidth > b.bandwidth;
              });

    if (sorted.size() < 2) {
        logger::warn("VMAFAnalyzer: need at least 2 variants for manifest comparison");
        return;
    }

    {
        std::lock_guard g(mtx_);
        results_.clear();
        for (std::size_t i = 1; i < sorted.size(); ++i) {
            const auto& dis = sorted[i];
            const auto& ref = sorted[0];
            std::string label = std::format("{}x{}  {:.1f} Mbps",
                dis.width, dis.height, dis.bandwidth / 1e6);
            results_.push_back({
                .ref_url   = ref.url,
                .dis_url   = dis.url,
                .label     = std::move(label),
                .bandwidth = dis.bandwidth,
            });
        }
    }

    running_.store(true);
    progress_.store(0.0f);

    worker_ = std::jthread([this](std::stop_token st) {
        std::vector<VMAFResult> pairs;
        { std::lock_guard g(mtx_); pairs = results_; }

        const float n = static_cast<float>(pairs.size());

        for (std::size_t i = 0; i < pairs.size() && !st.stop_requested(); ++i) {
            float base = static_cast<float>(i) / n;
            float span = 1.0f / n;
            analyze_pair(pairs[i], st, progress_, base, span);
            { std::lock_guard g(mtx_); results_[i] = pairs[i]; }
        }

        progress_.store(1.0f);
        running_.store(false);
    });
}

void VMAFAnalyzer::cancel()
{
    worker_.request_stop();
    if (worker_.joinable()) worker_.join();
    running_.store(false);
}

std::vector<VMAFResult> VMAFAnalyzer::results() const
{
    std::lock_guard g(mtx_);
    return results_;
}

bool VMAFAnalyzer::write_json(const std::string& path) const
{
    std::vector<VMAFResult> snap;
    { std::lock_guard g(mtx_); snap = results_; }

    std::ofstream f(path);
    if (!f) {
        logger::warn("VMAFAnalyzer: cannot write report to {}", path);
        return false;
    }

    f << "{\n  \"results\": [\n";
    for (std::size_t i = 0; i < snap.size(); ++i) {
        const auto& r = snap[i];
        f << "    {\n";
        f << std::format("      \"label\": \"{}\",\n", r.label);
        f << std::format("      \"ref\":   \"{}\",\n", r.ref_url);
        f << std::format("      \"dis\":   \"{}\",\n", r.dis_url);
        f << std::format("      \"bandwidth\": {},\n", r.bandwidth);
        f << std::format("      \"resolution\": \"{}x{}\",\n", r.width, r.height);
        if (r.done && r.error.empty()) {
            f << std::format("      \"vmaf_mean\": {:.4f},\n", r.vmaf_mean);
            f << std::format("      \"vmaf_min\":  {:.4f},\n", r.vmaf_min);
            f << std::format("      \"vmaf_p5\":   {:.4f},\n", r.vmaf_p5);
            f << "      \"per_frame\": [";
            for (std::size_t j = 0; j < r.per_frame.size(); ++j) {
                if (j % 10 == 0) f << "\n        ";
                f << std::format("{:.4f}", r.per_frame[j]);
                if (j + 1 < r.per_frame.size()) f << ", ";
            }
            f << "\n      ]\n";
        } else {
            f << std::format("      \"error\": \"{}\"\n", r.error);
        }
        f << "    }";
        if (i + 1 < snap.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
    logger::info("VMAFAnalyzer: wrote report to {}", path);
    return true;
}
