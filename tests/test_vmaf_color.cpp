// vmafcolor: the tags FFmpeg's section 5 filter graph negotiates for both
// inputs of its libvmaf filter, and Mirage's conversion of both frames of a
// pair against the same graph run through libavfilter (scale, format and
// libvmaf filters, as eval/consistency/run_delta.py runs them through ffmpeg).

#include <catch2/catch_test_macros.hpp>

#include "VMAFColor.h"

#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace {

using vmafcolor::Tags;

constexpr AVColorSpace kNoSpace = AVCOL_SPC_UNSPECIFIED;
constexpr AVColorRange kNoRange = AVCOL_RANGE_UNSPECIFIED;
constexpr AVColorRange kTv      = AVCOL_RANGE_MPEG;
constexpr AVColorRange kPc      = AVCOL_RANGE_JPEG;

std::string name(const Tags& t)
{
    return std::format("{}/{}", av_color_space_name(t.space), av_color_range_name(t.range));
}

// A w x h yuv420p frame with luma and chroma gradients, so that a change of
// matrix moves its luma, tagged `t`.
AVFrame* make_frame(int w, int h, const Tags& t, int seed = 0)
{
    AVFrame* f = av_frame_alloc();
    f->format = AV_PIX_FMT_YUV420P;
    f->width = w;
    f->height = h;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            f->data[0][y * f->linesize[0] + x] =
                static_cast<uint8_t>(16 + (x * 219 / w + y * 37 + seed) % 220);
    for (int y = 0; y < (h + 1) / 2; ++y) {
        for (int x = 0; x < (w + 1) / 2; ++x) {
            f->data[1][y * f->linesize[1] + x] = static_cast<uint8_t>(16 + (x * 448 / w) % 225);
            f->data[2][y * f->linesize[2] + x] = static_cast<uint8_t>(240 - (y * 448 / h) % 225);
        }
    }
    f->colorspace = t.space;
    f->color_range = t.range;
    f->pts = 0;
    return f;
}

// A w x h frame of any pixel format the tests use, tagged `t`: every
// component at its own bit depth, mostly inside the limited range, with values
// that differ from pixel to pixel, so that a change of matrix, range, chroma
// subsampling or bit depth shows in the bytes.
AVFrame* make_any(AVPixelFormat fmt, int w, int h, const Tags& t, int seed = 0)
{
    AVFrame* f = av_frame_alloc();
    f->format = fmt;
    f->width = w;
    f->height = h;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(fmt);
    std::vector<std::uint16_t> line(static_cast<std::size_t>(w));
    for (int c = 0; c < d->nb_components; ++c) {
        const bool chroma = (c == 1 || c == 2) && !(d->flags & AV_PIX_FMT_FLAG_RGB);
        const int cw = chroma ? AV_CEIL_RSHIFT(w, d->log2_chroma_w) : w;
        const int ch = chroma ? AV_CEIL_RSHIFT(h, d->log2_chroma_h) : h;
        const int top = (1 << d->comp[c].depth) - 1;
        const int lo = top / 16, span = top - 2 * lo + 1;
        for (int y = 0; y < ch; ++y) {
            for (int x = 0; x < cw; ++x) {
                unsigned v = static_cast<unsigned>(x * 7919 + y * 104729 + c * 31337 + seed * 977);
                v ^= v >> 7;
                v *= 2654435761u;
                v ^= v >> 13;
                const int value = lo + static_cast<int>(v % static_cast<unsigned>(span));
                line[static_cast<std::size_t>(x)] = static_cast<std::uint16_t>(value);
            }
            av_write_image_line2(line.data(), f->data, f->linesize, d, 0, y, c, cw, 2);
        }
    }
    f->colorspace = t.space;
    f->color_range = t.range;
    f->pts = 0;
    return f;
}

AVFilterContext* source(AVFilterGraph* g, const char* label, const AVFrame& f)
{
    const std::string args = std::format(
        "video_size={}x{}:pix_fmt={}:time_base=1/25:pixel_aspect=1/1:colorspace={}:range={}",
        f.width, f.height, static_cast<int>(f.format), static_cast<int>(f.colorspace),
        static_cast<int>(f.color_range));
    AVFilterContext* ctx = nullptr;
    REQUIRE(avfilter_graph_create_filter(&ctx, avfilter_get_by_name("buffer"), label,
                                         args.c_str(), nullptr, g) >= 0);
    return ctx;
}

AVFilterInOut* endpoint(const char* label, AVFilterContext* ctx, AVFilterInOut* next = nullptr)
{
    AVFilterInOut* io = avfilter_inout_alloc();
    io->name = av_strdup(label);
    io->filter_ctx = ctx;
    io->pad_idx = 0;
    io->next = next;
    return io;
}

// Keeps FFmpeg's log quiet while it lives: the libvmaf filter's score line,
// and the error of a graph FFmpeg refuses.
struct QuietLog {
    int level = av_log_get_level();
    QuietLog() { av_log_set_level(AV_LOG_QUIET); }
    ~QuietLog() { av_log_set_level(level); }
};

