#include "VMAFAnalyzer.h"
#include "FileHash.h"
#include "ManifestParser.h"
#include "Logger.h"
#include "VMAFColor.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <numeric>
#include <utility>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
}

#include <libvmaf/libvmaf.h>

// ── Internal decode helper ────────────────────────────────────────────────────

namespace {

// Scoring settings applied to every pair (recorded in VMAFResult::settings).
// The reference keeps its size; only its pixel format is converted, with
// these flags, whatever scaler the distorted frames get.
constexpr int           kRefScaleFlags = SWS_BILINEAR;
constexpr int           kScaleThreads  = 1;            // swscale threads for the distorted frames
constexpr AVPixelFormat kScorePixFmt   = AV_PIX_FMT_YUV420P;
constexpr unsigned      kScoreBpc      = 8;
constexpr unsigned      kThreads       = 4;
constexpr unsigned      kSubsample     = 1;

// swscale's flags for a scaler: the flags FFmpeg's scale filter gets from
// flags=<vmaf_scaler_name(s)>, with swscale's default parameters for it.
unsigned sws_flags_of(VMAFScaler s)
{
    switch (s) {
    case VMAFScaler::Bilinear: return SWS_BILINEAR;
    case VMAFScaler::Bicubic:  return SWS_BICUBIC;
    case VMAFScaler::Lanczos:  return SWS_LANCZOS;
    }
    return SWS_BILINEAR;
}

std::string av_error_text(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return buf;
}

std::string name_or_unknown(const char* name) { return name ? name : "unknown"; }

// Counts frame pair `index` under its color conversion in `list`; `seen`
// holds the tags of each entry of `list`.
void count_conversion(std::vector<VMAFColorConversion>& list,
                      std::vector<std::pair<vmafcolor::Tags, vmafcolor::Tags>>& seen,
                      const vmafcolor::Tags& from, const vmafcolor::Tags& to, unsigned index)
{
    for (std::size_t i = 0; i < seen.size(); ++i) {
        if (seen[i].first == from && seen[i].second == to) {
            ++list[i].pairs;
            return;
        }
    }
    const vmafcolor::Change ch = vmafcolor::change(from, to);
    seen.emplace_back(from, to);
    list.push_back({
        .from_space     = name_or_unknown(av_color_space_name(from.space)),
        .from_range     = name_or_unknown(av_color_range_name(from.range)),
        .to_space       = name_or_unknown(av_color_space_name(to.space)),
        .to_range       = name_or_unknown(av_color_range_name(to.range)),
        .matrix_changed = ch.matrix,
        .range_changed  = ch.range,
        .first_index    = index,
        .pairs          = 1,
    });
}

enum class DecodeState { Reading, Flushing, Done };

struct DecodeCtx {
    AVFormatContext* fmt_ctx   = nullptr;
    AVCodecContext*  codec_ctx = nullptr;
    AVPacket*        pkt       = nullptr;
    int              vid_idx   = -1;
    DecodeState      state     = DecodeState::Reading;
    int              error     = 0;   // FFmpeg error that ended decoding; 0 = clean EOF / not ended

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
            if (r != AVERROR(EAGAIN)) { error = r; state = DecodeState::Done; break; }

            // Codec needs more input
            if (state == DecodeState::Flushing) {
                // Already sent flush packet; keep trying to drain
                continue;
            }

