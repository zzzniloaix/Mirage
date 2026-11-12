#include "TimelineDetect.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <limits>

namespace timeline {

namespace {

// The baseline's points: the scores of the last W points that weren't low,
// in the order they came and sorted, so that the median is read off the
// sorted ones instead of sorting them again at every point.
class Window {
public:
    explicit Window(std::int64_t size) : size_(size) {}

    std::int64_t count() const { return static_cast<std::int64_t>(sorted_.size()); }

    // The middle score, or the mean of the middle two; count() > 0.
    double median() const
    {
        const std::size_t h = sorted_.size() / 2;
        return sorted_.size() % 2 == 1 ? sorted_[h] : (sorted_[h - 1] + sorted_[h]) / 2;
    }

    // `score` joins, and once there are more than W the oldest leaves. Of
    // scores that compare equal any can leave for it: they are one value,
    // and the median is the same.
    void push(double score)
    {
        sorted_.insert(std::upper_bound(sorted_.begin(), sorted_.end(), score), score);
        order_.push_back(score);
        if (static_cast<std::int64_t>(order_.size()) <= size_) return;
        sorted_.erase(std::lower_bound(sorted_.begin(), sorted_.end(), order_.front()));
        order_.pop_front();
    }

private:
    std::int64_t        size_;
    std::deque<double>  order_;    // oldest first
    std::vector<double> sorted_;   // ascending
};

// What is wrong with `curve` as detect() walks it, or empty. A drop can end
// at its last point's frame plus one, so no frame is the largest int64.
std::string check_curve(const Curve& curve)
{
    if (curve.ref_index.size() != curve.vmaf.size())
        return std::format("the curve has {} reference frames for {} scores",
                           curve.ref_index.size(), curve.vmaf.size());
    for (std::size_t i = 0; i < curve.ref_index.size(); ++i) {
        const std::int64_t f = curve.ref_index[i];
        if (f < 0)
            return std::format("point {} of the curve is at reference frame {}: reference "
                               "frames are positions from 0", i, f);
        if (f == std::numeric_limits<std::int64_t>::max())
            return std::format("point {} of the curve is at reference frame {}, after which "
                               "no frame can follow", i, f);
        if (i > 0 && f <= curve.ref_index[i - 1])
            return std::format("point {} of the curve is at reference frame {}, not after "
                               "point {}'s frame {}: a curve's frames ascend",
                               i, f, i - 1, curve.ref_index[i - 1]);
    }
    return {};
}

} // namespace

std::string check_options(const DetectOptions& options)
{
    if (!std::isfinite(options.threshold) || options.threshold < 0)
        return std::format("the threshold T wants a number of VMAF points from 0, not {}",
                           options.threshold);
    if (options.window && *options.window < 1)
        return std::format("the window W wants a whole number of points from 1, not {}",
                           *options.window);
    if (options.min_points && *options.min_points < 0)
        return std::format("the minimum N wants a whole number of points from 0, not {}",
                           *options.min_points);
    return {};
}

std::int64_t warm_up(std::int64_t window) { return std::max<std::int64_t>(1, window / 2); }

std::vector<Drop> detect(const Curve& curve, const DetectOptions& options, std::string& err)
{
    err.clear();
    std::vector<Drop> drops;
    if (!options.given()) return drops;   // no detection without W and N (D-077)
    err = check_options(options);
    if (err.empty()) err = check_curve(curve);
    if (!err.empty()) return drops;

    const std::int64_t n    = *options.min_points;
    const std::int64_t need = warm_up(*options.window);
    Window baseline(*options.window);

    // The run of low points being walked.
    struct Run {
        std::size_t  first = 0, last = 0;   // positions of its first and last points
        std::int64_t points = 0;
        double       baseline = 0.0, min = 0.0, sum = 0.0;
    };
    std::optional<Run> run;
    // The run ends, with the reference frame its interval ends at: a drop
    // when it has more than N points.
    const auto end_run = [&](std::int64_t end) {
        if (run && run->points > n) {
            Drop d;
            d.interval       = Interval{ .ref_index = curve.ref_index[run->first], .ref_end = end };
            d.first_point    = run->first;
            d.last_point     = run->last;
            d.points         = run->points;
            d.baseline       = run->baseline;
            d.min            = run->min;
            d.mean           = run->sum / static_cast<double>(run->points);
            d.drop_magnitude = d.baseline - d.min;
            d.mean_drop      = d.baseline - d.mean;
            drops.push_back(d);
        }
        run.reset();
    };

    for (std::size_t i = 0; i < curve.vmaf.size(); ++i) {
        const double score = curve.vmaf[i];
        if (!std::isfinite(score)) continue;   // no score: skipped, as a gap is
        if (baseline.count() >= need) {
            const double b = baseline.median();
            if (score < b - options.threshold) {   // low: it stays out of the baseline
                if (!run) run = Run{ .first = i, .baseline = b, .min = score };
                run->last = i;
                ++run->points;
                run->min = std::min(run->min, score);
                run->sum += score;
                continue;
            }
        }
        end_run(curve.ref_index[i]);   // the curve's next point after the run
        baseline.push(score);
    }
    if (run) end_run(curve.ref_index[run->last] + 1);   // the curve ends in the run
    return drops;
}

Payload detection_record(const DetectOptions& options)
{
    const auto or_null = [](const std::optional<std::int64_t>& v) {
        return v ? Value{ *v } : Value{};
    };
    Payload record;
    record.emplace_back("threshold", Value{ options.threshold });
    record.emplace_back("window", or_null(options.window));
    record.emplace_back("min_points", or_null(options.min_points));
    record.emplace_back("baseline", Value{ std::string("median-non-low") });
    record.emplace_back("warm_up", options.window ? Value{ warm_up(*options.window) } : Value{});
    record.emplace_back("context_first", Value{ std::string("no-special-case") });
    return record;
}

} // namespace timeline
