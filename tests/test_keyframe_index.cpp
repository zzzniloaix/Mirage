// KeyframeIndex (keyframes read by the demuxer + HLS segment starts from the
// variant playlist, used once confirmed) and the media playlist parser it
// gets segment durations from. Pure logic; the bipbop-like numbers: 30 fps,
// keyframes every 2 s, 6 s segments, first frame at 10.0333 s (dts 10.0).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "HlsVariants.h"
#include "KeyframeIndex.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

constexpr double kFrame = 1.0 / 30.0;
constexpr double kStart = 10.0 + kFrame;   // first frame pts; its dts is 10.0

// Keyframe k of the bipbop-like stream (every 2 s): pts, dts.
double kf_pts(int k) { return kStart + 2.0 * k; }
double kf_dts(int k) { return 10.0 + 2.0 * k; }

std::vector<double> pts_of(const std::vector<KeyframeIndex::Keyframe>& v)
{
    std::vector<double> out;
    for (const auto& k : v) out.push_back(k.pts);
    return out;
}

// Stream 3 with five 6 s segments set up as at start().
void bipbop_index(KeyframeIndex& idx, bool with_segments = true)
{
    idx.set_hls(true);
    idx.set_stream(3, kStart, kFrame);
    if (with_segments) idx.set_segments(3, { 6.0, 6.0, 6.0, 6.0, 6.0 });
}

} // namespace

// ── parse_media_playlist ─────────────────────────────────────────────────────

TEST_CASE("parse_media_playlist: EXTINF durations and playlist flags", "[keyframes][hls]")
{
    const std::string vod =
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-VERSION:7\n#EXT-X-MEDIA-SEQUENCE:1\n"
        "#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-INDEPENDENT-SEGMENTS\n#EXT-X-MAP:URI=\"main.mp4\",BYTERANGE=\"718@0\"\n"
        "#EXTINF:6.00000,\t\n#EXT-X-BYTERANGE:830431@718\nmain.mp4\n"
        "#EXTINF:6.00000,\t\n#EXT-X-BYTERANGE:827983@831149\nmain.mp4\n"
        "#EXTINF:4.5,title\nmain.mp4\n#EXT-X-ENDLIST\n";
    const hls::MediaPlaylist p = hls::parse_media_playlist(vod);
    REQUIRE(p.durations.size() == 3);
    CHECK(p.durations[0] == 6.0);
    CHECK(p.durations[2] == 4.5);
    CHECK(p.vod);
    CHECK(p.independent);
    CHECK_FALSE(p.discontinuity);
    CHECK_FALSE(p.gap);

    SECTION("live: no ENDLIST / VOD type") {
        const auto live = hls::parse_media_playlist("#EXTM3U\n#EXTINF:6,\na.ts\n#EXTINF:6,\nb.ts\n");
        CHECK(live.durations.size() == 2);
        CHECK_FALSE(live.vod);
    }
    SECTION("ENDLIST alone marks it complete; CRLF and a BOM are fine") {
        const auto e = hls::parse_media_playlist("\xEF\xBB\xBF#EXTM3U\r\n#EXTINF:2.002,\r\na.ts\r\n#EXT-X-ENDLIST\r\n");
        REQUIRE(e.durations.size() == 1);
        CHECK(e.durations[0] == 2.002);
        CHECK(e.vod);
    }
    SECTION("discontinuities and gaps are reported") {
        const auto d = hls::parse_media_playlist(
            "#EXTM3U\n#EXTINF:6,\na.ts\n#EXT-X-DISCONTINUITY\n#EXTINF:6,\nb.ts\n#EXT-X-GAP\n#EXTINF:6,\nc.ts\n#EXT-X-ENDLIST\n");
        CHECK(d.durations.size() == 3);
        CHECK(d.discontinuity);
        CHECK(d.gap);
    }
    SECTION("not a media playlist") {
        CHECK(hls::parse_media_playlist("").durations.empty());
        CHECK(hls::parse_media_playlist("#EXTINF:6,\na.ts\n").durations.empty());   // no header
        CHECK(hls::parse_media_playlist("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nv.m3u8\n#EXTINF:6,\na.ts\n")
                  .durations.empty());                                                 // a master
        CHECK(hls::parse_media_playlist("#EXTM3U\n#EXTINF:abc,\na.ts\n").durations.empty());
        CHECK(hls::parse_media_playlist("#EXTM3U\n#EXTINF:,\na.ts\n").durations.empty());
        CHECK(hls::parse_media_playlist("#EXTM3U\n#EXT-X-ENDLIST\n").durations.empty());
    }
}

