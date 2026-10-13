#pragma once

// Pure planning helpers for the background thumbnail decode (ThumbnailStrip).
// No GL / FFmpeg here so they can be unit-tested in mirage_tests.

#include <cstddef>
#include <vector>

namespace thumbs {

// Minimum spacing (seconds) between sampled keyframes so that a clip of
// `duration` seconds yields at most ~max_thumbs thumbnails. 0 when the
// duration is unknown (live) or max_thumbs < 2 — i.e. no thinning.
double sample_stride(double duration, int max_thumbs);

// Indices into the ascending keyframe list `kf` to decode thumbnails for.
// Greedy: always keeps kf[0], then each keyframe >= stride seconds after the
// last kept one. If that still exceeds max_thumbs (e.g. the index spans more
// than the reported duration), the result is thinned uniformly to fit.
std::vector<std::size_t> sample_keyframes(const std::vector<double>& kf,
                                          double stride, int max_thumbs);

// Decode order over positions 0..n-1 for a coarse-to-fine sweep: every
// `step`-th position first (0, step, 2*step, ...), then all remaining ones in
// ascending order. Each position appears exactly once. step <= 1 → identity.
std::vector<std::size_t> coarse_to_fine(std::size_t n, std::size_t step);

} // namespace thumbs
