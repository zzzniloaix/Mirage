// vmafpool::pool / percentile (src/VMAFPool.h): known values, worked out by
// hand or with eval/pooling/pooling.py's definitions, and the mean, minimum
// and harmonic mean against libvmaf's own pooling, to the last bit: of
// scores imported into a libvmaf context, and of the scores of one context
// that scores frames itself, through vmaf_score_pooled().

#include <catch2/catch_test_macros.hpp>

#include "VMAFPool.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include <libvmaf/libvmaf.h>

namespace {

// The values of `p` in order: mean, harmonic mean, min, min without the
// first frame, p1, p5, p10, p25, p50.
std::vector<double> values(const VMAFPooled& p)
{
    return { p.mean, p.harmonic_mean, p.min, p.min_without_first_frame,
             p.p1, p.p5, p.p10, p.p25, p.p50 };
}

VmafConfiguration quiet_config()
{
    return { .log_level = VMAF_LOG_LEVEL_NONE, .n_threads = 0, .n_subsample = 1,
             .cpumask = 0, .gpumask = 0 };
}

// What libvmaf's pooling of `scores`, imported into a new context as one
// feature, gives with `method`.
double libvmaf_pooled(const std::vector<double>& scores, VmafPoolingMethod method)
{
    VmafContext* vmaf = nullptr;
    REQUIRE(vmaf_init(&vmaf, quiet_config()) == 0);
    int imported = 0;
    for (unsigned i = 0; i < scores.size(); ++i)
        imported |= vmaf_import_feature_score(vmaf, "mirage_pool_test", scores[i], i);
    double v = 0.0;
    const int r = vmaf_feature_score_pooled(vmaf, "mirage_pool_test", method, &v, 0,
                                            static_cast<unsigned>(scores.size() - 1));
    vmaf_close(vmaf);
    REQUIRE(imported == 0);
    REQUIRE(r == 0);
    return v;
}

} // namespace

TEST_CASE("vmafpool::pool: no scores, and one", "[vmafpool]")
{
    for (double v : values(vmafpool::pool({}))) CHECK(std::isnan(v));

    const VMAFPooled one = vmafpool::pool({ 3.0 });
    CHECK(one.mean == 3.0);
    CHECK(one.harmonic_mean == 3.0);   // 1 / (1 / 4) - 1
    CHECK(one.min == 3.0);
    CHECK(std::isnan(one.min_without_first_frame));
    for (double v : { one.p1, one.p5, one.p10, one.p25, one.p50 }) CHECK(v == 3.0);
}

TEST_CASE("vmafpool::pool: known values", "[vmafpool]")
{
    // 1 / (x + 1) is 1, 1/2, 1/4 and 1/8: their sum, 1.875, is exact.
    const VMAFPooled a = vmafpool::pool({ 0.0, 1.0, 3.0, 7.0 });
    CHECK(a.mean == 2.75);
    CHECK(a.harmonic_mean == 4.0 / 1.875 - 1.0);
    CHECK(a.harmonic_mean == 1.1333333333333333);
    CHECK(a.min == 0.0);
    CHECK(a.min_without_first_frame == 1.0);
    // h = 3 p / 100: 0.03, 0.15, 0.3, 0.75 between 0 and 1; 1.5 between 1 and 3.
    CHECK(a.p1 == 0.03);
    CHECK(a.p5 == 0.15);
    CHECK(a.p10 == 0.3);
    CHECK(a.p25 == 0.75);
    CHECK(a.p50 == 2.0);

    // Frames out of order, the first one the lowest. pooling.py's values.
    const VMAFPooled b = vmafpool::pool({ 12.5, 40.25, 30.0, 20.0, 99.0, 61.75 });
    CHECK(b.mean == 43.916666666666664);
    CHECK(b.harmonic_mean == 28.393053227705867);
    CHECK(b.min == 12.5);
    CHECK(b.min_without_first_frame == 20.0);
    CHECK(b.p1 == 12.875);
    CHECK(b.p5 == 14.375);
    CHECK(b.p10 == 16.25);
    CHECK(b.p25 == 22.5);
    CHECK(b.p50 == 35.125);   // the median of an even count: halfway

    // The lowest frame later on: it is the minimum both ways.
    const VMAFPooled c = vmafpool::pool({ 50.0, 7.0, 60.0 });
    CHECK(c.min == 7.0);
    CHECK(c.min_without_first_frame == 7.0);
}

