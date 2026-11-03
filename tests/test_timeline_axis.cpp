// timeline::build_axis() (src/TimelineAxis.h) on references the tests write:
// a ladder report's pairs, the demux pass's segment map and a media playlist
// for each. The media keys where PTS restart at the main content's origin,
// jump forward or are scored on a coarser grid; the seconds of D-073's
// option and of its fallback; seconds turned into frames, in index order and
// from a frame on; scored frames at positions outside the frames their pair
// read; and the check of eval/phase3-survey/design.md §4.2 in the shapes its
// records give (eval/phase3-survey/design/runs/seconds-check.txt and
// eval/phase3-survey/review-3/runs/seconds-check-sequence-start.txt), each
// rebuilt here from the test's own numbers: testgen's ad ladder, eight
// segments of 25 frames at 25 fps in 1/90000, its ad in segments 3 and 4
// restarting at the main content's origin, 10 s.

#include <catch2/catch_test_macros.hpp>

#include "HlsAlign.h"
#include "LadderReportRead.h"
#include "TimelineAxis.h"
#include "TimelineBuild.h"

extern "C" {
#include <libavutil/avutil.h>   // AV_NOPTS_VALUE
}

#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using timeline::Axis;
using timeline::AxisRun;

namespace {

constexpr AVRational   kTs      = { 1, 90000 };   // MPEG-TS's time base
constexpr std::int64_t kOrigin  = 900000;         // 10 s: where testgen's main content starts
constexpr std::int64_t kFrame25 = 3600;           // one frame at 25 fps, in 1/90000

// A reference as the timeline build gets it: its frames' PTS, its segments
// in playlist order with their EXTINF as written, and where an
// EXT-X-DISCONTINUITY precedes one.
struct Reference {
    std::vector<std::int64_t> pts;          // per frame, ticks of time_base
    std::vector<std::int64_t> seg_frames;   // per segment
    std::vector<std::string>  extinf;       // per segment
    std::vector<bool>         disc;         // per segment
    AVRational time_base = kTs;
    AVRational rate      = { 25, 1 };
};

// testgen's ad ladder's reference: main content from 10 s, the ad's 50
// frames from 10 s again after a discontinuity, the main content back at
// 15 s, where it would be without the ad, after another.
Reference ad_shape()
{
    Reference r;
    for (std::int64_t i = 0; i < 200; ++i)
        r.pts.push_back(i < 75 || i >= 125 ? kOrigin + i * kFrame25 : kOrigin + (i - 75) * kFrame25);
    r.seg_frames.assign(8, 25);
    r.extinf.assign(8, "1.000");
    r.disc = { false, false, false, true, false, true, false, false };
    return r;
}

// The same with the ad from 1 s: a jump back at frame 75 and one of more
// than 10 s forward at frame 125.
Reference ad_from_1s()
{
    Reference r = ad_shape();
    for (std::int64_t i = 75; i < 125; ++i) r.pts[static_cast<std::size_t>(i)] = 90000 + (i - 75) * kFrame25;
    return r;
}

// `r` without reference frame k, its segment one frame shorter: a frame the
// stream lacks (seconds-check.txt's simulation "--drop").
Reference without_frame(Reference r, std::int64_t k)
{
    r.pts.erase(r.pts.begin() + k);
    std::int64_t first = 0;
    for (std::int64_t& n : r.seg_frames) {
        if (k < first + n) {
            --n;
            break;
        }
        first += n;
    }
    return r;
}

// Frames a to b at `fps`: frame a keeps its PTS, each later one comes 1/fps s
// after the one before, the frames after b keep theirs, and the EXTINF of
// every segment within a to b becomes its frames / fps, written to nine
// decimals ("--retime").
Reference retimed(Reference r, std::int64_t a, std::int64_t b, std::int64_t fps)
{
    const std::int64_t step = 90000 / fps;   // exact for 24, 30, 50 and 60
    for (std::int64_t i = a + 1; i <= b; ++i)
        r.pts[static_cast<std::size_t>(i)] = r.pts[static_cast<std::size_t>(a)] + (i - a) * step;
    std::int64_t first = 0;
    for (std::size_t s = 0; s < r.seg_frames.size(); ++s) {
        const std::int64_t n = r.seg_frames[s];
        if (first >= a && first + n - 1 <= b)
            r.extinf[s] = std::format("{:.9f}", static_cast<double>(n) / static_cast<double>(fps));
        first += n;
    }
    return r;
}

// The reference's sequences as the aligner finds them (src/VMAFAlign.h): a
// new one at a backward step or a forward jump of more than 10 s.
std::vector<timeline::Sequence> sequences_of(const Reference& r)
{
    std::vector<timeline::Sequence> out;
    const std::int64_t jump = 10 * r.time_base.den / r.time_base.num;
    for (std::size_t i = 0; i < r.pts.size(); ++i) {
        if (i == 0 || r.pts[i] < r.pts[i - 1] || r.pts[i] - r.pts[i - 1] > jump)
            out.push_back({ .first_index = static_cast<std::int64_t>(i), .first_pts = r.pts[i] });
        ++out.back().frames;
    }
    return out;
}

// A ladder report whose k-th pair scores the reference frames scored[k],
// every frame when scored is empty.
timeline::LadderScores report(const Reference& r, std::vector<std::vector<std::int64_t>> scored = {})
{
    if (scored.empty()) {
        scored.emplace_back();
        for (std::size_t i = 0; i < r.pts.size(); ++i) scored.back().push_back(static_cast<std::int64_t>(i));
    }
    timeline::LadderScores s;
    s.status         = "complete";
    s.reference_mode = "top-rung";
    for (std::size_t k = 0; k < scored.size(); ++k) {
        timeline::PairScores p;
        p.index         = k;
        p.ref_time_base = r.time_base;
        p.dis_time_base = r.time_base;
        p.ref_rate      = r.rate;
        p.dis_rate      = r.rate;
        std::int64_t dis = 0;
        for (std::int64_t i : scored[k]) {
            p.ref_index.push_back(i);
            p.dis_index.push_back(dis++);
            p.ref_pts.push_back(r.pts[static_cast<std::size_t>(i)]);
            p.dis_pts.push_back(r.pts[static_cast<std::size_t>(i)]);
            p.vmaf.push_back(90.0);
        }
        p.ref_sequences = sequences_of(r);
        p.dis_sequences = p.ref_sequences;
        s.pairs.push_back(std::move(p));
    }
    return s;
}

// The demux pass of `r`: its segments, media sequence numbers from 0, and
// every frame's PTS.
timeline::VariantMedia media_of(const Reference& r)
{
    timeline::VariantMedia m;
    std::int64_t first = 0;
    for (std::size_t s = 0; s < r.seg_frames.size(); ++s) {
        m.segments.push_back({ .seq = static_cast<std::int64_t>(s), .first_index = first,
                               .frames = r.seg_frames[s], .bytes = 0 });
        first += r.seg_frames[s];
    }
    m.pts = r.pts;
    return m;
}

// `r`'s media playlist, as hls::parse_media_timeline reads it.
hls::MediaTimeline playlist_of(const Reference& r)
{
    std::string text = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:2\n"
                       "#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-MEDIA-SEQUENCE:0\n";
    for (std::size_t s = 0; s < r.extinf.size(); ++s) {
        if (r.disc[s]) text += "#EXT-X-DISCONTINUITY\n";
        text += std::format("#EXTINF:{},\nseg_{:03}.ts\n", r.extinf[s], s);
    }
    text += "#EXT-X-ENDLIST\n";
    hls::MediaTimeline pl = hls::parse_media_timeline(text, "http://test.invalid/v360/index.m3u8");
    REQUIRE(pl.ok);
    return pl;
}

// The axis of `r` with its segment map and playlist, every frame scored.
Axis with_map(const Reference& r)
{
    const timeline::VariantMedia m  = media_of(r);
    const hls::MediaTimeline     pl = playlist_of(r);
    return timeline::build_axis(report(r), &m, &pl);
}

// The axis of `r` as a playlist whose map the demux pass refused.
Axis map_refused(const Reference& r)
{
    timeline::VariantMedia m = media_of(r);
    m.refused = "a frame's PTS differs from the report's";
    const hls::MediaTimeline pl = playlist_of(r);
    return timeline::build_axis(report(r), &m, &pl);
}

// A run's numbers as seconds-check.txt prints them.
std::string printed(const AxisRun& run)
{
    return std::format("frames {}-{} ({} frames), largest {:+.3f} s at frame {}", run.first, run.last,
                       run.frames, run.largest, run.largest_at);
}

std::vector<std::string> printed_runs(const Axis& axis, AxisRun::Test test)
{
    std::vector<std::string> out;
    for (const AxisRun& r : axis.runs)
        if (r.test == test) out.push_back(printed(r));
    return out;
}

using Runs = std::vector<std::string>;

} // namespace

