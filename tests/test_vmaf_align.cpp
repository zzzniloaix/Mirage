// vmafalign::Aligner (src/VMAFAlign.h) on timestamp sequences, without media:
// the cases of docs/plans/frame-alignment.md, "Tests" (equal streams, a
// dropped distorted frame, a dropped reference frame, a duplicated distorted
// timestamp, a distorted input at half and at double the reference's rate, a
// later and an earlier start, a backward and a forward jump in one input or both, frames without
// timestamps), each with the pairs, the frames dropped and the counts it
// should give, and the rules D-028 leaves to the implementation.

#include <catch2/catch_test_macros.hpp>

#include "VMAFAlign.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using vmafalign::Aligner;
using vmafalign::Clock;
using vmafalign::Failure;
using vmafalign::Input;
using vmafalign::Reason;
using vmafalign::Step;
using vmafalign::kNoTimestamp;

namespace {

constexpr Input kRef = Input::Reference;
constexpr Input kDis = Input::Distorted;

// Matroska keeps milliseconds: at 25 fps frame k is at 40 k, and the
// tolerance is 20.
const Clock kMkv25{ .time_base = { 1, 1000 }, .frame_rate = { 25, 1 }, .start = 0 };

// `n` timestamps from `first`, `step` apart.
std::vector<int64_t> frames(int n, int64_t step = 40, int64_t first = 0)
{
    std::vector<int64_t> out;
    for (int k = 0; k < n; ++k) out.push_back(first + k * step);
    return out;
}

// `v` without the element at `i`.
std::vector<int64_t> without(std::vector<int64_t> v, std::size_t i)
{
    v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));
    return v;
}

// What an Aligner did with two inputs, fed as the analyzer feeds it: a frame
// of an input whenever it asks for one.
struct Run {
    std::vector<std::pair<int64_t, int64_t>> pairs;   // (reference index, distorted index)
    std::vector<std::size_t> new_contexts;            // the pairs that start a libvmaf context
    std::vector<int64_t> dropped_ref, dropped_dis;
    std::size_t ref_read = 0, dis_read = 0;           // frames handed to it
    bool done = false;
    vmafalign::Counts    counts;
    std::vector<Failure> failures;
};

Run align(const std::vector<int64_t>& ref, const std::vector<int64_t>& dis,
          const Clock& ref_clock = kMkv25, const Clock& dis_clock = kMkv25,
          std::size_t max_pairs = 0)
{
    Aligner a(ref_clock, dis_clock);
    REQUIRE(a.ok());
    Run out;
    for (int guard = 0; guard < 1000000 && !out.done; ++guard) {
        if (max_pairs && out.pairs.size() == max_pairs) break;
        const Step s = a.next();
        switch (s.kind) {
        case Step::Kind::NeedReference:
            if (out.ref_read < ref.size()) a.push(kRef, ref[out.ref_read++]);
            else a.end(kRef);
            break;
        case Step::Kind::NeedDistorted:
            if (out.dis_read < dis.size()) a.push(kDis, dis[out.dis_read++]);
            else a.end(kDis);
            break;
        case Step::Kind::Pair:
            if (s.new_context) out.new_contexts.push_back(out.pairs.size());
            out.pairs.emplace_back(s.ref_index, s.dis_index);
            break;
        case Step::Kind::DropReference: out.dropped_ref.push_back(s.ref_index); break;
        case Step::Kind::DropDistorted: out.dropped_dis.push_back(s.dis_index); break;
        case Step::Kind::Done:          out.done = true; break;
        }
    }
    out.counts   = a.counts();
    out.failures = a.failures();
    return out;
}

// (k, k) for k in [from, to).
std::vector<std::pair<int64_t, int64_t>> diagonal(int64_t from, int64_t to, int64_t dis_offset = 0)
{
    std::vector<std::pair<int64_t, int64_t>> out;
    for (int64_t k = from; k < to; ++k) out.emplace_back(k, k + dis_offset);
    return out;
}

template <typename T>
std::vector<T> operator+(std::vector<T> a, const std::vector<T>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

bool same(const Failure& f, Input in, int64_t index, int64_t ts, Reason r)
{
    return f.input == in && f.index == index && f.timestamp == ts && f.reason == r;
}

} // namespace