// One frame of a pair as the libvmaf filter of FFmpeg's section 5 graph
// receives it: [dis]scale=W:H:flags=<scaler>,format=yuv420p[d];
// [ref]format=yuv420p[r];[d][r]libvmaf. The libvmaf filter passes its first
// input on, so the sink gets the distorted frame; with `reference`, the graph
// ends in [r][d]libvmaf and the sink gets the reference frame. The order of
// the pads changes nothing else: the libvmaf filter shares one list of
// matrices and one of ranges over its inputs and its output
// (libavfilter/formats.c, ff_default_query_formats), and its filters are made
// in the same order. nullptr if FFmpeg cannot build the graph.
AVFrame* ffmpeg_frame(const AVFrame& dis, const AVFrame& ref, const char* scaler, bool reference)
{
    const QuietLog quiet;
    AVFilterGraph* g = avfilter_graph_alloc();
    AVFilterContext* src_dis = source(g, "dis", dis);
    AVFilterContext* src_ref = source(g, "ref", ref);
    AVFilterContext* sink = nullptr;
    REQUIRE(avfilter_graph_create_filter(&sink, avfilter_get_by_name("buffersink"), "out",
                                         nullptr, nullptr, g) >= 0);
    const std::string text = std::format(
        "[dis]scale={}:{}:flags={},format=yuv420p[d];[ref]format=yuv420p[r];"
        "{}libvmaf=model=version=vmaf_v0.6.1:n_threads=1[out]", ref.width, ref.height, scaler,
        reference ? "[r][d]" : "[d][r]");
    AVFilterInOut* outputs = endpoint("dis", src_dis, endpoint("ref", src_ref));
    AVFilterInOut* inputs = endpoint("out", sink);
    const int parsed = avfilter_graph_parse_ptr(g, text.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    REQUIRE(parsed >= 0);
    if (avfilter_graph_config(g, nullptr) < 0) {
        avfilter_graph_free(&g);
        return nullptr;
    }

    AVFrame* d = av_frame_clone(&dis);
    AVFrame* r = av_frame_clone(&ref);
    REQUIRE(av_buffersrc_add_frame(src_dis, d) >= 0);
    REQUIRE(av_buffersrc_add_frame(src_ref, r) >= 0);
    av_frame_free(&d);
    av_frame_free(&r);
    REQUIRE(av_buffersrc_add_frame(src_dis, nullptr) >= 0);
    REQUIRE(av_buffersrc_add_frame(src_ref, nullptr) >= 0);
    AVFrame* out = av_frame_alloc();
    REQUIRE(av_buffersink_get_frame(sink, out) >= 0);
    avfilter_graph_free(&g);
    return out;
}

AVFrame* ffmpeg_distorted(const AVFrame& dis, const AVFrame& ref, const char* scaler = "bilinear")
{
    return ffmpeg_frame(dis, ref, scaler, false);
}

AVFrame* ffmpeg_reference(const AVFrame& dis, const AVFrame& ref)
{
    return ffmpeg_frame(dis, ref, "bilinear", true);
}

// The tags the analyzer gives both frames (VMAFAnalyzer.cpp): negotiated()
// when FFmpeg's graph converts the reference, else D-016's target().
Tags analyzer_tags(const AVFrame& ref, const AVFrame& dis)
{
    return vmafcolor::converts_reference(static_cast<AVPixelFormat>(ref.format))
        ? vmafcolor::negotiated(vmafcolor::input_of(ref), vmafcolor::input_of(dis))
        : vmafcolor::target(vmafcolor::tags_of(ref), vmafcolor::tags_of(dis));
}

// Mirage's conversion of `in` to a w x h yuv420p frame with the tags `to`,
// with swscale's `flags` on `threads` threads, the analyzer's one by default.
AVFrame* mirage_convert(const AVFrame& in, int w, int h, const Tags& to, unsigned flags,
                        int threads = 1)
{
    SwsContext* sws = sws_alloc_context();
    sws->flags = flags;
    sws->threads = threads;
    AVFrame* copy = av_frame_clone(&in);
    AVFrame* out = av_frame_alloc();
    out->format = AV_PIX_FMT_YUV420P;
    out->width = w;
    out->height = h;
    REQUIRE(vmafcolor::convert(sws, *copy, *out, to) == 0);
    av_frame_free(&copy);
    sws_free_context(&sws);
    return out;
}

// Mirage's conversion of a reference that isn't yuv420p, as VMAFAnalyzer.cpp does it.
AVFrame* mirage_reference(const AVFrame& ref, const AVFrame& dis)
{
    REQUIRE(vmafcolor::converts_reference(static_cast<AVPixelFormat>(ref.format)));
    return mirage_convert(ref, ref.width, ref.height, analyzer_tags(ref, dis),
                          vmafcolor::kInsertedScaleFlags);
}

// Mirage's conversion of `dis` to `ref`'s size and to target()'s tags, with
// swscale's scaler `flags`.
AVFrame* mirage_distorted(const AVFrame& dis, const AVFrame& ref, unsigned flags = SWS_BILINEAR)
{
    SwsContext* sws = sws_alloc_context();
    sws->flags = flags;
    sws->threads = 1;
    AVFrame* in = av_frame_clone(&dis);
    AVFrame* out = av_frame_alloc();
    out->format = AV_PIX_FMT_YUV420P;
    out->width = ref.width;
    out->height = ref.height;
    REQUIRE(vmafcolor::convert(sws, *in, *out,
                               vmafcolor::target(vmafcolor::tags_of(ref), vmafcolor::tags_of(*in)))
            == 0);
    av_frame_free(&in);
    sws_free_context(&sws);
    return out;
}

// `f`'s pixels and tags in another pixel format at its size. yuvj420p has
// yuv420p's layout, so a yuvj420p frame only gets the name and full range.
AVFrame* in_format(const AVFrame& f, AVPixelFormat fmt)
{
    AVFrame* out = av_frame_alloc();
    if (fmt == AV_PIX_FMT_YUVJ420P) {
        REQUIRE(av_frame_ref(out, &f) >= 0);
        out->format = fmt;
        out->color_range = kPc;
        return out;
    }
    out->format = fmt;
    out->width = f.width;
    out->height = f.height;
    REQUIRE(av_frame_copy_props(out, &f) >= 0);
    REQUIRE(av_frame_get_buffer(out, 0) >= 0);
    SwsContext* sws = sws_alloc_context();
    sws->flags = SWS_BILINEAR;
    REQUIRE(sws_scale_frame(sws, out, &f) >= 0);
    sws_free_context(&sws);
    return out;
}

// The rows of plane p that differ between two yuv420p frames of one size.
int differing_rows(const AVFrame& a, const AVFrame& b, int p)
{
    const int w = p == 0 ? a.width : (a.width + 1) / 2;
    const int h = p == 0 ? a.height : (a.height + 1) / 2;
    int rows = 0;
    for (int y = 0; y < h; ++y)
        rows += std::memcmp(a.data[p] + y * a.linesize[p], b.data[p] + y * b.linesize[p], w) != 0;
    return rows;
}

struct GraphCase {
    const char* label;   // eval/consistency/color_cases.py's case of these tags
    Tags        ref;
    Tags        dis;
};

// The tags of the cases in eval/consistency/color_cases.py that FFmpeg scores.
const std::vector<GraphCase>& graph_cases()
{
    static const std::vector<GraphCase> cases = {
        { "m01_bt709_vs_smpte170m",     { AVCOL_SPC_BT709, kTv },      { AVCOL_SPC_SMPTE170M, kTv } },
        { "m02_bt709_vs_bt709",         { AVCOL_SPC_BT709, kTv },      { AVCOL_SPC_BT709, kTv } },
        { "m03_bt709_vs_untagged",      { AVCOL_SPC_BT709, kTv },      { kNoSpace, kNoRange } },
        { "m04_untagged_vs_bt709",      { kNoSpace, kNoRange },        { AVCOL_SPC_BT709, kTv } },
        { "m05_untagged_vs_smpte170m",  { kNoSpace, kNoRange },        { AVCOL_SPC_SMPTE170M, kTv } },
        { "m06_untagged_vs_untagged",   { kNoSpace, kNoRange },        { kNoSpace, kNoRange } },
        { "m07_smpte170m_vs_bt470bg",   { AVCOL_SPC_SMPTE170M, kTv },  { AVCOL_SPC_BT470BG, kTv } },
        { "m08_bt2020nc_vs_bt709",      { AVCOL_SPC_BT2020_NCL, kTv }, { AVCOL_SPC_BT709, kTv } },
        { "r01_tv_vs_pc",               { AVCOL_SPC_BT709, kTv },      { AVCOL_SPC_BT709, kPc } },
        { "r02_pc_vs_tv",               { AVCOL_SPC_BT709, kPc },      { AVCOL_SPC_BT709, kTv } },
        { "r03_norange_vs_pc",          { AVCOL_SPC_BT709, kNoRange }, { AVCOL_SPC_BT709, kPc } },
        { "r04_pc_vs_norange",          { AVCOL_SPC_BT709, kPc },      { AVCOL_SPC_BT709, kNoRange } },
        { "r05_norange_vs_tv",          { AVCOL_SPC_BT709, kNoRange }, { AVCOL_SPC_BT709, kTv } },
        { "r06_tv_vs_norange",          { AVCOL_SPC_BT709, kTv },      { AVCOL_SPC_BT709, kNoRange } },
        { "x01_bt709tv_vs_smpte170mpc", { AVCOL_SPC_BT709, kTv },      { AVCOL_SPC_SMPTE170M, kPc } },
    };
    return cases;
}

} // namespace