// ── KeyframeIndex ────────────────────────────────────────────────────────────

TEST_CASE("KeyframeIndex: keyframes read are listed with the seek that lands on them", "[keyframes]")
{
    KeyframeIndex idx;
    bipbop_index(idx, /*with_segments*/ false);
    const auto v0 = idx.version();
    // The open reads from segment 0; then the rest of it.
    idx.observe(3, kf_pts(0), kf_dts(0), true, 1, true);
    idx.observe(3, kf_pts(1), kf_dts(1), true, 1, false);
    idx.observe(3, kf_pts(2), kf_dts(2), true, 1, false);
    CHECK(idx.version() > v0);
    const auto l = idx.list(3);
    REQUIRE(l.size() == 3);
    for (int k = 0; k < 3; ++k) {
        CHECK_THAT(l[k].pts, WithinAbs(kf_pts(k), 1e-9));
        // HLS: forward to its dts — a backward seek would land on the segment's start.
        CHECK(l[k].at_or_after);
        CHECK_THAT(l[k].seek_s, WithinAbs(kf_dts(k), 1e-9));
        CHECK_FALSE(l[k].predicted);
    }
    // A keyframe read again (seek back) is not listed twice.
    idx.observe(3, kf_pts(1), kf_dts(1), true, 2, true);
    CHECK(idx.list(3).size() == 3);
    CHECK(idx.list(7).empty());   // other streams have their own lists

    SECTION("not HLS: backward seek to the pts, like container-index keyframes") {
        idx.set_hls(false);
        for (const auto& k : idx.list(3)) {
            CHECK_FALSE(k.at_or_after);
            CHECK(k.seek_s == k.pts);
        }
    }
}

TEST_CASE("KeyframeIndex: next / prev step from the frame on screen", "[keyframes]")
{
    KeyframeIndex idx;
    bipbop_index(idx, false);
    for (int k = 0; k < 4; ++k) idx.observe(3, kf_pts(k), kf_dts(k), true, 1, k == 0);
    CHECK_FALSE(idx.next(9, 0.0));   // unknown stream
    REQUIRE(idx.next(3, 0.0));
    CHECK_THAT(idx.next(3, 0.0)->pts, WithinAbs(kf_pts(0), 1e-9));
    // On a keyframe (1 ms tolerance): the next / previous one, not itself.
    CHECK_THAT(idx.next(3, kf_pts(1) - 0.0005)->pts, WithinAbs(kf_pts(2), 1e-9));
    CHECK_THAT(idx.prev(3, kf_pts(2) + 0.0005)->pts, WithinAbs(kf_pts(1), 1e-9));
    // Between keyframes.
    CHECK_THAT(idx.next(3, 13.0)->pts, WithinAbs(kf_pts(2), 1e-9));
    CHECK_THAT(idx.prev(3, 13.0)->pts, WithinAbs(kf_pts(1), 1e-9));
    CHECK_FALSE(idx.next(3, kf_pts(3)));
    CHECK_FALSE(idx.prev(3, kf_pts(0)));
}

