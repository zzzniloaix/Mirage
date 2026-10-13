#pragma once

// Pure planning helpers for the background thumbnail decode (ThumbnailStrip).
// No GL / FFmpeg here so they can be unit-tested in mirage_tests.

#include <cstddef>
#include <cstdint>
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

// True when a container index with `n_keyframes` keyframes is too thin to
// drive thumbnails for a clip of `duration` seconds — e.g. Matroska before its
// Cues are loaded (just the first keyframe) or HLS (no index at all). Wants at
// least 2 keyframes, and one per ~20 s up to 16, when the duration is known.
bool index_too_sparse(std::size_t n_keyframes, double duration);

// Evenly spaced offsets (seconds from the stream start) at which to seek and
// decode when there is no usable index: k * duration / n for k = 0..n-1, with
// n = max_samples, reduced so neighbours are >= min_spacing apart. Empty when
// the duration is unknown (live) or max_samples < 1.
std::vector<double> sample_times(double duration, int max_samples, double min_spacing);

// A video stream the thumbnail decoder could read instead of the playing one
// (HLS exposes every variant as its own stream).
struct VideoCandidate {
    int     index    = -1;
    int     codec_id = 0;   // AVCodecID
    int     height   = 0;   // 0 = unknown
    int64_t bit_rate = 0;   // 0 = unknown
};

// Stream index to decode thumbnails from: the lowest-bitrate candidate with
// the same codec as `current` that is at least min(min_height, current's
// height) tall. Falls back to `current` when it is not among the candidates,
// or nothing is known to be cheaper.
int pick_thumb_stream(const std::vector<VideoCandidate>& cands, int current, int min_height);

} // namespace thumbs