            // Read the next video packet and feed it
            while (true) {
                r = av_read_frame(fmt_ctx, pkt);
                if (r < 0) {
                    if (r != AVERROR_EOF) error = r;   // still drained like EOF
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
        error   = 0;
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

    // A timestamp of the video stream in seconds; NaN when there is none.
    double seconds(int64_t ts) const
    {
        if (ts == AV_NOPTS_VALUE) return std::numeric_limits<double>::quiet_NaN();
        const AVRational tb = fmt_ctx->streams[vid_idx]->time_base;
        return static_cast<double>(ts) * tb.num / tb.den;
    }

    // The opened video stream as the decoder sees it.
    VMAFInputInfo info() const
    {
        VMAFInputInfo in;
        if (!codec_ctx) return in;
        in.width           = codec_ctx->width;
        in.height          = codec_ctx->height;
        in.pix_fmt         = name_or_unknown(av_get_pix_fmt_name(codec_ctx->pix_fmt));
        in.color_space     = name_or_unknown(av_color_space_name(codec_ctx->colorspace));
        in.color_range     = name_or_unknown(av_color_range_name(codec_ctx->color_range));
        in.color_primaries = name_or_unknown(av_color_primaries_name(codec_ctx->color_primaries));
        in.color_trc       = name_or_unknown(av_color_transfer_name(codec_ctx->color_trc));
        const AVRational tb = fmt_ctx->streams[vid_idx]->time_base;
        in.time_base_num   = tb.num;
        in.time_base_den   = tb.den;
        return in;
    }
};

}  // namespace

// ── analyze_pair ─────────────────────────────────────────────────────────────

void VMAFAnalyzer::analyze_pair(VMAFResult& result, std::stop_token st,
                                 std::atomic<float>& progress_total,
                                 float base, float span, const VMAFOptions& opts)
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
    result.ref_info = ref_ctx.info();
    result.dis_info = dis_ctx.info();
    const bool model_file = !opts.model.path.empty();
    result.settings = {
        .model        = model_file ? opts.model.path : opts.model.version,
        .model_source = model_file ? "file" : "built-in",
        .scaler       = std::string(vmaf_scaler_name(opts.scaler)),
        .pix_fmt      = name_or_unknown(av_get_pix_fmt_name(kScorePixFmt)),
        .bit_depth    = static_cast<int>(kScoreBpc),
        .n_threads    = static_cast<int>(kThreads),
        .n_subsample  = static_cast<int>(kSubsample),
    };

    // The reference is only brought to YUV420P, at its own size. FFmpeg's
    // graph converts a reference in another format to the distorted input's
    // color instead, which is not done here: say so.
    if (vmafcolor::converts_reference(ref_ctx.pix_fmt())) {
        result.warnings.push_back(std::format(
            "the reference's frames are {}, not {}: FFmpeg's section 5 graph then converts "
            "the reference to the distorted input's color matrix and range, where Mirage "
            "converts the distorted frames to the reference's, so the scores can differ from "
            "FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)",
            name_or_unknown(av_get_pix_fmt_name(ref_ctx.pix_fmt())),
            name_or_unknown(av_get_pix_fmt_name(kScorePixFmt))));
        logger::warn("VMAFAnalyzer: {}", result.warnings.back());
    }
    SwsContext* ref_sws = sws_getContext(
        ref_ctx.width(), ref_ctx.height(), ref_ctx.pix_fmt(),
        ref_w, ref_h, kScorePixFmt,
        kRefScaleFlags, nullptr, nullptr, nullptr);

    // The distorted frames are scaled to the reference's size with the
    // chosen scaler and converted to the color FFmpeg's filter graph gives
    // them, frame by frame through swscale's frame API as FFmpeg's scale
    // filter does (src/VMAFColor.h).
    SwsContext* dis_sws = sws_alloc_context();
    if (dis_sws) {
        dis_sws->flags   = sws_flags_of(opts.scaler);
        dis_sws->threads = kScaleThreads;
    }

    // Reusable scaled buffer (output of sws_scale; NOT given directly to vmaf)
    AVFrame* ref_scaled = av_frame_alloc();
    ref_scaled->format = kScorePixFmt;
    ref_scaled->width  = ref_w;
    ref_scaled->height = ref_h;
    av_frame_get_buffer(ref_scaled, 32);

    VmafContext* vmaf  = nullptr;
    VmafModel*   model = nullptr;

    auto cleanup = [&] {
        if (model)      vmaf_model_destroy(model);
        if (vmaf)       vmaf_close(vmaf);
        sws_freeContext(ref_sws);
        sws_free_context(&dis_sws);
        if (ref_scaled) av_frame_free(&ref_scaled);
        ref_ctx.close();
        dis_ctx.close();
    };

    if (!dis_sws) {
        cleanup();
        return fail("cannot allocate the distorted input's scaler");
    }

    VmafConfiguration cfg {
        .log_level   = VMAF_LOG_LEVEL_NONE,
        .n_threads   = kThreads,
        .n_subsample = kSubsample,
        .cpumask     = 0,
        .gpumask     = 0,
    };
    if (vmaf_init(&vmaf, cfg) < 0) {
        cleanup();
        return fail("vmaf_init failed");
    }