TEST_CASE("vmafalign: equal streams pair frame for frame, without decoding ahead", "[vmafalign]")
{
    const Run r = align(frames(12), frames(12));
    CHECK(r.done);
    CHECK(r.pairs == diagonal(0, 12));
    CHECK(r.dropped_ref.empty());
    CHECK(r.dropped_dis.empty());
    CHECK(r.new_contexts.empty());
    CHECK(r.failures.empty());
    CHECK(r.counts.paired == 12);
    CHECK(r.counts.failed() == 0);
    CHECK(r.counts.failure_rate() == 0.0);
    CHECK_FALSE(r.counts.exceeded());

    // Stopped after 5 pairs, it has asked for 5 frames of each input, no
    // more: an exact match needs no look at the next frame.
    const Run five = align(frames(12), frames(12), kMkv25, kMkv25, 5);
    CHECK(five.pairs == diagonal(0, 5));
    CHECK(five.ref_read == 5);
    CHECK(five.dis_read == 5);
}

TEST_CASE("vmafalign: the clock: grid, tolerance and where times are measured from", "[vmafalign]")
{
    Aligner a(kMkv25, kMkv25);
    REQUIRE(a.ok());
    CHECK(a.grid() == kRef);
    CHECK(av_cmp_q(a.tolerance(), AVRational{ 1, 50 }) == 0);   // half of 1/25 s
    CHECK(a.start(kRef) == 0);
    CHECK(a.start_from_clock(kRef));

    // Without a start time, an input's times count from its first
    // timestamped frame.
    const Clock no_start{ .time_base = { 1, 1000 }, .frame_rate = { 25, 1 } };
    Aligner b(no_start, no_start);
    REQUIRE(b.ok());
    CHECK_FALSE(b.start(kDis).has_value());
    CHECK_FALSE(b.start_from_clock(kDis));
    CHECK(b.next().kind == Step::Kind::NeedReference);   // the grid input's frame first
    b.push(kRef, kNoTimestamp);
    b.push(kDis, 5000);
    CHECK(b.start(kDis) == 5000);
    CHECK_FALSE(b.start(kRef).has_value());   // no timestamped frame yet

    // 30000/1001 fps: the tolerance is 1001/60000 s.
    const Clock ts{ .time_base = { 1, 90000 }, .frame_rate = { 30000, 1001 }, .start = 900000 };
    Aligner c(ts, ts);
    REQUIRE(c.ok());
    CHECK(av_cmp_q(c.tolerance(), AVRational{ 1001, 60000 }) == 0);
}

TEST_CASE("vmafalign: inputs with other time bases and origins that line up", "[vmafalign]")
{
    // An MPEG-TS reference from 10 s at 90 kHz, and a Matroska input from 0
    // in milliseconds, both at 30000/1001 fps: the same frames, the
    // Matroska times rounded to the millisecond.
    const Clock ts{ .time_base = { 1, 90000 }, .frame_rate = { 30000, 1001 }, .start = 900000 };
    const Clock mkv{ .time_base = { 1, 1000 }, .frame_rate = { 30000, 1001 }, .start = 0 };
    std::vector<int64_t> ms;
    for (int k = 0; k < 300; ++k) ms.push_back((k * 1001 + 15) / 30);   // round(k * 1001 / 30)
    const Run r = align(frames(300, 3003, 900000), ms, ts, mkv);
    CHECK(r.pairs == diagonal(0, 300));
    CHECK(r.failures.empty());
    // The other way round as well.
    const Run back = align(ms, frames(300, 3003, 900000), mkv, ts);
    CHECK(back.pairs == diagonal(0, 300));
    CHECK(back.failures.empty());

    // The same times from the first frames when neither container has a
    // start time.
    const Clock ts_nostart{ .time_base = { 1, 90000 }, .frame_rate = { 30000, 1001 } };
    const Clock mkv_nostart{ .time_base = { 1, 1000 }, .frame_rate = { 30000, 1001 } };
    std::vector<int64_t> ms_later;
    for (int64_t t : ms) ms_later.push_back(t + 123456);
    const Run firsts = align(frames(300, 3003, 900000), ms_later, ts_nostart, mkv_nostart);
    CHECK(firsts.pairs == diagonal(0, 300));
    CHECK(firsts.failures.empty());
}