TEST_CASE("vmafpool::pool: the minimum without the first pair of each libvmaf context (D-037)",
          "[vmafpool]")
{
    const std::vector<double> scores = { 10.0, 50.0, 40.0, 5.0, 60.0, 30.0 };

    // One context: the same as the minimum without the first frame.
    const VMAFPooled one = vmafpool::pool(scores);
    CHECK(one.min_without_first_frame == 5.0);
    CHECK(one.min_without_context_first_frames == 5.0);
    CHECK(vmafpool::pool(scores, { 0 }).min_without_context_first_frames == 5.0);

    // Contexts from pairs 0 and 3: pair 3, the lowest, starts one.
    const VMAFPooled two = vmafpool::pool(scores, { 0, 3 });
    CHECK(two.min_without_first_frame == 5.0);
    CHECK(two.min_without_context_first_frames == 30.0);
    CHECK(two.min == 5.0);
    CHECK(two.mean == one.mean);            // the other values don't depend on the contexts
    CHECK(two.p50 == one.p50);

    // Pair 0 starts a context whether or not it is listed; a start past the
    // last pair is ignored.
    CHECK(vmafpool::pool(scores, { 3 }).min_without_context_first_frames == 30.0);
    CHECK(vmafpool::pool(scores, { 0, 3, 99 }).min_without_context_first_frames == 30.0);

    // Every pair starts a context, or there is one pair: undefined.
    CHECK(std::isnan(vmafpool::pool({ 7.0, 8.0 }, { 0, 1 }).min_without_context_first_frames));
    CHECK(std::isnan(vmafpool::pool({ 7.0 }).min_without_context_first_frames));
    CHECK(std::isnan(vmafpool::pool({}).min_without_context_first_frames));
}

TEST_CASE("vmafpool::percentile: each step rounded on its own, as pooling.py", "[vmafpool]")
{
    // 279 frames: p1 has h = 2.78, between ranks 2 and 3. pooling.py's
    // 76.18872516467002 rounds 0.78 * (hi - lo) and then the sum; a fused
    // multiply-add rounds once and gives 76.18872516467.
    const double lo = 57.09136896467, hi = 81.57515896467001;
    std::vector<double> scores(275, 100.0);
    scores.insert(scores.end(), { hi, 1.0, lo, 0.0 });
    const VMAFPooled p = vmafpool::pool(scores);
    CHECK(p.p1 == 76.18872516467002);
    CHECK(p.p5 == 100.0);
    CHECK(p.p50 == 100.0);
    CHECK(p.min == 0.0);
    CHECK(p.min_without_first_frame == 0.0);

    std::vector<double> sorted = scores;
    std::sort(sorted.begin(), sorted.end());
    CHECK(vmafpool::percentile(sorted, 0) == 0.0);
    CHECK(vmafpool::percentile(sorted, 100) == 100.0);   // the last rank
}

TEST_CASE("vmafpool::pool: libvmaf's pooling of the same scores, to the last bit", "[vmafpool][vmaf]")
{
    std::mt19937_64 rng(2027);
    std::uniform_real_distribution<double> score(0.0, 100.0);
    std::vector<std::vector<double>> series = {
        { 0.0 }, { 100.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 100.0, 0.0, 100.0 },
        { 1e-12, 99.99999999999, 0.1 + 0.2, 1.0 / 3.0 },
    };
    for (std::size_t n : { 2u, 7u, 300u, 5000u }) {
        std::vector<double> s(n);
        for (double& v : s) v = score(rng);
        series.push_back(s);
    }
    {   // mostly zeros, as the lowest rung scores
        std::vector<double> s(1000, 0.0);
        for (std::size_t i = 0; i < s.size(); i += 37) s[i] = score(rng);
        series.push_back(s);
    }
    for (const std::vector<double>& s : series) {
        INFO("frames: " << s.size() << ", first: " << s.front());
        const VMAFPooled p = vmafpool::pool(s);
        CHECK(p.mean == libvmaf_pooled(s, VMAF_POOL_METHOD_MEAN));
        CHECK(p.min == libvmaf_pooled(s, VMAF_POOL_METHOD_MIN));
        CHECK(p.harmonic_mean == libvmaf_pooled(s, VMAF_POOL_METHOD_HARMONIC_MEAN));
    }
}

