#include "VMAFAnalyzer.h"
#include "FileHash.h"
#include "ManifestParser.h"
#include "Logger.h"
#include "VMAFAlign.h"
#include "VMAFColor.h"
#include "VMAFNet.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <format>
#include <fstream>
#include <limits>
#include <numeric>
#include <utility>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
}

#include <libvmaf/libvmaf.h>

// ── Internal decode helper ────────────────────────────────────────────────────

namespace {

// Scoring settings applied to every pair (recorded in VMAFResult::settings).
// A yuv420p reference keeps its size and pixels: it is copied with these
// flags, whatever scaler the distorted frames get. One in another pixel
// format is converted with vmafcolor::kInsertedScaleFlags.
constexpr int           kRefScaleFlags = SWS_BILINEAR;
constexpr int           kScaleThreads  = 1;            // swscale threads of each conversion
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

static_assert(vmafalign::kNoTimestamp == AV_NOPTS_VALUE);

const char* input_name(vmafalign::Input in)
{
    return in == vmafalign::Input::Reference ? "reference" : "distorted";
}

// The warning for a pairing whose failure rate is above the threshold.
std::string alignment_warning(const vmafalign::Counts& c)
{
    std::string kinds;
    for (const auto& [n, what] : { std::pair{ c.unmatched_reference, "unmatched reference" },
                                   std::pair{ c.extra_distorted, "extra distorted" },
                                   std::pair{ c.duplicate_distorted, "duplicate distorted" },
                                   std::pair{ c.no_timestamp, "without a timestamp" },
                                   std::pair{ c.after_discontinuity, "after a discontinuity" } })
        if (n > 0) kinds += std::format("{}{} {}", kinds.empty() ? "" : ", ", n, what);
    return std::format("{:.2f}% of the frames failed to pair by timestamp ({} of {}: {}), above "
                       "the threshold of {}% (DECISIONS.md D-028); the scores are those of the "
                       "{} frame pairs that paired",
                       100.0 * c.failure_rate(), c.failed(), c.paired + c.failed(), kinds,
                       100.0 * vmafalign::kFailureThreshold, c.paired);
}

// The warning for frames in a pixel format vmafcolor::verified() doesn't list.
std::string unverified(const char* side, AVPixelFormat format)
{
    return std::format("the {}'s frames are {}, a pixel format whose conversion isn't checked "
                       "against FFmpeg's section 5 graph (vmafcolor::verified), so the scores "
                       "can differ from FFmpeg's",
                       side, name_or_unknown(av_get_pix_fmt_name(format)));
}

// Converts `in` into a new yuv420p frame `out` of w x h with the tags `to`
// (vmafcolor::convert). Returns 0 or a negative AVERROR; `out` may be set
// either way.
int convert_into(SwsContext* sws, AVFrame& in, int w, int h, const vmafcolor::Tags& to,
                 AVFrame*& out)
{
    out = av_frame_alloc();
    if (!out) return AVERROR(ENOMEM);
    out->format = kScorePixFmt;
    out->width  = w;
    out->height = h;
    return vmafcolor::convert(sws, in, *out, to);
}

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

// One input's demuxer and video decoder. It frees what it holds when it goes
// out of scope, so an open() that fails halfway, after the input was opened,
// leaks nothing (DECISIONS.md D-032).
struct DecodeCtx {
    AVFormatContext* fmt_ctx   = nullptr;
    AVCodecContext*  codec_ctx = nullptr;
    AVPacket*        pkt       = nullptr;
    int              vid_idx   = -1;
    DecodeState      state     = DecodeState::Reading;
    int              error     = 0;   // FFmpeg error that ended decoding; 0 = clean EOF / not ended

    DecodeCtx() = default;
    ~DecodeCtx() { close(); }
    DecodeCtx(const DecodeCtx&)            = delete;
    DecodeCtx& operator=(const DecodeCtx&) = delete;