TEST_CASE("vmafcolor::target: the tags of FFmpeg's scale output in the color tag cases", "[vmafcolor]")
{
    // The scale filter's output tags in FFmpeg 8.1.1's verbose log of each case:
    // eval/consistency/color-cases-before/main/<case>/ffmpeg_colorspace_lines.txt.
    const std::vector<Tags> want = {
        { AVCOL_SPC_BT709, kTv },      { AVCOL_SPC_BT709, kTv },  { AVCOL_SPC_BT709, kTv },
        { kNoSpace, kTv },             { kNoSpace, kTv },         { kNoSpace, kNoRange },
        { AVCOL_SPC_SMPTE170M, kTv },  { AVCOL_SPC_BT2020_NCL, kTv },
        { AVCOL_SPC_BT709, kTv },      { AVCOL_SPC_BT709, kPc },  { AVCOL_SPC_BT709, kNoRange },
        { AVCOL_SPC_BT709, kPc },      { AVCOL_SPC_BT709, kTv },  { AVCOL_SPC_BT709, kTv },
        { AVCOL_SPC_BT709, kTv },
    };
    REQUIRE(want.size() == graph_cases().size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        const GraphCase& c = graph_cases()[i];
        INFO(c.label);
        CHECK(name(vmafcolor::target(c.ref, c.dis)) == name(want[i]));
    }
}

