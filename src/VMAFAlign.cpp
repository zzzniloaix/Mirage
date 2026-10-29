#include "VMAFAlign.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <format>
#include <numeric>

namespace vmafalign {

namespace {

using i128 = __int128;

// `q` in lowest terms with a positive denominator; {0, 1} without one.
AVRational reduced(AVRational q)
{
    AVRational out{ 0, 1 };
    if (q.den == 0) return out;
    av_reduce(&out.num, &out.den, q.num, q.den, INT_MAX);
    return out;
}

// The least common multiple of two positive numbers; 0 when it is larger
// than kMaxTicksPerSecond.
int64_t lcm_within(int64_t a, int64_t b)
{
    const i128 l = static_cast<i128>(a / std::gcd(a, b)) * b;
    return l > kMaxTicksPerSecond ? 0 : static_cast<int64_t>(l);
}

// `q`, reduced, in ticks of 1 / `den`, which its denominator divides.
int64_t to_ticks(AVRational q, int64_t den) { return q.num * (den / q.den); }

std::string text(AVRational q) { return std::format("{}/{}", q.num, q.den); }

} // namespace

const char* reason_name(Reason r)
{
    switch (r) {
    case Reason::UnmatchedReference: return "unmatched_reference";
    case Reason::ExtraDistorted:     return "extra_distorted";
    case Reason::DuplicateDistorted: return "duplicate_distorted";
    case Reason::NoTimestamp:        return "no_timestamp";
    case Reason::AfterDiscontinuity: return "after_discontinuity";
    }
    return "";
}

int64_t Counts::failed() const
{
    return unmatched_reference + extra_distorted + duplicate_distorted + no_timestamp
         + after_discontinuity;
}

double Counts::failure_rate() const
{
    const int64_t n = paired + failed();
    return n == 0 ? std::numeric_limits<double>::quiet_NaN()
                  : static_cast<double>(failed()) / static_cast<double>(n);
}

bool Counts::exceeded() const { return failure_rate() > kFailureThreshold; }   // false for NaN

Aligner::Aligner(const Clock& reference, const Clock& distorted)
{
    ref_.clock = reference;
    dis_.clock = distorted;
    const AVRational ref_tb = reduced(reference.time_base);
    const AVRational dis_tb = reduced(distorted.time_base);
    if (ref_tb.num <= 0 || dis_tb.num <= 0) {
        error_ = std::format("the time bases {} and {} can't both be compared",
                             text(reference.time_base), text(distorted.time_base));
        return;
    }
    if (reference.frame_rate.num <= 0 || reference.frame_rate.den <= 0) {
        error_ = "the reference's frame rate is unknown, so frames can't be paired by timestamp: "
                 "the tolerance is half the reference's frame duration (DECISIONS.md D-028)";
        return;
    }
    const AVRational ref_dur = reduced(av_inv_q(reference.frame_rate));
    tolerance_ = reduced(av_mul_q(ref_dur, AVRational{ 1, 2 }));
    const bool dis_rate = distorted.frame_rate.num > 0 && distorted.frame_rate.den > 0;
    const int  cmp      = dis_rate ? av_cmp_q(distorted.frame_rate, reference.frame_rate) : 0;
    rates_equal_ = cmp == 0;
    grid_        = cmp < 0 ? Input::Distorted : Input::Reference;
    const AVRational grid_dur =
        grid_ == Input::Distorted ? reduced(av_inv_q(distorted.frame_rate)) : ref_dur;

    int64_t den = 1;
    for (AVRational q : { ref_tb, dis_tb, ref_dur, tolerance_, grid_dur, kForwardJump })
        if (den != 0) den = lcm_within(den, q.den);
    if (den == 0) {
        error_ = std::format("the time bases {} and {} and the frame rates {} and {} have no "
                             "common time base of at most {} ticks per second",
                             text(reference.time_base), text(distorted.time_base),
                             text(reference.frame_rate), text(distorted.frame_rate),
                             kMaxTicksPerSecond);
        tolerance_ = { 0, 1 };
        return;
    }
    tick_den_ = den;
    tol_      = to_ticks(tolerance_, den);
    grid_dur_ = to_ticks(grid_dur, den);
    jump_     = to_ticks(kForwardJump, den);
    for (Side* s : { &ref_, &dis_ }) {
        s->has_start = s->clock.start != kNoTimestamp;
        s->start     = s->clock.start;
        s->seq_start = s->clock.start;
    }
}

std::optional<int64_t> Aligner::start(Input in) const
{
    const Side& s = side(in);
    return s.has_start ? std::optional<int64_t>(s.start) : std::nullopt;
}

bool Aligner::start_from_clock(Input in) const { return side(in).clock.start != kNoTimestamp; }

int64_t Aligner::ticks(const Side& s, int64_t timestamp) const
{
    // Beyond 2^62 ticks either way a time saturates: a jump that far is a
    // discontinuity anyway.
    constexpr i128 kLimit = i128{ 1 } << 62;
    const AVRational tb   = reduced(s.clock.time_base);
    const i128       mul  = static_cast<i128>(tb.num) * (tick_den_ / tb.den);
    const i128       diff = static_cast<i128>(timestamp) - s.seq_start;
    if ((diff < 0 ? -diff : diff) > kLimit / mul) return diff < 0 ? -(int64_t{ 1 } << 62)
                                                                  : int64_t{ 1 } << 62;
    return static_cast<int64_t>(diff * mul);
}

void Aligner::push(Input in, int64_t timestamp)
{
    Side& s = side(in);
    Entry e{ .index = s.next_index++, .timestamp = timestamp, .t = 0, .repeats = false,
             .seq = s.seq };
    if (timestamp != kNoTimestamp && ok()) {
        if (!s.has_start) {
            s.has_start = true;
            s.start     = timestamp;
            s.seq_start = timestamp;
        }
        e.t = ticks(s, timestamp);
        if (s.has_last) {
            e.repeats = e.t == s.last_t;
            // In 128 bits: two saturated times are 2^63 ticks apart.
            if (e.t < s.last_t || static_cast<i128>(e.t) - s.last_t > jump_) {
                // The input's next discontinuity sequence, measured from this
                // frame (D-046).
                ++counts_.discontinuities;
                s.ends.push_back(s.last_t);
                e.seq       = ++s.seq;
                s.seq_start = timestamp;
                e.t         = 0;
                e.repeats   = false;
            }
        }
        s.has_last = true;
        s.last_t   = e.t;
    }
    s.queue.push_back(e);
}

void Aligner::end(Input in) { side(in).ended = true; }

void Aligner::fail(Input in, const Entry& e, Reason r)
{
    failures_.push_back({ .input = in, .index = e.index, .timestamp = e.timestamp, .reason = r });
    switch (r) {
    case Reason::UnmatchedReference: ++counts_.unmatched_reference; break;
    case Reason::ExtraDistorted:     ++counts_.extra_distorted;     break;
    case Reason::DuplicateDistorted: ++counts_.duplicate_distorted; break;
    case Reason::NoTimestamp:        ++counts_.no_timestamp;        break;
    case Reason::AfterDiscontinuity: ++counts_.after_discontinuity; break;
    }
}

bool Aligner::has_frame(const Side& s) const
{
    return !s.queue.empty() && s.queue.front().seq == seq_;
}

bool Aligner::done_with_sequence(const Side& s) const
{
    return s.queue.empty() ? s.ended : s.queue.front().seq > seq_;
}

Step Aligner::drop(Input in)
{
    Side& s = side(in);
    Step step{ .kind = in == Input::Reference ? Step::Kind::DropReference
                                              : Step::Kind::DropDistorted };
    (in == Input::Reference ? step.ref_index : step.dis_index) = s.queue.front().index;
    s.queue.pop_front();
    return step;
}

// The inputs' jumps aren't at the same place (D-050): the other input's frames
// dropped since the grid input's last decided frame are counted as before the
// stop, and pairing stops.
Step Aligner::stop()
{
    close_interval(prev_grid_, std::nullopt);
    stopped_ = true;
    return next();
}

// The grid input's frame `g` pairs with no frame: it ends the interval of the
// other input's dropped frames before it, and counts as failed.
void Aligner::unpaired_grid_frame(const Entry& g)
{
    close_interval(prev_grid_, g.t);
    prev_grid_ = g.t;
    if (grid_ == Input::Reference) {
        fail(Input::Reference, g, Reason::UnmatchedReference);
        gap_ = true;
    } else {
        fail(Input::Distorted, g, g.repeats ? Reason::DuplicateDistorted : Reason::ExtraDistorted);
    }
}

// Counts the other input's dropped frames between the grid input's frames at
// `left` and `right` (nullopt: before its first frame, after its last).
void Aligner::close_interval(std::optional<int64_t> left, std::optional<int64_t> right)
{
    if (pending_.empty()) return;
    const Input in = other(grid_);
    // Without a grid frame to place the grid by, every frame fails. With equal
    // frame rates every unpaired distorted frame does. On a coarser grid, the
    // frames at the points no grid frame takes do.
    std::vector<bool> at_point(pending_.size(), !left && !right);
    if (!rates_equal_ && (left || right)) {
        std::vector<int64_t> points;   // in increasing order
        if (left && right) {
            const i128 n = (2 * static_cast<i128>(*right - *left) + grid_dur_)
                               / (2 * static_cast<i128>(grid_dur_)) - 1;
            for (i128 k = 1; k <= n; ++k) points.push_back(*left + static_cast<int64_t>(k) * grid_dur_);
        } else if (right) {
            for (int64_t k = (*right - pending_.front().t + tol_) / grid_dur_; k >= 1; --k)
                points.push_back(*right - k * grid_dur_);
        } else {
            for (int64_t k = 1; *left + k * grid_dur_ - tol_ <= pending_.back().t; ++k)
                points.push_back(*left + k * grid_dur_);
        }
        // Each point takes the nearest frame nearer than the tolerance that no
        // point took, the earlier of two equally near.
        std::size_t from = 0;
        for (int64_t m : points) {
            while (from < pending_.size() && pending_[from].t <= m - tol_) ++from;
            std::optional<std::size_t> best;
            for (std::size_t i = from; i < pending_.size() && pending_[i].t < m + tol_; ++i)
                if (!at_point[i] &&
                    (!best || std::abs(pending_[i].t - m) < std::abs(pending_[*best].t - m)))
                    best = i;
            if (best) at_point[*best] = true;
        }
    }
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        const Entry& e = pending_[i];
        if (in == Input::Distorted) {
            if (e.repeats)                       fail(in, e, Reason::DuplicateDistorted);
            else if (rates_equal_ || at_point[i]) fail(in, e, Reason::ExtraDistorted);
            else                                 ++counts_.off_grid_distorted;
        } else if (e.repeats || at_point[i]) {
            fail(in, e, Reason::UnmatchedReference);
            gap_ = true;
        } else {
            ++counts_.off_grid_reference;
        }
    }
    pending_.clear();
}