TEST_CASE("KeyframeIndex: playlist segment starts are listed once a read confirms them", "[keyframes][hls]")
{
    KeyframeIndex idx;
    bipbop_index(idx);
    // The open: segment 0's keyframes. Its start is the stream's start time —
    // no evidence the EXTINF timeline is right.
    for (int k = 0; k < 3; ++k) idx.observe(3, kf_pts(k), kf_dts(k), true, 1, k == 0);
    CHECK_FALSE(idx.status(3).trusted);
    CHECK(idx.list(3).size() == 3);

    // Reading on reaches segment 1's start (16.0333) where predicted.
    idx.observe(3, kf_pts(3), kf_dts(3), true, 1, false);
    const auto st = idx.status(3);
    CHECK(st.trusted);
    CHECK(st.aligned);
    CHECK(st.confirmed == 1);
    CHECK(st.contradicted == 0);
    CHECK(st.segments == 5);
    CHECK(st.predicted == 3);   // segments 2..4 not read yet
    const auto l = idx.list(3);
    const std::vector<double> want{ kf_pts(0), kf_pts(1), kf_pts(2), kf_pts(3),
                                    kStart + 12.0, kStart + 18.0, kStart + 24.0 };
    REQUIRE(l.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) CHECK_THAT(l[i].pts, WithinAbs(want[i], 1e-9));
    // A predicted start: backward seek to its pts lands on the segment's first frame.
    CHECK(l[4].predicted);
    CHECK_FALSE(l[4].at_or_after);
    CHECK_THAT(l[4].seek_s, WithinAbs(kStart + 12.0, 1e-9));
    // Stepping past what was read goes to the next segment start.
    CHECK_THAT(idx.next(3, kf_pts(3))->pts, WithinAbs(kStart + 12.0, 1e-9));

    // Read later (a seek there): the keyframe read replaces the prediction.
    idx.observe(3, kStart + 12.0, 10.0 + 12.0, true, 2, true);
    const auto l2 = idx.list(3);
    REQUIRE(l2.size() == want.size());
    CHECK_FALSE(l2[4].predicted);
    CHECK(l2[4].at_or_after);
    CHECK(idx.status(3).confirmed == 2);
}

TEST_CASE("KeyframeIndex: a switched-to stream's landing confirms its segment starts", "[keyframes][hls]")
{
    // 60 fps variant: first frame one 60 fps frame after its dts.
    KeyframeIndex idx;
    idx.set_hls(true);
    const double f60 = 1.0 / 60.0;
    idx.set_stream(5, 10.0 + f60, f60);
    idx.set_segments(5, std::vector<double>(100, 6.0));
    // The switch repositions to a segment start (16.0167).
    idx.observe(5, 16.0 + f60, 16.0, true, 7, true);
    CHECK(idx.status(5).trusted);
    CHECK(idx.list(5).size() == 100);
    CHECK_THAT(idx.prev(5, 16.0 + f60)->pts, WithinAbs(10.0 + f60, 1e-9));
    CHECK_THAT(idx.next(5, 16.0 + f60)->pts, WithinAbs(22.0 + f60, 1e-9));
}

TEST_CASE("KeyframeIndex: segment starts the reads contradict are not listed", "[keyframes][hls]")
{
    KeyframeIndex idx;
    bipbop_index(idx);
    idx.observe(3, kf_pts(0), kf_dts(0), true, 1, true);

    SECTION("a run read across a predicted start without a keyframe there") {
        // Keyframes at 10.03 and 17.03 in one run: 16.03 should have had one.
        idx.observe(3, kStart + 7.0, 17.0, true, 1, false);
        const auto st = idx.status(3);
        CHECK_FALSE(st.trusted);
        CHECK(st.contradicted == 1);
        CHECK(idx.list(3).size() == 2);
        // Even a later match does not restore it.
        idx.observe(3, kStart + 12.0, 22.0, true, 2, true);
        CHECK_FALSE(idx.status(3).trusted);
        CHECK(idx.list(3).size() == 3);
    }
    SECTION("keyframes of different runs leave the start between them open") {
        idx.observe(3, kStart + 7.0, 17.0, true, 2, false);
        idx.observe(3, kf_pts(3), kf_dts(3), true, 3, false);   // 16.03 read later, matches
        CHECK(idx.status(3).trusted);
    }
    SECTION("a read landing on a segment start elsewhere") {
        idx.observe(3, kStart + 3.0, 13.0, true, 2, true);
        CHECK(idx.status(3).contradicted == 1);
        CHECK_FALSE(idx.status(3).trusted);
    }
    SECTION("a segment start that is not a keyframe") {
        idx.observe(3, kf_pts(3), kf_dts(3), true, 1, false);
        REQUIRE(idx.status(3).trusted);
        idx.observe(3, kStart + 12.0, 22.0, false, 2, true);
        CHECK_FALSE(idx.status(3).trusted);
        CHECK(idx.list(3).size() == 2);   // the two keyframes read
    }
    SECTION("within half a frame is the same frame") {
        idx.observe(3, kf_pts(3) + 0.4 * kFrame, kf_dts(3), true, 1, false);
        CHECK(idx.status(3).trusted);
    }
}