TEST_CASE("vmafcolor::supported: the matrices libswscale converts", "[vmafcolor]")
{
    for (AVColorSpace s : { AVCOL_SPC_BT709, AVCOL_SPC_SMPTE170M, AVCOL_SPC_BT470BG,
                            AVCOL_SPC_UNSPECIFIED, AVCOL_SPC_BT2020_NCL, AVCOL_SPC_SMPTE240M,
                            AVCOL_SPC_FCC, AVCOL_SPC_RGB }) {
        INFO(av_color_space_name(s));
        CHECK(vmafcolor::supported(s));
    }
    for (AVColorSpace s : { AVCOL_SPC_YCGCO, AVCOL_SPC_BT2020_CL, AVCOL_SPC_ICTCP,
                            AVCOL_SPC_CHROMA_DERIVED_NCL, AVCOL_SPC_SMPTE2085 }) {
        INFO(av_color_space_name(s));
        CHECK_FALSE(vmafcolor::supported(s));
    }
}

TEST_CASE("vmafcolor::change: what libswscale converts between two tag sets", "[vmafcolor]")
{
    using C = vmafcolor::Change;
    // Matrices with other coefficients; unspecified has BT.601's.
    CHECK(vmafcolor::change({ AVCOL_SPC_SMPTE170M, kTv }, { AVCOL_SPC_BT709, kTv }) == C{ true, false });
    CHECK(vmafcolor::change({ kNoSpace, kNoRange }, { AVCOL_SPC_BT709, kTv }) == C{ true, false });
    CHECK(vmafcolor::change({ AVCOL_SPC_BT709, kTv }, { kNoSpace, kTv }) == C{ true, false });
    CHECK(vmafcolor::change({ AVCOL_SPC_BT709, kTv }, { AVCOL_SPC_BT2020_NCL, kTv }) == C{ true, false });
    CHECK(vmafcolor::change({ AVCOL_SPC_SMPTE170M, kTv }, { kNoSpace, kTv }) == C{ false, false });
    CHECK(vmafcolor::change({ AVCOL_SPC_BT470BG, kTv }, { AVCOL_SPC_SMPTE170M, kTv }) == C{ false, false });
    CHECK(vmafcolor::change({ AVCOL_SPC_BT709, kTv }, { AVCOL_SPC_BT709, kTv }) == C{ false, false });
    // Ranges: only full against not full; unspecified counts as limited.
    CHECK(vmafcolor::change({ AVCOL_SPC_BT709, kPc }, { AVCOL_SPC_BT709, kTv }) == C{ false, true });
    CHECK(vmafcolor::change({ AVCOL_SPC_BT709, kPc }, { AVCOL_SPC_BT709, kNoRange }) == C{ false, true });
    CHECK(vmafcolor::change({ AVCOL_SPC_BT709, kNoRange }, { AVCOL_SPC_BT709, kPc }) == C{ false, true });
    CHECK(vmafcolor::change({ AVCOL_SPC_BT709, kNoRange }, { AVCOL_SPC_BT709, kTv }) == C{ false, false });
    CHECK(vmafcolor::change({ AVCOL_SPC_SMPTE170M, kPc }, { AVCOL_SPC_BT709, kTv }) == C{ true, true });
}

TEST_CASE("vmafcolor::convert: the pixels and tags FFmpeg's graph gives the distorted frame", "[vmafcolor]")
{
    if (!avfilter_get_by_name("libvmaf"))
        SKIP("this libavfilter has no libvmaf filter");
    for (const GraphCase& c : graph_cases()) {
        INFO(c.label);
        AVFrame* dis = make_frame(64, 36, c.dis);
        AVFrame* ref = make_frame(192, 108, c.ref, 7);
        AVFrame* want = ffmpeg_distorted(*dis, *ref);
        REQUIRE(want);
        AVFrame* got = mirage_distorted(*dis, *ref);
        CHECK(name(vmafcolor::tags_of(*got)) == name(vmafcolor::tags_of(*want)));
        REQUIRE(got->width == want->width);
        REQUIRE(got->height == want->height);
        REQUIRE(want->format == AV_PIX_FMT_YUV420P);
        for (int p = 0; p < 3; ++p) {
            INFO("plane " << p);
            CHECK(differing_rows(*got, *want, p) == 0);
        }
        av_frame_free(&got);
        av_frame_free(&want);
        av_frame_free(&ref);
        av_frame_free(&dis);
    }
}