Step Aligner::next()
{
    using K = Step::Kind;
    if (!ok()) return {};

    if (stopped_) {
        // Pairing stopped where the inputs' jumps weren't at the same place
        // (D-050): every frame not paired or counted yet fails, and so does
        // every frame still to come.
        for (Input in : { Input::Reference, Input::Distorted }) {
            if (side(in).queue.empty()) continue;
            fail(in, side(in).queue.front(), Reason::AfterDiscontinuity);
            return drop(in);
        }
        if (!ref_.ended) return { .kind = K::NeedReference };
        if (!dis_.ended) return { .kind = K::NeedDistorted };
        return { .kind = K::Done };
    }

    // A frame without a timestamp is dropped once it is at the head of its queue.
    for (Input in : { Input::Reference, Input::Distorted }) {
        const std::deque<Entry>& q = side(in).queue;
        if (q.empty() || q.front().timestamp != kNoTimestamp) continue;
        fail(in, q.front(), Reason::NoTimestamp);
        if (in == Input::Reference) gap_ = true;
        return drop(in);
    }

    const Input g_in = grid_, o_in = other(grid_);
    const K need_g = g_in == Input::Reference ? K::NeedReference : K::NeedDistorted;
    const K need_o = o_in == Input::Reference ? K::NeedReference : K::NeedDistorted;
    Side& g = side(g_in);
    Side& o = side(o_in);

    if (!has_frame(g)) {
        if (!done_with_sequence(g)) return { .kind = need_g };
        // The grid input jumped to its next sequence: a frame of the other
        // input's sequence later than its last by more than a grid frame puts
        // the two jumps at different places (D-050).
        if (!g.queue.empty() && has_frame(o) && o.queue.front().t > g.ends[seq_] + grid_dur_)
            return stop();
        // The grid input has no more frames in this sequence: the rest of the
        // other input's pair with nothing.
        if (has_frame(o)) {
            pending_.push_back(o.queue.front());
            return drop(o_in);
        }
        if (!done_with_sequence(o)) return { .kind = need_o };
        close_interval(prev_grid_, std::nullopt);
        if (g.queue.empty() && o.queue.empty()) return { .kind = K::Done };   // both ended
        // Both go on to the next sequence either has (D-046), in a new libvmaf
        // context.
        seq_ = std::min(g.queue.empty() ? INT64_MAX : g.queue.front().seq,
                        o.queue.empty() ? INT64_MAX : o.queue.front().seq);
        prev_grid_.reset();
        gap_ = true;
        return next();
    }
    const Entry gf = g.queue.front();
    if (!has_frame(o)) {
        if (!done_with_sequence(o)) return { .kind = need_o };
        // The other input jumped: so too, the grid input's frame (D-050).
        if (!o.queue.empty() && gf.t > o.ends[seq_] + grid_dur_) return stop();
        unpaired_grid_frame(gf);
        return drop(g_in);
    }
    const Entry of = o.queue.front();
    if (of.t < gf.t - tol_) {   // too early for this grid frame, and so for every later one
        pending_.push_back(of);
        return drop(o_in);
    }
    if (of.t > gf.t + tol_) {   // nothing within the tolerance of this grid frame
        unpaired_grid_frame(gf);
        return drop(g_in);
    }
    if (of.t < gf.t) {
        // Within the tolerance but earlier: the other input's next frame may be nearer.
        if (o.queue.size() < 2 && !o.ended) return { .kind = need_o };
        if (o.queue.size() >= 2) {
            const Entry& later = o.queue[1];
            if (later.seq == seq_ && later.timestamp != kNoTimestamp &&
                std::abs(later.t - gf.t) < gf.t - of.t) {
                pending_.push_back(of);
                return drop(o_in);
            }
        }
    }

    close_interval(prev_grid_, gf.t);
    prev_grid_ = gf.t;
    Step step{ .kind = K::Pair, .new_context = gap_ && counts_.paired > 0 };
    gap_ = false;
    ++counts_.paired;
    step.ref_index = ref_.queue.front().index;
    step.dis_index = dis_.queue.front().index;
    ref_.queue.pop_front();
    dis_.queue.pop_front();
    return step;
}

} // namespace vmafalign
