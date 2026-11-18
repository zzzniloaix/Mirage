#pragma once

// The synthetic ladders of §3.2's two attribution tests (DECISIONS.md D-069,
// D-080), made within the tests with FFmpeg's libraries, never its programs:
// testgen's prototype (eval/phase3-survey/testgen/src/testgen.cpp, described
// in eval/phase3-survey/testgen.md) ported, with only the choices D-080
// settles (eval/phase3-survey/design.md, WP10).
//
//   degraded  eight segments of one content; one segment of one lower rung,
//             v270's segment 4, encoded at a much lower quality than the rest;
//   ad        main content in segments 0 to 2 and 5 to 7, an "ad" of other
//             content in segments 3 and 4 between EXT-X-DISCONTINUITY tags,
//             EXT-X-CUE-OUT before its first segment, EXT-X-CUE-OUT-CONT
//             before its second and EXT-X-CUE-IN before the first segment
//             after it (testgen.md §2.3); one lower rung's ad, v180's, at the
//             low quality. The main content's timeline goes on through the
//             break, as where an ad replaces content of the same length:
//             segment 5 holds the content's frames from 5 s.
//
// Both: three rungs, v360 (640x360), v270 (480x270) and v180 (320x180), each
// of eight MPEG-TS segments of 25 frames at 25 fps (1 s each, 200 frames a
// rung). Every
// rung is the same source, made at the top rung's size and scaled down with
// libswscale, as a ladder's rungs are encoded from one mezzanine; the source
// is computed with integer arithmetic only, so that its pixels don't depend
// on a machine's floating point. Each segment of each rung has an encoder of
// its own, so it starts on a key frame and the quantizer changes exactly at
// its first frame. One MPEG-TS muxer writes a rung's consecutive segments
// within a playlist discontinuity sequence, as FFmpeg's hls muxer does, so
// that continuity counters and timestamps go on from one segment to the next
// (RFC 8216 §3); each sequence after the first gets a muxer of its own whose
// first packets carry the discontinuity indicator. The media playlists and
// the master are written as text; BANDWIDTH and AVERAGE-BANDWIDTH
// (RFC 8216 §4.3.4.2) are the peak and the average segment bit rate measured
// from the segments' sizes. layout.json, written beside them, is the ground
// truth (P-Q): what each segment is, its first frame, timestamps, tags with
// their lines, and each rung's quantizer and bytes (its fields are listed at
// layout_json()).
//
// D-080's choices, and nothing else:
// - The encoder (Q33 (c)): libx264 when this FFmpeg build has it, else
//   FFmpeg's MPEG-2 encoder; the layout records which (Layout::encoder,
//   layout.json's "codec"). libx264: preset veryfast, constant QP 22, the low
//   quality at QP 46, keyint 25, scenecut 0, one thread; MPEG-2: qscale 4,
//   the low quality at 31, a closed GOP of 25 with 2 B-frames, the quantizer
//   on every frame (testgen.md §1, §2.4).
// - The ad's timestamps (Q34 (d)): from the main content's origin, 10 s, so
//   that PTS repeat across the splice (acceptance test 2 proper); from 1 s, a
//   backward jump of more than 10 s; or continuing the main content's, from
//   13 s. The main content's PTS are the same in all three.
// - The rest (Q35 (a)): the fractal-noise source, the calm ad, continuity
//   counters marked at each splice, no binary SCTE-35 form in the playlists,
//   this file compiled at -O2 in every build type (CMakeLists.txt), and
//   ladders for top-rung mode (D-002): the top rung's measured BANDWIDTH is
//   the highest, which write_ladder() checks.
//
// Each ladder is made once per process, which under ctest is once per
// Catch2 test case (P-Q; eval/phase3-survey/design.md §9.5), and with
// MIRAGE_KEEP_MEDIA=<dir> it is kept, each test case under <dir>/<its
// name>/ (P-U; media_dir()). write_ladder() returns its errors as strings,
// as src/ does.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace synthladder {

inline constexpr int kFps           = 25;
inline constexpr int kSegmentFrames = 25;    // 1 s segments
inline constexpr int kSegments      = 8;     // 8 s, in both ladders
inline constexpr int kFrames        = kSegments * kSegmentFrames;   // 200 per rung
inline constexpr int kTicksPerFrame = 90000 / kFps;   // MPEG-TS's 90 kHz clock

// The main content's first PTS: 10 s, in 90 kHz ticks (testgen.md §2.3).
inline constexpr std::int64_t kMainOrigin = 10 * 90000;

struct Rung {
    const char* name;
    int         width, height;
};
// The rungs, from the top: the master lists them in this order, and every
// per-rung array below is in it.
inline constexpr std::array<Rung, 3> kRungs = { {
    { "v360", 640, 360 }, { "v270", 480, 270 }, { "v180", 320, 180 },
} };

