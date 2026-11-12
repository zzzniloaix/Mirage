#pragma once

// The SCTE-35 splice_info_section decoder (DECISIONS.md D-070; WP05 of
// eval/phase3-survey/design.md §14, §5.5): a message's bytes, from the base64
// or the hexadecimal text an HLS tag carries it in, decoded into its fields,
// in the layout of SCTE 35 2023r1 as eval/phase3-survey/scte35.md §3 gives it,
// each struct below naming its table. What the fields mean on the timeline
// is src/Scte35Events.h's; nothing here reads a file or knows a playlist. No
// FFmpeg but libavutil: av_crc for the CRC_32 and av_base64_decode for base64,
// the design's P-L; unit-tested on sections the tests build from field values
// (tests/Scte35TestSections.h, tests/test_scte35.cpp; D-107).
//
// The rules.
//
// - Text (from_base64(), from_hex()). Base64 is RFC 4648 §4's, with its
//   padding: a multiple of 4 characters of its alphabet, '=' at the end
//   only. Anything else, a blank or a line break included, is refused rather
//   than skipped, since av_base64_decode() stops at the first '=' and takes
//   unpadded text, which RFC 4648 doesn't (D-109 (e)). Hex is an even number
//   of hex digits in either case after an optional 0x or 0X, which takes RFC
//   8216 §4.2's hexadecimal-sequence, the form of an EXT-X-DATERANGE's
//   SCTE35-*.
// - Bounds. The decoder never reads past its input, nor a structure past its
//   own length: the section past 3 + section_length, the command past
//   splice_command_length (unless that is 0xFFF, the legacy value after which
//   the command is as long as its syntax), the descriptor loop past
//   descriptor_loop_length, a descriptor past its descriptor_length (§10.1).
//   Input bytes after the section are counted, not read.
// - What leaves a section undecoded (SpliceInfo::error): an input shorter
//   than its section, or a section too short for its header and CRC_32; a
//   table_id other than 0xFC; a protocol_version other than 0, the only one
//   SCTE 35 2023r1 defines, a later version's tables being free to differ in
//   structure (§9.6.1), both D-109 (e)'s; a field that runs past the
//   structure holding it; a command whose length isn't known, a private or a
//   reserved one under splice_command_length 0xFFF; a command that doesn't
//   end on a byte; a descriptor whose descriptor_length runs past the
//   descriptor loop; and an encrypted section (encrypted_packet 1), whose
//   command and descriptors aren't decoded, the work order putting encrypted
//   streams out of scope (design §5.5). The fields read before the error keep
//   their values: an encrypted section keeps its header, which is in the
//   clear, and its CRC_32.
// - The CRC_32 is CRC-32/MPEG-2 over the whole section, table_id through
//   CRC_32, before any decryption (§9.6.1; scte35.md §3.6), checked as
//   FFmpeg's MPEG-TS demuxer checks a section: av_crc() of the
//   AV_CRC_32_IEEE table from UINT32_MAX over the section, CRC_32 included,
//   is 0 (P-L). A section whose CRC_32 fails is still decoded; what becomes
//   of it is the events' rule (D-106 (c)).
// - Descriptors (§10.1, Table 16). Those of the identifier CUEI with tags
//   0x00 to 0x04 are decoded (avail, DTMF, segmentation, time, audio); every
//   other one, a private identifier's or a tag SCTE reserves, is skipped by
//   its length, its bytes kept. A decoded descriptor whose fields run past
//   its length keeps the reason in Descriptor::error and its bytes, without
//   failing the section, and the bytes its fields leave are counted. A
//   segmentation_descriptor keeps its UPID's type and bytes, undecoded
//   (scte35.md §3.4), and has sub_segment_num and sub_segments_expected only
//   where Table 20's types allow them and its descriptor_length holds them.
// - Times. pts_time, pts_adjustment, the durations and pts_offset are ticks
//   of a 90 kHz clock; a splice time is (pts_time + pts_adjustment) mod 2^33,
//   the carry ignored (§9.6.1; scte35.md §3.5), which splice_times() gives
//   with the raw value. splice_schedule's utc_splice_time is a UTC time in
//   seconds since 1980-01-06, not a PTS, and gives no splice time.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace scte35 {

