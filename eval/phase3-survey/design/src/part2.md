## 16. Open issues

- **MEASUREMENT PENDING**: the demux pass's time on real ladders, local and over HTTP; the byte-position join on fMP4 with EXT-X-MAP, ID3-timestamped audio, open GOPs and D-011's ladders; the document's size on real ladders; the JSON reader's memory and time on the largest committed report; the ported generator's time and the acceptance tests' time in a Debug build (§9.5); the spread of the fetch's times between runs; the detection parameters' behaviour on D-011's ladders (§6.3); how often D-011's references trip the axis check (R11); whether the player's PTS equal `mirage_vmaf`'s for the same frame (R16); decode errors from H.264 and HEVC decoders, since only MPEG-2's were observed (sources.md §12); whether FFmpeg's demuxers deliver an SCTE-35 or timed-ID3 stream through `mirage_vmaf`'s input settings (scte35.md §10, item 6); the continuous-timestamp ad ladder of Q34 (c), not yet generated or scored; the ad from 1 s and the continuous ad with MPEG-2 (Q33); a seek into an ad that restarts at the main content's origin (Q37).
- **Not run by any survey or by this design**: `NetworkLogger`'s manual tracking over HTTP in a headless process (R5), a redirect included (Q44 (d), R22); a paced fetch (Q44 (f) 2 and 3); the player itself (ui.md's findings F1, F2 and F6 are by reading).
- **The scte35 survey's open issues stand**: SCTE 35-1 2023r2 and SCTE 35-2 2026 unread (scte.org returned HTTP 403); the PDF's table text from the agent's own extractor; one public VOD stream with SCTE-35, generated on request by a vendor demo.
- **Fixtures made with FFmpeg's hls muxer** write `TARGETDURATION:0` and a last EXTINF shorter than its frames (sources.md §12): WP06's tests that use one should check their EXTINF against their frames; testgen's generator writes its playlists itself (testgen.md §2.3).
- **Q02, Q25 and Q29 together decide the acceptance tests' assertions on offsets in seconds**; the tests assert offsets in frames (P-R), and the seconds are checked in WP01's unit tests once Q02 is answered.
- **The survey directory is committed without** its generated media, binaries, copies of FFmpeg's source, third-party downloads and extracts, and the SCTE-35 messages it collected (Q21); this design cites none of them for a fact (Conventions).

---

## Appendix A. Runs this design cites

**This design's runs** (`<out>/design/runs/`, each made by `design/tools/run.sh`):