TEST_CASE("vmafalign: a dropped distorted frame leaves one reference frame unmatched", "[vmafalign]")
{
    const Run r = align(frames(12), without(frames(12), 5));
    CHECK(r.done);
    CHECK(r.pairs == diagonal(0, 5) + diagonal(6, 12, -1));
    CHECK(r.dropped_ref == std::vector<int64_t>{ 5 });
    CHECK(r.dropped_dis.empty());
    REQUIRE(r.failures.size() == 1);
    CHECK(same(r.failures[0], kRef, 5, 200, Reason::UnmatchedReference));
    CHECK(r.counts.unmatched_reference == 1);
    CHECK(r.counts.failed() == 1);
    CHECK(r.counts.failure_rate() == 1.0 / 12.0);
    CHECK(r.counts.exceeded());
    // The pair after the gap starts a new libvmaf context.
    CHECK(r.new_contexts == std::vector<std::size_t>{ 5 });
}

TEST_CASE("vmafalign: a dropped reference frame leaves one distorted frame extra", "[vmafalign]")
{
    const Run r = align(without(frames(12), 5), frames(12));
    CHECK(r.pairs == diagonal(0, 5) + diagonal(5, 11, 1));
    CHECK(r.dropped_dis == std::vector<int64_t>{ 5 });
    REQUIRE(r.failures.size() == 1);
    CHECK(same(r.failures[0], kDis, 5, 200, Reason::ExtraDistorted));
    CHECK(r.counts.extra_distorted == 1);
    CHECK(r.counts.failure_rate() == 1.0 / 12.0);
    // The reference frames libvmaf gets are the reference's own, in order:
    // no new context.
    CHECK(r.new_contexts.empty());
}

TEST_CASE("vmafalign: a duplicated distorted timestamp", "[vmafalign]")
{
    std::vector<int64_t> dis = frames(12);
    dis.insert(dis.begin() + 6, 200);   // frame 5's timestamp twice
    const Run r = align(frames(12), dis);
    CHECK(r.pairs == diagonal(0, 6) + diagonal(6, 12, 1));
    CHECK(r.dropped_dis == std::vector<int64_t>{ 6 });
    REQUIRE(r.failures.size() == 1);
    CHECK(same(r.failures[0], kDis, 6, 200, Reason::DuplicateDistorted));
    CHECK(r.counts.duplicate_distorted == 1);
    CHECK(r.counts.extra_distorted == 0);
    CHECK(r.new_contexts.empty());
}

TEST_CASE("vmafalign: a distorted input at half the reference's rate is scored on its grid", "[vmafalign]")
{
    const Clock half{ .time_base = { 1, 1000 }, .frame_rate = { 25, 2 }, .start = 0 };
    const Run r = align(frames(12), frames(6, 80), kMkv25, half);
    CHECK(r.done);
    CHECK(r.pairs == std::vector<std::pair<int64_t, int64_t>>{ { 0, 0 }, { 2, 1 }, { 4, 2 },
                                                               { 6, 3 }, { 8, 4 }, { 10, 5 } });
    CHECK(r.dropped_ref == std::vector<int64_t>{ 1, 3, 5, 7, 9, 11 });
    CHECK(r.failures.empty());
    CHECK(r.counts.off_grid_reference == 6);
    CHECK(r.counts.failure_rate() == 0.0);
    CHECK(r.new_contexts.empty());   // the grid's reference frames have no gap

    Aligner a(kMkv25, half);
    CHECK(a.grid() == kDis);
    CHECK(av_cmp_q(a.tolerance(), AVRational{ 1, 50 }) == 0);   // still the reference's

    // A distorted frame missing from its grid: the reference frame at its
    // point is unmatched, and a new context starts after it.
    const Run drop = align(frames(12), without(frames(6, 80), 3), kMkv25, half);
    CHECK(drop.pairs == std::vector<std::pair<int64_t, int64_t>>{ { 0, 0 }, { 2, 1 }, { 4, 2 },
                                                                  { 8, 3 }, { 10, 4 } });
    REQUIRE(drop.failures.size() == 1);
    CHECK(same(drop.failures[0], kRef, 6, 240, Reason::UnmatchedReference));
    CHECK(drop.counts.off_grid_reference == 6);   // 1, 3, 5, 7, 9, 11
    CHECK(drop.new_contexts == std::vector<std::size_t>{ 3 });
}