    // A network input's reads wait at most 5 s, and a stop requested on `st`
    // interrupts a blocking call (src/VMAFNet.h, DECISIONS.md D-036). `st`
    // must outlive the context: it is the interrupt callback's opaque.
    [[nodiscard]] bool open(const std::string& url, const std::stop_token& st)
    {
        fmt_ctx = avformat_alloc_context();
        if (!fmt_ctx) return false;
        fmt_ctx->interrupt_callback.callback = vmafnet::stop_requested;
        fmt_ctx->interrupt_callback.opaque   = const_cast<std::stop_token*>(&st);
        AVDictionary* opts = nullptr;
        vmafnet::set_io_limits(&opts, url);
        // On failure avformat_open_input frees fmt_ctx and nulls the pointer.
        const int r = avformat_open_input(&fmt_ctx, url.c_str(), nullptr, &opts);
        av_dict_free(&opts);
        if (r < 0) return false;
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

    // The video stream's timing for pairing by timestamp (src/VMAFAlign.h):
    // its time base; the frame rate `ffmpeg` gives its filter graph for the
    // stream when the command line names none (av_guess_frame_rate(),
    // fftools/ffmpeg_demux.c of FFmpeg 8.1.1); and the container's start
    // time in the stream's time base, rounded as `ffmpeg` rounds the offset
    // it shifts the input's timestamps by without -copyts.
    vmafalign::Clock clock() const
    {
        AVStream* st = fmt_ctx->streams[vid_idx];
        return {
            .time_base  = st->time_base,
            .frame_rate = av_guess_frame_rate(fmt_ctx, st, nullptr),
            .start      = fmt_ctx->start_time == AV_NOPTS_VALUE
                              ? vmafalign::kNoTimestamp
                              : av_rescale_q(fmt_ctx->start_time, AV_TIME_BASE_Q, st->time_base),
        };
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

// What `a` did with the frames of the two inputs, for VMAFResult::alignment.
void record_alignment(VMAFAlignment& out, const vmafalign::Aligner& a,
                      const DecodeCtx& ref, const vmafalign::Clock& ref_clock,
                      const DecodeCtx& dis, const vmafalign::Clock& dis_clock)
{
    using vmafalign::Input;
    out.grid      = input_name(a.grid());
    out.tolerance = av_q2d(a.tolerance());
    const auto input = [&](Input in, const DecodeCtx& ctx, const vmafalign::Clock& clock,
                           std::string& rate, double& start, std::string& from) {
        rate = std::format("{}/{}", clock.frame_rate.num, clock.frame_rate.den);
        if (const std::optional<int64_t> s = a.start(in)) {
            start = ctx.seconds(*s);
            from  = a.start_from_clock(in) ? "container" : "first-frame";
        }
    };
    input(Input::Reference, ref, ref_clock, out.ref_frame_rate, out.ref_start, out.ref_start_from);
    input(Input::Distorted, dis, dis_clock, out.dis_frame_rate, out.dis_start, out.dis_start_from);

    const vmafalign::Counts& c = a.counts();
    out.paired              = c.paired;
    out.unmatched_reference = c.unmatched_reference;
    out.extra_distorted     = c.extra_distorted;
    out.duplicate_distorted = c.duplicate_distorted;
    out.no_timestamp        = c.no_timestamp;
    out.after_discontinuity = c.after_discontinuity;
    out.off_grid_reference  = c.off_grid_reference;
    out.off_grid_distorted  = c.off_grid_distorted;
    out.discontinuities     = c.discontinuities;
    out.failed              = c.failed();
    out.failure_rate        = c.failure_rate();
    out.threshold           = vmafalign::kFailureThreshold;
    out.exceeded            = c.exceeded();
    out.discontinuity_jump  = av_q2d(vmafalign::kForwardJump);
    out.failures.clear();
    for (const vmafalign::Failure& f : a.failures())
        out.failures.push_back({
            .input  = input_name(f.input),
            .index  = f.index,
            .pts    = (f.input == Input::Reference ? ref : dis).seconds(f.timestamp),
            .reason = vmafalign::reason_name(f.reason),
        });
    out.warning = out.exceeded ? alignment_warning(c) : std::string();
}

}  // namespace

// ── The settings ─────────────────────────────────────────────────────────────

VMAFSettings vmaf_settings(const VMAFOptions& opts)
{
    const bool model_file = !opts.model.path.empty();
    return {
        .model        = model_file ? opts.model.path : opts.model.version,
        .model_source = model_file ? "file" : "built-in",
        .scaler       = std::string(vmaf_scaler_name(opts.scaler)),
        .pix_fmt      = name_or_unknown(av_get_pix_fmt_name(kScorePixFmt)),
        .bit_depth    = static_cast<int>(kScoreBpc),
        .n_threads    = static_cast<int>(kThreads),
        .n_subsample  = static_cast<int>(kSubsample),
    };
}

// ── FFmpeg's versions ────────────────────────────────────────────────────────

std::vector<VMAFLibraryVersion> vmaf_ffmpeg_libraries()
{
    const auto entry = [](const char* name, unsigned v) {
        return VMAFLibraryVersion{ name, std::format("{}.{}.{}", AV_VERSION_MAJOR(v),
                                                     AV_VERSION_MINOR(v), AV_VERSION_MICRO(v)) };
    };
    return { entry("libavutil", avutil_version()),     entry("libavcodec", avcodec_version()),
             entry("libavformat", avformat_version()), entry("libswscale", swscale_version()),
             entry("libavfilter", avfilter_version()) };
}

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

    if (!ref_ctx.open(result.ref_url, st))
        return fail("cannot open reference: " + result.ref_url);
    if (!dis_ctx.open(result.dis_url, st))
        return fail("cannot open distorted: " + result.dis_url);

    const int ref_w = ref_ctx.width();
    const int ref_h = ref_ctx.height();
    result.width  = ref_w;
    result.height = ref_h;
    result.ref_info = ref_ctx.info();
    result.dis_info = dis_ctx.info();
    const bool model_file = !opts.model.path.empty();
    result.settings = vmaf_settings(opts);

    // A yuv420p reference is used as it is, as FFmpeg's graph passes it on:
    // copied at its own size.
    SwsContext* ref_sws = sws_getContext(
        ref_w, ref_h, kScorePixFmt,
        ref_w, ref_h, kScorePixFmt,
        kRefScaleFlags, nullptr, nullptr, nullptr);

    // A reference in another pixel format is converted to yuv420p and to the
    // color matrix and range FFmpeg's graph negotiates, as the scale filter
    // FFmpeg inserts for it converts it (DECISIONS.md D-022). The distorted
    // frames are scaled to the reference's size with the chosen scaler and
    // converted to those tags, as FFmpeg's scale filter does. Frame by frame
    // through swscale's frame API (src/VMAFColor.h).
    SwsContext* ref_conv_sws = sws_alloc_context();
    if (ref_conv_sws) {
        ref_conv_sws->flags   = vmafcolor::kInsertedScaleFlags;
        ref_conv_sws->threads = kScaleThreads;
    }
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

    // The decoded frames the aligner holds, in its order (src/VMAFAlign.h).
    std::deque<AVFrame*> ref_queue, dis_queue;

    auto cleanup = [&] {
        if (model)      vmaf_model_destroy(model);
        if (vmaf)       vmaf_close(vmaf);
        sws_freeContext(ref_sws);
        sws_free_context(&ref_conv_sws);
        sws_free_context(&dis_sws);
        if (ref_scaled) av_frame_free(&ref_scaled);
        for (auto* q : { &ref_queue, &dis_queue })
            for (AVFrame*& f : *q) av_frame_free(&f);
        ref_queue.clear();
        dis_queue.clear();
        ref_ctx.close();
        dis_ctx.close();
    };

    if (!ref_conv_sws || !dis_sws) {
        cleanup();
        return fail("cannot allocate the scalers");
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

    // What can make the scores differ from FFmpeg's, each said once.
    auto warn = [&](std::string text) {
        if (std::ranges::find(result.warnings, text) != result.warnings.end()) return;
        logger::warn("VMAFAnalyzer: {}", text);
        result.warnings.push_back(std::move(text));
    };

    // The two inputs' frames are paired by timestamp (src/VMAFAlign.h,
    // DECISIONS.md D-028).
    const vmafalign::Clock ref_clock = ref_ctx.clock();
    const vmafalign::Clock dis_clock = dis_ctx.clock();
    vmafalign::Aligner aligner(ref_clock, dis_clock);
    if (!aligner.ok()) {
        cleanup();
        return fail(aligner.error());
    }

    unsigned frame_idx = 0;      // frame pairs scored
    unsigned ctx_first = 0;      // the first of them in the current libvmaf context
    std::string convert_error;   // why a frame could not be converted
    std::vector<std::pair<vmafcolor::Tags, vmafcolor::Tags>> ref_conversion_tags;
    std::vector<std::pair<vmafcolor::Tags, vmafcolor::Tags>> dis_conversion_tags;

    // Flushes the current libvmaf context's feature extractors and, with
    // `read`, reads the scores of its pairs.
    auto finish_context = [&](bool read) {
        if (int r = vmaf_read_pictures(vmaf, nullptr, nullptr, 0); r < 0)
            score_failed("vmaf_read_pictures (flush)", r);
        if (!read || frame_idx == ctx_first) return;
        result.per_frame.resize(frame_idx);
        for (unsigned i = ctx_first; i < frame_idx; ++i)
            if (int r = vmaf_score_at_index(vmaf, model, &result.per_frame[i], i - ctx_first); r < 0)
                score_failed("vmaf_score_at_index", r);
        result.alignment.contexts.push_back({ .first_index = ctx_first,
                                              .pairs       = frame_idx - ctx_first });
    };

    // Converts both frames of a pair and gives them to libvmaf; frees them.
    // False when a frame could not be converted (convert_error says why).
    // `ref_index` is the reference frame's position in its input.
    auto score_pair = [&](AVFrame* ref_frame, AVFrame* dis_frame, int64_t ref_index) {
        // Both frames → YUV420P @ ref_w × ref_h, in the color matrix and
        // range FFmpeg's graph negotiates for the two (src/VMAFColor.h).
        const vmafcolor::Input ref_in = vmafcolor::input_of(*ref_frame);
        const vmafcolor::Input dis_in = vmafcolor::input_of(*dis_frame);
        AVFrame* ref_conv   = nullptr;   // the reference converted, when it isn't yuv420p
        AVFrame* dis_scaled = nullptr;
        if (ref_frame->width != ref_w || ref_frame->height != ref_h) {
            // The pairs are scored at the size the reference's decoder had
            // when it was opened, and FFmpeg's libvmaf filter refuses inputs
            // of different sizes, so a reference whose size changes isn't
            // scored; recorded for the owner's confirmation in DECISIONS.md
            // P-063.
            convert_error = std::format(
                "the reference's frames change size, from {}x{} to {}x{} at its frame {}: a "
                "reference is scored at one size, as FFmpeg's libvmaf filter takes inputs of "
                "one size only",
                ref_w, ref_h, ref_frame->width, ref_frame->height, ref_index);
        } else if (!vmafcolor::supported(ref_in.tags.space) ||
                   !vmafcolor::supported(dis_in.tags.space)) {
            const bool ref_bad = !vmafcolor::supported(ref_in.tags.space);
            convert_error = std::format(
                "the {} has the color matrix {}, which FFmpeg's scale filter does not "
                "convert, so the pair can't be scored as FFmpeg scores it (DECISIONS.md D-016)",
                ref_bad ? "reference" : "distorted input",
                name_or_unknown(av_color_space_name(ref_bad ? ref_in.tags.space
                                                            : dis_in.tags.space)));
        } else if (vmafcolor::converts_reference(ref_in.format)) {
            const vmafcolor::Tags to = vmafcolor::negotiated(ref_in, dis_in);
            int r = convert_into(ref_conv_sws, *ref_frame, ref_w, ref_h, to, ref_conv);
            if (r < 0)
                convert_error = std::format("converting reference frame {} failed: {}",
                                            frame_idx, av_error_text(r));
            else if ((r = convert_into(dis_sws, *dis_frame, ref_w, ref_h, to, dis_scaled)) < 0)
                convert_error = std::format("converting distorted frame {} failed: {}",
                                            frame_idx, av_error_text(r));
            if (convert_error.empty()) {
                count_conversion(result.ref_conversions, ref_conversion_tags, ref_in.tags, to,
                                 frame_idx);
                count_conversion(result.dis_conversions, dis_conversion_tags, dis_in.tags, to,
                                 frame_idx);
            }
            if (!vmafcolor::verified(ref_in.format)) warn(unverified("reference", ref_in.format));
        } else {
            sws_scale(ref_sws,
                      ref_frame->data, ref_frame->linesize, 0, ref_h,
                      ref_scaled->data, ref_scaled->linesize);
            const vmafcolor::Tags to = vmafcolor::target(ref_in.tags, dis_in.tags);
            if (int r = convert_into(dis_sws, *dis_frame, ref_w, ref_h, to, dis_scaled); r < 0)
                convert_error = std::format("converting distorted frame {} failed: {}",
                                            frame_idx, av_error_text(r));
            else
                count_conversion(result.dis_conversions, dis_conversion_tags, dis_in.tags, to,
                                 frame_idx);
        }
        if (convert_error.empty() && !vmafcolor::verified(dis_in.format))
            warn(unverified("distorted input", dis_in.format));
        av_frame_free(&ref_frame);
        av_frame_free(&dis_frame);
        if (!convert_error.empty()) {
            av_frame_free(&ref_conv);
            av_frame_free(&dis_scaled);
            return false;
        }
        const AVFrame* ref_out = ref_conv ? ref_conv : ref_scaled;

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
            const auto* rsrc = ref_out->data[p];
            const auto* dsrc = dis_scaled->data[p];
            auto* rdst = static_cast<uint8_t*>(ref_pic.data[p]);
            auto* ddst = static_cast<uint8_t*>(dis_pic.data[p]);
            for (int row = 0; row < ph; ++row) {
                std::memcpy(rdst + row * ref_pic.stride[p],
                            rsrc + row * ref_out->linesize[p], pw);
                std::memcpy(ddst + row * dis_pic.stride[p],
                            dsrc + row * dis_scaled->linesize[p], pw);
            }
        }
        av_frame_free(&ref_conv);
        av_frame_free(&dis_scaled);

        // Context takes ownership of ref_pic / dis_pic backing memory; its
        // indices count from its first pair.
        if (int r = vmaf_read_pictures(vmaf, &ref_pic, &dis_pic, frame_idx++ - ctx_first); r < 0)
            score_failed("vmaf_read_pictures", r);
        return true;
    };

    using StepKind = vmafalign::Step::Kind;
    while (!st.stop_requested() && (opts.max_frames == 0 || frame_idx < opts.max_frames)) {
        const vmafalign::Step step = aligner.next();
        if (step.kind == StepKind::Done) break;

        if (step.kind == StepKind::NeedReference || step.kind == StepKind::NeedDistorted) {
            const bool ref = step.kind == StepKind::NeedReference;
            const vmafalign::Input in = ref ? vmafalign::Input::Reference
                                            : vmafalign::Input::Distorted;
            if (AVFrame* f = (ref ? ref_ctx : dis_ctx).next_video_frame()) {
                (ref ? ref_queue : dis_queue).push_back(f);
                aligner.push(in, f->best_effort_timestamp);
            } else {
                aligner.end(in);
            }
            continue;
        }
        if (step.kind == StepKind::DropReference || step.kind == StepKind::DropDistorted) {
            std::deque<AVFrame*>& q = step.kind == StepKind::DropReference ? ref_queue : dis_queue;
            av_frame_free(&q.front());
            q.pop_front();
            continue;
        }

        // A pair, scored in a new libvmaf context after a gap in the
        // reference's frames, so that no motion feature spans it.
        AVFrame* ref_frame = ref_queue.front();
        AVFrame* dis_frame = dis_queue.front();
        ref_queue.pop_front();
        dis_queue.pop_front();
        if (step.new_context) {
            finish_context(true);
            vmaf_close(vmaf);
            vmaf      = nullptr;
            ctx_first = frame_idx;
            VmafContext* fresh = nullptr;
            const char* failed = vmaf_init(&fresh, cfg) < 0 ? "vmaf_init failed" : nullptr;
            if (!failed) {
                vmaf = fresh;
                if (vmaf_use_features_from_model(vmaf, model) < 0)
                    failed = "vmaf_use_features_from_model failed";
            }
            if (failed) {
                av_frame_free(&ref_frame);
                av_frame_free(&dis_frame);
                cleanup();
                return fail(failed);
            }
        }
        if (opts.record_pts) {
            result.ref_pts.push_back(ref_ctx.seconds(ref_frame->best_effort_timestamp));
            result.dis_pts.push_back(dis_ctx.seconds(dis_frame->best_effort_timestamp));
            result.ref_index.push_back(step.ref_index);
            result.dis_index.push_back(step.dis_index);
        }
        if (!score_pair(ref_frame, dis_frame, step.ref_index)) break;
        if (opts.scored) opts.scored->fetch_add(1, std::memory_order_relaxed);

        float pair_frac = std::min(static_cast<float>(frame_idx) /
                                   static_cast<float>(total_est), 0.99f);
        progress_total.store(base + span * pair_frac);
    }

    if (!convert_error.empty()) {
        cleanup();
        return fail(convert_error);
    }

    // Flush feature extractors, and collect the last context's scores. A
    // cancelled analysis has none. The stop is read once: one requested
    // after it leaves the analysis complete, its scores and pooled values
    // both, not scores without pooled values.
    const bool cancelled = st.stop_requested();
    finish_context(!cancelled);
    if (cancelled) {
        result.per_frame.clear();
        result.alignment.contexts.clear();
    }

    // Pooled stats
    if (frame_idx > 0 && !cancelled) {
        // Pooled from the per-frame scores, with libvmaf's formulas for the
        // mean, the harmonic mean and the minimum, so that a pair scored in
        // one libvmaf context gets vmaf_score_pooled()'s values and one
        // scored in several still pools as one run (src/VMAFPool.h); the
        // contexts' first pairs give the second minimum of D-037.
        std::vector<std::size_t> context_starts;
        for (const VMAFContextRun& c : result.alignment.contexts)
            context_starts.push_back(c.first_index);
        result.pooled    = vmafpool::pool(result.per_frame, context_starts);
        result.vmaf_mean = result.pooled.mean;
        result.vmaf_min  = result.pooled.min;
        result.vmaf_p5   = result.pooled.p5;

        logger::info("VMAFAnalyzer: {} frames  mean={:.2f}  min={:.2f}  p5={:.2f}  [{}]",
                     frame_idx, result.vmaf_mean, result.vmaf_min, result.vmaf_p5,
                     result.label);
    }

    // How the frames paired, with a warning above the threshold (D-028).
    record_alignment(result.alignment, aligner, ref_ctx, ref_clock, dis_ctx, dis_clock);
    if (result.alignment.exceeded) warn(result.alignment.warning);

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
                                  const VMAFOptions& opts, std::stop_token st)
{
    VMAFResult result{
        .ref_url = std::move(ref_url),
        .dis_url = std::move(dis_url),
        .label   = "Analysis",
    };
    std::atomic<float> progress{ 0.0f };
    analyze_pair(result, st, progress, 0.0f, 1.0f, opts);
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
