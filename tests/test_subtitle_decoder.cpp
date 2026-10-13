#include <catch2/catch_test_macros.hpp>

#include "SubtitleDecoder.h"
#include "SidecarSubtitles.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// ── palette_to_rgba — PAL8 (BGRA) → RGBA8 conversion ──────────────────────────

TEST_CASE("palette_to_rgba — single palette entry expanded across pixels",
          "[subtitle][palette]")
{
    // Palette[0] = transparent black, Palette[1] = opaque red (BGRA).
    std::array<std::uint8_t, 256 * 4> pal{};
    pal[1 * 4 + 0] = 0;     // B
    pal[1 * 4 + 1] = 0;     // G
    pal[1 * 4 + 2] = 255;   // R
    pal[1 * 4 + 3] = 255;   // A

    constexpr int W = 4, H = 2;
    std::array<std::uint8_t, W * H> indices = {
        1, 1, 1, 1,
        0, 1, 0, 1,
    };
    std::vector<std::uint8_t> out(W * H * 4);

    SubtitleDecoder::palette_to_rgba(indices.data(), W,
                                      pal.data(), W, H, out.data());

    // First row: all opaque red.
    for (int x = 0; x < W; ++x) {
        CHECK(out[x * 4 + 0] == 255);   // R
        CHECK(out[x * 4 + 1] == 0);     // G
        CHECK(out[x * 4 + 2] == 0);     // B
        CHECK(out[x * 4 + 3] == 255);   // A
    }
    // Second row: alternating transparent / red.
    CHECK(out[(W * 1 + 0) * 4 + 3] == 0);
    CHECK(out[(W * 1 + 1) * 4 + 0] == 255);
    CHECK(out[(W * 1 + 1) * 4 + 3] == 255);
    CHECK(out[(W * 1 + 2) * 4 + 3] == 0);
    CHECK(out[(W * 1 + 3) * 4 + 0] == 255);
}

TEST_CASE("palette_to_rgba — non-tight stride is respected",
          "[subtitle][palette]")
{
    std::array<std::uint8_t, 256 * 4> pal{};
    pal[5 * 4 + 0] = 0x40;     // B
    pal[5 * 4 + 1] = 0x80;     // G
    pal[5 * 4 + 2] = 0xC0;     // R
    pal[5 * 4 + 3] = 0xFF;     // A

    constexpr int W = 3, H = 2;
    constexpr int STRIDE = 8;   // 5 bytes of padding per row
    std::array<std::uint8_t, STRIDE * H> indices{};
    // First row: 5, 5, 5 then padding garbage.
    indices[0] = 5; indices[1] = 5; indices[2] = 5;
    indices[3] = indices[4] = indices[5] = indices[6] = indices[7] = 0xAA;
    // Second row: 0, 5, 0 then padding garbage.
    indices[STRIDE + 0] = 0; indices[STRIDE + 1] = 5; indices[STRIDE + 2] = 0;
    indices[STRIDE + 3] = indices[STRIDE + 4] = indices[STRIDE + 5]
        = indices[STRIDE + 6] = indices[STRIDE + 7] = 0xAA;

    std::vector<std::uint8_t> out(W * H * 4);
    SubtitleDecoder::palette_to_rgba(indices.data(), STRIDE,
                                      pal.data(), W, H, out.data());

    // Padding bytes (idx=0xAA) must NOT leak into output. First row all entry 5.
    for (int x = 0; x < W; ++x) {
        CHECK(out[x * 4 + 0] == 0xC0);
        CHECK(out[x * 4 + 1] == 0x80);
        CHECK(out[x * 4 + 2] == 0x40);
        CHECK(out[x * 4 + 3] == 0xFF);
    }
    // Second row middle pixel = entry 5; flanking = entry 0 (transparent).
    CHECK(out[(W + 0) * 4 + 3] == 0x00);
    CHECK(out[(W + 1) * 4 + 0] == 0xC0);
    CHECK(out[(W + 2) * 4 + 3] == 0x00);
}

TEST_CASE("palette_to_rgba — BGRA palette swapped to RGBA correctly",
          "[subtitle][palette]")
{
    // Construct a palette entry whose 4 bytes are clearly distinguishable so
    // we can spot a B/R or A/B swap.
    std::array<std::uint8_t, 256 * 4> pal{};
    pal[7 * 4 + 0] = 0x11;     // B
    pal[7 * 4 + 1] = 0x22;     // G
    pal[7 * 4 + 2] = 0x33;     // R
    pal[7 * 4 + 3] = 0x44;     // A

    std::array<std::uint8_t, 1> indices = { 7 };
    std::vector<std::uint8_t> out(4);

    SubtitleDecoder::palette_to_rgba(indices.data(), 1, pal.data(),
                                      1, 1, out.data());

    CHECK(out[0] == 0x33);   // R
    CHECK(out[1] == 0x22);   // G
    CHECK(out[2] == 0x11);   // B
    CHECK(out[3] == 0x44);   // A
}