TEST_CASE("vmafalign: a distorted input at double the reference's rate is scored on the reference's grid", "[vmafalign]")
{
    const Clock dbl{ .time_base = { 1, 1000 }, .frame_rate = { 50, 1 }, .start = 0 };
    const Run r = align(frames(12), frames(24, 20), kMkv25, dbl);
    CHECK(r.done);
    std::vector<std::pair<int64_t, int64_t>> want;
    for (int64_t k = 0; k < 12; ++k) want.emplace_back(k, 2 * k);
    CHECK(r.pairs == want);
    CHECK(r.dropped_dis.size() == 12);   // every frame halfway between two reference frames
    CHECK(r.failures.empty());
    CHECK(r.counts.off_grid_distorted == 12);
    Aligner a(kMkv25, dbl);
    CHECK(a.grid() == kRef);

    // A reference frame missing from its grid: the distorted frame at its
    // point is extra.
    const Run hole = align(without(frames(12), 5), frames(24, 20), kMkv25, dbl);
    REQUIRE(hole.failures.size() == 1);
    CHECK(same(hole.failures[0], kDis, 10, 200, Reason::ExtraDistorted));
    CHECK(hole.counts.off_grid_distorted == 12);
    CHECK(hole.pairs.size() == 11);
    CHECK(hole.new_contexts.empty());
}

TEST_CASE("vmafalign: a later and an earlier start", "[vmafalign]")
{
    // The distorted input's first three frames are missing from its start:
    // the reference's first three frames are unmatched.
    const Run later = align(frames(12), frames(9, 40, 120));
    CHECK(later.pairs == diagonal(3, 12, -3));
    REQUIRE(later.failures.size() == 3);
    for (int64_t k = 0; k < 3; ++k) CHECK(same(later.failures[k], kRef, k, 40 * k, Reason::UnmatchedReference));
    CHECK(later.new_contexts.empty());   // nothing was scored before them
    CHECK(later.counts.failure_rate() == 3.0 / 12.0);

    // The reference starts three frames later: the distorted input's first
    // three frames are extra.
    const Run earlier = align(frames(9, 40, 120), frames(12));
    CHECK(earlier.pairs == diagonal(0, 9, 3));
    REQUIRE(earlier.failures.size() == 3);
    for (int64_t k = 0; k < 3; ++k) CHECK(same(earlier.failures[k], kDis, k, 40 * k, Reason::ExtraDistorted));

    // Times count from each input's start: two inputs whose frames start at
    // their own start pair from the first, whatever their timestamps.
    const Clock at_5s{ .time_base = { 1, 1000 }, .frame_rate = { 25, 1 }, .start = 5000 };
    const Run starts = align(frames(12), frames(12, 40, 5000), kMkv25, at_5s);
    CHECK(starts.pairs == diagonal(0, 12));
    CHECK(starts.failures.empty());
}

TEST_CASE("vmafalign: one input longer than the other", "[vmafalign]")
{
    const Run longer_ref = align(frames(12), frames(8));
    CHECK(longer_ref.done);
    CHECK(longer_ref.pairs == diagonal(0, 8));
    CHECK(longer_ref.ref_read == 12);   // read to its end, to count what is left
    CHECK(longer_ref.counts.unmatched_reference == 4);
    REQUIRE(longer_ref.failures.size() == 4);
    CHECK(same(longer_ref.failures[0], kRef, 8, 320, Reason::UnmatchedReference));

    const Run longer_dis = align(frames(8), frames(12));
    CHECK(longer_dis.pairs == diagonal(0, 8));
    CHECK(longer_dis.counts.extra_distorted == 4);
    CHECK(same(longer_dis.failures[3], kDis, 11, 440, Reason::ExtraDistorted));
}