TEST_CASE("vmafpool::pool: vmaf_score_pooled() of a context that scored frames, to the last bit", "[vmafpool][vmaf]")
{
    // 24 frame pairs of a moving ramp, the distorted frames with noise whose
    // strength changes from frame to frame, scored with vmaf_v0.6.1.
    constexpr unsigned kW = 192, kH = 108, kFrames = 24;
    VmafContext* vmaf = nullptr;
    REQUIRE(vmaf_init(&vmaf, quiet_config()) == 0);
    VmafModel* model = nullptr;
    VmafModelConfig model_cfg{ .name = "vmaf", .flags = VMAF_MODEL_FLAGS_DEFAULT };
    REQUIRE(vmaf_model_load(&model, &model_cfg, "vmaf_v0.6.1") == 0);
    REQUIRE(vmaf_use_features_from_model(vmaf, model) == 0);

    std::mt19937 rng(9);
    for (unsigned i = 0; i < kFrames; ++i) {
        VmafPicture ref{}, dis{};
        REQUIRE(vmaf_picture_alloc(&ref, VMAF_PIX_FMT_YUV420P, 8, kW, kH) == 0);
        REQUIRE(vmaf_picture_alloc(&dis, VMAF_PIX_FMT_YUV420P, 8, kW, kH) == 0);
        std::uniform_int_distribution<int> noise(-static_cast<int>(2 + (i * 7) % 23),
                                                 static_cast<int>(2 + (i * 7) % 23));
        for (unsigned y = 0; y < kH; ++y) {
            auto* r = static_cast<std::uint8_t*>(ref.data[0]) + y * ref.stride[0];
            auto* d = static_cast<std::uint8_t*>(dis.data[0]) + y * dis.stride[0];
            for (unsigned x = 0; x < kW; ++x) {
                r[x] = static_cast<std::uint8_t>((x * 2 + y + i * 5) & 0xff);
                d[x] = static_cast<std::uint8_t>(std::clamp(r[x] + noise(rng), 0, 255));
            }
        }
        for (int plane = 1; plane < 3; ++plane)
            for (unsigned y = 0; y < (kH + 1) / 2; ++y) {
                std::fill_n(static_cast<std::uint8_t*>(ref.data[plane]) + y * ref.stride[plane],
                            (kW + 1) / 2, std::uint8_t{ 128 });
                std::fill_n(static_cast<std::uint8_t*>(dis.data[plane]) + y * dis.stride[plane],
                            (kW + 1) / 2, std::uint8_t{ 128 });
            }
        REQUIRE(vmaf_read_pictures(vmaf, &ref, &dis, i) == 0);
    }
    REQUIRE(vmaf_read_pictures(vmaf, nullptr, nullptr, 0) == 0);

    std::vector<double> scores(kFrames);
    for (unsigned i = 0; i < kFrames; ++i)
        REQUIRE(vmaf_score_at_index(vmaf, model, &scores[i], i) == 0);
    double mean = 0.0, min = 0.0, harmonic = 0.0;
    REQUIRE(vmaf_score_pooled(vmaf, model, VMAF_POOL_METHOD_MEAN, &mean, 0, kFrames - 1) == 0);
    REQUIRE(vmaf_score_pooled(vmaf, model, VMAF_POOL_METHOD_MIN, &min, 0, kFrames - 1) == 0);
    REQUIRE(vmaf_score_pooled(vmaf, model, VMAF_POOL_METHOD_HARMONIC_MEAN, &harmonic, 0,
                              kFrames - 1) == 0);
    vmaf_model_destroy(model);
    vmaf_close(vmaf);

    // The scores differ from frame to frame, so the pooling has work to do.
    const auto [lowest, highest] = std::minmax_element(scores.begin(), scores.end());
    CHECK(*highest - *lowest > 1.0);
    const VMAFPooled p = vmafpool::pool(scores);
    CHECK(p.mean == mean);
    CHECK(p.min == min);
    CHECK(p.harmonic_mean == harmonic);
}