// ── Persistent (sidecar) mode ─────────────────────────────────────────────────

namespace {

// SubRip decoder fed hand-built packets in a 1/1000 time base.
struct SrtFeeder {
    SubtitleDecoder dec;
    explicit SrtFeeder(bool persistent)
    {
        AVCodecParameters* par = avcodec_parameters_alloc();
        par->codec_type = AVMEDIA_TYPE_SUBTITLE;
        par->codec_id   = AV_CODEC_ID_SUBRIP;
        dec.set_persistent(persistent);
        REQUIRE(dec.open(par, AVRational{ 1, 1000 }));
        avcodec_parameters_free(&par);
    }
    void cue(int64_t start_ms, int64_t dur_ms, const std::string& text)
    {
        AVPacket* pkt = av_packet_alloc();
        REQUIRE(av_new_packet(pkt, static_cast<int>(text.size())) == 0);
        std::memcpy(pkt->data, text.data(), text.size());
        pkt->pts = pkt->dts = start_ms;
        pkt->duration = dur_ms;
        dec.decode_packet(pkt);
        av_packet_free(&pkt);
    }
};

} // namespace

TEST_CASE("SubtitleDecoder — persistent mode keeps > 200 events and survives flush",
          "[subtitle][persistent]")
{
    SrtFeeder f(true);
    for (int i = 0; i < 300; ++i)
        f.cue(i * 1000, 500, "cue " + std::to_string(i));
    CHECK(f.dec.event_count() == 300);
    f.dec.flush();
    CHECK(f.dec.event_count() == 300);
    CHECK(f.dec.at(0.2) == "cue 0");
    CHECK(f.dec.at(250.25) == "cue 250");
    CHECK(f.dec.at(250.75).empty());     // gap between cues (duration from pkt)

    SrtFeeder capped(false);
    for (int i = 0; i < 300; ++i)
        capped.cue(i * 1000, 500, "cue " + std::to_string(i));
    CHECK(capped.dec.event_count() == 200);
    capped.dec.flush();
    CHECK(capped.dec.event_count() == 0);
}

TEST_CASE("SubtitleDecoder — at() joins overlapping cues in start order",
          "[subtitle][persistent]")
{
    SrtFeeder f(true);
    f.cue(5000, 1000, "late");             // out-of-order arrival
    f.cue(0, 10000, "long");
    f.cue(4000, 3000, "middle");
    CHECK(f.dec.at(1.0) == "long");
    CHECK(f.dec.at(4.5) == "long\nmiddle");
    CHECK(f.dec.at(5.5) == "long\nmiddle\nlate");
    CHECK(f.dec.at(8.0) == "long");
    CHECK(f.dec.at(10.0).empty());
}

TEST_CASE("SidecarSubtitles — decodes data/sub_test/test.srt", "[subtitle][sidecar]")
{
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path()
                    / "data" / "sub_test" / "test.srt";
    SubtitleDecoder dec;
    dec.set_persistent(true);
    REQUIRE(SidecarSubtitles::decode_file(path.string(), dec));
    CHECK(dec.event_count() == 3);
    CHECK(dec.at(0.5).empty());
    CHECK(dec.at(1.0) == "Hello from bitmap subtitle line 1");
    CHECK(dec.at(3.99) == "Hello from bitmap subtitle line 1");
    CHECK(dec.at(4.5).empty());                      // cue 1 ends at 4.000
    CHECK(dec.at(6.0) == "Second cue with italics\nand a line break");
    CHECK(dec.at(10.0) == "Third caption \u2014 bottom of screen");
    CHECK(dec.at(12.5).empty());

    SubtitleDecoder bad;
    bad.set_persistent(true);
    CHECK_FALSE(SidecarSubtitles::decode_file("/nonexistent/x.srt", bad));
}

TEST_CASE("ass_to_text — libavcodec packet events and Dialogue lines", "[subtitle][ass]")
{
    CHECK(SubtitleDecoder::ass_to_text("0,0,Default,,0,0,0,,Hello, world") == "Hello, world");
    CHECK(SubtitleDecoder::ass_to_text("1,0,Default,,0,0,0,,with {\\i1}italics{\\i0}\\Nline two")
          == "with italics\nline two");
    CHECK(SubtitleDecoder::ass_to_text("2,0,Default,,0,0,0,,12, numbers first") == "12, numbers first");
    CHECK(SubtitleDecoder::ass_to_text(
              "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\\hb") == "a b");
    CHECK(SubtitleDecoder::ass_to_text("garbage").empty());
    CHECK(SubtitleDecoder::ass_to_text(nullptr).empty());
}
