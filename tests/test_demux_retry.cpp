#include <catch2/catch_test_macros.hpp>

#include "DemuxRetry.h"

extern "C" {
#include <libavutil/error.h>
}

using demux::ReadError;
using demux::classify_read_error;

// Argument order: err, is_network, avio_eof, avio_error, avio_short, failures_so_far

TEST_CASE("classify_read_error: EAGAIN always waits", "[demux]")
{
    CHECK(classify_read_error(AVERROR(EAGAIN), false, false, false, false, 0) == ReadError::Wait);
    CHECK(classify_read_error(AVERROR(EAGAIN), true, false, false, false, 0) == ReadError::Wait);
    CHECK(classify_read_error(AVERROR(EAGAIN), true, false, false, false, demux::kMaxReadRetries)
          == ReadError::Wait);
}

TEST_CASE("classify_read_error: local files never retry", "[demux]")
{
    CHECK(classify_read_error(AVERROR_EOF, false, true, false, false, 0) == ReadError::Eof);
    CHECK(classify_read_error(AVERROR(EIO), false, true, true, false, 0) == ReadError::Eof);
    CHECK(classify_read_error(AVERROR_INVALIDDATA, false, false, false, false, 0) == ReadError::Eof);
}

TEST_CASE("classify_read_error: clean network EOF is final", "[demux]")
{
    CHECK(classify_read_error(AVERROR_EOF, true, false, false, false, 0) == ReadError::Eof);  // HLS end
    CHECK(classify_read_error(AVERROR_EOF, true, true, false, false, 0) == ReadError::Eof);  // http end
    CHECK(classify_read_error(AVERROR_EXIT, true, false, true, false, 0) == ReadError::Eof);  // interrupted
    // bytes ran out cleanly even though the demuxer reported something else
    CHECK(classify_read_error(AVERROR_INVALIDDATA, true, true, false, false, 0) == ReadError::Eof);
}

TEST_CASE("classify_read_error: AVERROR_EOF over a failed connection retries", "[demux]")
{
    // Demuxers turn avio_feof() into AVERROR_EOF even when the HTTP layer
    // failed (pb->error set) — that must not end playback.
    CHECK(classify_read_error(AVERROR_EOF, true, true, true, false, 0) == ReadError::Retry);
    CHECK(classify_read_error(AVERROR_EOF, true, true, true, false, demux::kMaxReadRetries - 1)
          == ReadError::Retry);
    CHECK(classify_read_error(AVERROR_EOF, true, true, true, false, demux::kMaxReadRetries)
          == ReadError::Eof);
}

TEST_CASE("classify_read_error: network I/O errors retry up to the cap", "[demux]")
{
    for (int err : { AVERROR(EIO), AVERROR(ETIMEDOUT), AVERROR(ECONNRESET), AVERROR_INVALIDDATA }) {
        CHECK(classify_read_error(err, true, false, false, false, 0) == ReadError::Retry);
        CHECK(classify_read_error(err, true, false, false, false, demux::kMaxReadRetries - 1)
              == ReadError::Retry);
        CHECK(classify_read_error(err, true, false, false, false, demux::kMaxReadRetries)
              == ReadError::Eof);
    }
}

TEST_CASE("classify_read_error: short read without an error has a small budget", "[demux]")
{
    // Bytes stopped short of the size with no recorded error: a dropped
    // connection, or a real EOF the heuristic misread (moov-at-end MP4, a
    // trailing stream that is discarded). Must end after a few retries.
    CHECK(classify_read_error(AVERROR_EOF, true, true, false, true, 0) == ReadError::Retry);
    CHECK(classify_read_error(AVERROR_EOF, true, true, false, true,
                              demux::kMaxShortReadRetries - 1) == ReadError::Retry);
    CHECK(classify_read_error(AVERROR_EOF, true, true, false, true,
                              demux::kMaxShortReadRetries) == ReadError::Eof);
    CHECK(demux::kMaxShortReadRetries < demux::kMaxReadRetries);
    // A recorded byte-layer error keeps the full budget.
    CHECK(classify_read_error(AVERROR_EOF, true, true, true, true,
                              demux::kMaxShortReadRetries) == ReadError::Retry);
    // Local files never retry, even if the heuristic flag were set.
    CHECK(classify_read_error(AVERROR_EOF, false, true, false, true, 0) == ReadError::Eof);
}

TEST_CASE("retry_backoff: doubles from 50 ms, capped at 1 s", "[demux]")
{
    using std::chrono::milliseconds;
    CHECK(demux::retry_backoff(1) == milliseconds(50));
    CHECK(demux::retry_backoff(2) == milliseconds(100));
    CHECK(demux::retry_backoff(5) == milliseconds(800));
    CHECK(demux::retry_backoff(6) == milliseconds(1000));
    CHECK(demux::retry_backoff(50) == milliseconds(1000));
    CHECK(demux::retry_backoff(0) == milliseconds(50));

    milliseconds total{ 0 };
    for (int i = 1; i <= demux::kMaxReadRetries; ++i) total += demux::retry_backoff(i);
    CHECK(total <= milliseconds(10000));   // bounded give-up time
}
