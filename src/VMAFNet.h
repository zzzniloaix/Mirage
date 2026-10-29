#pragma once

// The limits on the VMAF analyzer's network reads (DECISIONS.md D-036): its
// inputs and the ladder's master playlist get the player's I/O timeouts
// (src/FfOpen.cpp), so a read that waits longer fails instead of waiting
// for good, and both an interrupt callback on the run's stop token, so a
// cancel interrupts a read that waits (the master's: vmafladder::
// read_master()). Header-only: mirage_vmaf doesn't link the player's FfOpen
// and its network logger.

#include <stop_token>
#include <string>

extern "C" {
#include <libavutil/dict.h>
}

namespace vmafnet {

// 5 s, in FFmpeg's microseconds: the most a connection attempt, and any
// single read or write, may wait (src/FfOpen.cpp's values).
inline constexpr const char* kIoTimeoutUs = "5000000";

// True for http(s)/rtmp/rtsp URLs (the rule of ffopen::is_network_url).
inline bool is_network_url(const std::string& url)
{
    return url.starts_with("http://")  ||
           url.starts_with("https://") ||
           url.starts_with("rtmp://")  ||
           url.starts_with("rtsp://");
}

// Adds FFmpeg's `timeout` and `rw_timeout` options for a network `url` to
// *opts; leaves a local input's options alone.
inline void set_io_limits(AVDictionary** opts, const std::string& url)
{
    if (!is_network_url(url)) return;
    av_dict_set(opts, "timeout",    kIoTimeoutUs, 0);
    av_dict_set(opts, "rw_timeout", kIoTimeoutUs, 0);
}

// An AVIOInterruptCB callback whose opaque is a std::stop_token: 1, which
// ends FFmpeg's blocking call, once a stop is requested.
inline int stop_requested(void* opaque)
{
    return static_cast<const std::stop_token*>(opaque)->stop_requested() ? 1 : 0;
}

} // namespace vmafnet