// The 90 kHz clock of pts_time, pts_adjustment and every duration, and the
// wrap of its 33-bit values (§9.6.1).
inline constexpr std::int64_t kClock   = 90000;
inline constexpr std::int64_t kPtsWrap = std::int64_t{ 1 } << 33;

// The table_id of every splice_info_section (Table 5).
inline constexpr std::uint8_t kTableId = 0xFC;
// The identifier of the descriptors SCTE 35 defines, "CUEI" (§10.2).
inline constexpr std::uint32_t kCuei = 0x43554549;
// splice_command_length's legacy value, after which the command's own syntax
// gives its length (Table 5's semantics).
inline constexpr std::uint16_t kLegacyCommandLength = 0xFFF;

// splice_command_type (Table 7).
inline constexpr std::uint8_t kSpliceNull           = 0x00;
inline constexpr std::uint8_t kSpliceSchedule       = 0x04;
inline constexpr std::uint8_t kSpliceInsert         = 0x05;
inline constexpr std::uint8_t kTimeSignal           = 0x06;
inline constexpr std::uint8_t kBandwidthReservation = 0x07;
inline constexpr std::uint8_t kPrivateCommand       = 0xFF;

// "splice_null", "splice_schedule", "splice_insert", "time_signal",
// "bandwidth_reservation", "private_command", or "reserved" for any other
// value (Table 7).
std::string_view command_name(std::uint8_t splice_command_type);

// splice_descriptor_tag under the identifier CUEI (Table 16).
inline constexpr std::uint8_t kAvailDescriptor        = 0x00;
inline constexpr std::uint8_t kDtmfDescriptor         = 0x01;
inline constexpr std::uint8_t kSegmentationDescriptor = 0x02;
inline constexpr std::uint8_t kTimeDescriptor         = 0x03;
inline constexpr std::uint8_t kAudioDescriptor        = 0x04;

// A segmentation_type_id's name in Table 23, as scte35.md §3.4 and
// eval/phase3-survey/scte35_evidence/tools/scte35_decode.py give them:
// "Break Start", "Provider Advertisement End", ...; "reserved" for a value
// the table doesn't name.
std::string_view segmentation_type_name(std::uint8_t segmentation_type_id);

// ── The fields ───────────────────────────────────────────────────────────────

// splice_time() (Table 14).
struct SpliceTime {
    bool         time_specified = false;
    std::int64_t pts_time = 0;              // 33 bits, as written; 0 without time_specified
};

// break_duration() (Table 15).
struct BreakDuration {
    bool         auto_return = false;
    std::int64_t duration = 0;              // 33 bits, 90 kHz ticks
};

// splice_insert() (Table 10). A cancelled event has its id and its cancel
// indicator only.
struct SpliceInsert {
    std::uint32_t splice_event_id = 0;
    bool          cancel = false;           // splice_event_cancel_indicator
    bool          out_of_network = false;   // out_of_network_indicator: 1 an Out Point, 0 an In Point
    bool          program_splice = false;   // program_splice_flag; 0: component mode (deprecated)
    bool          duration_flag = false;
    bool          splice_immediate = false; // splice_immediate_flag: no splice_time()
    bool          event_id_compliance = false;   // event_id_compliance_flag
    std::optional<SpliceTime> splice_time;  // program mode, not immediate
    struct Component {
        std::uint8_t tag = 0;               // component_tag
        std::optional<SpliceTime> splice_time;   // not immediate
    };
    std::vector<Component> components;      // component mode
    std::optional<BreakDuration> break_duration;   // duration_flag
    std::uint16_t unique_program_id = 0;
    std::uint8_t  avail_num = 0;
    std::uint8_t  avails_expected = 0;
};

// time_signal() (Table 11): a splice_time() alone.
struct TimeSignal {
    SpliceTime splice_time;
};

