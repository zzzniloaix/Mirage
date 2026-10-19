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
// `master` (HLS): the playlist text playback opened in place of url's master
// (a deduplicated copy, HlsMaster.h) — opened instead, so stream indices
// match playback's; empty = `url` as is.
int open_input(AVFormatContext** out, const std::string& url, const std::stop_token& st,
               NetInitiator who = NetInitiator::Other, const std::string& master = {});

// A data: URL carrying HLS playlist text, for opening with the hls demuxer
// forced (probing would not recognise it). Empty if encoding fails.
std::string hls_data_url(const std::string& playlist);

// One HLS variant's media playlist, which a helper opens instead of the
// master playback opened (see HlsVariants.h), and what its video stream must
// look like: the parameters of the playback stream it stands for.
struct VariantInput {
    std::string url;            // media playlist; empty = none known
    int         codec_id = 0;   // AVCodecID
    int         width    = 0;   // 0 = not checked
    int         height   = 0;
};

// Open `v.url` for decoding its video: the input must be an HLS media
// playlist with exactly one video stream, of v's codec and size. Every other
// stream is discarded, then the stream is probed (avformat_find_stream_info).
// On success returns 0 with *out open and *video_idx its video stream; on any
// failure (open error, mismatch) *out is nullptr and the caller falls back
// to opening the master. Same `st` / `who` contract as open_input.
int open_variant(AVFormatContext** out, int* video_idx, const VariantInput& v,
                 const std::stop_token& st, NetInitiator who);

} // namespace ffopen
