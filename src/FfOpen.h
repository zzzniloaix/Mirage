#pragma once

// avformat_open_input() for the background helper decoders (ThumbnailStrip,
// ScrubDecoder): network inputs get I/O timeouts, and every blocking FFmpeg
// call on the context aborts once `st` is stop-requested, so the owning
// std::jthread can join promptly on quit even while a URL is unreachable.

#include "NetworkLogger.h"   // NetInitiator

#include <stop_token>
#include <string>

struct AVFormatContext;

namespace ffopen {

// True for http(s)/rtmp/rtsp URLs (same rule as Demuxer::is_network_url).
bool is_network_url(const std::string& url);

// Opens `url` into *out (nullptr on failure) and returns the FFmpeg status.
// `st` must outlive the context: it is the interrupt callback's opaque.
// `who` tags the context's traffic in the network inspector.
int open_input(AVFormatContext** out, const std::string& url, const std::stop_token& st,
               NetInitiator who = NetInitiator::Other);

} // namespace ffopen