// splice_schedule() (Table 9): each event at a UTC time, not a PTS.
struct ScheduledEvent {
    std::uint32_t splice_event_id = 0;
    bool          cancel = false;
    bool          event_id_compliance = false;
    bool          out_of_network = false;
    bool          program_splice = false;
    bool          duration_flag = false;
    std::optional<std::uint32_t> utc_splice_time;   // program mode
    struct Component {
        std::uint8_t  tag = 0;
        std::uint32_t utc_splice_time = 0;
    };
    std::vector<Component> components;      // component mode
    std::optional<BreakDuration> break_duration;
    std::uint16_t unique_program_id = 0;
    std::uint8_t  avail_num = 0;
    std::uint8_t  avails_expected = 0;
};
struct SpliceSchedule {
    std::vector<ScheduledEvent> events;
};

// private_command() (Table 13).
struct PrivateCommand {
    std::uint32_t identifier = 0;
    std::vector<std::uint8_t> bytes;        // its private bytes
};

// avail_descriptor() (Table 18).
struct AvailDescriptor {
    std::uint32_t provider_avail_id = 0;
};

// DTMF_descriptor() (Table 19).
struct DtmfDescriptor {
    std::uint8_t preroll = 0;
    std::string  chars;                     // its DTMF_char bytes, as written
};

// segmentation_descriptor() (Table 20). A cancelled event has its id and its
// two indicators only.
struct SegmentationDescriptor {
    std::uint32_t event_id = 0;             // segmentation_event_id
    bool          cancel = false;           // segmentation_event_cancel_indicator
    bool          event_id_compliance = false;   // segmentation_event_id_compliance_indicator
    bool          program_segmentation = false;  // program_segmentation_flag; 0: a component loop
    bool          duration_flag = false;    // segmentation_duration_flag
    bool          delivery_not_restricted = false;   // delivery_not_restricted_flag
    // Where delivery is restricted (delivery_not_restricted_flag 0):
    bool          web_delivery_allowed = false;
    bool          no_regional_blackout = false;
    bool          archive_allowed = false;
    std::uint8_t  device_restrictions = 0;  // 2 bits (Table 21)
    struct Component {
        std::uint8_t tag = 0;               // component_tag
        std::int64_t pts_offset = 0;        // 33 bits, 90 kHz ticks
    };
    std::vector<Component> components;      // without program_segmentation (deprecated)
    std::optional<std::int64_t> duration;   // segmentation_duration: 40 bits, 90 kHz ticks
    std::uint8_t  upid_type = 0;            // segmentation_upid_type (Table 22)
    std::vector<std::uint8_t> upid;         // segmentation_upid(), as written
    std::uint8_t  type_id = 0;              // segmentation_type_id (Table 23)
    std::uint8_t  segment_num = 0;
    std::uint8_t  segments_expected = 0;
    std::optional<std::uint8_t> sub_segment_num, sub_segments_expected;
};

// time_descriptor() (Table 27).
struct TimeDescriptor {
    std::int64_t  tai_seconds = 0;          // 48 bits
    std::uint32_t tai_ns = 0;
    std::uint16_t utc_offset = 0;
};

// audio_descriptor() (Table 28).
struct AudioChannel {
    std::uint8_t component_tag = 0;
    std::string  iso_code;                  // its 3 bytes, as written
    std::uint8_t bit_stream_mode = 0;       // 3 bits
    std::uint8_t num_channels = 0;          // 4 bits
    bool         full_srvc_audio = false;
};
struct AudioDescriptor {
    std::vector<AudioChannel> channels;
};

// One splice_descriptor() of the descriptor loop (Table 17).
struct Descriptor {
    std::uint8_t tag = 0;                   // splice_descriptor_tag
    std::uint8_t length = 0;                // descriptor_length: the bytes after it
    std::optional<std::uint32_t> identifier;   // nullopt: a length under 4, which holds none
    std::vector<std::uint8_t> bytes;        // the bytes after the identifier, as written
    // The fields of a CUEI descriptor of tag 0x00 to 0x04; monostate for
    // every other one, and for one whose fields run past its length.
    std::variant<std::monostate, AvailDescriptor, DtmfDescriptor, SegmentationDescriptor,
                 TimeDescriptor, AudioDescriptor> body;
    std::size_t unparsed = 0;               // a decoded one's bytes after its fields
    std::string error;                      // why a CUEI one of tag 0x00 to 0x04 has no fields
};

