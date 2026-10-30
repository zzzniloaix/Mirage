#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PixelView.h"
#include "Scopes.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}

#include <chrono>
#include <cstring>
#include <memory>
#include <thread>

using Catch::Matchers::WithinAbs;

namespace {

struct FrameDel { void operator()(AVFrame* f) const { av_frame_free(&f); } };
using FramePtr = std::unique_ptr<AVFrame, FrameDel>;

FramePtr make_frame(AVPixelFormat fmt, int w, int h)
{
    FramePtr f{ av_frame_alloc() };
    f->format = fmt;
    f->width  = w;
    f->height = h;
    REQUIRE(av_frame_get_buffer(f.get(), 0) >= 0);
    return f;
}

// Write component c at (x, y) of its own grid, as the format lays it out.
void put(AVFrame* f, int c, int x, int y, int v)
{
    const AVPixFmtDescriptor*    d  = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(f->format));
    const AVComponentDescriptor& cd = d->comp[c];
    std::uint8_t* p = f->data[cd.plane] + y * f->linesize[cd.plane] + x * cd.step + cd.offset;
    const unsigned val = static_cast<unsigned>(v) << cd.shift;
    if (cd.depth + cd.shift <= 8) {
        p[0] = static_cast<std::uint8_t>(val);
    } else if (d->flags & AV_PIX_FMT_FLAG_BE) {
        p[0] = static_cast<std::uint8_t>(val >> 8);
        p[1] = static_cast<std::uint8_t>(val);
    } else {
        p[0] = static_cast<std::uint8_t>(val);
        p[1] = static_cast<std::uint8_t>(val >> 8);
    }
}

// Every Y′ sample yv(x, y), every chroma sample (cb, cr).
template <class F>
void fill(AVFrame* f, F&& yv, int cb, int cr)
{
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(f->format));
    for (int y = 0; y < f->height; ++y)
        for (int x = 0; x < f->width; ++x) put(f, 0, x, y, yv(x, y));
    const int cw = (f->width + (1 << d->log2_chroma_w) - 1) >> d->log2_chroma_w;
    const int ch = (f->height + (1 << d->log2_chroma_h) - 1) >> d->log2_chroma_h;
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) {
            put(f, 1, x, y, cb);
            put(f, 2, x, y, cr);
        }
}

std::uint32_t pixel(const scopes::Image& img, int x, int y)
{
    return img.rgba[static_cast<std::size_t>(y) * img.w + x];
}
constexpr std::uint32_t kBlack = 0xFF000000u;

} // namespace

TEST_CASE("pixel_values: limited-range codes to R'G'B' and legality", "[scopes]")
{
    const auto white = pixel_values(235, 128, 128, 8, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, AVCOL_TRC_BT709);
    CHECK_THAT(white.y, WithinAbs(1.0, 1e-12));
    CHECK_THAT(white.r, WithinAbs(1.0, 1e-12));
    CHECK_THAT(white.g, WithinAbs(1.0, 1e-12));
    CHECK_THAT(white.b, WithinAbs(1.0, 1e-12));
    CHECK_FALSE(white.above_white);

    const auto black = pixel_values(16, 128, 128, 8, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, AVCOL_TRC_BT709);
    CHECK_THAT(black.y, WithinAbs(0.0, 1e-12));
    CHECK_FALSE(black.below_black);
    CHECK(pixel_values(10, 128, 128, 8, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, AVCOL_TRC_BT709).below_black);
    CHECK(pixel_values(240, 128, 128, 8, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, AVCOL_TRC_BT709).above_white);
    CHECK(pixel_values(128, 250, 128, 8, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, AVCOL_TRC_BT709).chroma_out);

    // 10-bit reference white is exactly 1.0 (not 255/219-scaled 8-bit constants).
    const auto w10 = pixel_values(940, 512, 512, 10, AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_MPEG, AVCOL_TRC_BT709);
    CHECK_THAT(w10.y, WithinAbs(1.0, 1e-12));
    CHECK_THAT(w10.r, WithinAbs(1.0, 1e-12));

    // BT.709 red, quantised to 8-bit codes (63, 102, 240).
    const auto red = pixel_values(63, 102, 240, 8, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, AVCOL_TRC_BT709);
    CHECK_THAT(red.r, WithinAbs(1.0, 0.01));
    CHECK_THAT(red.g, WithinAbs(0.0, 0.01));
    CHECK_THAT(red.b, WithinAbs(0.0, 0.01));

    const auto full = pixel_values(255, 128, 128, 8, AVCOL_SPC_BT709, AVCOL_RANGE_JPEG, AVCOL_TRC_BT709);
    CHECK_THAT(full.y, WithinAbs(1.0, 1e-12));
    CHECK_FALSE(full.above_white);   // no legality on full range
}