TEST_CASE("vmafcolor::convert: each scaler gives the pixels of FFmpeg's scale filter with those flags", "[vmafcolor]")
{
    if (!avfilter_get_by_name("libvmaf"))
        SKIP("this libavfilter has no libvmaf filter");
    // The scalers of VMAFScaler, by the names flags= takes, and their swscale flags.
    const std::pair<const char*, unsigned> scalers[] = {
        { "bilinear", SWS_BILINEAR }, { "bicubic", SWS_BICUBIC }, { "lanczos", SWS_LANCZOS },
    };
    for (const GraphCase& c : graph_cases()) {
        INFO(c.label);
        AVFrame* dis = make_frame(64, 36, c.dis);
        AVFrame* ref = make_frame(192, 108, c.ref, 7);
        std::vector<AVFrame*> got;
        for (const auto& [scaler, flags] : scalers) {
            INFO(scaler);
            AVFrame* want = ffmpeg_distorted(*dis, *ref, scaler);
            REQUIRE(want);
            got.push_back(mirage_distorted(*dis, *ref, flags));
            CHECK(name(vmafcolor::tags_of(*got.back())) == name(vmafcolor::tags_of(*want)));
            for (int p = 0; p < 3; ++p) {
                INFO("plane " << p);
                CHECK(differing_rows(*got.back(), *want, p) == 0);
            }
            av_frame_free(&want);
        }
        // The scalers give different pixels, so a scaler that is not applied shows.
        CHECK(differing_rows(*got[0], *got[1], 0) > 0);
        CHECK(differing_rows(*got[1], *got[2], 0) > 0);
        for (AVFrame*& f : got) av_frame_free(&f);
        av_frame_free(&ref);
        av_frame_free(&dis);
    }
}

TEST_CASE("vmafcolor::convert: chroma siting, interlacing, primaries and transfer, as FFmpeg's scale filter takes them", "[vmafcolor]")
{
    if (!avfilter_get_by_name("libvmaf"))
        SKIP("this libavfilter has no libvmaf filter");
    const Tags ref_tags{ AVCOL_SPC_BT709, kTv };
    AVFrame* ref = make_frame(192, 108, ref_tags, 7);
    for (const Tags& dis_tags : { Tags{ AVCOL_SPC_BT709, kTv }, Tags{ AVCOL_SPC_SMPTE170M, kTv } }) {
        INFO(name(dis_tags));
        AVFrame* plain = make_frame(64, 36, dis_tags);
        AVFrame* sited = make_frame(64, 36, dis_tags);
        sited->chroma_location = AVCHROMA_LOC_TOPLEFT;
        sited->flags |= AV_FRAME_FLAG_INTERLACED | AV_FRAME_FLAG_TOP_FIELD_FIRST;
        sited->color_primaries = AVCOL_PRI_BT470BG;   // kept, not mapped to the reference's
        sited->color_trc = AVCOL_TRC_GAMMA28;

        AVFrame* want = ffmpeg_distorted(*sited, *ref);
        REQUIRE(want);
        AVFrame* got = mirage_distorted(*sited, *ref);
        AVFrame* base = mirage_distorted(*plain, *ref);
        for (int p = 0; p < 3; ++p) {
            INFO("plane " << p);
            CHECK(differing_rows(*got, *want, p) == 0);
            CHECK(differing_rows(*got, *base, p) == 0);
        }
        CHECK((got->flags & AV_FRAME_FLAG_INTERLACED) == 0);
        CHECK(got->chroma_location == AVCHROMA_LOC_UNSPECIFIED);
        CHECK(got->color_primaries == AVCOL_PRI_BT470BG);
        CHECK(want->color_primaries == AVCOL_PRI_BT470BG);
        CHECK(got->color_trc == AVCOL_TRC_GAMMA28);
        CHECK(want->color_trc == AVCOL_TRC_GAMMA28);

        // Scaling them as interlaced, top-left sited frames would differ.
        SwsContext* sws = sws_alloc_context();
        sws->flags = SWS_BILINEAR;
        AVFrame* as_tagged = av_frame_alloc();
        as_tagged->format = AV_PIX_FMT_YUV420P;
        as_tagged->width = 192;
        as_tagged->height = 108;
        as_tagged->colorspace = ref_tags.space;
        as_tagged->color_range = ref_tags.range;
        as_tagged->chroma_location = AVCHROMA_LOC_TOPLEFT;
        as_tagged->flags = sited->flags;
        REQUIRE(av_frame_get_buffer(as_tagged, 0) >= 0);
        REQUIRE(sws_scale_frame(sws, as_tagged, sited) >= 0);
        CHECK(differing_rows(*as_tagged, *want, 0) + differing_rows(*as_tagged, *want, 1) > 0);
        sws_free_context(&sws);

        av_frame_free(&as_tagged);
        av_frame_free(&base);
        av_frame_free(&got);
        av_frame_free(&want);
        av_frame_free(&sited);
        av_frame_free(&plain);
    }
    av_frame_free(&ref);
}