    // A model file is identified in the report by its SHA-256, read right
    // before libvmaf reads the file.
    VmafModelConfig model_cfg { .name = "vmaf", .flags = VMAF_MODEL_FLAGS_DEFAULT };
    if (model_file) {
        std::string err;
        result.settings.model_sha256 = filehash::sha256_file(opts.model.path, err);
        if (result.settings.model_sha256.empty()) {
            cleanup();
            return fail(std::format("cannot read the model file {}: {}", opts.model.path, err));
        }
        if (vmaf_model_load_from_path(&model, &model_cfg, opts.model.path.c_str()) < 0) {
            cleanup();
            return fail(std::format("vmaf_model_load_from_path failed: libvmaf cannot load "
                                    "the model file {}", opts.model.path));
        }
    } else if (vmaf_model_load(&model, &model_cfg, opts.model.version.c_str()) < 0) {
        cleanup();
        return fail(std::format("vmaf_model_load failed: this libvmaf has no built-in model {}",
                                opts.model.version));
    }

    if (vmaf_use_features_from_model(vmaf, model) < 0) {
        cleanup();
        return fail("vmaf_use_features_from_model failed");
    }

    // Estimate total frames for progress reporting
    double total_est = ref_ctx.duration_seconds() * ref_ctx.fps_estimate();
    if (opts.max_frames > 0) total_est = std::min(total_est, static_cast<double>(opts.max_frames));
    if (total_est < 1.0) total_est = 1.0;

    // The first libvmaf failure while scoring; the analysis itself goes on.
    auto score_failed = [&](const char* call, int r) {
        if (result.score_error.empty())
            result.score_error = std::format("{} failed: {}", call, av_error_text(r));
    };

    unsigned frame_idx = 0;
    std::string convert_error;   // why a distorted frame could not be converted
    std::vector<std::pair<vmafcolor::Tags, vmafcolor::Tags>> conversion_tags;

