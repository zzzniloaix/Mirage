#pragma once

// A media playlist's events, its cues and their ad breaks (DECISIONS.md D-076): filled in by WP04.

extern "C" {
#include <libavutil/rational.h>
}

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace timeline {

// What carried an SCTE-35 message.
enum class CueCarrier : std::uint8_t {
    PlaylistTag,        // D-070's tags: OATCLS, DATERANGE's SCTE35-*, CUE-OUT-CONT's SCTE35=
    DashEventStream,    // an event of a DASH EventStream
    TsSection,          // a section of an MPEG-TS stream (stream type 0x86)
    Id3Priv,            // an ID3 PRIV frame in a segment
};

// A tag's place in its playlist: the segment whose URI follows it, and its line.
struct PlaylistAnchor {
    int          variant = -1;              // index into Timeline::variants
    std::int64_t segment = 0;               // media sequence number
    int          line    = 0;               // the tag's, from 1
};

// A media PTS on the carrying variant's own clock.
struct MediaPtsAnchor {
    int          variant   = -1;            // index into Timeline::variants
    std::int64_t pts       = 0;             // ticks of time_base
    AVRational   time_base = { 0, 1 };
    std::optional<std::int64_t> segment;    // media sequence number, where known
};

// A time in a DASH period.
struct PeriodAnchor {
    std::string  period;                    // the period's id
    std::int64_t presentation_time = 0;     // the event's presentationTime, in timescale units
    std::int64_t timescale = 1;
};

// An SCTE-35 message from any carrier, with the anchor its carrier gives:
// the interface of DECISIONS.md D-070 through which every message reaches
// the decoder (src/Scte35Events.h; eval/phase3-survey/design.md §5.5).
// Phase 3 reads playlist tags only; the other carriers are declared so that
// a reader of them needs no change of these types.
struct CueInput {
    CueCarrier carrier = CueCarrier::PlaylistTag;
    std::variant<PlaylistAnchor, MediaPtsAnchor, PeriodAnchor> anchor;
    std::string form;                       // the tag or attribute that carried it: "OATCLS",
                                            // "DATERANGE SCTE35-OUT", "CUE-OUT-CONT SCTE35", ...
    // The message as its carrier gives it, for the decoder to read: a tag's
    // value as written (base64 or hex), a section's bytes from the others.
    std::vector<std::uint8_t> bytes;
};

} // namespace timeline