TEST_CASE("KeyframeIndex: durations that are not whole frames are never predicted", "[keyframes][hls]")
{
    KeyframeIndex idx;
    idx.set_hls(true);
    const double f2997 = 1001.0 / 30000.0;
    idx.set_stream(0, 0.0, f2997);
    idx.set_segments(0, { 6.0, 6.0, 6.0 });   // really 6.006 s each: rounded
    idx.observe(0, 0.0, 0.0, true, 1, true);
    idx.observe(0, 6.0, 6.0, true, 1, false);
    CHECK_FALSE(idx.status(0).aligned);
    CHECK_FALSE(idx.status(0).trusted);
    CHECK(idx.list(0).size() == 2);

    SECTION("unknown start time or frame rate") {
        KeyframeIndex n;
        n.set_hls(true);
        n.set_stream(1, std::nan(""), kFrame);
        n.set_segments(1, { 6.0, 6.0 });
        CHECK(n.status(1).segments == 2);
        CHECK_FALSE(n.status(1).aligned);
        KeyframeIndex z;
        z.set_stream(2, 10.0, 0.0);
        z.set_segments(2, { 6.0, 6.0 });
        CHECK_FALSE(z.status(2).aligned);
    }
}

TEST_CASE("KeyframeIndex: tolerance is half a frame, at least 2 ms", "[keyframes]")
{
    CHECK_THAT(KeyframeIndex::tolerance(1.0 / 30.0), WithinAbs(1.0 / 60.0, 1e-12));
    CHECK_THAT(KeyframeIndex::tolerance(0.001), WithinAbs(0.002, 1e-12));
    CHECK(KeyframeIndex::tolerance(0.0) > 0.0);
}

TEST_CASE("KeyframeIndex: at_or_before is where decoding a frame can start", "[keyframes]")
{
    KeyframeIndex idx;
    bipbop_index(idx, false);
    for (int k = 0; k < 3; ++k) idx.observe(3, kf_pts(k), kf_dts(k), true, 1, k == 0);
    CHECK_FALSE(idx.at_or_before(3, kf_pts(0) - 0.5));
    CHECK_THAT(idx.at_or_before(3, kf_pts(1))->pts, WithinAbs(kf_pts(1), 1e-9));            // itself
    CHECK_THAT(idx.at_or_before(3, kf_pts(1) - 0.0005)->pts, WithinAbs(kf_pts(1), 1e-9));   // 1 ms tolerance
    CHECK_THAT(idx.at_or_before(3, kf_pts(2) - 0.01)->pts, WithinAbs(kf_pts(1), 1e-9));
    CHECK_THAT(idx.at_or_before(3, 100.0)->pts, WithinAbs(kf_pts(2), 1e-9));
}

TEST_CASE("KeyframeIndex: a stream that isn't tracked ignores what is read", "[keyframes]")
{
    KeyframeIndex idx;
    idx.set_stream(0, 0.0, 1.0 / 24.0, /*tracked*/ false);
    const auto v = idx.version();
    idx.observe(0, 5.088, 5.0, true, 1, false);
    CHECK(idx.list(0).empty());
    CHECK(idx.version() == v);
    idx.set_stream(0, 0.0, 1.0 / 24.0, true);
    idx.observe(0, 5.088, 5.0, true, 1, false);
    CHECK(idx.list(0).size() == 1);
}

// ── Steps: certain only where the stream was read through ────────────────────