TEST_CASE("vmafcolor: FFmpeg's graph refuses a matrix libswscale lacks", "[vmafcolor]")
{
    if (!avfilter_get_by_name("libvmaf"))
        SKIP("this libavfilter has no libvmaf filter");
    const Tags ycgco{ AVCOL_SPC_YCGCO, kTv };
    const Tags bt709{ AVCOL_SPC_BT709, kTv };
    for (const auto& [ref_tags, dis_tags] : { std::pair{ bt709, ycgco }, std::pair{ ycgco, bt709 },
                                              std::pair{ ycgco, ycgco } }) {
        INFO(name(ref_tags) << " vs " << name(dis_tags));
        AVFrame* dis = make_frame(64, 36, dis_tags);
        AVFrame* ref = make_frame(192, 108, ref_tags, 7);
        CHECK(ffmpeg_distorted(*dis, *ref) == nullptr);
        CHECK_FALSE((vmafcolor::supported(ref_tags.space) && vmafcolor::supported(dis_tags.space)));
        av_frame_free(&ref);
        av_frame_free(&dis);
    }
}

namespace {

// Matrix and range tags of the negotiation tests: tagged, full range, no tags,
// no range tag, no matrix tag.
const std::vector<Tags>& tag_cases()
{
    static const std::vector<Tags> t = {
        { AVCOL_SPC_BT709, kTv }, { AVCOL_SPC_SMPTE170M, kPc }, { kNoSpace, kNoRange },
        { AVCOL_SPC_BT709, kNoRange }, { kNoSpace, kTv },
    };
    return t;
}

// One pixel format of each kind negotiated() tells apart: yuv420p, another
// plain YUV format, a yuvj format, and formats that aren't plain YUV.
constexpr AVPixelFormat kKinds[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_YUVJ420P,
                                     AV_PIX_FMT_GRAY8, AV_PIX_FMT_RGB24 };

bool kind_offers_one_range(AVPixelFormat f)
{
    return f == AV_PIX_FMT_YUV420P || f == AV_PIX_FMT_YUV420P10LE;
}

std::string input_name(const AVFrame& f)
{
    return std::format("{} {}", av_get_pix_fmt_name(static_cast<AVPixelFormat>(f.format)),
                       name(vmafcolor::tags_of(f)));
}

int differing_rows_all(const AVFrame& a, const AVFrame& b)
{
    return differing_rows(a, b, 0) + differing_rows(a, b, 1) + differing_rows(a, b, 2);
}

} // namespace

TEST_CASE("vmafcolor::negotiated: the tags libavfilter gives both inputs of the libvmaf filter", "[vmafcolor]")
{
    if (!avfilter_get_by_name("libvmaf"))
        SKIP("this libavfilter has no libvmaf filter");
    for (AVPixelFormat rf : kKinds) {
        for (const Tags& rt : tag_cases()) {
            AVFrame* ref = make_any(rf, 192, 108, rt, 7);
            for (AVPixelFormat df : kKinds) {
                for (std::size_t k = 0; k < 4; ++k) {
                    AVFrame* dis = make_any(df, 64, 36, tag_cases()[k]);
                    INFO("reference " << input_name(*ref) << ", distorted " << input_name(*dis));
                    const std::string want = name(vmafcolor::negotiated(vmafcolor::input_of(*ref),
                                                                        vmafcolor::input_of(*dis)));
                    AVFrame* d = ffmpeg_distorted(*dis, *ref);
                    REQUIRE(d);
                    CHECK(name(vmafcolor::tags_of(*d)) == want);
                    av_frame_free(&d);
                    // The reference frame gets the same tags: one list for both.
                    if (df == AV_PIX_FMT_YUV420P || df == AV_PIX_FMT_GRAY8) {
                        AVFrame* r = ffmpeg_reference(*dis, *ref);
                        REQUIRE(r);
                        CHECK(name(vmafcolor::tags_of(*r)) == want);
                        av_frame_free(&r);
                    }
                    av_frame_free(&dis);
                }
            }
            av_frame_free(&ref);
        }
    }
}

TEST_CASE("vmafcolor::target: negotiated()'s tags for a yuv420p reference, but for one range tag", "[vmafcolor]")
{
    // D-016's rule as implemented (D-021) takes the distorted frames' range
    // tag where libavfilter takes what their buffer source offers. They differ
    // only for a reference without a range tag and distorted frames tagged
    // limited in a format whose source offers more than limited, and there
    // target() says limited where FFmpeg's graph says unspecified, which
    // libswscale converts alike (next test).
    for (const Tags& rt : tag_cases()) {
        for (AVPixelFormat df : kKinds) {
            for (const Tags& dt : tag_cases()) {
                INFO(name(rt) << " vs " << av_get_pix_fmt_name(df) << " " << name(dt));
                const vmafcolor::Input ref{ AV_PIX_FMT_YUV420P, rt };
                const vmafcolor::Input dis{ df, dt };
                const Tags mine = vmafcolor::target(rt, dt);
                const Tags theirs = vmafcolor::negotiated(ref, dis);
                const bool differ =
                    rt.range == kNoRange && dt.range == kTv && !kind_offers_one_range(df);
                CHECK((mine == theirs) == !differ);
                if (differ) {
                    CHECK(name(mine) == name({ rt.space, kTv }));
                    CHECK(name(theirs) == name({ rt.space, kNoRange }));
                }
            }
        }
    }
}