TEST_CASE("timeline axis: media keys where the ad restarts its PTS at the main content's origin",
          "[timeline][timeline_axis]")
{
    const Reference r = ad_shape();
    const Axis axis = with_map(r);
    REQUIRE(axis.frames == 200);
    REQUIRE(axis.keys.size() == 200);
    CHECK(axis.kind == "reference-frames");
    CHECK(av_cmp_q(axis.time_base, kTs) == 0);
    CHECK(av_cmp_q(axis.frame_duration, AVRational{ 1, 25 }) == 0);

    // The report's two reference sequences: the jump back at frame 75 starts
    // one; the 3.04 s forward at frame 125 stays inside it.
    CHECK(axis.keys[0].sequence == 0);
    CHECK(axis.keys[74].sequence == 0);
    CHECK(axis.keys[75].sequence == 1);
    CHECK(axis.keys[199].sequence == 1);
    CHECK(axis.keys[0].pts == kOrigin);
    CHECK(axis.keys[75].pts == kOrigin);
    CHECK(axis.keys[125].pts == 1350000);

    // PTS alone: 150 values for 200 frames; with the sequence, 200.
    std::set<std::int64_t> pts;
    std::set<std::pair<std::int32_t, std::int64_t>> keys;
    for (const timeline::MediaKey& k : axis.keys) {
        REQUIRE(k.pts);
        pts.insert(*k.pts);
        keys.insert({ k.sequence, *k.pts });
    }
    CHECK(pts.size() == 150);
    CHECK(keys.size() == 200);
}