namespace {

// One reader's run over the bipbop-like stream: every frame from pts `a` to
// `b` (inclusive) in decode order, dts one frame before pts, a keyframe
// every 2 s. segment_start: the run began on a segment start.
void read_run(KeyframeIndex& idx, std::uint32_t run, double a, double b, bool segment_start)
{
    const long n0 = std::lround((a - kStart) / kFrame), n1 = std::lround((b - kStart) / kFrame);
    for (long n = n0; n <= n1; ++n) {
        const double pts = kStart + static_cast<double>(n) * kFrame;
        idx.observe(3, pts, pts - kFrame, n % 60 == 0, run, n == n0 && segment_start);
    }
}

} // namespace

TEST_CASE("KeyframeIndex::step: back into a segment not read is not certain until it is read", "[keyframes][hls]")
{
    KeyframeIndex idx;
    bipbop_index(idx);
    // The open reads segment 0 and on into segment 1: its start confirmed.
    read_run(idx, idx.new_run(), kStart, kStart + 6.5, true);
    REQUIRE(idx.status(3).trusted);
    // A seek lands on segment 3's start (28.03) and reads a second of it.
    const double t = kStart + 18.0;
    read_run(idx, idx.new_run(), t, t + 1.0, true);

    // , from 28.03: of segment 2 only its start (22.03) is known.
    KeyframeIndex::Step q = idx.step(3, t, false);
    REQUIRE(q.kf);
    CHECK_THAT(q.kf->pts, WithinAbs(kStart + 12.0, 1e-9));
    CHECK(q.kf->predicted);
    CHECK_FALSE(q.certain);
    // Read from that start (backward seek to its pts: lands on the segment
    // start) up to the frame stepped from.
    CHECK_THAT(q.probe.seek_s, WithinAbs(kStart + 12.0, 1e-9));
    CHECK_FALSE(q.probe.at_or_after);
    CHECK(q.probe.segment_start);
    CHECK_FALSE(q.probe.forward);
    CHECK_THAT(q.probe.until, WithinAbs(t, 1e-9));
    CHECK_THAT(q.probe.frame_s, WithinAbs(kFrame, 1e-12));

    // The probe reads segment 2 until dts >= t - tol: its keyframes at 24.03
    // and 26.03 are known, and the step goes to 26.03, not 22.03.
    read_run(idx, idx.new_run(), kStart + 12.0, t + kFrame, true);
    q = idx.step(3, t, false);
    REQUIRE(q.kf);
    CHECK(q.certain);
    CHECK_THAT(q.kf->pts, WithinAbs(kStart + 16.0, 1e-9));
    CHECK(q.kf->at_or_after);   // inside its segment: forward to its dts
    // On back through what was read: certain at once.
    for (const double k : { kStart + 16.0, kStart + 14.0 }) {
        q = idx.step(3, k, false);
        REQUIRE(q.kf);
        CHECK(q.certain);
        CHECK_THAT(q.kf->pts, WithinAbs(k - 2.0, 1e-9));
    }
    // From 22.03 back into segment 1, read only to 16.5 at the open: not
    // certain; read on from 16.03, the last keyframe read before (forward to
    // its dts), up to 22.03.
    q = idx.step(3, kStart + 12.0, false);
    REQUIRE(q.kf);
    CHECK_FALSE(q.certain);
    CHECK_THAT(q.kf->pts, WithinAbs(kStart + 6.0, 1e-9));
    CHECK(q.probe.at_or_after);
    CHECK_THAT(q.probe.seek_s, WithinAbs(kStart + 6.0 - kFrame, 1e-9));
    CHECK_FALSE(q.probe.segment_start);
    CHECK(idx.status(3).contradicted == 0);
}