TEST_CASE("vmafcolor::convert: where target() and FFmpeg's graph tag the range differently, the pixels are the same", "[vmafcolor]")
{
    if (!avfilter_get_by_name("libvmaf"))
        SKIP("this libavfilter has no libvmaf filter");
    AVFrame* ref = make_any(AV_PIX_FMT_YUV420P, 192, 108, { AVCOL_SPC_BT709, kNoRange }, 7);
    for (AVPixelFormat df : { AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_GRAY8, AV_PIX_FMT_RGB24 }) {
        INFO(av_get_pix_fmt_name(df));
        AVFrame* dis = make_any(df, 64, 36, { AVCOL_SPC_SMPTE170M, kTv });
        AVFrame* want = ffmpeg_distorted(*dis, *ref);
        REQUIRE(want);
        AVFrame* got = mirage_distorted(*dis, *ref);
        CHECK(name(vmafcolor::tags_of(*want)) == "bt709/unknown");
        CHECK(name(vmafcolor::tags_of(*got)) == "bt709/tv");
        CHECK(differing_rows_all(*got, *want) == 0);
        av_frame_free(&got);
        av_frame_free(&want);
        av_frame_free(&dis);
    }
    av_frame_free(&ref);
}

TEST_CASE("vmafcolor::convert: a reference that isn't yuv420p gets the bytes of the scale filter FFmpeg inserts (D-022)", "[vmafcolor]")
{
    if (!avfilter_get_by_name("libvmaf"))
        SKIP("this libavfilter has no libvmaf filter");
    // Every verified format but yuv420p, against distorted frames whose tags
    // differ from the reference's in matrix, range or both, or are missing,
    // and against one that needs a format conversion as well.
    const std::pair<AVPixelFormat, Tags> dis_cases[] = {
        { AV_PIX_FMT_YUV420P, { AVCOL_SPC_SMPTE170M, kTv } },
        { AV_PIX_FMT_YUV420P, { AVCOL_SPC_BT709, kPc } },
        { AV_PIX_FMT_YUV420P, { kNoSpace, kNoRange } },
        { AV_PIX_FMT_YUV420P10LE, { AVCOL_SPC_BT709, kTv } },
    };
    const Tags ref_tags[] = { { AVCOL_SPC_BT709, kTv }, { kNoSpace, kNoRange },
                              { AVCOL_SPC_SMPTE170M, kPc } };
    for (AVPixelFormat rf : vmafcolor::verified_formats()) {
        if (!vmafcolor::converts_reference(rf)) continue;
        for (const Tags& rt : ref_tags) {
            AVFrame* ref = make_any(rf, 192, 108, rt, 7);
            for (const auto& [df, dt] : dis_cases) {
                AVFrame* dis = make_any(df, 64, 36, dt);
                INFO("reference " << input_name(*ref) << ", distorted " << input_name(*dis));
                AVFrame* want = ffmpeg_reference(*dis, *ref);
                REQUIRE(want);
                REQUIRE(want->format == AV_PIX_FMT_YUV420P);
                AVFrame* got = mirage_reference(*ref, *dis);
                CHECK(name(vmafcolor::tags_of(*got)) == name(vmafcolor::tags_of(*want)));
                for (int p = 0; p < 3; ++p) {
                    INFO("plane " << p);
                    CHECK(differing_rows(*got, *want, p) == 0);
                }
                av_frame_free(&got);
                av_frame_free(&want);
                av_frame_free(&dis);
            }
            av_frame_free(&ref);
        }
    }
}