TEST_CASE("timeline axis: a forward jump under 10 s stays in its sequence, one over 10 s starts one",
          "[timeline][timeline_axis]")
{
    // The ad from 1 s: back at frame 75, then 12.04 s forward at frame 125,
    // as testgen's ad-x264-origin1 report has its three sequences.
    const Axis origin1 = with_map(ad_from_1s());
    CHECK(origin1.keys[74].sequence == 0);
    CHECK(origin1.keys[75].sequence == 1);
    CHECK(origin1.keys[124].sequence == 1);
    CHECK(origin1.keys[125].sequence == 2);
    CHECK(origin1.runs.empty());
    CHECK(origin1.warnings.empty());

    // A reference that isn't a playlist whose PTS jump 3 s forward at frame
    // 100: one sequence, so the jump is in the keys' PTS only, and the check
    // of the report's sequence warns from the jump on.
    Reference jump;
    for (std::int64_t i = 0; i < 200; ++i)
        jump.pts.push_back(kOrigin + i * kFrame25 + (i >= 100 ? 270000 : 0));
    const Axis axis = timeline::build_axis(report(jump), nullptr, nullptr);
    CHECK(axis.keys[99].sequence == 0);
    CHECK(axis.keys[100].sequence == 0);
    CHECK(axis.keys[100].pts == kOrigin + 100 * kFrame25 + 270000);
    CHECK(printed_runs(axis, AxisRun::Test::ReportGrid) ==
          Runs{ "frames 100-199 (100 frames), largest +3.000 s at frame 100" });
    REQUIRE(axis.runs.size() == 1);
    CHECK(axis.runs[0].warning);
    CHECK(axis.warnings.size() == 1);
}

TEST_CASE("timeline axis: a pair on a coarser grid, and the frames no pair scored",
          "[timeline][timeline_axis]")
{
    // A 50 fps reference with a 25 fps rung, scored at every other
    // reference frame, as a 30 fps rung against a 60 fps reference is
    // (eval/phase3-survey/schema.md §3.1).
    Reference r;
    r.rate = { 50, 1 };
    for (std::int64_t i = 0; i < 100; ++i) r.pts.push_back(kOrigin + i * 1800);
    r.seg_frames = { 50, 50 };
    r.extinf     = { "1.000", "1.000" };
    r.disc       = { false, false };
    std::vector<std::int64_t> even;
    for (std::int64_t i = 0; i < 100; i += 2) even.push_back(i);

    // The demux pass gives the odd frames their PTS; the check, on the even
    // frames, finds the grid of the reference's frame duration.
    const timeline::VariantMedia m  = media_of(r);
    const hls::MediaTimeline     pl = playlist_of(r);
    const Axis axis = timeline::build_axis(report(r, { even }), &m, &pl);
    CHECK(axis.frames == 100);
    CHECK(axis.keys[1].pts == kOrigin + 1800);
    CHECK(axis.runs.empty());
    CHECK(axis.clock == "playlist-time");
    CHECK(axis.seconds(1) == 0.02);
    CHECK(axis.seconds(51) == 1.02);

    // Without a demux pass the odd frames have no PTS, but their sequence;
    // their seconds are a frame after the frame before them.
    const Axis bare = timeline::build_axis(report(r, { even }), nullptr, nullptr);
    CHECK(bare.frames == 100);
    CHECK_FALSE(bare.keys[1].pts.has_value());
    CHECK(bare.keys[1].sequence == 0);
    CHECK(bare.keys[2].pts == kOrigin + 3600);
    CHECK(bare.runs.empty());
    CHECK(bare.seconds(1) == 0.02);

    // A segment map with frames that have no PTS: the odd frames sit a frame
    // after the even ones.
    timeline::VariantMedia sparse = media_of(r);
    for (std::size_t i = 1; i < sparse.pts.size(); i += 2) sparse.pts[i] = AV_NOPTS_VALUE;
    const Axis gaps = timeline::build_axis(report(r, { even }), &sparse, &pl);
    CHECK(gaps.clock == "playlist-time");
    CHECK_FALSE(gaps.keys[1].pts.has_value());
    CHECK(gaps.seconds(1) == 0.02);
    CHECK(gaps.seconds(99) == 1.98);
    CHECK(gaps.runs.empty());

    // Two pairs, the full-rate one and the coarse one, agree on every PTS.
    std::vector<std::int64_t> all;
    for (std::int64_t i = 0; i < 100; ++i) all.push_back(i);
    const Axis both = timeline::build_axis(report(r, { all, even }), &m, &pl);
    CHECK(both.warnings.empty());
    CHECK(both.runs.empty());
}

TEST_CASE("timeline axis: the playlist time with a segment map, the frame grid without one",
          "[timeline][timeline_axis]")
{
    // On testgen's ad shape the two clocks agree.
    const Reference r = ad_shape();
    const Axis axis = with_map(r);
    CHECK(axis.clock == "playlist-time");
    CHECK(axis.checked == "segment-map");
    CHECK(axis.seconds(0) == 0.0);
    CHECK(axis.seconds(75) == 3.0);
    CHECK(axis.seconds(125) == 5.0);
    CHECK(axis.seconds(199) == 7.96);
    // Past the last frame, a frame duration a frame: a span's end.
    CHECK(axis.seconds(200) == 8.0);
    CHECK(axis.seconds(-1) == -0.04);
    CHECK(axis.seconds_between(75, 125) == 2.0);
    CHECK(axis.seconds_between(125, 75) == -2.0);

    // With the ad at 24 fps, its EXTINF following it, the playlist time
    // follows the frames and the frame grid doesn't.
    const Reference slow = retimed(r, 75, 124, 24);
    const Axis played = with_map(slow);
    CHECK(played.clock == "playlist-time");
    CHECK(played.seconds(100) == 4.041666667);   // 3 s + "1.041666667"
    CHECK(played.seconds(125) == 5.083333334);   // 3 s + twice that
    CHECK(played.seconds_between(75, 76) == 1.0 / 24);
    const Axis grid = map_refused(slow);
    CHECK(grid.clock == "frame-grid");
    CHECK(grid.checked == "report-only");
    CHECK(grid.seconds(100) == 4.0);
    CHECK(grid.seconds(125) == 5.0);

    // A reference that isn't a playlist: the frame grid, from the report.
    const Axis mezzanine = timeline::build_axis(report(r), nullptr, nullptr);
    CHECK(mezzanine.clock == "frame-grid");
    CHECK(mezzanine.seconds(125) == 5.0);
}

