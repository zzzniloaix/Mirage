#pragma once

// Per-frame metadata for the frame inspector (P): what `ffprobe -show_frames`
// prints for the frame on screen, with the side data decoded (HDR10 mastering
// display and light level, HDR10+, Dolby Vision, captions, timecodes, the
// encoder-settings SEI x264/x265 write, ...), and findings: the frame's colour
// tags against what the renderer converts with, missing HDR signalling,
// decode errors. Pure functions — no ImGui, no GL; the panel draws a Report.

extern "C" {
#include <libavcodec/codec_par.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace frameinfo {

enum class Level : std::uint8_t { Normal, Dim, Info, Warn, Error };

struct Row {
    std::string label;
    std::string value;
    Level       level = Level::Normal;
    std::string tip;              // hover text (empty = none)
};

struct Section {
    std::string      title;
    std::vector<Row> rows;
};

// What the player does with the frame, for the comparisons.
struct Context {
    double start_pts = 0.0;   // stream start (s): frame numbers count from here
    double fps       = 0.0;   // nominal rate for frame numbers; 0 = none shown
    // The interpretation the renderer converts with (VideoRenderer::interpretation()).
    AVColorSpace                  cs        = AVCOL_SPC_UNSPECIFIED;
    AVColorRange                  range     = AVCOL_RANGE_UNSPECIFIED;
    AVColorTransferCharacteristic trc       = AVCOL_TRC_UNSPECIFIED;
    AVColorPrimaries              primaries = AVCOL_PRI_UNSPECIFIED;
    // The stream: its coded side data (Dolby Vision configuration, static
    // HDR metadata in the container). May be null.
    const AVCodecParameters* par = nullptr;
};

struct Report {
    std::vector<Row>     findings;   // most severe first
    std::vector<Section> sections;
    std::string          text;       // plain-text dump (clipboard)
};

// Encoder settings in an unregistered user-data SEI: x264 and x265 write
// "<banner> - options: key=value key=value ...". `data` is the side data
// (16-byte UUID, then the payload). nullopt when it isn't such a string.
struct EncoderInfo {
    std::string                                      banner;   // "x264 - core 164 r3108 31e19f9"
    std::vector<std::pair<std::string, std::string>> options;  // in order; "key" alone has ""
};
[[nodiscard]] std::optional<EncoderInfo> parse_encoder_sei(const std::uint8_t* data, std::size_t size);

// What some streams send on a few frames only — x264 and x265 write their
// settings once, on the first IDR frame: the latest seen, and when.
struct Memory {
    std::optional<EncoderInfo> encoder;
    double                     encoder_pts = 0.0;   // seconds
};
// Remember what `f` carries. Cheap (a look at its side data list): call it
// for every frame shown.
void remember(const AVFrame* f, Memory& m);

// `mem` (optional) fills in what this frame lacks, labelled as seen earlier.
[[nodiscard]] Report describe(const AVFrame* f, const Context& ctx, const Memory* mem = nullptr);

// Display luminance (cd/m²) of a PQ code value normalised to [0, 1]
// (SMPTE ST 2084 EOTF).
[[nodiscard]] double pq_to_nits(double e);

} // namespace frameinfo