    while (!st.stop_requested() && (opts.max_frames == 0 || frame_idx < opts.max_frames)) {
        AVFrame* ref_frame = ref_ctx.next_video_frame();
        AVFrame* dis_frame = dis_ctx.next_video_frame();

        if (!ref_frame || !dis_frame) {
            if (ref_frame) av_frame_free(&ref_frame);
            if (dis_frame) av_frame_free(&dis_frame);
            break;
        }

        if (opts.record_pts) {
            result.ref_pts.push_back(ref_ctx.seconds(ref_frame->best_effort_timestamp));
            result.dis_pts.push_back(dis_ctx.seconds(dis_frame->best_effort_timestamp));
        }

        // Reference → YUV420P; distorted → YUV420P @ ref_w × ref_h, in the
        // reference's color matrix and range as FFmpeg negotiates them.
        sws_scale(ref_sws,
                  ref_frame->data, ref_frame->linesize, 0, ref_ctx.height(),
                  ref_scaled->data, ref_scaled->linesize);

        const vmafcolor::Tags ref_tags = vmafcolor::tags_of(*ref_frame);
        const vmafcolor::Tags dis_tags = vmafcolor::tags_of(*dis_frame);
        av_frame_free(&ref_frame);

        AVFrame* dis_scaled = nullptr;
        if (!vmafcolor::supported(ref_tags.space) || !vmafcolor::supported(dis_tags.space)) {
            const bool ref_bad = !vmafcolor::supported(ref_tags.space);
            convert_error = std::format(
                "the {} has the color matrix {}, which FFmpeg's scale filter does not "
                "convert, so the pair can't be scored as FFmpeg scores it (DECISIONS.md D-016)",
                ref_bad ? "reference" : "distorted input",
                name_or_unknown(av_color_space_name(ref_bad ? ref_tags.space : dis_tags.space)));
        } else {
            const vmafcolor::Tags to = vmafcolor::target(ref_tags, dis_tags);
            dis_scaled = av_frame_alloc();
            if (!dis_scaled) {
                convert_error = "cannot allocate a frame for the distorted input";
            } else {
                dis_scaled->format = kScorePixFmt;
                dis_scaled->width  = ref_w;
                dis_scaled->height = ref_h;
                if (int r = vmafcolor::convert(dis_sws, *dis_frame, *dis_scaled, to); r < 0)
                    convert_error = std::format("converting distorted frame {} failed: {}",
                                                frame_idx, av_error_text(r));
                else
                    count_conversion(result.dis_conversions, conversion_tags, dis_tags, to,
                                     frame_idx);
            }
        }
        av_frame_free(&dis_frame);
        if (!convert_error.empty()) {
            if (dis_scaled) av_frame_free(&dis_scaled);
            break;
        }

        // Allocate VmafPictures and copy planes.
        // vmaf_read_pictures() takes ownership of the backing memory.
        VmafPicture ref_pic{}, dis_pic{};
        vmaf_picture_alloc(&ref_pic, VMAF_PIX_FMT_YUV420P, kScoreBpc,
                           static_cast<unsigned>(ref_w), static_cast<unsigned>(ref_h));
        vmaf_picture_alloc(&dis_pic, VMAF_PIX_FMT_YUV420P, kScoreBpc,
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
        av_frame_free(&dis_scaled);

        // Context takes ownership of ref_pic / dis_pic backing memory.
        if (int r = vmaf_read_pictures(vmaf, &ref_pic, &dis_pic, frame_idx++); r < 0)
            score_failed("vmaf_read_pictures", r);

        float pair_frac = std::min(static_cast<float>(frame_idx) /
                                   static_cast<float>(total_est), 0.99f);
        progress_total.store(base + span * pair_frac);
    }

    if (!convert_error.empty()) {
        cleanup();
        return fail(convert_error);
    }

    // Flush feature extractors
    if (int r = vmaf_read_pictures(vmaf, nullptr, nullptr, 0); r < 0)
        score_failed("vmaf_read_pictures (flush)", r);

    // Collect per-frame scores and pooled stats
    if (frame_idx > 0 && !st.stop_requested()) {
        result.per_frame.resize(frame_idx);
        for (unsigned i = 0; i < frame_idx; ++i)
            if (int r = vmaf_score_at_index(vmaf, model, &result.per_frame[i], i); r < 0)
                score_failed("vmaf_score_at_index", r);

        if (int r = vmaf_score_pooled(vmaf, model, VMAF_POOL_METHOD_MEAN,
                                      &result.vmaf_mean, 0, frame_idx - 1); r < 0)
            score_failed("vmaf_score_pooled (mean)", r);
        if (int r = vmaf_score_pooled(vmaf, model, VMAF_POOL_METHOD_MIN,
                                      &result.vmaf_min, 0, frame_idx - 1); r < 0)
            score_failed("vmaf_score_pooled (min)", r);

        // 5th-percentile (manual — no pooling method in libvmaf for percentiles)
        auto sorted = result.per_frame;
        std::sort(sorted.begin(), sorted.end());
        result.vmaf_p5 = sorted[sorted.size() * 5 / 100];

        logger::info("VMAFAnalyzer: {} frames  mean={:.2f}  min={:.2f}  p5={:.2f}  [{}]",
                     frame_idx, result.vmaf_mean, result.vmaf_min, result.vmaf_p5,
                     result.label);
    }

    // How each input ended (before cleanup() resets the decode contexts).
    auto record_end = [](const DecodeCtx& ctx, VMAFInputInfo& info) {
        info.ended     = ctx.state == DecodeState::Done;
        info.end_error = ctx.error ? av_error_text(ctx.error) : std::string();
    };
    record_end(ref_ctx, result.ref_info);
    record_end(dis_ctx, result.dis_info);

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

        analyze_pair(local, st, progress_, 0.0f, 1.0f, {});

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
            analyze_pair(pairs[i], st, progress_, base, span, {});
            { std::lock_guard g(mtx_); results_[i] = pairs[i]; }
        }

        progress_.store(1.0f);
        running_.store(false);
    });
}

VMAFResult VMAFAnalyzer::analyze(std::string ref_url, std::string dis_url,
                                  const VMAFOptions& opts)
{
    VMAFResult result{
        .ref_url = std::move(ref_url),
        .dis_url = std::move(dis_url),
        .label   = "Analysis",
    };
    std::atomic<float> progress{ 0.0f };
    analyze_pair(result, std::stop_token{}, progress, 0.0f, 1.0f, opts);
    return result;
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