TEST_CASE("vmafcolor::convert: distorted frames of every verified format get the bytes of FFmpeg's scale filter", "[vmafcolor]")
{
    if (!avfilter_get_by_name("libvmaf"))
        SKIP("this libavfilter has no libvmaf filter");
    // Against yuv420p references, tagged and untagged, which pass
    // unconverted, and against references FFmpeg converts too; the first with
    // each scaler.
    const std::pair<AVPixelFormat, Tags> ref_cases[] = {
        { AV_PIX_FMT_YUV420P, { AVCOL_SPC_BT709, kTv } },
        { AV_PIX_FMT_YUV420P, { kNoSpace, kNoRange } },
        { AV_PIX_FMT_YUV422P, { AVCOL_SPC_BT709, kTv } },
        { AV_PIX_FMT_YUV420P10LE, { AVCOL_SPC_SMPTE170M, kPc } },
    };
    const std::pair<const char*, unsigned> scalers[] = {
        { "bilinear", SWS_BILINEAR }, { "bicubic", SWS_BICUBIC }, { "lanczos", SWS_LANCZOS },
    };
    const Tags dis_tags[] = { { AVCOL_SPC_SMPTE170M, kTv }, { AVCOL_SPC_BT709, kPc },
                              { kNoSpace, kNoRange } };
    for (AVPixelFormat df : vmafcolor::verified_formats()) {
        for (const Tags& dt : dis_tags) {
            AVFrame* dis = make_any(df, 64, 36, dt);
            for (std::size_t k = 0; k < std::size(ref_cases); ++k) {
                AVFrame* ref = make_any(ref_cases[k].first, 192, 108, ref_cases[k].second, 7);
                for (std::size_t s = 0; s < (k == 0 ? std::size(scalers) : 1); ++s) {
                    INFO("reference " << input_name(*ref) << ", distorted " << input_name(*dis)
                         << ", " << scalers[s].first);
                    AVFrame* want = ffmpeg_distorted(*dis, *ref, scalers[s].first);
                    REQUIRE(want);
                    AVFrame* got = mirage_convert(*dis, 192, 108, analyzer_tags(*ref, *dis),
                                                  scalers[s].second);
                    // The same tags, but where target() tags the range limited
                    // and FFmpeg's graph unspecified (the tests above).
                    CHECK(name(vmafcolor::tags_of(*want)) ==
                          name(vmafcolor::negotiated(vmafcolor::input_of(*ref),
                                                     vmafcolor::input_of(*dis))));
                    for (int p = 0; p < 3; ++p) {
                        INFO("plane " << p);
                        CHECK(differing_rows(*got, *want, p) == 0);
                    }
                    av_frame_free(&got);
                    av_frame_free(&want);
                }
                av_frame_free(&ref);
            }
            av_frame_free(&dis);
        }
    }
}

TEST_CASE("vmafcolor::verified: not NV12 or NV21, which libswscale converts differently with more threads", "[vmafcolor]")
{
    // With a change of matrix, one swscale thread, as the analyzer uses, and
    // four give other bytes; FFmpeg's scale filters use as many threads as
    // `ffmpeg` finds CPUs. Without one they agree.
    for (AVPixelFormat fmt : { AV_PIX_FMT_NV12, AV_PIX_FMT_NV21 }) {
        INFO(av_get_pix_fmt_name(fmt));
        CHECK_FALSE(vmafcolor::verified(fmt));
        AVFrame* in = make_any(fmt, 192, 108, { AVCOL_SPC_BT709, kTv });
        for (const Tags& to : { Tags{ AVCOL_SPC_SMPTE170M, kTv }, Tags{ AVCOL_SPC_BT709, kTv } }) {
            INFO(name(to));
            AVFrame* one = mirage_convert(*in, 192, 108, to, SWS_BICUBIC, 1);
            AVFrame* four = mirage_convert(*in, 192, 108, to, SWS_BICUBIC, 4);
            CHECK((differing_rows_all(*one, *four) > 0) == (to.space != AVCOL_SPC_BT709));
            av_frame_free(&four);
            av_frame_free(&one);
        }
        av_frame_free(&in);
    }
    for (AVPixelFormat fmt : vmafcolor::verified_formats())
        CHECK(vmafcolor::verified(fmt));
}

TEST_CASE("sws_getContext(), the analyzer's reference conversion before D-022, makes a yuvj420p reference limited range", "[vmafcolor]")
{
    // Until D-022, VMAFAnalyzer converted every reference with
    // sws_getContext() from the decoder's pixel format to yuv420p at the same
    // size, with bilinear flags; eval/consistency/format-cases-after/ was made
    // so. libswscale takes a yuvj420p input as full range and yuv420p as
    // limited.
    AVFrame* src = make_frame(64, 36, { AVCOL_SPC_BT709, kTv });
    src->data[0][0] = 0;
    src->data[0][1] = 255;
    for (AVPixelFormat fmt : { AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUV420P }) {
        INFO(av_get_pix_fmt_name(fmt));
        AVFrame* out = av_frame_alloc();
        out->format = AV_PIX_FMT_YUV420P;
        out->width = 64;
        out->height = 36;
        REQUIRE(av_frame_get_buffer(out, 0) >= 0);
        SwsContext* sws = nullptr;
        {
            const QuietLog quiet;   // libswscale's "deprecated pixel format used"
            sws = sws_getContext(64, 36, fmt, 64, 36, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                                 nullptr, nullptr, nullptr);
            REQUIRE(sws);
            sws_scale(sws, src->data, src->linesize, 0, 36, out->data, out->linesize);
        }
        if (fmt == AV_PIX_FMT_YUVJ420P) {
            CHECK(out->data[0][0] == 16);
            CHECK(out->data[0][1] == 235);
        } else {
            CHECK(differing_rows(*out, *src, 0) == 0);
        }
        av_frame_free(&out);
        sws_freeContext(sws);
    }
    av_frame_free(&src);
}