| Record | Command (from the record) | What it shows |
|---|---|---|
| `detection-semantics-ad.txt` | in `<out>/testgen`: `python3 -I -B <out>/design/tools/detection_semantics.py reports/ad-x264.json reports/ad-x264-a.layout.json` | on testgen's ad ladder report, with the prototype detector at T = 6, W = 50, N = 12: where `v180`'s drop starts when the first pair of each libvmaf context is treated like any other, left out of the curve, or left out of the baseline only, and whether the cue-out's frame lies in it; a session's drops for two scripted plans, detected on the session's curve with its own baseline, and as the pairs' drops clipped to the played spans |
| `axis-check.txt` | in `<out>/testgen`: `python3 -I -B <out>/design/tools/axis_check.py` on the reports and layouts of `ad-x264`, `ad-mpeg2`, `ad-x264-origin1` and `degraded-x264`, with `--drop 40` | revision 1's record: the first version's check of Q02 (d)'s seconds (labelled "round-1 wording" in the record) against revision 1's check of steps, with the layouts as the reference's segment map; (c)'s seconds against (d)'s; the same on the first report with reference frame 40 removed (simulated) |
| `seconds-check.txt` | in `<out>/testgen`: `python3 -I -B <out>/design/tools/seconds_check.py` on the same four reports and layouts, on the mezzanine-mode reports `degraded-x264-mezzanine` and `ad-x264-mezzanine` without a map, with `--drop 40 180`, `--retime 75 124 25 24 30 50 60` and `--extinf 1 1` | revision 2's check of §4.2, (1) and (2), with and without the segment map, beside revision 1's check of steps and the second review's comparison at segment starts; the largest difference of (d) from (c); the same on simulated copies of the first report: reference frame 40 or 180 removed, the ad's frames 75 to 124 at 25, 24, 30, 50 and 60 fps with their EXTINF following, and segment 1's EXTINF one frame longer |
| `networklogger-users.txt` | in `<repo>`: `grep -rl -e NetworkLogger src tools tests CMakeLists.txt`, sorted and counted | the 21 files that name `NetworkLogger`, which Q41 (b)'s rename would edit |
| `log-callback-grep.txt` | in `<repo>`: `grep -rn -e av_log_set_callback -e av_log_set_level -e avformat_network_init src tools tests` | which code installs FFmpeg's log callback, sets its level and initializes its network |
| `question-lengths.txt` | in `<out>`: the second review's `review-2/tools/question_lengths.py` on `design/owner_questions.json` | every question's, option's and summary's count of sentences against L4's ranges |
| `package-check.txt` | in `<out>`: the second review's `review-2/tools/package_check.py` on `design/work_packages.json` | every file two packages name, and whether one of them depends on the other |
| `privacy-check.txt` | in `<out>`: the second review's `review-2/tools/privacy_check.py` on `design.md` and every file under `design/` but itself and `quote-check.txt`, which are made after it | counts of local paths, e-mail addresses and the local account's identity words, without the values |
| `repo-state.txt` | in `<repo>`: `git --no-optional-locks log -1` and `status --short --branch` | `<repo>`'s commit and status, read without changing them |
| `render-check.txt` | in `<out>`: `render.py` writing into a pipe that `cmp` compares with `design.md` | that `design.md` is byte-identical to the render of its sources, and that the renderer's consistency checks passed |
| `quote-check.txt` | in `<out>`: the quote checker on `design.md` and every file under `design/` but itself, against the work order's translation kept outside the repository | each file's count of passages shared with the translation, by lines, without the shared words |

`design/tools/run.sh <name> <dir> <command…>` writes the record (working directory, exact command, start and end in UTC, full output, exit status), with local paths replaced by `<out>`, `<repo>` and `<scratch>`. The three Python tools of `design/tools/` that read testgen's reports and layouts in `<out>/testgen/reports/` (`axis_check.py`, `detection_semantics.py`, `seconds_check.py`) write nothing but their records; `axis_check.py` is kept as revision 1 left it, since its record and the second review's `rate-change-check` were made with it.

**The first review's runs** (`<out>/review-adversarial/runs/`), cited by name: `axis-d-check` (the first version's check applied to testgen's reports), `ctest-registration` (how ctest runs the Catch2 test cases), `switch-t-sweep` (a switch alone under Q22 (b), as T changes). **The second review's runs** (`<out>/review-2/runs/`), cited by name: `rate-change-check` (revision 1's check on the ad's frames re-timed at other rates, simulated).

**The two designs' runs**, cited by name:

| Where | Runs | What they show |
|---|---|---|
| `<out>/design-library/runs/` | `map-by-pos-ad-v360`, `-ad-v180`, `-ad-origin1-v360`, `-degraded-v270` (from the `logprobe-*` records of the same directory) | packets to segments by byte position, 200 of 200 on four rungs; by PTS against EXTINF, 145 and 193 of 200 |
| | `axis-ad`, `axis-ad-origin1`, `axis-degraded`, `axis-ad-float`, `axis-degraded-float` | the four axes on testgen's ladders, in exact ticks and in doubles |
| `<out>/design-evaluation/runs/` | `03_segmap`, `04_join_check` | the demux pass on 15 playlists; the join with every frame's PTS checked |
| | `05_detection_sweep` | three detectors over W, T and N on 21 pair curves against the generator's ground truth |
| | `06_session_fetch` | a scripted session's 11 files fetched from a local server that answers one segment 2.3 s late |
| | `07_attribute_seconds_as_doubles`, `08_attribute_ticks`, `09_axes`, `10_attribute_final`, `11_doc_sizes`, `12_repro_check` | the prototype's attributions with doubles and with ticks; the boundary frames on three axes; the end-to-end results; the documents' sizes; the reproduction of runs 07, 08 and 10 |

