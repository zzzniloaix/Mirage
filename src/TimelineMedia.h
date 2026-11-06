#pragma once

// The timeline build's media (DECISIONS.md D-074; eval/phase3-survey/design.md
// §3.4 steps 3 and 4, §5.2): each scored media playlist read again, with
// where it was read from and its SHA-256, and the demux pass over each
// scored input, which places every frame in its segment by byte position
// and checks each scored frame's PTS against the ladder report's. The one
// adapter of the timeline that opens media (libavformat): it returns the
// plain structs of src/TimelineBuild.h, which a test can build by hand, and
// its rules are in map_frames(), which runs without media.
//
// The rules. Where D-074 and D-081 leave a value open, the value here is the
// design's process choice named with it (P-D, P-F, P-G).
//
// - Playlists (P-D). read_playlist() reads a media playlist as
//   vmafladder::read_master() reads a master: any #EXTM3U text, with the
//   analyzer's limits on a network read (D-036), the location it was read
//   from after HTTP redirects, and the SHA-256 of its bytes. The ladder
//   report isn't changed for it.
// - The pass (P-F). An input is opened as the analyzer opens one
//   (DecodeCtx::open() in src/VMAFAnalyzer.cpp): an interrupt callback on the
//   stop token, vmafnet::set_io_limits(), vmafsegments::set_hls_options()
//   (one connection per file, the player's retries),
//   avformat_find_stream_info(), and the video stream av_find_best_stream()
//   picks with a decoder. Its packets are read with av_read_frame(), and none
//   is decoded. The pass's own io_open and io_close2 callbacks record each
//   file the demuxer opens, in open order, and how many bytes the demuxer
//   took from it by its close. That count, not P-F's AVIOContext::bytes_read
//   at the close, is a segment's size, on purpose: the two differ on a byte
//   range (MediaFile).
// - Packets to segments (P-G). FFmpeg's hls demuxer hands its segment
//   demuxer one stream of bytes, the segments' bytes one after the other
//   (read_data_continuous(), hls.c:1641-1754 of FFmpeg 8.1.1), and
//   AVPacket::pos is a position in that stream. The files opened after the
//   input's own open, the playlist's, are the playlist's segments in its
//   order, each known by the last part of its URI and, for an
//   EXT-X-BYTERANGE, by the range's first byte: the k-th holds the positions
//   from the bytes the demuxer took from the files before it to that plus
//   its own. The demuxer opens other files through the same callbacks: an
//   EXT-X-MAP's initialization section, whose bytes go into the stream
//   before its segment's (hls.c:1724-1730), and an EXT-X-KEY's key file
//   (read_key(), hls.c:1350). A file that isn't the next segment, files and
//   segments that don't pair one for one, a packet without a byte position
//   or past every segment, and a byte range the demuxer didn't take in full
//   leave the variant without a segment map, saying why: the byte map is
//   measured on MPEG-TS segments only, without EXT-X-MAP or encryption
//   (eval/phase3-survey/sources.md §2; design §5.2).
// - A known limitation: a ladder of fMP4 segments (EXT-X-MAP) or of
//   segments encrypted under EXT-X-KEY keeps no segment map. The first file
//   the demuxer opens after such a playlist is the initialization section
//   or the key file, which isn't the playlist's next segment, so each such
//   variant is refused as above. Supporting EXT-X-MAP would mean counting
//   the initialization section's bytes into its segment, the one they come
//   before in the stream (hls.c:1724-1730 of FFmpeg 8.1.1).
// - Frames to packets (P-G). An input's frames, in the order the analyzer's
//   decoder outputs them, are each segment's packets in PTS order, the
//   segments in the order they were read; an input that isn't a playlist (a
//   mezzanine, D-002's alternative) is one run of packets in PTS order. A
//   packet without a PTS can't be ordered: no segment map.
// - The check (P-G; D-081 (c)). Every frame a pair of the ladder report
//   scored must have, in the pass, the PTS that pair's report gives it, to
//   the tick, in the same time base: the reference's frames in every pair,
//   a distorted input's in its pair. One that doesn't, or a scored frame
//   the pass doesn't have, leaves the variant without a segment map. A
//   variant without one has no frame the pass can place: its VariantMedia
//   holds the reason and nothing else, no keyframe, corrupt packet or PTS,
//   since those are positions in a frame order the report contradicts (the
//   owner's answer to Q45 (D-086)).
// - Keyframes and corrupt packets (D-074, D-081 (h)): the frames of the
//   packets FFmpeg flags AV_PKT_FLAG_KEY and AV_PKT_FLAG_CORRUPT, each at its
//   own frame; next_boundary() gives the segment boundary that follows a
//   frame, which a corrupt packet's event names.
// - Errors. The pass fails, with an error and an empty result, when it can't
//   read the input to its end as the analyzer would: it can't be opened, has
//   no video stream, a read fails, a file is gone without (an open that
//   failed every try, a read error, as src/VMAFSegments.h counts them;
//   D-055), a stop is requested, or the playlist is live (D-081 (a)).

#include "HlsAlign.h"
#include "LadderReportRead.h"
#include "TimelineBuild.h"