TEST_CASE("pixel_values: PQ and HLG display light", "[scopes]")
{
    // 10-bit PQ grey at the code nearest 100 cd/m² (signal 0.5081).
    const auto pq = pixel_values(509, 512, 512, 10, AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_MPEG, AVCOL_TRC_SMPTE2084);
    REQUIRE(pq.has_nits);
    CHECK_THAT(pq.nits_y, WithinAbs(100.0, 1.0));
    CHECK_THAT(pq.nits_r, WithinAbs(pq.nits_y, 1e-9));

    // HLG 75 % grey (code 721 = 64 + 0.75 · 876) on a 1,000 cd/m² display: about 203 cd/m² (BT.2408).
    const auto hlg = pixel_values(721, 512, 512, 10, AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_MPEG, AVCOL_TRC_ARIB_STD_B67);
    REQUIRE(hlg.has_nits);
    CHECK_THAT(hlg.nits_y, WithinAbs(203.0, 1.0));

    CHECK_FALSE(pixel_values(509, 512, 512, 10, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, AVCOL_TRC_BT709).has_nits);
}

TEST_CASE("PixelView: P010 read at its 10-bit codes", "[scopes]")
{
    auto f = make_frame(AV_PIX_FMT_P010LE, 4, 4);
    // Written by hand: P010 keeps each 10-bit value in the top bits of 16.
    auto px16 = [](std::uint8_t* row, int i, int v) {
        const unsigned s = static_cast<unsigned>(v) << 6;
        row[2 * i]     = static_cast<std::uint8_t>(s & 0xFF);
        row[2 * i + 1] = static_cast<std::uint8_t>(s >> 8);
    };
    px16(f->data[0] + 1 * f->linesize[0], 1, 940);   // Y′ (1, 1)
    px16(f->data[1], 0, 512);                         // Cb of chroma (0, 0)
    px16(f->data[1], 1, 600);                         // Cr of chroma (0, 0)

    PixelView v;
    REQUIRE(v.map(f.get()));
    f.reset();                                        // the view holds its own reference
    CHECK(v.depth() == 10);
    const auto c = v.at(1, 1);
    CHECK(c.y == 940);
    CHECK(c.cb == 512);
    CHECK(c.cr == 600);
}

TEST_CASE("PixelView: chroma of a pixel from the sample covering it", "[scopes]")
{
    for (AVPixelFormat fmt : { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12, AV_PIX_FMT_YUV422P10LE, AV_PIX_FMT_YUV444P }) {
        auto f = make_frame(fmt, 8, 8);
        const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(fmt);
        fill(f.get(), [](int x, int y) { return 16 + x + 8 * y; }, 128, 128);
        put(f.get(), 1, 5 >> d->log2_chroma_w, 3 >> d->log2_chroma_h, 77);
        put(f.get(), 2, 5 >> d->log2_chroma_w, 3 >> d->log2_chroma_h, 199);
        PixelView v;
        REQUIRE(v.map(f.get()));
        const auto c = v.at(5, 3);
        CHECK(c.y == 16 + 5 + 24);
        CHECK(c.cb == 77);
        CHECK(c.cr == 199);
    }
}