**The surveys' runs and records**: `<out>/runs/` (sources.md, Appendix A), `<out>/scte35_evidence/runs/` (scte35.md), `<out>/testgen/logs/` (testgen.md), `<out>/schema-survey/runs/` (schema.md), `<out>/experiments/hls-seek-probe/run-output.txt` (ui.md).

---

## Appendix B. API sketch

In the repository's style: namespaces of free functions and structs, errors as strings, no exceptions. Names are proposals.

```cpp
// src/TimelineModel.h — the timeline's data (core: C++20 and libavutil only).
namespace timeline {
enum class Source : std::uint8_t { Vmaf, Network, Playlist, Scte35, Player, Decoder };
inline constexpr std::size_t kSourceCount = 6;
std::string_view source_name(Source);                 // "vmaf", "network", ...

struct MediaKey {                                     // the reference frame an event sits at
    std::int32_t sequence = 0;                        // the report's reference sequence (D-057)
    std::optional<std::int64_t> pts;                  // ticks of the reference's time base
};
struct Value;                                         // null, bool, int64, double, string, list, object
using Payload = std::vector<std::pair<std::string, Value>>;
struct Event { /* §4.3 */ };

struct Variant {
    int index = -1, pair = -1;                        // pair -1: the reference
    std::vector<std::size_t> entries;                 // master entries naming it
    std::string url, playlist_sha256, playlist_location;
    std::int64_t bandwidth = 0;  std::optional<std::string> resolution, codecs;
    int rank_from_top = 0, rank_from_bottom = 0;      // Q07, Q43 (e)
    std::string segment_map = "ok";                   // or "refused: <why>" (Q43 (c))
};
struct Curve {
    int variant = -1;                                 // a pair's distorted variant
    std::vector<std::int64_t> ref_index;  std::vector<double> vmaf;
    std::vector<std::int64_t> context_first;          // reference frames starting a libvmaf context
};
struct CurveRef { enum class Kind : std::uint8_t { Pair, Session } kind = Kind::Pair; int index = 0; };
// Declared by WP00b in round 2, once Q11 and Q12 are answered, with CurveRef's Session kind:
struct Span { std::int64_t ref_index = 0, ref_end = 0; int variant = -1; bool scored = true; };
struct Session { std::string id, name, wallclock_anchor_utc; std::vector<Span> spans; };

struct Timeline {
    Axis axis;                                        // src/TimelineAxis.h
    std::vector<Variant> variants;
    std::vector<Curve>   curves;                      // one per pair
    std::vector<Event>   events;                      // canonical order (P-I)
    std::vector<Session> sessions;                    // added by WP00b in round 2
    std::vector<std::string> warnings;
    // Indices of the events meeting [a, b), margins included (P-J, Q29).
    std::vector<std::size_t> overlapping(std::int64_t a, std::int64_t b) const;
};
// Declared by WP00a; defined in src/TimelineModel.cpp, a pair's by WP01, a session's,
// derived from the pairs' (§4.4), by WP07.
Curve curve_points(const Timeline&, CurveRef);
}

// src/TimelineAxis.h — media keys and seconds (Q02).
namespace timeline {
struct Axis {
    std::string  kind;                                // Q02's option
    std::string  clock;                               // axis.seconds: "playlist-time" or "frame-grid"
    AVRational   time_base{}, frame_duration{};       // the reference's
    std::int64_t frames = 0;                          // reference frames
    std::vector<MediaKey> keys;                       // per reference frame
    std::string  checked;                             // "segment-map" or "report-only" (§4.2)
    std::vector<std::string> warnings;                // the check's runs, (1) and (2) (§4.2)
    double seconds(std::int64_t ref_index) const;     // for output only
    std::int64_t frame_at_or_after(AVRational seconds) const;   // first in index order (§4.2)
};
// The reference's segment map and playlist (EXTINF, discontinuities) when it has them.
Axis build_axis(const LadderScores&, const VariantMedia* reference,
                const hls::MediaTimeline* reference_playlist, std::string_view kind);
}

// src/LadderReportRead.h — a ladder report, read (Q43 (b)).
namespace timeline {
struct PairScores {
    std::size_t index = 0;  std::vector<std::size_t> entries;
    std::string ref_url, dis_url;  AVRational ref_time_base{}, dis_time_base{}, ref_rate{}, dis_rate{};
    std::vector<std::int64_t> ref_index, dis_index, ref_pts, dis_pts;   // pts in ticks
    std::vector<double> vmaf;
    std::vector<std::int64_t> context_first;          // pair indices
    std::vector<Failure> failures;                    // alignment.failures
    std::vector<Sequence> ref_sequences, dis_sequences;
};
struct LadderScores { /* settings, master entries, reference, pairs */ };
LadderScores read_ladder_report(std::string_view text, std::string& err);
std::string scores_text(const LadderScores&);         // the digest's canonical text (Q06, P-Z)
}

// src/PlaylistEvents.h — a media playlist's events, the cue interface, the pairing (WP04).
namespace timeline {
struct PlaylistTag { int segment = -1; int line = 0; std::string name, value; };

// D-070's interfaces: a cue message from any carrier, with the anchor its carrier gives (§5.5).
enum class CueCarrier : std::uint8_t { PlaylistTag, DashEventStream, TsSection, Id3Priv };
struct PlaylistAnchor { int variant = -1; std::int64_t segment = 0; int line = 0; };
struct MediaPtsAnchor { int variant = -1; std::int64_t pts = 0; AVRational time_base{};
                        std::optional<std::int64_t> segment; };     // on the carrying variant's clock
struct PeriodAnchor   { std::string period; std::int64_t presentation_time = 0, timescale = 1; };
struct CueInput {
    CueCarrier carrier = CueCarrier::PlaylistTag;
    std::variant<PlaylistAnchor, MediaPtsAnchor, PeriodAnchor> anchor;
    std::string form;                                 // "OATCLS", "DATERANGE SCTE35-OUT", "CUE-OUT-CONT SCTE35", ...
    std::vector<std::uint8_t> bytes;                  // the splice_info_section
};

struct PlaylistEvents {
    hls::MediaTimeline timeline;                      // src/HlsAlign.h
    std::vector<PlaylistTag> tags;
    std::vector<Event> cues;                          // cue_out, cue_out_cont, cue_in of the plain tags
    std::vector<CueInput> messages;                   // every embedded message, for Scte35Events
    std::string error;                                // a live playlist, say (Q43 (a))
};
PlaylistEvents parse_playlist_events(const std::string& text, const std::string& base_url, int variant);
std::vector<Event> playlist_events(const PlaylistEvents&, int variant, const CueOptions&);  // Q18-Q20
// Every cue, from plain tags and from decoded messages, paired into ad_break (Q17).
std::vector<Event> pair_breaks(const std::vector<Event>& cues, const CueOptions&);
}

// src/Scte35.h, src/Scte35Events.h — the decoder and the message events (WP05).
namespace scte35 {
struct SpliceInfo { /* scte35.md §3's fields, raw and with pts_adjustment applied; crc_valid */ };
std::optional<std::vector<std::uint8_t>> from_base64(std::string_view);   // av_base64_decode
std::optional<std::vector<std::uint8_t>> from_hex(std::string_view);      // "0xFC..."
SpliceInfo decode(std::span<const std::uint8_t> section);                  // never reads past it
}
namespace timeline {
struct MessageEvents { std::vector<Event> cues, splice_points, others; std::vector<std::string> warnings; };
// Decodes, classes (Q15, Q16), merges into plain cues (Q44 (e)) and places every anchor kind (§5.1).
MessageEvents scte35_events(const std::vector<CueInput>&, std::vector<Event>& plain_cues,
                            const std::vector<VariantMedia>&, const LadderScores&, const CueOptions&);
}

// src/SessionPlan.h — sessions (WP07; types declared by WP00b).
namespace timeline {
struct PlanSpan { std::size_t entry = 0; std::int64_t first_seq = 0, last_seq = 0; };
struct SessionPlan { std::string name; std::vector<PlanSpan> spans; };
SessionPlan parse_session_plan(std::string_view json, std::string& err);   // Q11, Q44 (a)
std::vector<Drop> session_drops(const Timeline&, int session, const DetectOptions&);   // Q22, §6.3
}

// src/TimelineBuild.h — the merge.
namespace timeline {
struct SegmentFrames { std::int64_t seq = 0; std::int64_t first_index = 0, frames = 0, bytes = 0; };
struct VariantMedia {                                 // from the demux pass (or the report, Q08 (b))
    std::vector<SegmentFrames> segments;
    std::vector<std::int64_t> keyframes, corrupt;     // frame indices of the input
    std::vector<std::int64_t> pts;                    // per frame, ticks
    std::string refused;                              // why the map was refused (Q43 (c))
};
struct RequestRow { /* one NetworkLogger row: URL, status, status text, times, bytes, error */ };
struct Request { std::string file; std::vector<RequestRow> chain; };      // one fetched file (Q44 (d))
struct BuildInputs {
    LadderScores scores;
    std::vector<PlaylistEvents> playlists;            // parallel to the variants
    std::vector<VariantMedia>   media;
    std::optional<SessionPlan>  plan;  std::vector<Request> requests;     // round 2
    std::string axis_kind;  CueOptions cues;
};
Timeline build(const BuildInputs&);                   // warnings in Timeline::warnings
}

// src/TimelineQuery.h, src/TimelineDetect.h — §6.1. Written by WP01 and WP02, not by WP00a,
// since their options encode Q23 to Q29.
namespace timeline {
struct QueryOptions { AVRational margin_before{0, 1}, margin_after{0, 1}; /* scope, Q28 */ };
struct DetectOptions {
    double threshold = 0;                             // Q26: 6 only if the owner says so
    std::int64_t window = 0, min_points = 0;          // Q23, Q24: required until Q26 says otherwise
};
struct Drop { Interval interval; std::int64_t first_point = 0, last_point = 0;
              double baseline = 0, min = 0, mean = 0; };
std::vector<Drop> detect(const Curve&, const DetectOptions&);
Attribution attribute(const Timeline&, CurveRef, Interval, const QueryOptions&);
}

// src/TimelineDoc.h — §8.
namespace timeline {
struct DocInfo { /* report path and hashes, provenance copied, config file, plan, playlists, parameters */ };
std::string to_json(const Timeline&, const std::vector<Attribution>&, const DocInfo&);
bool from_json(std::string_view text, Timeline&, std::vector<Attribution>&, DocInfo&, std::string& err);
}

// Adapters (libavformat): src/TimelineMedia.h, src/SessionFetch.h
namespace timeline {
struct PlaylistText { std::string url, location, sha256, text, error; };   // vmafladder::read_master
PlaylistText read_playlist(const std::string& url, std::stop_token);
VariantMedia read_media(const std::string& playlist_url, const PairScores* check, bool reference,
                        std::stop_token, std::string& err);              // the demux pass (P-F, P-G)
// The session's files in play order (P-N), with the pacing and playlist fetches of Q44 (f), (g).
std::vector<Request> fetch_session(const std::vector<std::string>& urls, std::stop_token);
}

// UI math (ImGui-free; mirage, mirage_tests): src/TimelineLoad.h (WP12).
namespace timeline {
struct Snapshot { Timeline timeline; std::vector<Attribution> attributions; DocInfo info; };
class Loader {                                        // the panel's loading off the main thread
public:
    void start(std::string document_path);            // cancels a load in progress
    std::shared_ptr<const Snapshot> take();           // once, when ready; null before
    std::string error() const;                        // a hash mismatch, an unknown version, ...
};
}
```
