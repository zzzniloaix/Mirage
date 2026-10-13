#include "DemuxRetry.h"

#include <algorithm>

extern "C" {
#include <libavutil/error.h>
}

namespace demux {

ReadError classify_read_error(int err, bool is_network, bool avio_eof, bool avio_error,
                              bool avio_short, int failures_so_far)
{
    if (err == AVERROR(EAGAIN)) return ReadError::Wait;
    if (!is_network)            return ReadError::Eof;
    // AVERROR_EXIT means an interrupt callback asked to abort, which no
    // amount of retrying will change.
    if (err == AVERROR_EXIT)    return ReadError::Eof;
    const bool retry_left = failures_so_far < kMaxReadRetries;
    // The byte stream recorded an I/O error: the demuxer's AVERROR_EOF (or
    // anything else) is a dropped connection, not the end of the file.
    if (avio_error)             return retry_left ? ReadError::Retry : ReadError::Eof;
    // Bytes stopped short of the size with no recorded error: probably a
    // connection HTTP gave up on, but possibly the real end (see header).
    if (avio_short)
        return failures_so_far < kMaxShortReadRetries ? ReadError::Retry : ReadError::Eof;
    // Real end of stream: the demuxer says so, or the bytes ran out cleanly.
    if (err == AVERROR_EOF || avio_eof) return ReadError::Eof;
    // Anything else on a network input (EIO from a dropped TLS/HTTP
    // connection inside HLS, ETIMEDOUT, INVALIDDATA from a truncated
    // segment, ...) is assumed transient until it keeps failing.
    return retry_left ? ReadError::Retry : ReadError::Eof;
}

std::chrono::milliseconds retry_backoff(int attempt)
{
    const int shift = std::clamp(attempt - 1, 0, 5);   // 50,100,200,400,800,1000...
    return std::chrono::milliseconds(std::min(50 << shift, 1000));
}

} // namespace demux