TEST_CASE("timeline axis: seconds to frames, for intervals and durations",
          "[timeline][timeline_axis]")
{
    const Axis axis = with_map(ad_shape());
    CHECK(axis.frame_at_or_after(AVRational{ 3, 1 }) == 75);
    CHECK(axis.frame_at_or_after(AVRational{ 5, 1 }) == 125);
    CHECK(axis.frame_at_or_after(AVRational{ 299, 100 }) == 75);
    CHECK(axis.frame_at_or_after(AVRational{ 301, 100 }) == 76);
    CHECK(axis.frame_at_or_after(AVRational{ -1, 1 }) == 0);
    CHECK(axis.frame_at_or_after(AVRational{ 796, 100 }) == 199);
    CHECK(axis.frame_at_or_after(AVRational{ 797, 100 }) == 200);   // none: the end
    CHECK(axis.frame_at_or_after(AVRational{ -3, -1 }) == 75);      // a negative denominator
    CHECK(axis.frame_at_or_after(AVRational{ 1, 0 }) == 200);       // +infinity
    CHECK(axis.frame_at_or_after(AVRational{ -1, 0 }) == 0);

    // A declared duration from a frame, the form the build gives WP04's
    // FrameAfter: the ad's 2 s from its cue-out at frame 75 end at frame
    // 125, a frame's from frame 199 at the end, frame 200; a duration of
    // zero or less, or of -infinity, at the frame itself; +infinity at the
    // end. A frame the axis doesn't have gives none.
    CHECK(axis.frame_after(75, AVRational{ 2, 1 }) == 125);
    CHECK(axis.frame_after(199, AVRational{ 1, 25 }) == 200);
    CHECK(axis.frame_after(75, AVRational{ 0, 1 }) == 75);
    CHECK(axis.frame_after(75, AVRational{ -1, 25 }) == 75);
    CHECK(axis.frame_after(75, AVRational{ -1, 0 }) == 75);
    CHECK(axis.frame_after(75, AVRational{ 1, 0 }) == 200);
    CHECK(axis.frame_after(75, AVRational{ -2, -1 }) == 125);   // a negative denominator
    CHECK_FALSE(axis.frame_after(-1, AVRational{ 1, 1 }).has_value());
    CHECK_FALSE(axis.frame_after(200, AVRational{ 1, 1 }).has_value());
    // A margin, in index order: 2 s after frame 75 is frame 125 here too; one
    // frame before frame 75 is frame 74, and 0.03 s before it frame 75 itself.
    CHECK(axis.frame_at_or_after(75, AVRational{ 2, 1 }) == 125);
    CHECK(axis.frame_at_or_after(75, AVRational{ -1, 25 }) == 74);
    CHECK(axis.frame_at_or_after(75, AVRational{ -3, 100 }) == 75);
    CHECK(axis.frame_at_or_after(199, AVRational{ 1, 25 }) == 200);
    CHECK(axis.frame_at_or_after(0, AVRational{ -1, 1 }) == 0);

    // With the ad at 24 fps, 2 s from frame 75 is frame 123, 3 s + 48/24.
    const Axis slow = with_map(retimed(ad_shape(), 75, 124, 24));
    CHECK(slow.frame_after(75, AVRational{ 2, 1 }) == 123);
    CHECK(slow.frame_at_or_after(75, AVRational{ 2, 1 }) == 123);
    CHECK(slow.frame_at_or_after(AVRational{ 5, 1 }) == 123);

    // An EXTINF shorter than its frames: segment 0's 25 frames listed as
    // 0.92 s, so segment 1 starts at 0.92 s while segment 0's frame 24 is at
    // 0.96 s. Seconds are turned into the first frame at or after them in
    // index order: 0.94 s is frame 24, before segment 1's frames at 0.92 s
    // and on; and the check warns from frame 25, where the playlist time
    // steps back.
    Reference short_extinf;
    for (std::int64_t i = 0; i < 75; ++i) short_extinf.pts.push_back(kOrigin + i * kFrame25);
    short_extinf.seg_frames = { 25, 25, 25 };
    short_extinf.extinf     = { "0.920", "1.000", "1.000" };
    short_extinf.disc       = { false, false, false };
    const Axis back = with_map(short_extinf);
    CHECK(back.seconds(24) == 0.96);
    CHECK(back.seconds(25) == 0.92);
    CHECK(back.frame_at_or_after(AVRational{ 94, 100 }) == 24);
    CHECK(back.frame_at_or_after(AVRational{ 92, 100 }) == 23);
    CHECK(back.frame_at_or_after(AVRational{ 97, 100 }) == 27);   // 0.92 s + 2 frames
    CHECK(printed_runs(back, AxisRun::Test::Clocks) ==
          Runs{ "frames 25-74 (50 frames), largest +0.080 s at frame 25" });
    CHECK(printed_runs(back, AxisRun::Test::PlaylistGrid).empty());

    // From frame 25, at 0.92 s, 1/50 s on is 0.94 s: in index order frame 24,
    // at 0.96 s, comes first, a margin's answer before frame 25; a duration
    // from frame 25 ends at frame 26, the first from frame 25 on. A duration
    // of zero ends at frame 25 itself, where the index order gives frame 23,
    // at 0.92 s as well.
    CHECK(back.frame_at_or_after(25, AVRational{ 1, 50 }) == 24);
    CHECK(back.frame_after(25, AVRational{ 1, 50 }) == 26);
    CHECK(back.frame_at_or_after(25, AVRational{ 0, 1 }) == 23);
    CHECK(back.frame_after(25, AVRational{ 0, 1 }) == 25);
    CHECK(back.frame_at_or_after(24, AVRational{ 1, 25 }) == 27);   // 1.00 s
    CHECK(back.frame_after(24, AVRational{ 1, 25 }) == 27);
    // From every frame and for durations from zero to beyond the end, the
    // duration form gives the first frame from its frame on at or after the
    // frame's seconds plus the duration, and never one before its frame.
    REQUIRE(back.ticks.size() == 75);
    for (std::int64_t i = 0; i < 75; ++i) {
        for (const AVRational d : { AVRational{ 0, 1 }, AVRational{ 1, 50 }, AVRational{ 1, 25 },
                                    AVRational{ 3, 50 }, AVRational{ 1, 1 }, AVRational{ 4, 1 } }) {
            INFO("frame " << i << ", " << d.num << "/" << d.den << " s");
            const auto at_or_after = [&](std::int64_t j) {
                return back.ticks[static_cast<std::size_t>(j)] * d.den >=
                       back.ticks[static_cast<std::size_t>(i)] * d.den + d.num * back.tick_den;
            };
            std::int64_t first = i;
            while (first < 75 && !at_or_after(first)) ++first;
            const std::optional<std::int64_t> f = back.frame_after(i, d);
            REQUIRE(f.has_value());
            CHECK(*f == first);
            CHECK(*f >= i);
        }
    }
}