enum class Kind : std::uint8_t { Degraded, Ad };

// Q34's three arrangements of the ad's timestamps (D-080).
enum class AdTimestamps : std::uint8_t {
    MainOrigin,      // from 10 s, the main content's origin: PTS repeat across the splice
    FromOneSecond,   // from 1 s: back by 11.96 s at the cue-out, on by 12.04 s at the cue-in
    Continuous,      // from 13 s: no jump at either splice
};

enum class Encoder : std::uint8_t { X264, Mpeg2 };

const char* kind_name(Kind k);                    // "degraded", "ad"
const char* ad_timestamps_name(AdTimestamps a);   // "main-origin", "from-1s", "continuous"
const char* encoder_name(Encoder e);              // FFmpeg's names: "libx264", "mpeg2video"

// D-080 (Q33 (c)): libx264 when this FFmpeg build has it, else MPEG-2.
Encoder default_encoder();

// The quantizers of an encoder (testgen.md §2.3): x264's constant QP, 22 and
// 46; MPEG-2's qscale, 4 and 31.
int q_normal(Encoder e);
int q_low(Encoder e);

// Which ladder.
struct Spec {
    Kind         kind = Kind::Degraded;
    AdTimestamps ad   = AdTimestamps::MainOrigin;   // Kind::Ad only
    // Unset: default_encoder(), as D-080 has it. Set only by a test of the
    // generator itself, to make a ladder with the other encoder.
    std::optional<Encoder> encoder;
};

// The encoder `spec` is made with.
Encoder encoder_of(const Spec& spec);

// The ladder's name, its directory's under media_dir(): "degraded",
// "ad-main-origin", "ad-from-1s", "ad-continuous", with "-mpeg2video" or
// "-libx264" appended when the spec names an encoder other than the default.
std::string name(const Spec& spec);

// A tag before a segment's EXTINF, as written, with its line in the media
// playlist, counted from 1; every rung's media playlist has the same text.
struct Tag {
    std::string text;   // as written: "#EXT-X-CUE-OUT:2.000"
    int         line = 0;

    bool operator==(const Tag&) const = default;
};

// One segment, the same in every rung but for its quantizer and bytes.
struct SegmentLayout {
    int          index = 0;          // its media sequence number (EXT-X-MEDIA-SEQUENCE:0)
    std::string  file;               // in each rung's directory: seg_004.ts, main_000.ts, ad_000.ts
    std::string  period;             // "main" or "ad"
    int          first_frame = 0;    // the index of its first frame in each rung's 200, the reference's too
    int          frames = kSegmentFrames;
    std::int64_t pts_first = 0;      // its first frame's PTS, 90 kHz ticks; frame k's adds k kTicksPerFrame
    int          source_first = 0;   // the frame of its period's source it starts with
    int          sequence = 0;       // the playlist's discontinuity sequence number it belongs to
    bool         discontinuity = false;    // an EXT-X-DISCONTINUITY before it
    int          discontinuity_line = 0;   // that tag's line; 0: none
    std::vector<Tag> tags;           // the other tags before it: the ad's cue tags
    int          extinf_line = 0;    // the line of its EXTINF
    int          uri_line = 0;       // the line of its URI
    std::array<int, 3> q{};          // each rung's quantizer
    std::array<std::uint64_t, 3> bytes{};   // each rung's file size; 0 until written

    // Its playlist time, the sum of the EXTINF before it (1 s each), in seconds.
    int position_s() const { return index * kSegmentFrames / kFps; }
};

struct RungLayout {
    std::string   name;               // v360, v270, v180
    int           width = 0, height = 0;
    std::string   uri;                // as the master gives it: "v360/index.m3u8"
    std::int64_t  bandwidth = 0;      // BANDWIDTH: the peak segment bit rate; 0 until written
    std::int64_t  average_bandwidth = 0;   // AVERAGE-BANDWIDTH: the average one
    std::string   codecs;             // CODECS, from the first SPS: "avc1.64001e"; empty for MPEG-2
    std::uint64_t bytes = 0;          // its segments' bytes
};