TEST_CASE("KeyframeIndex::step: forward past what was read is not certain", "[keyframes][hls]")
{
    KeyframeIndex idx;
    bipbop_index(idx);
    read_run(idx, idx.new_run(), kStart, kStart + 6.5, true);
    REQUIRE(idx.status(3).trusted);
    const double t = kStart + 18.0;
    const std::uint32_t run = idx.new_run();
    read_run(idx, run, t, t + 1.0, true);   // landed; 30.03 not read yet

    // . from 28.03: only segment 4's start (34.03) is known after it.
    KeyframeIndex::Step q = idx.step(3, t, true);
    REQUIRE(q.kf);
    CHECK(q.kf->predicted);
    CHECK_THAT(q.kf->pts, WithinAbs(kStart + 24.0, 1e-9));
    CHECK_FALSE(q.certain);
    // Read from the keyframe on screen (forward to its dts) to the next one.
    CHECK(q.probe.forward);
    CHECK(q.probe.at_or_after);
    CHECK_THAT(q.probe.seek_s, WithinAbs(t - kFrame, 1e-9));
    CHECK_FALSE(q.probe.segment_start);

    // The demuxer reads on to 30.03 in the same run: certain.
    read_run(idx, run, t + 1.0 + kFrame, t + 2.0, false);
    q = idx.step(3, t, true);
    REQUIRE(q.kf);
    CHECK(q.certain);
    CHECK_THAT(q.kf->pts, WithinAbs(t + 2.0, 1e-9));

    SECTION("from a frame between keyframes") {
        q = idx.step(3, t + 0.5, true);
        CHECK(q.certain);
        CHECK_THAT(q.kf->pts, WithinAbs(t + 2.0, 1e-9));
        // Back: the frames between the keyframe and t were read, no keyframe
        // after it yet — still certain.
        q = idx.step(3, t + 0.5, false);
        CHECK(q.certain);
        CHECK_THAT(q.kf->pts, WithinAbs(t, 1e-9));
    }
}

TEST_CASE("KeyframeIndex: readers interleaving keep their runs apart", "[keyframes][hls]")
{
    KeyframeIndex idx;
    bipbop_index(idx);
    read_run(idx, idx.new_run(), kStart, kStart + 6.5, true);
    REQUIRE(idx.status(3).trusted);
    // The demuxer reads forward from 28.03 while a probe reads 22.03 → 28.03,
    // packet by packet in turn.
    const std::uint32_t demux = idx.new_run(), probe = idx.new_run();
    const double t = kStart + 18.0;
    for (int n = 0; n <= 180; ++n) {
        const double pd = t + n * kFrame, pp = kStart + 12.0 + n * kFrame;
        idx.observe(3, pd, pd - kFrame, n % 60 == 0, demux, n == 0);
        if (pp <= t + kFrame + 1e-9) idx.observe(3, pp, pp - kFrame, n % 60 == 0, probe, n == 0);
    }
    const auto st = idx.status(3);
    CHECK(st.trusted);
    CHECK(st.contradicted == 0);
    const std::vector<double> got = pts_of(idx.list(3));
    for (int k = 6; k <= 12; ++k)   // 22.03 .. 34.03, every keyframe read
        CHECK(std::any_of(got.begin(), got.end(), [&](double p) { return std::abs(p - kf_pts(k)) < 1e-9; }));
    CHECK(idx.step(3, t, false).certain);
    CHECK(idx.step(3, t, true).certain);

    SECTION("a run crossing a predicted start without a keyframe is still caught") {
        // Keyframes at 31.03 and 35.03 in one run, another reader's in
        // between: the start at 34.03 has none.
        const std::uint32_t r = idx.new_run();
        idx.observe(3, kStart + 21.0, kStart + 21.0 - kFrame, true, r, false);
        idx.observe(3, kStart + 26.0, kStart + 26.0 - kFrame, true, demux, false);
        idx.observe(3, kStart + 25.0, kStart + 25.0 - kFrame, true, r, false);
        CHECK(idx.status(3).contradicted == 1);
        CHECK_FALSE(idx.status(3).trusted);
    }
}