TEST_CASE("timeline axis: the check finds nothing on testgen's ad shape with its map, and lists its "
          "jump as a fact without one",
          "[timeline][timeline_axis]")
{
    const Reference r = ad_shape();
    const Axis axis = with_map(r);
    CHECK(axis.runs.empty());
    CHECK(axis.warnings.empty());

    // Without a map, frames 125 to 199 lie 3 s past the frame grid of the
    // report's sequence 1: a jump the playlist signals, listed as a fact.
    const Axis refused = map_refused(r);
    CHECK(refused.checked == "report-only");
    CHECK(printed_runs(refused, AxisRun::Test::ReportGrid) ==
          Runs{ "frames 125-199 (75 frames), largest +3.000 s at frame 125" });
    REQUIRE(refused.runs.size() == 1);
    CHECK_FALSE(refused.runs[0].warning);
    CHECK(refused.runs[0].sequence == 1);
    CHECK(refused.warnings.empty());

    // The same with no demux pass at all.
    const hls::MediaTimeline pl = playlist_of(r);
    const Axis no_pass = timeline::build_axis(report(r), nullptr, &pl);
    REQUIRE(no_pass.runs.size() == 1);
    CHECK_FALSE(no_pass.runs[0].warning);
    CHECK(no_pass.warnings.empty());

    // A last EXTINF shorter than its frames, as FFmpeg's hls muxer writes
    // 0.36 s for 10 frames (eval/phase3-survey/sources.md §2), moves no
    // segment: nothing to warn of.
    Reference last;
    for (std::int64_t i = 0; i < 60; ++i) last.pts.push_back(kOrigin + i * kFrame25);
    last.seg_frames = { 25, 25, 10 };
    last.extinf     = { "1.000000", "1.000000", "0.360000" };
    last.disc       = { false, false, false };
    const Axis muxer = with_map(last);
    CHECK(muxer.runs.empty());
    CHECK(muxer.seconds(59) == 2.36);
}