TEST_CASE("vmafalign: the nearest frame within the tolerance, and ties", "[vmafalign]")
{
    // Reference slot at 40: distorted frames at 25 and 45 are both within
    // 20 of it; 45 is nearer, so 25 is extra.
    const Run nearer = align({ 0, 40, 80 }, { 0, 25, 45, 80 });
    CHECK(nearer.pairs == std::vector<std::pair<int64_t, int64_t>>{ { 0, 0 }, { 1, 2 }, { 2, 3 } });
    REQUIRE(nearer.failures.size() == 1);
    CHECK(same(nearer.failures[0], kDis, 1, 25, Reason::ExtraDistorted));

    // 30 and 50 are equally near: the earlier pairs.
    const Run tie = align({ 0, 40, 80 }, { 0, 30, 50, 80 });
    CHECK(tie.pairs == std::vector<std::pair<int64_t, int64_t>>{ { 0, 0 }, { 1, 1 }, { 2, 3 } });
    REQUIRE(tie.failures.size() == 1);
    CHECK(same(tie.failures[0], kDis, 2, 50, Reason::ExtraDistorted));

    // At exactly the tolerance a frame still pairs; beyond it, not.
    const Run edge = align({ 0, 40, 80 }, { 20, 61, 80 });
    CHECK(edge.pairs == std::vector<std::pair<int64_t, int64_t>>{ { 0, 0 }, { 2, 2 } });
    CHECK(edge.counts.unmatched_reference == 1);   // 40: 20 is taken, 61 is 21 away
    CHECK(edge.counts.extra_distorted == 1);       // 61, which 80 is nearer to
}

TEST_CASE("vmafalign: each discontinuity sequence pairs with the other input's of the same rank",
          "[vmafalign]")
{
    // Both inputs' timestamps restart at their frame 6, as an HLS ladder's
    // rungs do at an EXT-X-DISCONTINUITY (D-046): two sequences each, paired
    // one with the other, the second in a new libvmaf context.
    std::vector<int64_t> restart = frames(6);
    for (int64_t t : frames(6)) restart.push_back(t);
    const Run both = align(restart, restart);
    CHECK(both.done);
    CHECK(both.pairs == diagonal(0, 12));
    CHECK(both.new_contexts == std::vector<std::size_t>{ 6 });
    CHECK(both.counts.discontinuities == 2);
    CHECK(both.counts.failed() == 0);

    // A forward jump in both, and each sequence measured from its own start:
    // the second sequences start 60 s and 20 s later, and still pair.
    std::vector<int64_t> ref = frames(6), dis = frames(6);
    for (int64_t t : frames(6, 40, 200 + 60000)) ref.push_back(t);
    for (int64_t t : frames(6, 40, 200 + 20000)) dis.push_back(t);
    const Run jumps = align(ref, dis);
    CHECK(jumps.pairs == diagonal(0, 12));
    CHECK(jumps.new_contexts == std::vector<std::size_t>{ 6 });
    CHECK(jumps.counts.discontinuities == 2);
    CHECK(jumps.counts.failed() == 0);

    // Where an input's times are measured from, as the report gives it, stays
    // its start: each sequence's own start is the pairing's alone.
    const Clock no_start{ .time_base = { 1, 1000 }, .frame_rate = { 25, 1 } };
    Aligner a(no_start, no_start);
    for (int64_t t : { 5000, 5040, 80000, 80040 }) a.push(kRef, t);
    for (int64_t t : { 5000, 5040, 0, 40 }) a.push(kDis, t);
    CHECK(a.start(kRef) == 5000);
    CHECK(a.start(kDis) == 5000);

    // A dropped frame within the second sequence counts as it would anywhere.
    const Run dropped = align(restart, without(restart, 8));
    CHECK(dropped.pairs == diagonal(0, 8) + std::vector<std::pair<int64_t, int64_t>>{
                               { 9, 8 }, { 10, 9 }, { 11, 10 } });
    CHECK(dropped.counts.unmatched_reference == 1);
    CHECK(dropped.new_contexts == std::vector<std::size_t>{ 6, 8 });
}

