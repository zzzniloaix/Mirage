#pragma once

// Pure helpers for Demuxer::read_loop's handling of av_read_frame errors.
// No threads / queues here so the policy can be unit-tested in mirage_tests.

#include <chrono>

namespace demux {

enum class ReadError {
    Wait,    // EAGAIN: data not ready yet — poll again, not a failure
    Retry,   // transient network I/O error — back off and read again
    Eof,     // end of input (or give up) — signal EOF to the decoders
};

// Consecutive network read failures tolerated before read_loop gives up.
// With retry_backoff() below this spans ~6.5 s of sleeping, on top of each
// failing read's own I/O time (bounded by rw_timeout).
inline constexpr int kMaxReadRetries = 10;

// Retries allowed when the only evidence of a failure is that the bytes
// stopped short of the known size more than a second before the media's end
// (avio_short). That heuristic can also fire at a genuine end of file (e.g.
// an MP4 whose moov box sits after the media data, or a trailing stream that
// is discarded), so it gets a smaller budget: ~2.5 s of backoff, enough for a
// connection lost for a moment (3 retries, ~350 ms, ended playback as if the
// file were over), while a misread EOF still ends soon.
inline constexpr int kMaxShortReadRetries = 6;

// Classify an av_read_frame() error.
//   err             — the negative AVERROR returned by av_read_frame
//   is_network      — input was opened from an http(s)/rtmp/rtsp URL
//   avio_eof        — fmt_ctx->pb exists and avio_feof() is set
//   avio_error      — fmt_ctx->pb exists and the byte layer recorded an error
//                     (pb->error < 0). Demuxers report AVERROR_EOF whenever
//                     avio_feof() is set, so this is what tells a dropped
//                     connection apart from the real end of the file.
//   avio_short      — no recorded error, but the bytes stopped short of the
//                     known size before the media reached its duration (a
//                     connection HTTP gave up on can read as plain EOF).
//                     A heuristic: retried at most kMaxShortReadRetries times.
//                     Both are false when there is no pb, e.g. HLS, whose
//                     segments are read through nested contexts.
//   failures_so_far — consecutive Retry results without forward progress
//                     (no new packet delivered) before this one
// Local files keep the historical behaviour: anything but EAGAIN is EOF.
ReadError classify_read_error(int err, bool is_network, bool avio_eof, bool avio_error,
                              bool avio_short, int failures_so_far);

// Sleep before retry number `attempt` (1-based): 50 ms doubling, capped at 1 s.
std::chrono::milliseconds retry_backoff(int attempt);

} // namespace demux