TEST_CASE("timeline axis: the check warns at a missing frame, at an ad at another rate and at an "
          "EXTINF a frame too long",
          "[timeline][timeline_axis]")
{
    const Reference r = ad_shape();
    using T = AxisRun::Test;

    SECTION("reference frame 40 missing") {
        const Axis axis = with_map(without_frame(r, 40));
        CHECK(printed_runs(axis, T::Clocks) ==
              Runs{ "frames 40-198 (159 frames), largest -0.040 s at frame 40" });
        CHECK(printed_runs(axis, T::PlaylistGrid) ==
              Runs{ "frames 40-73 (34 frames), largest +0.040 s at frame 40" });
        REQUIRE(axis.runs.size() == 2);
        CHECK(axis.runs[1].sequence == 0);
        CHECK(axis.runs[0].warning);
        CHECK(axis.runs[1].warning);
        REQUIRE(axis.warnings.size() == 2);
        CHECK(axis.warnings[0] == "reference frames 40 to 198 (159 scored): the frame grid's seconds "
                                  "differ from the playlist time by up to -0.040 s, at frame 40");
        CHECK(axis.warnings[1] == "reference frames 40 to 73 (34 scored): the PTS differ from the "
                                  "frame grid of the playlist's discontinuity sequence 0 by up to "
                                  "+0.040 s, at frame 40");
        // The playlist time stays right after the missing frame; the frame
        // grid is a frame early.
        CHECK(axis.seconds(40) == 1.64);
    }
    SECTION("reference frame 180 missing, in the last segment") {
        const Axis axis = with_map(without_frame(r, 180));
        CHECK(printed_runs(axis, T::Clocks) ==
              Runs{ "frames 180-198 (19 frames), largest -0.040 s at frame 180" });
        CHECK(printed_runs(axis, T::PlaylistGrid) ==
              Runs{ "frames 180-198 (19 frames), largest +0.040 s at frame 180" });
        REQUIRE(axis.runs.size() == 2);
        CHECK(axis.runs[1].sequence == 2);
    }
    SECTION("the ad at 24 fps after its discontinuity") {
        const Axis axis = with_map(retimed(r, 75, 124, 24));
        CHECK(printed_runs(axis, T::Clocks) ==
              Runs{ "frames 88-199 (112 frames), largest -0.083 s at frame 125" });
        CHECK(printed_runs(axis, T::PlaylistGrid) ==
              Runs{ "frames 88-124 (37 frames), largest +0.082 s at frame 124" });
    }
    SECTION("the ad at 30 fps after its discontinuity") {
        const Axis axis = with_map(retimed(r, 75, 124, 30));
        CHECK(printed_runs(axis, T::Clocks) ==
              Runs{ "frames 79-199 (121 frames), largest +0.333 s at frame 125" });
        CHECK(printed_runs(axis, T::PlaylistGrid) ==
              Runs{ "frames 79-124 (46 frames), largest -0.327 s at frame 124" });
    }
    SECTION("the ad at 25 fps again: nothing") {
        CHECK(with_map(retimed(r, 75, 124, 25)).runs.empty());
    }
    SECTION("segment 1's EXTINF a frame too long: the clocks part, the PTS keep to the grid") {
        Reference longer = r;
        longer.extinf[1] = "1.040";
        const Axis axis = with_map(longer);
        CHECK(printed_runs(axis, T::Clocks) ==
              Runs{ "frames 50-199 (150 frames), largest -0.040 s at frame 50" });
        CHECK(printed_runs(axis, T::PlaylistGrid).empty());
        // Here the playlist time is the one that is a frame late.
        CHECK(axis.seconds(50) == 2.04);
    }
}

TEST_CASE("timeline axis: a frame missing at the start of a playlist discontinuity sequence is "
          "warned by the clocks alone, from the next segment on",
          "[timeline][timeline_axis]")
{
    // review-3/runs/seconds-check-sequence-start.txt's three cases.
    using T = AxisRun::Test;
    const Reference r = ad_shape();
    const std::vector<std::pair<std::int64_t, std::string>> cases = {
        { 0,   "frames 24-198 (175 frames), largest -0.040 s at frame 24" },
        { 75,  "frames 99-198 (100 frames), largest -0.040 s at frame 99" },
        { 125, "frames 149-198 (50 frames), largest -0.040 s at frame 149" },
    };
    for (const auto& [frame, run] : cases) {
        INFO("reference frame " << frame << " missing");
        const Axis axis = with_map(without_frame(r, frame));
        CHECK(printed_runs(axis, T::Clocks) == Runs{ run });
        CHECK(printed_runs(axis, T::PlaylistGrid).empty());
    }
    // Within the segment that lost its first frame, both clocks put the
    // frame after it at the segment's start.
    const Axis first = with_map(without_frame(r, 0));
    CHECK(first.seconds(0) == 0.0);
    CHECK(first.keys[0].pts == kOrigin + kFrame25);
}

TEST_CASE("timeline axis: a reference that isn't a playlist is warned where its PTS change rate",
          "[timeline][timeline_axis]")
{
    // A mezzanine of 200 frames at 25 fps in milliseconds: nothing.
    Reference mezzanine;
    mezzanine.time_base = { 1, 1000 };
    for (std::int64_t i = 0; i < 200; ++i) mezzanine.pts.push_back(10000 + i * 40);
    const Axis calm = timeline::build_axis(report(mezzanine), nullptr, nullptr);
    CHECK(calm.runs.empty());
    CHECK(calm.warnings.empty());
    CHECK(calm.clock == "frame-grid");
    CHECK(calm.checked == "report-only");
    CHECK(calm.seconds(100) == 4.0);

    // Frames 100 on at 30 fps: the PTS leave the 25 fps grid by more than
    // half a frame from frame 104 on, 4 * (1/25 - 1/30) s being 0.027 s.
    Reference faster;
    for (std::int64_t i = 0; i < 200; ++i)
        faster.pts.push_back(i < 100 ? kOrigin + i * kFrame25 : kOrigin + 100 * kFrame25 + (i - 100) * 3000);
    const timeline::VariantMedia pass = [&] {
        timeline::VariantMedia m;
        m.pts = faster.pts;   // a file: frames and PTS, no segments
        return m;
    }();
    const Axis axis = timeline::build_axis(report(faster), &pass, nullptr);
    CHECK(printed_runs(axis, AxisRun::Test::ReportGrid) ==
          Runs{ "frames 104-199 (96 frames), largest -0.660 s at frame 199" });
    REQUIRE(axis.runs.size() == 1);
    CHECK(axis.runs[0].warning);
    REQUIRE(axis.warnings.size() == 1);
    CHECK(axis.warnings[0] == "reference frames 104 to 199 (96 scored): the PTS differ from the "
                              "frame grid of the report's reference sequence 0 by up to -0.660 s, "
                              "at frame 199");
}