TEST_CASE("vmafalign: a jump in one input alone stops the pairing", "[vmafalign]")
{
    // Only the distorted input restarts, after its frame 5 (D-050): the
    // reference's frame 6, a frame after the distorted input's last, pairs
    // with nothing; its frame 7 is later by more than a frame, so the jumps
    // aren't at the same place and pairing stops there.
    std::vector<int64_t> dis = frames(6);
    for (int64_t t : frames(6)) dis.push_back(t);
    const Run r = align(frames(12), dis);
    CHECK(r.done);
    CHECK(r.pairs == diagonal(0, 6));
    CHECK(r.ref_read == 12);
    CHECK(r.dis_read == 12);
    CHECK(r.counts.discontinuities == 1);
    CHECK(r.counts.unmatched_reference == 1);
    CHECK(r.counts.extra_distorted == 0);
    CHECK(r.counts.after_discontinuity == 11);
    CHECK(r.counts.failure_rate() == 12.0 / 18.0);
    std::vector<int64_t> ref_failed, dis_failed;
    for (const Failure& f : r.failures)
        (f.input == kRef ? ref_failed : dis_failed).push_back(f.index);
    CHECK(ref_failed == std::vector<int64_t>{ 6, 7, 8, 9, 10, 11 });
    CHECK(dis_failed == std::vector<int64_t>{ 6, 7, 8, 9, 10, 11 });
    CHECK(same(r.failures[0], kRef, 6, 240, Reason::UnmatchedReference));

    // A forward jump beyond 10 seconds in the reference alone: the distorted
    // input's frame 6 is extra, and its frame 7 stops the pairing.
    std::vector<int64_t> ref = frames(6);
    for (int64_t t : frames(6, 40, 240 + 10001)) ref.push_back(t);
    const Run far = align(ref, frames(12));
    CHECK(far.pairs == diagonal(0, 6));
    CHECK(far.counts.discontinuities == 1);
    CHECK(far.counts.extra_distorted == 1);
    CHECK(far.counts.unmatched_reference == 0);
    CHECK(far.counts.after_discontinuity == 11);

    // 10 seconds exactly is no discontinuity: the frames after it pair with nothing.
    std::vector<int64_t> near = frames(6);
    for (int64_t t : frames(6, 40, 200 + 10000)) near.push_back(t);
    const Run gap = align(near, frames(12));
    CHECK(gap.counts.discontinuities == 0);
    CHECK(gap.pairs == diagonal(0, 6));
    CHECK(gap.counts.extra_distorted == 6);
    CHECK(gap.counts.unmatched_reference == 6);
}

TEST_CASE("vmafalign: jumps at different places in the two inputs stop the pairing",
          "[vmafalign]")
{
    // Each input loses 12 s, the reference after its frame 3 and the
    // distorted input after its frame 7, as when segments are skipped: their
    // second sequences start at different times, and D-046 alone would pair
    // them from their starts (M5's gaps). They aren't paired (D-050).
    std::vector<int64_t> ref = frames(4), dis = frames(8);
    for (int64_t t : frames(8, 40, 160 + 12000)) ref.push_back(t);
    for (int64_t t : frames(4, 40, 320 + 12000)) dis.push_back(t);
    const Run r = align(ref, dis);
    CHECK(r.done);
    CHECK(r.pairs == diagonal(0, 4));
    CHECK(r.counts.discontinuities == 2);
    CHECK(r.counts.extra_distorted == 1);   // distorted frame 4, a frame after the reference's last
    CHECK(r.counts.after_discontinuity == 15);
    CHECK(r.counts.paired + r.counts.failed() == 20);

    // Jumps a frame apart are at the same place: the reference's last frame
    // before its jump is missing, and the second sequences pair.
    std::vector<int64_t> ref2 = frames(6), dis2 = frames(7);
    for (int64_t t : frames(6)) {
        ref2.push_back(t);
        dis2.push_back(t);
    }
    const Run near = align(ref2, dis2);
    CHECK(near.pairs == diagonal(0, 6) + std::vector<std::pair<int64_t, int64_t>>{
                            { 6, 7 }, { 7, 8 }, { 8, 9 }, { 9, 10 }, { 10, 11 }, { 11, 12 } });
    CHECK(near.counts.extra_distorted == 1);
    CHECK(near.counts.after_discontinuity == 0);
    CHECK(near.new_contexts == std::vector<std::size_t>{ 6 });
}