// What a ladder is: the ground truth that the tests check against.
struct Layout {
    Kind         kind = Kind::Degraded;
    AdTimestamps ad = AdTimestamps::MainOrigin;   // Kind::Ad only
    Encoder      encoder = Encoder::X264;
    int          q_normal = 0, q_low = 0;
    int          low_rung = 0;        // the rung with the low-quality segments: v270, or the ad's v180
    int          control_rung = 0;    // the other lower rung, without one: v180, or the ad's v270
    std::vector<int> low_segments;    // their indices: {4}, or the ad's {3, 4}
    // The ad's (Kind::Ad only):
    int          cue_out_segment = -1, cue_in_segment = -1;   // 3 and 5
    int          break_s = 0;         // the duration the cue-out declares: 2
    std::int64_t ad_origin = 0;       // the ad's first PTS, 90 kHz ticks
    std::string  x264_sei;            // libx264's settings, from the top rung's first SEI; empty for MPEG-2
    std::vector<RungLayout>    rungs;      // in kRungs' order
    std::vector<SegmentLayout> segments;   // in play order

    // The frames of the low-quality segments, [first, end): [100, 125) in the
    // degraded ladder, the ad's [75, 125).
    std::pair<int, int> low_frames() const;
    // The playlist discontinuity sequences (0, then 1 and 2 in the ad's).
    int sequences() const { return segments.empty() ? 0 : segments.back().sequence + 1; }
};

// The layout of `spec` before anything is encoded: everything but the bytes,
// the BANDWIDTHs, the CODECS and the x264 SEI, which write_ladder() measures.
Layout plan(const Spec& spec);

// A rung's media playlist, the same text for every rung, and the master
// playlist (the BANDWIDTHs and CODECS of `layout` as they are), as
// write_ladder() writes them.
std::string media_playlist(const Layout& layout);
std::string master_playlist(const Layout& layout);

// layout.json's text: `layout`, and the FFmpeg libraries' run-time versions.
// It is the ground truth (P-Q) of WP11a's tests and, as a file, of WP14's
// check.py (design §9.4) and of the design's tools
// (eval/phase3-survey/design/tools/*.py, written against testgen's layouts),
// so it keeps testgen's format (the layout.json of testgen.cpp's
// write_ladder(), testgen.md §2.3) wherever testgen has the field: its
// names, its order and its meanings. Its fields, a + before each that
// testgen doesn't have:
//
//   kind               "degraded" or "ad"
//   codec              the encoder, FFmpeg's name: "libx264" or "mpeg2video"
//   source             "noise": the fractal-noise source
//   fps                25, the frame rate
//   segment_frames     25, each segment's frames
//   ts_discontinuity_indicator
//                      the ad's only: true, the continuity counters marked at
//                      each splice
//   x264_sei           libx264's settings, from the top rung's first SEI; ""
//                      with MPEG-2
// + ffmpeg_libraries   the run-time versions of libavutil, libavcodec,
//                      libavformat and libswscale, by name, as
//                      "major.minor.micro"
//   degraded           the degraded ladder's only: rung, + control, segment,
//                      q_normal and q_low
//   ad                 the ad's only: rung, + control, segments,
//                      cue_out_segment, cue_in_segment, duration_s,
//                      + timestamps, ad_origin_pts, content, q_normal and
//                      q_low
//     rung             the low rung, with the low-quality segments: "v270",
//                      the ad's "v180"
//   + control          the other lower rung, without them: "v180", the ad's
//                      "v270"
//     segment          the low-quality segment: 4
//     segments         the ad's segments, the low-quality ones: [3, 4]
//     cue_out_segment  the segment that EXT-X-CUE-OUT comes before: 3
//     cue_in_segment   the segment that EXT-X-CUE-IN comes before: 5
//     duration_s       the duration EXT-X-CUE-OUT declares, in seconds: 2
//   + timestamps       Q34's arrangement of the ad's timestamps:
//                      "main-origin", "from-1s" or "continuous"
//     ad_origin_pts    the ad's first PTS, in 90 kHz ticks
//     content          "calm": the calm ad
//     q_normal, q_low  the encoder's two quantizers: libx264's 22 and 46,
//                      MPEG-2's 4 and 31
//   rungs              from the top, each with name, width, height, + uri,
//                      bandwidth, average_bandwidth, codecs and bytes
//     name, width, height
//                      "v360", 640 and 360, and so on (kRungs)
//   + uri              its media playlist as the master gives it:
//                      "v360/index.m3u8"
//     bandwidth        BANDWIDTH: the peak segment bit rate, in bits per
//                      second
//     average_bandwidth   AVERAGE-BANDWIDTH: the average one
//     codecs           CODECS, from its first SPS, as "avc1.64001e"; "" with
//                      MPEG-2
//     bytes            its segments' bytes
//   segments           in play order, each with index, file, period,
//                      first_frame, frames, position_s, pts_first,
//                      pts_first_s, source_first, discontinuity,
//                      + discontinuity_sequence, + discontinuity_line, tags,
//                      + tag_lines, + extinf_line, + uri_line, q and bytes
//     index            its media sequence number, from 0
//     file             its file in each rung's directory: "seg_004.ts",
//                      "main_000.ts", "ad_000.ts"
//     period           "main" or "ad"
//     first_frame      the index of its first frame among each rung's 200,
//                      the reference's too
//     frames           25
//     position_s       its playlist time, the sum of the EXTINF before it, in
//                      seconds
//     pts_first        its first frame's PTS, in 90 kHz ticks; frame k's adds
//                      k times 3600
//     pts_first_s      the same PTS in seconds
//     source_first     the frame of its period's source that it starts with
//     discontinuity    whether an EXT-X-DISCONTINUITY comes before it
//   + discontinuity_sequence
//                      the playlist discontinuity sequence it belongs to
//   + discontinuity_line
//                      that EXT-X-DISCONTINUITY's line; null without one
//     tags             the other tags before it, as written: the ad's cue tags
//   + tag_lines        their lines, in the same order
//   + extinf_line      its EXTINF's line
//   + uri_line         its URI's line
//     q                each rung's quantizer, by the rung's name
//     bytes            the size of its file in each rung, in bytes, by the
//                      rung's name
//
// Lines are counted from 1 in the media playlist, whose text every rung
// shares. testgen's source_si_ti, the source's SI and TI, is left out, as
// SyntheticLadder.cpp says. Numbers are in json::number's shortest form
// (src/JsonText.h): pts_first_s is written by it, the integers by
// std::format, which writes them alike. So position_s is 3 where testgen
// wrote 3.000, duration_s 2 where it wrote 2.000 and pts_first_s 17 where it
// wrote 17.000000: the same values, which a reader that tells integers from
// decimals, as Python's json module does, now reads as integers. Until
// write_ladder() measures them, as in plan()'s layout, x264_sei and codecs
// are "" and bandwidth, average_bandwidth and bytes 0.
std::string layout_json(const Layout& layout);

