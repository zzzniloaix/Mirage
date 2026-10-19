#pragma once

// Which HLS media playlist a stream of a master-playlist context is read
// from, so the background helpers (thumbnails, scrubbing) can open just the
// playlist they decode. Opening the master again costs what the playback
// open does: FFmpeg's hls demuxer fetches every variant / rendition playlist
// (once per EXT-X-STREAM-INF — it does not merge entries that share a URI)
// and probes the first segment of each.
//
// FFmpeg exposes one AVProgram per EXT-X-STREAM-INF, in master order (the
// order ManifestParser::variants() has too), tagged "variant_bitrate" =
// BANDWIDTH. Each entry gets its own playlist and streams, so a variant's
// own video stream belongs to exactly one program and carries that
// program's variant_bitrate; an EXT-X-MEDIA rendition is in the program of
// every variant of its group.

#include "ManifestParser.h"

#include <cstdint>
#include <string>
#include <vector>

struct AVFormatContext;

namespace hls {

struct ProgramInfo {
    std::int64_t     variant_bitrate = -1;   // "variant_bitrate" metadata, -1 = absent
    std::vector<int> streams;                // AVProgram::stream_index
};

// Index into `variants` of the EXT-X-STREAM-INF whose own media playlist
// carries stream `stream` (its "variant_bitrate" metadata is
// `stream_bitrate`, -1 = absent), or -1 when that cannot be established:
//  * fewer than two variants (nothing to save; single-variant masters are
//    also where renditions share the only program),
//  * the master declares alternative video renditions (EXT-X-MEDIA
//    TYPE=VIDEO with a URI), whose streams could pass for a variant's own,
//  * the programs do not match the variants one for one (count, BANDWIDTH),
//  * the stream is in no program or in several (a shared rendition),
//  * its variant_bitrate is not its program's, or the variant has no URL.
int variant_of_stream(const std::vector<ProgramInfo>& programs, int stream,
                      std::int64_t stream_bitrate,
                      const std::vector<VariantStream>& variants,
                      const std::vector<MediaRendition>& renditions);

// The variants FFmpeg was given, in program order: `variants` itself, or —
// playback opened a deduplicated copy of the master (HlsMaster.h) — the
// entries `kept` lists. Empty if `kept` has an index out of range.
std::vector<VariantStream> opened_variants(const std::vector<VariantStream>& variants,
                                           const std::vector<int>& kept);

// The EXT-X-STREAM-INF being played (index into `variants`), given the one
// whose playlist carries the video (`video_variant`, from
// variant_index_for) and the routed audio stream `audio` (-1 = none): the
// entry for that video playlist with the audio's group — the group of the
// variants (programs) the audio stream belongs to. Falls back to
// video_variant when the audio's group is unknown or not listed with it.
int playing_variant(int video_variant, int audio, const std::vector<ProgramInfo>& programs,
                    const std::vector<VariantStream>& variants, const std::vector<int>& kept);

// The EXT-X-STREAM-INF being played with video stream `video` and audio
// stream `audio` (-1 = none) routed, from the structure gathered at open:
// `programs` (programs_of) and `video_variant` (variant_index_for of every
// stream). Master opened as served (`kept` empty, a program per variant):
// the first program carrying both, else playing_variant() — which also
// covers a deduplicated copy. -1 = unknown.
int current_variant(const std::vector<ProgramInfo>& programs, const std::vector<int>& video_variant,
                    int video, int audio, const std::vector<VariantStream>& variants,
                    const std::vector<int>& kept);

// One media playlist's segment timeline, as the hls demuxer lays it out:
// segment i starts sum(durations[0..i)) after the first one.
struct MediaPlaylist {
    std::vector<double> durations;     // EXTINF of each media segment, in order
    bool vod           = false;        // EXT-X-ENDLIST / PLAYLIST-TYPE:VOD: no segment will be added
    bool discontinuity = false;        // EXT-X-DISCONTINUITY: timestamps may jump
    bool gap           = false;        // EXT-X-GAP: a segment without media
    bool independent   = false;        // EXT-X-INDEPENDENT-SEGMENTS
};
// `text` as a media playlist; durations empty if it is not one (no #EXTM3U
// header, a master playlist, no segments, an EXTINF without a duration).
MediaPlaylist parse_media_playlist(const std::string& text);

// ── FFmpeg glue ──────────────────────────────────────────────────────────────

// Programs of an hls demuxer context; empty for any other input.
std::vector<ProgramInfo> programs_of(const AVFormatContext* fc);
// "variant_bitrate" metadata of stream `idx`, -1 if absent / out of range.
std::int64_t stream_variant_bitrate(const AVFormatContext* fc, int idx);
// Index into m.variants() of the EXT-X-STREAM-INF whose own playlist carries
// video stream `idx` of playback context `fc` (opened from `m`'s master, or
// from the deduplicated copy keeping `kept`); -1 when unknown (any non-HLS
// input, a stream that is not video).
int variant_index_for(const AVFormatContext* fc, int idx, const ManifestParser& m,
                      const std::vector<int>& kept = {});

} // namespace hls