extern "C" {
#include <libavutil/rational.h>
}

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace timeline {

// ── Playlists (P-D) ──────────────────────────────────────────────────────────

// A media playlist as the timeline build read it.
struct PlaylistText {
    std::string url;        // as given
    std::string location;   // where it was read from: the URL after HTTP redirects, else as given
    std::string sha256;     // of `text`, lowercase hex (filehash::sha256_hex)
    std::string text;       // its bytes
    std::string error;      // empty = read
};

// Reads the playlist at `url`, a path or any URL FFmpeg's avio opens, with
// vmafladder::read_master(): an error when it can't be read or doesn't start
// with #EXTM3U, and when a stop requested on `cancel` ends a read that waits.
PlaylistText read_playlist(const std::string& url, std::stop_token cancel = {});

// ── The demux pass (P-F, P-G) ────────────────────────────────────────────────

// The frames of one input that one pair of the ladder report scored: their
// positions in the input and their PTS, ticks of `time_base` (P-A). Views of
// the PairScores they come from, which must outlive them.
struct ScoredFrames {
    std::size_t pair = 0;                    // PairScores::index, to name it
    AVRational  time_base = { 0, 1 };        // the input's, as the report gives it
    std::span<const std::int64_t> index;     // per scored frame: its position in the input
    std::span<const std::int64_t> pts;       // parallel to index
};

// The reference's frames that each pair of `scores` scored, in the pairs' order.
std::vector<ScoredFrames> reference_frames(const LadderScores& scores);

// The frames of `pair`'s distorted input that it scored.
ScoredFrames distorted_frames(const PairScores& pair);

// A file FFmpeg's demuxer opened for an input, as the pass's recorder saw it.
// `bytes` is a segment's size in map_frames(). `bytes_read` is P-F's count,
// kept to show why it isn't one: on a byte range of a file, the file's
// context reads ahead past the range's end, and to a range that starts a
// short way on it reads from the file's first byte instead of seeking, and
// bytes_read counts both (tests/test_timeline_media.cpp asserts them). On a
// whole file the two agree.
struct MediaFile {
    std::string url;                         // as the demuxer opened it
    std::optional<std::int64_t> offset;      // a byte range's first byte (EXT-X-BYTERANGE)
    std::int64_t bytes = 0;                  // the bytes the demuxer took from it by its close
    std::int64_t bytes_read = 0;             // AVIOContext::bytes_read at its close
};

// A packet of the input's video stream.
struct MediaPacket {
    std::optional<std::int64_t> pts;         // ticks of the stream's time base; nullopt: none
    std::int64_t pos = -1;                   // AVPacket::pos; -1: unknown
    bool key     = false;                    // AV_PKT_FLAG_KEY
    bool corrupt = false;                    // AV_PKT_FLAG_CORRUPT
};

// What the demux pass read of one input, before its packets are placed.
struct Demuxed {
    bool       hls = false;                  // read by FFmpeg's hls demuxer
    AVRational time_base = { 0, 1 };         // the video stream's
    std::vector<MediaFile>   files;          // the files opened after the input's own, in open
                                             // order, each once its open succeeded
    std::vector<MediaPacket> packets;        // the video stream's, in the order read
};

// Reads the input at `url` as the analyzer opens it, every packet and no
// decoding (P-F). On an error (see above) `err` says why and the result is
// empty.
Demuxed demux(const std::string& url, std::stop_token cancel, std::string& err);

// The frames of `demuxed`: its segments, in `playlist`'s order, with their
// media sequence numbers, when it is a playlist's (nullptr: an input that
// isn't one); every frame's PTS; its keyframes and corrupt packets; checked
// against every pair of `scored`. A refused map leaves only
// VariantMedia::refused (the owner's answer to Q45 (D-086)).
VariantMedia map_frames(const Demuxed& demuxed, const hls::MediaTimeline* playlist,
                        const std::vector<ScoredFrames>& scored);

// The demux pass of the input at `url`: demux(), then map_frames(). An HLS
// input needs the playlist read for it (read_playlist(), parsed with
// hls::parse_media_timeline() against its location); a live one is an error
// (D-081 (a)). On an error `err` says why and the result is empty.
VariantMedia read_media(const std::string& url, const hls::MediaTimeline* playlist,
                        const std::vector<ScoredFrames>& scored, std::stop_token cancel,
                        std::string& err);

// ── Segment boundaries (D-081 (h)) ───────────────────────────────────────────

// The start of a variant's segment that follows another.
struct SegmentBoundary {
    std::int64_t seq         = 0;            // its media sequence number
    std::int64_t first_index = 0;            // its first frame's position in the input
    bool         discontinuity = false;      // an EXT-X-DISCONTINUITY before it
};

// The boundary of the segment after the one that holds frame `frame` of
// `media`, the variant `playlist` lists: where a corrupt packet's event
// names the boundary after it (D-081 (h)). nullopt when the frame is in the
// last segment, or in none.
std::optional<SegmentBoundary> next_boundary(const VariantMedia& media,
                                             const hls::MediaTimeline& playlist,
                                             std::int64_t frame);

} // namespace timeline
