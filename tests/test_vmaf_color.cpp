// vmafcolor: the tags FFmpeg's section 5 filter graph converts the distorted
// frames to, and Mirage's conversion of the frames against the same graph run
// through libavfilter (scale, format and libvmaf filters, as
// eval/consistency/run_delta.py runs them through ffmpeg).

#include <catch2/catch_test_macros.hpp>

#include "VMAFColor.h"

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

// The distorted frame as the libvmaf filter of FFmpeg's section 5 graph
// receives it: [dis]scale=W:H:flags=<scaler>,format=yuv420p[d];
// [ref]format=yuv420p[r];[d][r]libvmaf. The libvmaf filter passes its
// distorted input on, so the sink gets that frame. nullptr if FFmpeg cannot
// build the graph.
AVFrame* ffmpeg_distorted(const AVFrame& dis, const AVFrame& ref, const char* scaler = "bilinear")
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
        "[d][r]libvmaf=model=version=vmaf_v0.6.1:n_threads=1[out]", ref.width, ref.height, scaler);
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
    for (const auto& [ref_tags, dis_tags] : { std::pair{ bt709, ycgco }, std::pair{ ycgco, bt709 } }) {
        INFO(name(ref_tags) << " vs " << name(dis_tags));
        AVFrame* dis = make_frame(64, 36, dis_tags);
        AVFrame* ref = make_frame(192, 108, ref_tags, 7);
        CHECK(ffmpeg_distorted(*dis, *ref) == nullptr);
        CHECK_FALSE((vmafcolor::supported(ref_tags.space) && vmafcolor::supported(dis_tags.space)));
        av_frame_free(&ref);
        av_frame_free(&dis);
    }
}