TEST_CASE("PixelView: formats without Y'CbCr samples are refused", "[scopes]")
{
    auto rgb = make_frame(AV_PIX_FMT_RGB24, 4, 4);
    PixelView v;
    CHECK_FALSE(v.map(rgb.get()));
    CHECK_FALSE(v.valid());
    auto grey = make_frame(AV_PIX_FMT_GRAY8, 4, 4);
    CHECK(v.map(grey.get()));
}

TEST_CASE("scopes::compute: a flat grey frame", "[scopes]")
{
    auto f = make_frame(AV_PIX_FMT_YUV420P, 64, 32);
    fill(f.get(), [](int, int) { return 126; }, 128, 128);
    PixelView v;
    REQUIRE(v.map(f.get()));

    const auto r = scopes::compute(v, { scopes::Mode::Waveform, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG });
    REQUIRE(r.ok);
    CHECK(r.stats.y_min == 126);
    CHECK(r.stats.y_max == 126);
    CHECK_THAT(r.stats.y_avg, WithinAbs(126.0, 1e-9));
    CHECK(r.stats.codes_used == 1);
    CHECK(r.stats.codes_total == 256);
    CHECK(r.stats.below_black == 0.0);
    CHECK(r.stats.above_white == 0.0);
    REQUIRE(r.image.w == 64);   // one column per pixel of a frame narrower than the scope
    REQUIRE(r.image.h == scopes::kRows);
    const int row = scopes::kRows - 1 - 126;
    for (int x : { 0, r.image.w / 2, r.image.w - 1 }) {
        CHECK(pixel(r.image, x, row) != kBlack);
        CHECK(pixel(r.image, x, row - 1) == kBlack);
        CHECK(pixel(r.image, x, 0) == kBlack);
    }

    const auto h = scopes::compute(v, { scopes::Mode::Histogram, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG });
    REQUIRE(h.histogram.size() == 256);
    CHECK(h.histogram[126] == 1.0f);
    CHECK(h.histogram[125] == 0.0f);
}

TEST_CASE("scopes::compute: legal-range counts", "[scopes]")
{
    auto f = make_frame(AV_PIX_FMT_YUV420P, 16, 16);
    fill(f.get(), [](int, int y) { return y < 8 ? 10 : 240; }, 250, 128);   // half sub-black, half super-white
    PixelView v;
    REQUIRE(v.map(f.get()));
    const auto limited = scopes::compute(v, { scopes::Mode::Waveform, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG });
    CHECK_THAT(limited.stats.below_black, WithinAbs(0.5, 1e-12));
    CHECK_THAT(limited.stats.above_white, WithinAbs(0.5, 1e-12));
    CHECK_THAT(limited.stats.chroma_out, WithinAbs(1.0, 1e-12));
    const auto full = scopes::compute(v, { scopes::Mode::Waveform, AVCOL_SPC_BT709, AVCOL_RANGE_JPEG });
    CHECK_FALSE(full.stats.limited);
    CHECK(full.stats.below_black == 0.0);
}

TEST_CASE("scopes::compute: a 10-bit frame whose low bits are never used", "[scopes]")
{
    auto f = make_frame(AV_PIX_FMT_YUV420P10LE, 16, 16);
    fill(f.get(), [](int x, int y) { return 4 * (x + 16 * y); }, 512, 512);   // 0, 4, ..., 1020
    PixelView v;
    REQUIRE(v.map(f.get()));
    auto r = scopes::compute(v, { scopes::Mode::Histogram, AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_MPEG });
    CHECK(r.stats.depth == 10);
    CHECK(r.stats.lsb_unused);
    CHECK(r.stats.codes_used == 256);
    CHECK(r.stats.codes_total == 1024);

    put(f.get(), 0, 3, 3, 65);   // one odd code: the low bits carry something
    REQUIRE(v.map(f.get()));
    r = scopes::compute(v, { scopes::Mode::Histogram, AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_MPEG });
    CHECK_FALSE(r.stats.lsb_unused);
}

