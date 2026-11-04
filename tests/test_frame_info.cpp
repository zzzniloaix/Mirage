#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "FrameInfo.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hdr_dynamic_metadata.h>
#include <libavutil/mastering_display_metadata.h>
}

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

using Catch::Matchers::WithinAbs;
using frameinfo::Level;

namespace {

struct FrameDel { void operator()(AVFrame* f) const { av_frame_free(&f); } };
using FramePtr = std::unique_ptr<AVFrame, FrameDel>;

// A 1080p I frame at 2 s (time base 1/1000), tagged BT.709 SDR.
FramePtr make_frame()
{
    FramePtr f{ av_frame_alloc() };
    f->format          = AV_PIX_FMT_YUV420P;
    f->width           = 1920;
    f->height          = 1080;
    f->pts             = 2000;
    f->time_base       = { 1, 1000 };
    f->duration        = 40;
    f->pict_type       = AV_PICTURE_TYPE_I;
    f->flags          |= AV_FRAME_FLAG_KEY;
    f->color_range     = AVCOL_RANGE_MPEG;
    f->colorspace      = AVCOL_SPC_BT709;
    f->color_trc       = AVCOL_TRC_BT709;
    f->color_primaries = AVCOL_PRI_BT709;
    return f;
}

frameinfo::Context sdr_context()
{
    frameinfo::Context c;
    c.start_pts = 1.0;
    c.fps       = 25.0;
    c.cs        = AVCOL_SPC_BT709;
    c.range     = AVCOL_RANGE_MPEG;
    c.trc       = AVCOL_TRC_BT709;
    c.primaries = AVCOL_PRI_BT709;
    return c;
}

const frameinfo::Row* find_row(const frameinfo::Report& r, const std::string& section, const std::string& label)
{
    for (const auto& s : r.sections)
        if (s.title == section)
            for (const auto& row : s.rows)
                if (row.label == label) return &row;
    return nullptr;
}

const frameinfo::Row* find_finding(const frameinfo::Report& r, const std::string& label)
{
    for (const auto& row : r.findings)
        if (row.label == label) return &row;
    return nullptr;
}

void add_sei_text(AVFrame* f, const std::string& text)
{
    AVFrameSideData* sd = av_frame_new_side_data(f, AV_FRAME_DATA_SEI_UNREGISTERED, 16 + text.size() + 1);
    REQUIRE(sd);
    std::memset(sd->data, 0xAB, 16);   // UUID
    std::memcpy(sd->data + 16, text.c_str(), text.size() + 1);
}

// SMPTE ST 2084 inverse EOTF (cd/m² → [0, 1]), to check pq_to_nits against.
double nits_to_pq(double nits)
{
    const double m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
    const double c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
    const double y  = std::pow(nits / 10000.0, m1);
    return std::pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

const std::string kX264 =
    "x264 - core 164 r3108 31e19f9 - H.264/MPEG-4 AVC codec - Copyleft 2003-2023 - "
    "http://www.videolan.org/x264.html - options: cabac=1 ref=3 deblock=1:0:0 analyse=0x3:0x113 "
    "me=hex subme=7 bframes=3 keyint=250 rc=crf crf=23.0";

} // namespace

TEST_CASE("FrameInfo: encoder settings from an x264 user-data SEI", "[frameinfo]")
{
    std::string payload(16, '\x11');
    payload += kX264;
    payload += '\0';
    const auto info = frameinfo::parse_encoder_sei(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                                   payload.size());
    REQUIRE(info);
    CHECK(info->banner == "x264 - core 164 r3108 31e19f9");
    REQUIRE(info->options.size() == 10);
    CHECK(info->options[0] == std::pair<std::string, std::string>{ "cabac", "1" });
    CHECK(info->options[2] == std::pair<std::string, std::string>{ "deblock", "1:0:0" });
    CHECK(info->options[9] == std::pair<std::string, std::string>{ "crf", "23.0" });
}

TEST_CASE("FrameInfo: user-data SEI that is not encoder settings", "[frameinfo]")
{
    std::string text(16, '\x22');
    text += "some other note";
    CHECK_FALSE(frameinfo::parse_encoder_sei(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));

    std::string binary(16, '\x22');
    binary += std::string("\x01\x02 - options: a=1", 17);
    CHECK_FALSE(frameinfo::parse_encoder_sei(reinterpret_cast<const std::uint8_t*>(binary.data()), binary.size()));

    CHECK_FALSE(frameinfo::parse_encoder_sei(nullptr, 0));
}

TEST_CASE("FrameInfo: PQ code values to luminance", "[frameinfo]")
{
    CHECK_THAT(frameinfo::pq_to_nits(0.0), WithinAbs(0.0, 1e-9));
    CHECK_THAT(frameinfo::pq_to_nits(1.0), WithinAbs(10000.0, 1e-6));
    for (double nits : { 0.005, 1.0, 100.0, 203.0, 1000.0, 4000.0 })
        CHECK_THAT(frameinfo::pq_to_nits(nits_to_pq(nits)), WithinAbs(nits, nits * 1e-9 + 1e-9));
}

TEST_CASE("FrameInfo: frame rows, numbered from the stream start", "[frameinfo]")
{
    auto f = make_frame();
    const auto rep = frameinfo::describe(f.get(), sdr_context());

    const auto* type = find_row(rep, "Frame", "Type");
    REQUIRE(type);
    CHECK(type->value == "I · key");
    const auto* num = find_row(rep, "Frame", "Number");
    REQUIRE(num);
    CHECK(num->value == "25");   // (2 s − 1 s) × 25 fps
    const auto* pts = find_row(rep, "Frame", "PTS");
    REQUIRE(pts);
    CHECK(pts->value == "2.000000 s");
    const auto* dur = find_row(rep, "Frame", "Duration");
    REQUIRE(dur);
    CHECK(dur->value == "40.000 ms");
    const auto* px = find_row(rep, "Format", "Pixels");
    REQUIRE(px);
    CHECK(px->value == "yuv420p · 8-bit 4:2:0");
    // Tags agree with the renderer: nothing to report.
    CHECK(rep.findings.empty());
    CHECK(rep.text.find("[Frame]") != std::string::npos);
}

TEST_CASE("FrameInfo: a frame whose colour tags differ from the renderer's", "[frameinfo]")
{
    auto f = make_frame();
    f->color_trc       = AVCOL_TRC_SMPTE2084;
    f->colorspace      = AVCOL_SPC_BT2020_NCL;
    f->color_primaries = AVCOL_PRI_BT2020;
    const auto rep = frameinfo::describe(f.get(), sdr_context());

    const auto* trc = find_finding(rep, "Transfer");
    REQUIRE(trc);
    CHECK(trc->level == Level::Warn);
    CHECK(trc->value == "frame is smpte2084, rendered as bt709");
    const auto* mat = find_finding(rep, "Matrix");
    REQUIRE(mat);
    CHECK(mat->value == "bt2020nc (rendered as bt709)");
    // PQ with no static metadata anywhere.
    const auto* hdr10 = find_finding(rep, "HDR10");
    REQUIRE(hdr10);
    CHECK(hdr10->level == Level::Warn);
    // Warnings come before information.
    for (std::size_t i = 1; i < rep.findings.size(); ++i)
        CHECK(rep.findings[i - 1].level >= rep.findings[i].level);
}

TEST_CASE("FrameInfo: unspecified tags are reported as what the renderer assumes", "[frameinfo]")
{
    auto f = make_frame();
    f->colorspace  = AVCOL_SPC_UNSPECIFIED;
    f->color_range = AVCOL_RANGE_UNSPECIFIED;
    const auto rep = frameinfo::describe(f.get(), sdr_context());
    const auto* mat = find_row(rep, "Format", "Matrix");
    REQUIRE(mat);
    CHECK(mat->level == Level::Dim);
    CHECK(mat->value == "unspecified (rendered as bt709)");
    const auto* rng = find_row(rep, "Format", "Range");
    REQUIRE(rng);
    CHECK(rng->value == "unspecified (rendered as tv)");
    CHECK(rep.findings.empty());
}

TEST_CASE("FrameInfo: HDR10 static metadata and HDR10+ decoded", "[frameinfo]")
{
    auto f = make_frame();
    f->color_trc       = AVCOL_TRC_SMPTE2084;
    f->colorspace      = AVCOL_SPC_BT2020_NCL;
    f->color_primaries = AVCOL_PRI_BT2020;

    AVMasteringDisplayMetadata* m = av_mastering_display_metadata_create_side_data(f.get());
    REQUIRE(m);
    const int d = 50000;
    m->display_primaries[0][0] = { 35400, d }; m->display_primaries[0][1] = { 14600, d };   // R 0.708 0.292
    m->display_primaries[1][0] = {  8500, d }; m->display_primaries[1][1] = { 39850, d };   // G 0.170 0.797
    m->display_primaries[2][0] = {  6550, d }; m->display_primaries[2][1] = {  2300, d };   // B 0.131 0.046
    m->white_point[0] = { 15635, d }; m->white_point[1] = { 16450, d };                     // D65
    m->min_luminance  = { 50, 10000 };
    m->max_luminance  = { 1000, 1 };
    m->has_primaries  = 1;
    m->has_luminance  = 1;

    AVContentLightMetadata* cll = av_content_light_metadata_create_side_data(f.get());
    REQUIRE(cll);
    cll->MaxCLL  = 1000;
    cll->MaxFALL = 400;

    AVDynamicHDRPlus* hp = av_dynamic_hdr_plus_create_side_data(f.get());
    REQUIRE(hp);
    hp->application_version = 1;
    hp->num_windows = 1;
    hp->targeted_system_display_maximum_luminance = { 400, 1 };
    hp->params[0].maxscl[0] = { 10000, 100000 };   // 0.1 of 10,000 cd/m²
    hp->params[0].maxscl[1] = {  5000, 100000 };
    hp->params[0].maxscl[2] = {   100, 100000 };
    hp->params[0].average_maxrgb = { 120, 100000 };

    frameinfo::Context ctx = sdr_context();
    ctx.cs = AVCOL_SPC_BT2020_NCL;
    ctx.trc = AVCOL_TRC_SMPTE2084;
    ctx.primaries = AVCOL_PRI_BT2020;
    const auto rep = frameinfo::describe(f.get(), ctx);

    const auto* prim = find_row(rep, "HDR", "Mastering");
    REQUIRE(prim);
    CHECK(prim->value == "bt2020");
    const auto* lum = find_row(rep, "HDR", "Mastering lum");
    REQUIRE(lum);
    CHECK(lum->value == "0.0050 – 1000 cd/m²");
    const auto* ll = find_row(rep, "HDR", "Light level");
    REQUIRE(ll);
    CHECK(ll->value == "MaxCLL 1000 · MaxFALL 400 cd/m²");
    const auto* plus = find_row(rep, "HDR", "HDR10+");
    REQUIRE(plus);
    CHECK(plus->value == "app v1, 1 window, target 400 cd/m²");
    const auto* scl = find_row(rep, "HDR", "Scene maxRGB");
    REQUIRE(scl);
    CHECK(scl->value == "R 1000 · G 500 · B 10.0 cd/m²");
    CHECK(rep.findings.empty());   // fully signalled, tags agree
}

TEST_CASE("FrameInfo: side data listed with captions and encoder settings decoded", "[frameinfo]")
{
    auto f = make_frame();
    REQUIRE(av_frame_new_side_data(f.get(), AV_FRAME_DATA_A53_CC, 30));
    add_sei_text(f.get(), kX264);
    const auto rep = frameinfo::describe(f.get(), sdr_context());

    const auto* cc = find_row(rep, "Side data", "Captions (A53)");
    REQUIRE(cc);
    CHECK(cc->value == "10 cc_data triplets (CEA-608/708)");
    const auto* sei = find_row(rep, "Side data", "User data SEI");
    REQUIRE(sei);
    CHECK(sei->value == "x264 - core 164 r3108 31e19f9");
    const auto* crf = find_row(rep, "Encoder settings", "crf");
    REQUIRE(crf);
    CHECK(crf->value == "23.0");
}

TEST_CASE("FrameInfo: decode errors and interlacing become findings", "[frameinfo]")
{
    auto f = make_frame();
    f->flags |= AV_FRAME_FLAG_CORRUPT | AV_FRAME_FLAG_INTERLACED | AV_FRAME_FLAG_TOP_FIELD_FIRST;
    f->decode_error_flags = FF_DECODE_ERROR_MISSING_REFERENCE;
    const auto rep = frameinfo::describe(f.get(), sdr_context());
    REQUIRE_FALSE(rep.findings.empty());
    CHECK(rep.findings.front().level == Level::Error);
    CHECK(rep.findings.front().value == "corrupt: missing reference");
    const auto* scan = find_row(rep, "Frame", "Scan");
    REQUIRE(scan);
    CHECK(scan->value == "interlaced, top field first");
}

TEST_CASE("FrameInfo: encoder settings sent once are remembered for later frames", "[frameinfo]")
{
    frameinfo::Memory mem;
    auto first = make_frame();
    first->pts = 0;
    add_sei_text(first.get(), kX264);
    frameinfo::remember(first.get(), mem);
    REQUIRE(mem.encoder);
    CHECK(mem.encoder_pts == 0.0);

    auto later = make_frame();   // 2 s, no SEI of its own
    frameinfo::remember(later.get(), mem);
    REQUIRE(mem.encoder);        // a frame without settings keeps what was seen

    const auto with = frameinfo::describe(later.get(), sdr_context(), &mem);
    const auto* seen = find_row(with, "Encoder settings", "Seen at");
    REQUIRE(seen);
    CHECK(seen->value == "0.000 s (sent once, not on this frame)");
    const auto* ref = find_row(with, "Encoder settings", "ref");
    REQUIRE(ref);
    CHECK(ref->value == "3");

    const auto without = frameinfo::describe(later.get(), sdr_context());
    CHECK_FALSE(find_row(without, "Encoder settings", "Encoder"));
}