// One splice_info_section() (Table 5) as decode() read it.
struct SpliceInfo {
    // Empty when the section decoded; otherwise why it didn't, the fields
    // read before that keeping their values (see the rules above).
    std::string error;

    std::size_t   input_bytes = 0;          // the bytes given
    std::size_t   bytes_after = 0;          // those after the section

    std::uint8_t  table_id = 0;
    bool          section_syntax_indicator = false;
    bool          private_indicator = false;
    std::uint8_t  sap_type = 0;             // 2 bits (Table 6)
    std::uint16_t section_length = 0;       // 12 bits: the section's bytes after this field
    std::uint8_t  protocol_version = 0;
    bool          encrypted_packet = false;
    std::uint8_t  encryption_algorithm = 0; // 6 bits (Table 29)
    std::int64_t  pts_adjustment = 0;       // 33 bits, 90 kHz ticks
    std::uint8_t  cw_index = 0;
    std::uint16_t tier = 0;                 // 12 bits
    std::uint16_t splice_command_length = 0;   // 12 bits, or kLegacyCommandLength
    std::uint8_t  splice_command_type = 0;
    // The command: SpliceInsert, TimeSignal, SpliceSchedule or
    // PrivateCommand; monostate for splice_null and bandwidth_reservation,
    // which are empty, for a reserved command, and for one not decoded.
    std::variant<std::monostate, SpliceInsert, TimeSignal, SpliceSchedule, PrivateCommand> command;
    std::vector<std::uint8_t> command_bytes;   // a reserved command's bytes, as written
    std::size_t   command_unused = 0;       // bytes splice_command_length gives past the syntax's end
    std::uint16_t descriptor_loop_length = 0;
    std::vector<Descriptor> descriptors;
    std::size_t   alignment_stuffing = 0;   // bytes between the descriptor loop and CRC_32

    bool          has_crc = false;          // the section's bytes were there to check
    std::uint32_t crc_32 = 0;               // the CRC_32 field
    std::uint32_t crc_computed = 0;         // CRC-32/MPEG-2 of the section's bytes before it
    bool          crc_valid = false;        // the check of the rules above

    bool decoded() const { return error.empty(); }
};

// ── Decoding ─────────────────────────────────────────────────────────────────

// The bytes of base64 text (RFC 4648 §4, with its padding), decoded by
// av_base64_decode(); nullopt, with `err` saying why, for any other text.
std::optional<std::vector<std::uint8_t>> from_base64(std::string_view text, std::string& err);

// The bytes of hex text: an optional 0x or 0X, then an even number of hex
// digits in either case; nullopt, with `err` saying why, for any other text.
std::optional<std::vector<std::uint8_t>> from_hex(std::string_view text, std::string& err);

// The section at the start of `input`, read by the rules above.
SpliceInfo decode(std::span<const std::uint8_t> input);

// CRC-32/MPEG-2 of `bytes` (polynomial 0x04C11DB7, from 0xFFFFFFFF, neither
// reflected nor inverted: scte35.md §3.6), computed by av_crc(), as the
// CRC_32 field holds it, most significant byte first.
std::uint32_t crc32_mpeg2(std::span<const std::uint8_t> bytes);

// (a + b) mod 2^33, the carry ignored: for two 33-bit values, in [0, 2^33).
std::int64_t add_pts(std::int64_t a, std::int64_t b);

// A splice time a decoded section gives (scte35.md §3.5).
struct SplicePoint {
    std::int64_t pts_time = 0;              // as written
    std::int64_t adjusted = 0;              // (pts_time + pts_adjustment) mod 2^33
    std::optional<std::uint8_t> component_tag;   // a splice_insert's in component mode
};

// The splice times of `info`, each with time_specified_flag set: a
// time_signal's; a splice_insert's in program mode, or each component's in
// component mode, unless it is immediate or cancelled. None for any other
// command, nor for a section that didn't decode.
std::vector<SplicePoint> splice_times(const SpliceInfo& info);

} // namespace scte35