TEST_CASE("timeline axis: a segment the demux pass went without, and maps that don't fit",
          "[timeline][timeline_axis]")
{
    // Segment 2 of five missing from the stream, as the analyzer goes on
    // without a segment it can't open (D-055): the playlist time jumps over
    // its EXTINF and stays right, the frame grid doesn't.
    Reference r;
    for (std::int64_t i = 0; i < 100; ++i)
        r.pts.push_back(kOrigin + (i < 50 ? i : i + 25) * kFrame25);
    r.seg_frames = { 25, 25, 25, 25, 25 };
    r.extinf.assign(5, "1.000");
    r.disc.assign(5, false);
    timeline::VariantMedia m;
    m.pts = r.pts;
    m.segments = { { .seq = 0, .first_index = 0, .frames = 25 }, { .seq = 1, .first_index = 25, .frames = 25 },
                   { .seq = 3, .first_index = 50, .frames = 25 }, { .seq = 4, .first_index = 75, .frames = 25 } };
    const hls::MediaTimeline pl = playlist_of(r);
    const Axis axis = timeline::build_axis(report(r), &m, &pl);
    CHECK(axis.clock == "playlist-time");
    CHECK(axis.seconds(50) == 3.0);
    CHECK(printed_runs(axis, AxisRun::Test::Clocks) ==
          Runs{ "frames 50-99 (50 frames), largest -1.000 s at frame 50" });
    CHECK(printed_runs(axis, AxisRun::Test::PlaylistGrid) ==
          Runs{ "frames 50-99 (50 frames), largest +1.000 s at frame 50" });

    // A map naming a segment the playlist doesn't list: the frame grid, the
    // reason in the warnings, the runs facts.
    timeline::VariantMedia stray = m;
    stray.segments[3].seq = 9;
    const Axis unlisted = timeline::build_axis(report(r), &stray, &pl);
    CHECK(unlisted.clock == "frame-grid");
    REQUIRE_FALSE(unlisted.warnings.empty());
    CHECK(unlisted.warnings[0] == "the segment map names media sequence 9, which the playlist "
                                  "doesn't list: the seconds are the frame grid's");
    for (const AxisRun& run : unlisted.runs) CHECK_FALSE(run.warning);

    // A map with a gap between two segments, and one that covers fewer
    // frames than the report scored.
    timeline::VariantMedia gap = m;
    gap.segments[2].first_index = 51;
    const Axis holed = timeline::build_axis(report(r), &gap, &pl);
    CHECK(holed.clock == "frame-grid");
    CHECK(holed.warnings[0] == "the segment map's media sequence 3 starts at frame 51, not at frame "
                               "50: the seconds are the frame grid's");
    timeline::VariantMedia shorter = m;
    shorter.segments.pop_back();
    shorter.pts.resize(75);
    const Axis partial = timeline::build_axis(report(r), &shorter, &pl);
    CHECK(partial.frames == 100);
    CHECK(partial.clock == "frame-grid");
    CHECK(partial.warnings[0] == "the segment map covers 75 of the reference's 100 frames: the "
                                 "seconds are the frame grid's");
}