// BANDWIDTH and AVERAGE-BANDWIDTH of a rung whose segments, of
// kSegmentFrames frames and 1 s each, have these sizes, in bits per second
// rounded up to a whole bit: RFC 8216 §4.1's peak segment bit rate, that of
// the costliest run of segments lasting 0.5 to 1.5 target durations, which
// with the target duration of 1 s is a single segment's, and its average
// segment bit rate, every segment's bits over the playlist's duration.
std::pair<std::int64_t, std::int64_t> bandwidths(const std::vector<std::uint64_t>& segment_bytes);

// Why `rungs` aren't a ladder for top-rung mode, D-002's precondition: a
// BANDWIDTH not below the one before it. Empty when each is.
std::string rung_order_error(const std::vector<RungLayout>& rungs);

// A made ladder.
struct Ladder {
    Spec                  spec;
    std::filesystem::path dir;        // where it is
    std::string           master;     // dir/master.m3u8, for mirage_vmaf --ladder
    Layout                layout;     // plan(spec), with what writing it measured
    double                seconds = 0;          // wall time to make it
    double                source_seconds = 0;   // of which computing the source's pictures
    std::string           error;      // empty: made

    // A rung's media playlist: dir/<rung>/index.m3u8.
    std::string playlist(int rung) const;
};

// Writes the ladder of `spec` into `dir`, created if needed, which must not
// hold anything yet: a directory per rung with its eight segments and its
// index.m3u8, master.m3u8 and layout.json (29 files). FFmpeg's log is held
// at AV_LOG_WARNING meanwhile, as mirage_vmaf holds it
// (tools/mirage_vmaf.cpp:155), since libx264 writes its statistics at
// AV_LOG_INFO for each of the 24 encoders. On an error the directory keeps
// what was written so far.
Ladder write_ladder(const std::filesystem::path& dir, const Spec& spec);

// How many ladders write_ladder() has made in this process.
int made();

// The ladder of `spec` in media_dir()/name(spec), made by write_ladder() the
// first time it is asked for in a media directory and the same object after
// that: once per process without MIRAGE_KEEP_MEDIA, once per test case with
// it, and so once per Catch2 test case under ctest (P-Q). Called within a
// test case.
const Ladder& ladder(const Spec& spec);

// Where a test case's media go: with MIRAGE_KEEP_MEDIA=<dir> set and not
// empty, <dir> made absolute, then dir_name(<the current test case's name>),
// created, kept after the run (P-U); otherwise the process's temporary
// directory (tests/TestTemp.h), removed at its exit. Called within a test
// case. A ladder can be kept once only: write_ladder() refuses a directory a
// previous run filled.
std::filesystem::path media_dir();

// `test_name` as a directory name: each byte that isn't an ASCII letter,
// digit, '-', '_' or '.' becomes '_', and a name of dots alone or no byte at
// all gets a '_' before it.
std::string dir_name(std::string_view test_name);

} // namespace synthladder