TEST_CASE("KeyframeIndex::step: where nothing before the frame is known", "[keyframes]")
{
    SECTION("the stream's first frame: certain, no keyframe before") {
        KeyframeIndex idx;
        bipbop_index(idx, false);
        read_run(idx, idx.new_run(), kStart, kStart + 1.0, true);
        const auto q = idx.step(3, kStart, false);
        CHECK_FALSE(q.kf);
        CHECK(q.certain);
    }
    SECTION("later: read from where a backward seek before it lands") {
        KeyframeIndex idx;
        bipbop_index(idx, false);
        const double t = kStart + 18.0;
        read_run(idx, idx.new_run(), t, t + 1.0, true);
        const auto q = idx.step(3, t, false);
        CHECK_FALSE(q.kf);
        CHECK_FALSE(q.certain);
        CHECK_THAT(q.probe.seek_s, WithinAbs(t - 1.5 * kFrame, 1e-9));
        CHECK_FALSE(q.probe.at_or_after);
        CHECK(q.probe.segment_start);   // HLS: lands on a segment start
    }
    SECTION("a keyframe known long before: a backward seek reads less") {
        KeyframeIndex idx;
        bipbop_index(idx, false);
        read_run(idx, idx.new_run(), kStart, kStart + 0.5, true);
        const double t = kStart + 30.0;
        read_run(idx, idx.new_run(), t, t + 1.0, true);
        const auto q = idx.step(3, t, false);
        REQUIRE(q.kf);
        CHECK_THAT(q.kf->pts, WithinAbs(kStart, 1e-9));
        CHECK_FALSE(q.certain);
        CHECK_THAT(q.probe.seek_s, WithinAbs(t - 1.5 * kFrame, 1e-9));
    }
    SECTION("an unknown stream") {
        KeyframeIndex idx;
        const auto q = idx.step(9, 5.0, true);
        CHECK_FALSE(q.kf);
        CHECK_FALSE(q.certain);
        CHECK_THAT(q.probe.seek_s, WithinAbs(5.0, 1e-9));
        CHECK(q.probe.forward);
    }
    SECTION("not HLS: a backward seek to a keyframe's pts lands on it") {
        KeyframeIndex idx;
        idx.set_stream(0, 0.0, 1.0 / 24.0);
        const std::uint32_t a = idx.new_run(), b = idx.new_run();
        idx.observe(0, 5.088, 5.0, true, a, false);
        idx.observe(0, 8.3, 8.2, true, b, false);
        const auto q = idx.step(0, 8.3, false);
        REQUIRE(q.kf);
        CHECK_FALSE(q.certain);   // read in different runs: 5.088 .. 8.3 not read through
        CHECK_THAT(q.probe.seek_s, WithinAbs(5.088, 1e-9));
        CHECK_FALSE(q.probe.at_or_after);
        CHECK_FALSE(q.probe.segment_start);
    }
}

TEST_CASE("KeyframeIndex: run ids are unique", "[keyframes]")
{
    KeyframeIndex idx;
    const std::uint32_t a = idx.new_run(), b = idx.new_run();
    CHECK(a != 0);
    CHECK(b != a);
}

TEST_CASE("KeyframeIndex: many runs are folded, what they read through is kept", "[keyframes]")
{
    KeyframeIndex idx;
    bipbop_index(idx, false);
    // 3000 short runs (scrub requests): each reads a keyframe and a frame.
    for (int i = 0; i < 3000; ++i) {
        const std::uint32_t r = idx.new_run();
        const double p = kStart + 2.0 * (i % 5);
        idx.observe(3, p, p - kFrame, true, r, false);
        idx.observe(3, p + kFrame, p, false, r, false);
    }
    // One run reads 20.03 .. 26.03 through, then thousands more runs come.
    const std::uint32_t through = idx.new_run();
    read_run(idx, through, kStart + 10.0, kStart + 16.0, false);
    for (int i = 0; i < 3000; ++i)
        idx.observe(3, kStart, kStart - kFrame, true, idx.new_run(), false);
    const auto q = idx.step(3, kStart + 16.0, false);
    REQUIRE(q.kf);
    CHECK(q.certain);
    CHECK_THAT(q.kf->pts, WithinAbs(kStart + 14.0, 1e-9));
    CHECK(idx.list(3).size() == 9);   // 10.03 .. 18.03 and 20.03 .. 26.03, every 2 s
}
