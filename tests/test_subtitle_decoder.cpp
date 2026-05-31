#include <catch2/catch_test_macros.hpp>

#include "SubtitleDecoder.h"

#include <array>
#include <cstdint>
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