TEST_CASE("timeline axis: what the inputs can't give", "[timeline][timeline_axis]")
{
    const Reference r = ad_shape();

    // No complete pair: no time base, no frame rate, no seconds.
    const timeline::LadderScores none;
    const timeline::VariantMedia m = media_of(r);
    const Axis empty = timeline::build_axis(none, &m, nullptr);
    CHECK(empty.frames == 200);
    CHECK(empty.keys[5].pts == kOrigin + 5 * kFrame25);
    CHECK(empty.seconds(100) == 0.0);
    REQUIRE(empty.warnings.size() == 1);
    CHECK(empty.warnings[0] == "the report has no complete pair, so the reference's time base and "
                               "frame rate are unknown: every frame is at 0 s");
    CHECK(timeline::build_axis(none, nullptr, nullptr).frames == 0);

    // An axis without seconds: the index-order search finds frame 0 for
    // seconds at or before 0 s and the end for any later, so that a margin
    // reaches the first frame or the end; no duration has an end.
    CHECK(av_cmp_q(empty.frame_duration, AVRational{ 0, 1 }) == 0);
    CHECK(empty.frame_at_or_after(AVRational{ 0, 1 }) == 0);
    CHECK(empty.frame_at_or_after(AVRational{ 1, 1 }) == 200);
    CHECK(empty.frame_at_or_after(100, AVRational{ -1, 25 }) == 0);
    CHECK(empty.frame_at_or_after(100, AVRational{ 0, 1 }) == 0);
    CHECK(empty.frame_at_or_after(100, AVRational{ 1, 25 }) == 200);
    CHECK_FALSE(empty.frame_after(100, AVRational{ 1, 25 }).has_value());
    CHECK_FALSE(empty.frame_after(100, AVRational{ 0, 1 }).has_value());
    const Axis blank;
    CHECK(blank.frame_at_or_after(AVRational{ 1, 1 }) == 0);
    CHECK(blank.frame_at_or_after(0, AVRational{ 1, 1 }) == 0);
    CHECK_FALSE(blank.frame_after(0, AVRational{ 1, 1 }).has_value());

    // A second pair that gives frame 10 another PTS: the first pair's kept.
    timeline::LadderScores two = report(r, { { 0, 10, 20 }, { 10, 20 } });
    two.pairs[1].ref_pts[0] += 1;
    const Axis differ = timeline::build_axis(two, nullptr, nullptr);
    CHECK(differ.keys[10].pts == kOrigin + 10 * kFrame25);
    REQUIRE(differ.warnings.size() == 1);
    CHECK(differ.warnings[0] == "pair 1 gives 1 reference frame other PTS than an earlier pair, the "
                                "first frame 10: the earlier pair's are kept");

    // A pair that read the reference with another time base.
    timeline::LadderScores other = report(r, { { 0, 10 }, { 20 } });
    other.pairs[1].ref_time_base = { 1, 1000 };
    other.pairs[1].ref_pts[0]    = 10800;
    const Axis apart = timeline::build_axis(other, nullptr, nullptr);
    CHECK_FALSE(apart.keys[20].pts.has_value());
    REQUIRE(apart.warnings.size() == 1);
    CHECK(apart.warnings[0] == "pair 1 reads the reference with time base 1/1000 and frame rate "
                               "25/1, pair 0 with 1/90000 and 25/1: its reference PTS aren't used");

    // The demux pass disagreeing with the report on a scored frame.
    timeline::VariantMedia off = media_of(r);
    off.pts[3] += 1;
    const hls::MediaTimeline pl = playlist_of(r);
    const Axis pass = timeline::build_axis(report(r), &off, &pl);
    CHECK(pass.keys[3].pts == kOrigin + 3 * kFrame25);
    REQUIRE_FALSE(pass.warnings.empty());
    CHECK(pass.warnings[0] == "the demux pass gives 1 scored reference frame other PTS than the "
                              "report, the first frame 3: the report's are kept");
}

TEST_CASE("timeline axis: a scored frame at a position outside the frames its pair read isn't used",
          "[timeline][timeline_axis]")
{
    // Pair 0 scores frames 0 and 10 of the ad shape's 200 frames, all of
    // which it read. Pair 1, whose sequences cover the first 100 frames only,
    // scores frame 20 and frames at positions 150, 200, 2^40, INT64_MAX and
    // -1: none of these five is one of its frames, so none is used, and
    // none sizes the axis.
    const Reference r = ad_shape();
    timeline::LadderScores s = report(r, { { 0, 10 }, { 20, 21, 22, 23, 24, 25 } });
    timeline::PairScores& p = s.pairs[1];
    REQUIRE(p.ref_sequences.size() == 2);
    p.ref_sequences[1].frames -= 100;
    p.ref_index[1] = 150;
    p.ref_index[2] = 200;
    p.ref_index[3] = std::int64_t{ 1 } << 40;
    p.ref_index[4] = std::numeric_limits<std::int64_t>::max();
    p.ref_index[5] = -1;
    const std::string outside = "pair 1 has 5 scored frames outside the 100 reference frames it read, "
                                "the first at position 150: they are not used";

    const Axis bare = timeline::build_axis(s, nullptr, nullptr);
    CHECK(bare.frames == 200);
    REQUIRE(bare.keys.size() == 200);
    REQUIRE(bare.ticks.size() == 200);
    CHECK(bare.keys[20].pts == kOrigin + 20 * kFrame25);
    CHECK_FALSE(bare.keys[21].pts.has_value());
    CHECK_FALSE(bare.keys[150].pts.has_value());
    CHECK(bare.keys[150].sequence == 1);
    CHECK(bare.runs.empty());
    CHECK(bare.warnings == std::vector<std::string>{ outside });

    // With the demux pass and the playlist, the pass gives frame 150 its
    // PTS; the map's seconds, no run, the same warning.
    const timeline::VariantMedia m  = media_of(r);
    const hls::MediaTimeline     pl = playlist_of(r);
    const Axis axis = timeline::build_axis(s, &m, &pl);
    CHECK(axis.frames == 200);
    CHECK(axis.clock == "playlist-time");
    CHECK(axis.keys[150].pts == kOrigin + 150 * kFrame25);
    CHECK(axis.runs.empty());
    CHECK(axis.warnings == std::vector<std::string>{ outside });

    // A pair whose one scored frame is at INT64_MAX: no frame scored.
    timeline::LadderScores lone = report(r, { { 0 } });
    lone.pairs[0].ref_index[0] = std::numeric_limits<std::int64_t>::max();
    const Axis unscored = timeline::build_axis(lone, nullptr, nullptr);
    CHECK(unscored.frames == 200);
    CHECK_FALSE(unscored.keys[0].pts.has_value());
    CHECK(unscored.runs.empty());
    CHECK(unscored.warnings == std::vector<std::string>{
                                   "pair 0 has 1 scored frame outside the 200 reference frames it "
                                   "read, at position 9223372036854775807: it is not used" });
}