TEST_CASE("vmafalign: on the distorted input's coarser grid, jumps a grid frame apart are at the "
          "same place", "[vmafalign]")
{
    // A distorted input at half the reference's rate restarts after its frame
    // 3, at 240 ms. D-050's bound is a frame of the coarser input, the
    // distorted input's 80 ms, not the reference's 40 ms.
    const Clock half{ .time_base = { 1, 1000 }, .frame_rate = { 25, 2 }, .start = 0 };
    const std::vector<int64_t> dis = frames(4, 80) + frames(4, 80);

    // The reference restarts 80 ms later, after its frame at 320 ms. Its
    // frames at 280 and 320 ms pair with nothing: the one at the grid's
    // point, 320 ms, is unmatched, the other off the grid. The second
    // sequences pair.
    const Run near = align(frames(9) + frames(8), dis, kMkv25, half);
    CHECK(near.done);
    CHECK(near.pairs == std::vector<std::pair<int64_t, int64_t>>{ { 0, 0 }, { 2, 1 }, { 4, 2 },
                                                                 { 6, 3 }, { 9, 4 }, { 11, 5 },
                                                                 { 13, 6 }, { 15, 7 } });
    CHECK(near.counts.discontinuities == 2);
    REQUIRE(near.failures.size() == 1);
    CHECK(same(near.failures[0], kRef, 8, 320, Reason::UnmatchedReference));
    CHECK(near.counts.off_grid_reference == 8);   // 1, 3, 5, 7, 10, 12, 14, 16
    CHECK(near.counts.after_discontinuity == 0);
    CHECK(near.new_contexts == std::vector<std::size_t>{ 4 });

    // 120 ms later, more than a grid frame: pairing stops at the reference's
    // frame at 360 ms. The frames dropped before it are counted on the grid:
    // the one at 320 ms unmatched, the one at 280 ms off the grid.
    const Run far = align(frames(10) + frames(8), dis, kMkv25, half);
    CHECK(far.done);
    CHECK(far.pairs == std::vector<std::pair<int64_t, int64_t>>{ { 0, 0 }, { 2, 1 }, { 4, 2 },
                                                                { 6, 3 } });
    CHECK(far.counts.discontinuities == 2);
    CHECK(far.counts.unmatched_reference == 1);
    CHECK(far.counts.off_grid_reference == 4);     // 1, 3, 5, 7
    CHECK(far.counts.after_discontinuity == 13);   // reference frames 9 to 17, distorted 4 to 7

    // The reference restarts first, after its frame at 240 ms, and the
    // distorted input 80 ms later, after its frame at 320 ms: that frame is
    // extra, and the second sequences pair.
    const std::vector<int64_t> ref = frames(7) + frames(8);
    const Run later = align(ref, frames(5, 80) + frames(4, 80), kMkv25, half);
    CHECK(later.done);
    CHECK(later.pairs == std::vector<std::pair<int64_t, int64_t>>{ { 0, 0 }, { 2, 1 }, { 4, 2 },
                                                                  { 6, 3 }, { 7, 5 }, { 9, 6 },
                                                                  { 11, 7 }, { 13, 8 } });
    REQUIRE(later.failures.size() == 1);
    CHECK(same(later.failures[0], kDis, 4, 320, Reason::ExtraDistorted));
    CHECK(later.counts.off_grid_reference == 7);   // 1, 3, 5, 8, 10, 12, 14
    CHECK(later.counts.after_discontinuity == 0);
    CHECK(later.new_contexts == std::vector<std::size_t>{ 4 });

    // 160 ms later: the frame at 320 ms is extra, and pairing stops at the one
    // at 400 ms.
    const Run stop = align(ref, frames(6, 80) + frames(4, 80), kMkv25, half);
    CHECK(stop.done);
    CHECK(stop.pairs == std::vector<std::pair<int64_t, int64_t>>{ { 0, 0 }, { 2, 1 }, { 4, 2 },
                                                                 { 6, 3 } });
    CHECK(stop.counts.extra_distorted == 1);
    CHECK(stop.counts.after_discontinuity == 13);   // distorted frames 5 to 9, reference 7 to 14
}

TEST_CASE("vmafalign: a jump between saturated times is a discontinuity", "[vmafalign]")
{
    // Times more than 2^62 ticks either way of where they are measured from
    // saturate there: a frame at the lowest and the next at the highest are
    // 2^63 ticks apart, more than a 64-bit difference holds. Only corrupt
    // timestamps get that far.
    Aligner a(kMkv25, kMkv25);
    a.push(kRef, std::numeric_limits<int64_t>::min() + 1);
    a.push(kRef, std::numeric_limits<int64_t>::max());
    CHECK(a.counts().discontinuities == 1);
}