TEST_CASE("scopes::compute: vectorscope and parade of known colours", "[scopes]")
{
    auto f = make_frame(AV_PIX_FMT_YUV420P, 32, 32);
    fill(f.get(), [](int, int) { return 63; }, 102, 240);   // BT.709 red
    PixelView v;
    REQUIRE(v.map(f.get()));

    const auto vs = scopes::compute(v, { scopes::Mode::Vectorscope, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG });
    REQUIRE(vs.image.w == scopes::kVectorSize);
    CHECK(pixel(vs.image, 102, scopes::kVectorSize - 1 - 240) != kBlack);
    CHECK(pixel(vs.image, 128, 128) == kBlack);

    const auto pr = scopes::compute(v, { scopes::Mode::Parade, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG });
    REQUIRE(pr.image.w == 3 * 32);
    // R′ at white (code 235), G′ and B′ at black (16): within a code of quantisation.
    auto lit_row = [&](int k) {
        for (int y = 0; y < scopes::kRows; ++y)
            if (pixel(pr.image, k * (pr.image.w / 3) + 5, y) != kBlack) return y;
        return -1;
    };
    CHECK(std::abs(lit_row(0) - (scopes::kRows - 1 - 235)) <= 1);
    CHECK(std::abs(lit_row(1) - (scopes::kRows - 1 - 16)) <= 1);
    CHECK(std::abs(lit_row(2) - (scopes::kRows - 1 - 16)) <= 1);
}

TEST_CASE("scopes::compute: grey frames have no chroma scopes", "[scopes]")
{
    auto f = make_frame(AV_PIX_FMT_GRAY8, 8, 8);
    std::memset(f->data[0], 100, static_cast<std::size_t>(f->linesize[0]) * 8);
    PixelView v;
    REQUIRE(v.map(f.get()));
    CHECK(scopes::compute(v, { scopes::Mode::Waveform, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG }).ok);
    const auto vs = scopes::compute(v, { scopes::Mode::Vectorscope, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG });
    CHECK_FALSE(vs.ok);
    CHECK_FALSE(vs.error.empty());
}

TEST_CASE("scopes::Worker: the newest submitted frame is measured", "[scopes]")
{
    auto a = make_frame(AV_PIX_FMT_YUV420P, 64, 64);
    fill(a.get(), [](int, int) { return 50; }, 128, 128);
    auto b = make_frame(AV_PIX_FMT_YUV420P, 64, 64);
    fill(b.get(), [](int, int) { return 200; }, 128, 128);

    scopes::Worker w;
    w.submit(a.get(), { scopes::Mode::Waveform, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG }, 1, 1.0);
    w.submit(b.get(), { scopes::Mode::Waveform, AVCOL_SPC_BT709, AVCOL_RANGE_MPEG }, 2, 2.0);
    a.reset();
    b.reset();   // the worker keeps its own references

    scopes::Result r;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool got2 = false;
    while (!got2 && std::chrono::steady_clock::now() < deadline) {
        if (w.take(r)) {
            REQUIRE(r.ok);
            if (r.serial == 2) got2 = true;
            else CHECK(r.serial == 1);   // the first may have started before the second came
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    REQUIRE(got2);
    CHECK(r.stats.y_min == 200);
    CHECK(r.pts == 2.0);
}

TEST_CASE("scopes::compute: codes used are counted exactly above 12 bits", "[scopes]")
{
    auto f = make_frame(AV_PIX_FMT_YUV420P16LE, 64, 64);
    fill(f.get(), [](int x, int y) { return 1000 + x + 64 * y; }, 32768, 32768);   // 4096 distinct codes, 1 apart
    PixelView v;
    REQUIRE(v.map(f.get()));
    const auto r = scopes::compute(v, { scopes::Mode::Histogram, AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_MPEG });
    REQUIRE(r.ok);
    CHECK(r.stats.depth == 16);
    CHECK(r.stats.codes_total == 65536);
    CHECK(r.stats.codes_used == 4096);   // the histogram's 4096 bins would merge them 16 to a bin
    CHECK_FALSE(r.stats.lsb_unused);
}