TEST_CASE("vmafalign: frames without timestamps fail", "[vmafalign]")
{
    std::vector<int64_t> ref = frames(8);
    ref[3] = kNoTimestamp;
    const Run r = align(ref, frames(8));
    CHECK(r.pairs == diagonal(0, 3) + diagonal(4, 8));
    REQUIRE(r.failures.size() == 2);
    CHECK(same(r.failures[0], kRef, 3, kNoTimestamp, Reason::NoTimestamp));
    CHECK(same(r.failures[1], kDis, 3, 120, Reason::ExtraDistorted));
    CHECK(r.counts.no_timestamp == 1);
    CHECK(r.new_contexts == std::vector<std::size_t>{ 3 });   // a reference frame is missing

    std::vector<int64_t> dis = frames(8);
    dis[3] = kNoTimestamp;
    const Run d = align(frames(8), dis);
    CHECK(d.pairs == diagonal(0, 3) + diagonal(4, 8));
    REQUIRE(d.failures.size() == 2);
    CHECK(same(d.failures[0], kDis, 3, kNoTimestamp, Reason::NoTimestamp));
    CHECK(same(d.failures[1], kRef, 3, 120, Reason::UnmatchedReference));
    CHECK(d.new_contexts == std::vector<std::size_t>{ 3 });
}

TEST_CASE("vmafalign: the failure rate and its threshold", "[vmafalign]")
{
    vmafalign::Counts c;
    CHECK(std::isnan(c.failure_rate()));
    CHECK_FALSE(c.exceeded());
    c.paired = 99;
    c.unmatched_reference = 1;
    CHECK(c.failure_rate() == 0.01);
    CHECK_FALSE(c.exceeded());   // 1% is not above 1%
    c.extra_distorted = 1;
    CHECK(c.failed() == 2);
    CHECK(c.exceeded());
    c = {};
    c.no_timestamp = 3;
    CHECK(c.failure_rate() == 1.0);   // nothing paired
    c = {};
    c.paired = 10;
    c.off_grid_reference = 10;        // not failures
    c.discontinuities = 1;            // counted through the frames after it
    CHECK(c.failed() == 0);
    CHECK(vmafalign::kFailureThreshold == 0.01);
}

TEST_CASE("vmafalign: clocks it can't pair by", "[vmafalign]")
{
    const Clock no_rate{ .time_base = { 1, 1000 }, .frame_rate = { 0, 1 }, .start = 0 };
    const Aligner a(no_rate, kMkv25);
    CHECK_FALSE(a.ok());
    CHECK(a.error().starts_with("the reference's frame rate is unknown"));

    // A distorted input without a frame rate is taken to have the reference's.
    const Aligner b(kMkv25, no_rate);
    CHECK(b.ok());
    CHECK(b.grid() == kRef);
    const Run r = align(frames(5), frames(5), kMkv25, no_rate);
    CHECK(r.pairs == diagonal(0, 5));

    // Time bases with no common multiple within 2^40 ticks per second.
    const Clock p1{ .time_base = { 1, 2147483647 }, .frame_rate = { 25, 1 }, .start = 0 };
    const Clock p2{ .time_base = { 1, 2147483629 }, .frame_rate = { 25, 1 }, .start = 0 };
    const Aligner c(p1, p2);
    CHECK_FALSE(c.ok());
    CHECK(c.error().find("no common time base") != std::string::npos);

    const Clock bad_tb{ .time_base = { 0, 1 }, .frame_rate = { 25, 1 }, .start = 0 };
    CHECK_FALSE(Aligner(bad_tb, kMkv25).ok());
}

TEST_CASE("vmafalign: reason names as the report gives them", "[vmafalign]")
{
    CHECK(std::string(vmafalign::reason_name(Reason::UnmatchedReference)) == "unmatched_reference");
    CHECK(std::string(vmafalign::reason_name(Reason::ExtraDistorted)) == "extra_distorted");
    CHECK(std::string(vmafalign::reason_name(Reason::DuplicateDistorted)) == "duplicate_distorted");
    CHECK(std::string(vmafalign::reason_name(Reason::NoTimestamp)) == "no_timestamp");
    CHECK(std::string(vmafalign::reason_name(Reason::AfterDiscontinuity)) == "after_discontinuity");
}
