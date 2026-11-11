#include "Scte35Events.h"

#include "TimelineBuild.h"

extern "C" {
#include <libavutil/mathematics.h>
}

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <span>
#include <utility>
#include <variant>

namespace timeline {

namespace {

using scte35::SpliceInfo;

// Half of the 33-bit clock: a difference of PTS modulo 2^33 below it is
// "at or after", the usual reading of a clock that wraps.
constexpr std::int64_t kHalfWrap = scte35::kPtsWrap / 2;

// The forms of the messages that the rules name (src/PlaylistEvents.cpp
// writes them): a cue tag's own SCTE35=, and an EXT-X-DATERANGE's
// SCTE35-OUT and SCTE35-IN.
constexpr std::string_view kCueOutForm   = "CUE-OUT SCTE35";
constexpr std::string_view kContForm     = "CUE-OUT-CONT SCTE35";
constexpr std::string_view kCueInForm    = "CUE-IN SCTE35";
constexpr std::string_view kRangeOutForm = "DATERANGE SCTE35-OUT";
constexpr std::string_view kRangeInForm  = "DATERANGE SCTE35-IN";

// Read::signal of a message that is no cue-out message of its own.
constexpr std::size_t kNoSignal = static_cast<std::size_t>(-1);

// ── Payload values ───────────────────────────────────────────────────────────

Value int_value(std::int64_t v) { return Value{ v }; }
Value text_value(std::string s) { return Value{ std::move(s) }; }
Value flag_value(bool b) { return Value{ b }; }
Value number_value(double d) { return Value{ d }; }
Value opt_int(const std::optional<std::int64_t>& v) { return v ? int_value(*v) : Value{}; }
Value opt_number(const std::optional<double>& v) { return v ? number_value(*v) : Value{}; }

// Bytes as a hexadecimal-sequence, "0x" and two digits a byte; "" for none.
Value hex_value(std::span<const std::uint8_t> bytes)
{
    if (bytes.empty()) return text_value("");
    std::string s = "0x";
    for (std::uint8_t b : bytes) s += std::format("{:02X}", b);
    return text_value(std::move(s));
}

const Value* member(const Payload& p, std::string_view name)
{
    for (const auto& [n, v] : p)
        if (n == name) return &v;
    return nullptr;
}

Value* member(Payload& p, std::string_view name)
{
    for (auto& [n, v] : p)
        if (n == name) return &v;
    return nullptr;
}

std::optional<std::int64_t> int_member(const Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    if (const auto* i = v ? std::get_if<std::int64_t>(&v->v) : nullptr) return *i;
    return std::nullopt;
}

bool is_number(const Value& v)
{
    return std::holds_alternative<double>(v.v) || std::holds_alternative<std::int64_t>(v.v);
}

// ── The decoded message, by the standard's names ─────────────────────────────

Value splice_time_value(const scte35::SpliceTime& t, std::int64_t adjustment)
{
    return Value{ Value::Object{
        { "time_specified_flag", flag_value(t.time_specified) },
        { "pts_time", t.time_specified ? int_value(t.pts_time) : Value{} },
        { "pts_adjusted", t.time_specified ? int_value(scte35::add_pts(t.pts_time, adjustment)) : Value{} },
    } };
}

Value break_value(const std::optional<scte35::BreakDuration>& b)
{
    if (!b) return Value{};
    return Value{ Value::Object{
        { "auto_return", flag_value(b->auto_return) },
        { "duration", int_value(b->duration) },
        { "seconds", number_value(static_cast<double>(b->duration) / scte35::kClock) },
    } };
}

Value insert_value(const scte35::SpliceInsert& c, std::int64_t adjustment)
{
    Value::Object o;
    o.emplace_back("splice_event_id", int_value(c.splice_event_id));
    o.emplace_back("splice_event_cancel_indicator", flag_value(c.cancel));
    if (c.cancel) return Value{ std::move(o) };
    o.emplace_back("out_of_network_indicator", flag_value(c.out_of_network));
    o.emplace_back("program_splice_flag", flag_value(c.program_splice));
    o.emplace_back("duration_flag", flag_value(c.duration_flag));
    o.emplace_back("splice_immediate_flag", flag_value(c.splice_immediate));
    o.emplace_back("event_id_compliance_flag", flag_value(c.event_id_compliance));
    o.emplace_back("splice_time", c.splice_time ? splice_time_value(*c.splice_time, adjustment) : Value{});
    Value::List components;
    for (const auto& k : c.components)
        components.push_back(Value{ Value::Object{
            { "component_tag", int_value(k.tag) },
            { "splice_time", k.splice_time ? splice_time_value(*k.splice_time, adjustment) : Value{} },
        } });
    o.emplace_back("components", Value{ std::move(components) });
    o.emplace_back("break_duration", break_value(c.break_duration));
    o.emplace_back("unique_program_id", int_value(c.unique_program_id));
    o.emplace_back("avail_num", int_value(c.avail_num));
    o.emplace_back("avails_expected", int_value(c.avails_expected));
    return Value{ std::move(o) };
}

Value schedule_value(const scte35::SpliceSchedule& s)
{
    Value::List events;
    for (const scte35::ScheduledEvent& e : s.events) {
        Value::Object o;
        o.emplace_back("splice_event_id", int_value(e.splice_event_id));
        o.emplace_back("splice_event_cancel_indicator", flag_value(e.cancel));
        o.emplace_back("event_id_compliance_flag", flag_value(e.event_id_compliance));
        if (!e.cancel) {
            o.emplace_back("out_of_network_indicator", flag_value(e.out_of_network));
            o.emplace_back("program_splice_flag", flag_value(e.program_splice));
            o.emplace_back("duration_flag", flag_value(e.duration_flag));
            o.emplace_back("utc_splice_time", e.utc_splice_time ? int_value(*e.utc_splice_time) : Value{});
            Value::List components;
            for (const auto& k : e.components)
                components.push_back(Value{ Value::Object{
                    { "component_tag", int_value(k.tag) },
                    { "utc_splice_time", int_value(k.utc_splice_time) },
                } });
            o.emplace_back("components", Value{ std::move(components) });
            o.emplace_back("break_duration", break_value(e.break_duration));
            o.emplace_back("unique_program_id", int_value(e.unique_program_id));
            o.emplace_back("avail_num", int_value(e.avail_num));
            o.emplace_back("avails_expected", int_value(e.avails_expected));
        }
        events.push_back(Value{ std::move(o) });
    }
    return Value{ Value::Object{ { "events", Value{ std::move(events) } } } };
}

Value segmentation_value(const scte35::SegmentationDescriptor& d)
{
    Value::Object o;
    o.emplace_back("segmentation_event_id", int_value(d.event_id));
    o.emplace_back("segmentation_event_cancel_indicator", flag_value(d.cancel));
    o.emplace_back("segmentation_event_id_compliance_indicator", flag_value(d.event_id_compliance));
    if (d.cancel) return Value{ std::move(o) };
    o.emplace_back("program_segmentation_flag", flag_value(d.program_segmentation));
    o.emplace_back("segmentation_duration_flag", flag_value(d.duration_flag));
    o.emplace_back("delivery_not_restricted_flag", flag_value(d.delivery_not_restricted));
    if (!d.delivery_not_restricted) {
        o.emplace_back("web_delivery_allowed_flag", flag_value(d.web_delivery_allowed));
        o.emplace_back("no_regional_blackout_flag", flag_value(d.no_regional_blackout));
        o.emplace_back("archive_allowed_flag", flag_value(d.archive_allowed));
        o.emplace_back("device_restrictions", int_value(d.device_restrictions));
    }
    Value::List components;
    for (const auto& k : d.components)
        components.push_back(Value{ Value::Object{
            { "component_tag", int_value(k.tag) },
            { "pts_offset", int_value(k.pts_offset) },
        } });
    o.emplace_back("components", Value{ std::move(components) });
    o.emplace_back("segmentation_duration", opt_int(d.duration));
    o.emplace_back("segmentation_upid_type", int_value(d.upid_type));
    o.emplace_back("segmentation_upid", hex_value(d.upid));
    o.emplace_back("segmentation_type_id", int_value(d.type_id));
    o.emplace_back("segmentation_type_name",
                   text_value(std::string(scte35::segmentation_type_name(d.type_id))));
    o.emplace_back("segment_num", int_value(d.segment_num));
    o.emplace_back("segments_expected", int_value(d.segments_expected));
    const auto opt8 = [](const std::optional<std::uint8_t>& v) { return v ? int_value(*v) : Value{}; };
    o.emplace_back("sub_segment_num", opt8(d.sub_segment_num));
    o.emplace_back("sub_segments_expected", opt8(d.sub_segments_expected));
    return Value{ std::move(o) };
}

std::string descriptor_kind(const scte35::Descriptor& d)
{
    if (!d.identifier) return "malformed_descriptor";
    if (*d.identifier != scte35::kCuei) return "private_descriptor";
    switch (d.tag) {
    case scte35::kAvailDescriptor:        return "avail_descriptor";
    case scte35::kDtmfDescriptor:         return "DTMF_descriptor";
    case scte35::kSegmentationDescriptor: return "segmentation_descriptor";
    case scte35::kTimeDescriptor:         return "time_descriptor";
    case scte35::kAudioDescriptor:        return "audio_descriptor";
    default:                              return "reserved_descriptor";
    }
}

Value descriptor_value(const scte35::Descriptor& d)
{
    Value::Object o;
    o.emplace_back("splice_descriptor_tag", int_value(d.tag));
    o.emplace_back("descriptor_length", int_value(d.length));
    o.emplace_back("identifier", d.identifier ? int_value(*d.identifier) : Value{});
    o.emplace_back("name", text_value(descriptor_kind(d)));
    if (const auto* avail = std::get_if<scte35::AvailDescriptor>(&d.body)) {
        o.emplace_back("provider_avail_id", int_value(avail->provider_avail_id));
    } else if (const auto* dtmf = std::get_if<scte35::DtmfDescriptor>(&d.body)) {
        o.emplace_back("preroll", int_value(dtmf->preroll));
        o.emplace_back("dtmf_chars", text_value(dtmf->chars));
    } else if (const auto* seg = std::get_if<scte35::SegmentationDescriptor>(&d.body)) {
        Value fields = segmentation_value(*seg);
        for (auto& m : std::get<Value::Object>(fields.v)) o.push_back(std::move(m));
    } else if (const auto* time = std::get_if<scte35::TimeDescriptor>(&d.body)) {
        o.emplace_back("TAI_seconds", int_value(time->tai_seconds));
        o.emplace_back("TAI_ns", int_value(time->tai_ns));
        o.emplace_back("UTC_offset", int_value(time->utc_offset));
    } else if (const auto* audio = std::get_if<scte35::AudioDescriptor>(&d.body)) {
        Value::List channels;
        for (const scte35::AudioChannel& c : audio->channels)
            channels.push_back(Value{ Value::Object{
                { "component_tag", int_value(c.component_tag) },
                { "ISO_code", text_value(c.iso_code) },
                { "Bit_Stream_Mode", int_value(c.bit_stream_mode) },
                { "Num_Channels", int_value(c.num_channels) },
                { "Full_Srvc_Audio", flag_value(c.full_srvc_audio) },
            } });
        o.emplace_back("audio_channels", Value{ std::move(channels) });
    } else {
        // Skipped by its length (a private or reserved one), or not decoded.
        o.emplace_back("bytes", hex_value(d.bytes));
    }
    if (!std::holds_alternative<std::monostate>(d.body) && d.unparsed > 0)
        o.emplace_back("unparsed_bytes", int_value(static_cast<std::int64_t>(d.unparsed)));
    if (!d.error.empty()) o.emplace_back("error", text_value(d.error));
    return Value{ std::move(o) };
}

// Every field decode() reads of `s`, a decoded section.
Value message_value(const SpliceInfo& s)
{
    Value::Object o;
    o.emplace_back("table_id", int_value(s.table_id));
    o.emplace_back("section_syntax_indicator", flag_value(s.section_syntax_indicator));
    o.emplace_back("private_indicator", flag_value(s.private_indicator));
    o.emplace_back("sap_type", int_value(s.sap_type));
    o.emplace_back("section_length", int_value(s.section_length));
    o.emplace_back("protocol_version", int_value(s.protocol_version));
    o.emplace_back("encrypted_packet", flag_value(s.encrypted_packet));
    o.emplace_back("encryption_algorithm", int_value(s.encryption_algorithm));
    o.emplace_back("pts_adjustment", int_value(s.pts_adjustment));
    o.emplace_back("cw_index", int_value(s.cw_index));
    o.emplace_back("tier", int_value(s.tier));
    o.emplace_back("splice_command_length", int_value(s.splice_command_length));
    o.emplace_back("splice_command_type", int_value(s.splice_command_type));
    const std::string name(scte35::command_name(s.splice_command_type));
    o.emplace_back("command", text_value(name));
    if (const auto* c = std::get_if<scte35::SpliceInsert>(&s.command)) {
        o.emplace_back(name, insert_value(*c, s.pts_adjustment));
    } else if (const auto* t = std::get_if<scte35::TimeSignal>(&s.command)) {
        Value::Object signal;
        signal.emplace_back("splice_time", splice_time_value(t->splice_time, s.pts_adjustment));
        o.emplace_back(name, Value{ std::move(signal) });
    } else if (const auto* e = std::get_if<scte35::SpliceSchedule>(&s.command)) {
        o.emplace_back(name, schedule_value(*e));
    } else if (const auto* p = std::get_if<scte35::PrivateCommand>(&s.command)) {
        o.emplace_back(name, Value{ Value::Object{ { "identifier", int_value(p->identifier) },
                                                   { "private_bytes", hex_value(p->bytes) } } });
    } else if (!s.command_bytes.empty()) {
        o.emplace_back("command_bytes", hex_value(s.command_bytes));
    }
    o.emplace_back("command_unused_bytes", int_value(static_cast<std::int64_t>(s.command_unused)));
    o.emplace_back("descriptor_loop_length", int_value(s.descriptor_loop_length));
    Value::List descriptors;
    for (const scte35::Descriptor& d : s.descriptors) descriptors.push_back(descriptor_value(d));
    o.emplace_back("descriptors", Value{ std::move(descriptors) });
    o.emplace_back("alignment_stuffing_bytes", int_value(static_cast<std::int64_t>(s.alignment_stuffing)));
    o.emplace_back("bytes_after_section", int_value(static_cast<std::int64_t>(s.bytes_after)));
    o.emplace_back("crc_32", int_value(s.crc_32));
    o.emplace_back("crc_32_computed", int_value(s.crc_computed));
    return Value{ std::move(o) };
}

// ── Classes (D-106 (b)) ──────────────────────────────────────────────────────

struct Classes {
    bool cancel = false, in = false, out = false;
    std::optional<std::int64_t> splice_event_id;            // a cancelled splice_insert's
    std::vector<std::int64_t>   segmentation_event_ids;     // the cancelled descriptors'
    std::vector<std::int64_t>   segmentation_types;         // the others' types
    // What makes a cue-out message one signal with another (D-109 (a)): an
    // Out Point's splice_event_id, and the segmentation_event_id of each
    // segmentation descriptor that makes a time_signal a cue_out.
    std::optional<std::int64_t> out_splice_event_id;
    std::vector<std::int64_t>   out_segmentation_event_ids;

    bool any_cue() const { return in || out; }
    Value names() const
    {
        Value::List l;
        if (in) l.push_back(text_value(std::string(kTypeCueIn)));
        if (out) l.push_back(text_value(std::string(kTypeCueOut)));
        if (cancel) l.push_back(text_value(std::string(kTypeCancel)));
        if (!in && !out && !cancel) l.push_back(text_value(std::string(kTypeMessage)));
        return Value{ std::move(l) };
    }
};

bool is_cue_type(const MessageOptions& o, std::uint8_t type)
{
    return std::find(o.cue_types.begin(), o.cue_types.end(), type) != o.cue_types.end();
}

// Every segmentation type of the cue types counts alike, whatever its level
// in the hierarchy SCTE 35 §10.3.3.7 sets for advertising segments, a Break
// above its Placement Opportunities and Ad Blocks (scte35.md §3.4): the
// owner's answer, D-109 (e), an exception to D-087's rule that no ad_break
// spans another. Messages of their own for the levels, each of D-106 (b)'s
// default cue types, are cues of their own, which pair_breaks() pairs by
// D-087's rule otherwise: a Break Start that declares a duration and then
// an advertisement's Start make a break that the Break's duration ends,
// around the advertisements' own breaks, so that one ad_break spans
// others; a Break Start that declares none makes no break, with
// pair_breaks()' warning, and its Break End ends the break of the
// advertisement open at it. A type's role is its name's in Table 23
// (segmentation_role(), D-109 (e)).
Classes classify(const SpliceInfo& s, const MessageOptions& options)
{
    Classes c;
    const auto* insert = std::get_if<scte35::SpliceInsert>(&s.command);
    if (insert) {
        if (insert->cancel) {
            c.cancel          = true;
            c.splice_event_id = insert->splice_event_id;
        } else if (insert->out_of_network) {
            c.out                 = true;
            c.out_splice_event_id = insert->splice_event_id;
        } else {
            c.in = true;
        }
    }
    const bool signal = std::holds_alternative<scte35::TimeSignal>(s.command);
    for (const scte35::Descriptor& d : s.descriptors) {
        const auto* seg = std::get_if<scte35::SegmentationDescriptor>(&d.body);
        if (!seg) continue;
        if (seg->cancel) {
            c.cancel = true;
            c.segmentation_event_ids.push_back(seg->event_id);
            continue;
        }
        c.segmentation_types.push_back(seg->type_id);
        if (!signal || !is_cue_type(options, seg->type_id)) continue;
        const auto role = segmentation_role(seg->type_id);
        if (role == SegmentationRole::Start) {
            c.out = true;
            c.out_segmentation_event_ids.push_back(seg->event_id);
        }
        if (role == SegmentationRole::End) c.in = true;
    }
    return c;
}

// A duration a message gives in ticks of its 90 kHz clock, its
// break_duration or its segmentation_duration: in seconds, as the payload
// has always written it, and as the whole number of ticks that the member
// beside it gives, which pair_breaks() reads first (D-109 (d)).
struct Ticked {
    double       seconds = 0;
    std::int64_t ticks   = 0;
};

// pair_breaks() reads the ticks as ticks of kDurationClock
// (src/PlaylistEvents.h), and a message gives them in ticks of its 90 kHz
// clock (src/Scte35.h): two constants of one clock.
static_assert(kDurationClock == scte35::kClock, "pair_breaks() reads the ticks of the messages' clock");

Ticked ticked(std::int64_t ticks)
{
    return { .seconds = static_cast<double>(ticks) / scte35::kClock, .ticks = ticks };
}

// The segmentation_duration of a cue of `role`: that of the first
// segmentation descriptor, not cancelled, of a cue type of that role, that
// gives one, in the loop's order (D-109 (e)); in seconds and in its ticks
// (D-109 (d)).
std::optional<Ticked> segmentation_seconds(const SpliceInfo& s, const MessageOptions& options,
                                           SegmentationRole role)
{
    for (const scte35::Descriptor& d : s.descriptors) {
        const auto* seg = std::get_if<scte35::SegmentationDescriptor>(&d.body);
        if (!seg || seg->cancel || !seg->duration || !is_cue_type(options, seg->type_id)) continue;
        if (segmentation_role(seg->type_id) != role) continue;
        return ticked(*seg->duration);
    }
    return std::nullopt;
}

// ── Frames ───────────────────────────────────────────────────────────────────

// A 90 kHz value modulo 2^33, in [0, 2^33).
std::int64_t wrap33(std::int64_t v)
{
    const std::int64_t r = v % scte35::kPtsWrap;
    return r < 0 ? r + scte35::kPtsWrap : r;
}

bool valid_base(AVRational tb) { return tb.num > 0 && tb.den > 0; }

// One variant's frames: where each sits on the reference's frames and where
// its segments start, by the rules build() places by (src/TimelineBuild.h,
// design §5.1), which tests/test_scte35_events.cpp checks against build()'s
// own events; and its frames' PTS, for a splice time.
class VariantFrames {
public:
    VariantFrames(const MessagePlacement& pl, int v)
        : frames_(pl.axis->frames), playlist_(pl.variants[static_cast<std::size_t>(v)].playlist),
          media_(pl.variants[static_cast<std::size_t>(v)].media)
    {
        if (v == 0) {
            time_base_ = pl.axis->time_base;
        } else {
            pair_      = &pl.scores->pairs[static_cast<std::size_t>(v - 1)];
            time_base_ = pair_->dis_time_base;
        }
    }

    const PlaylistEvents* playlist() const { return playlist_; }
    const VariantMedia*   media() const { return media_; }
    AVRational            time_base() const { return time_base_; }

    // Where the input's frame `index` sits: the reference's at itself, a
    // distorted one at the reference frame of the first scored frame from it
    // on, its own or, unpaired, the next paired one's (D-081 (d)); nullopt
    // after the input's last paired frame, or off the axis.
    std::optional<std::int64_t> at(std::int64_t index) const
    {
        if (!pair_) return on_axis(index);
        const std::size_t k = paired_from(index);
        return k < size() ? on_axis(pair_->ref_index[k]) : std::nullopt;
    }

    // Where segment `seq` starts, as build() places its segment event: its
    // first paired frame's reference frame, when the demux pass gave it
    // frames and its span on the reference's frames is one build() takes.
    std::optional<std::int64_t> segment_start(std::int64_t seq) const
    {
        const SegmentFrames* s = segment(seq);
        if (!s || s->frames <= 0) return std::nullopt;
        const std::int64_t first = s->first_index, end = first + s->frames;
        if (!pair_) {
            if (first < 0 || first >= frames_) return std::nullopt;
            return first;
        }
        const std::size_t k = paired_from(first);
        if (k >= size() || pair_->dis_index[k] >= end) return std::nullopt;
        const std::size_t  next = paired_from(end);
        const std::int64_t to   = next < size() ? pair_->ref_index[next] : pair_->ref_index[next - 1] + 1;
        const auto start = on_axis(pair_->ref_index[k]);
        if (!start || to <= *start || to > frames_) return std::nullopt;
        return start;
    }

    // The demux pass's segment of media sequence number `seq`; null when
    // there is none, or the playlist doesn't list it.
    const SegmentFrames* segment(std::int64_t seq) const
    {
        if (!media_ || (playlist_ && playlist_->timeline.index_of(seq) < 0)) return nullptr;
        for (const SegmentFrames& s : media_->segments)
            if (s.seq == seq) return &s;
        return nullptr;
    }

    // The media sequence number of the segment holding the input's frame
    // `index`; nullopt for none.
    std::optional<std::int64_t> segment_of(std::int64_t index) const
    {
        if (!media_) return std::nullopt;
        for (const SegmentFrames& s : media_->segments)
            if (index >= s.first_index && index < s.first_index + s.frames) return s.seq;
        return std::nullopt;
    }

    // The playlist's discontinuity sequence of segment `seq`; nullopt when
    // the playlist doesn't list it.
    std::optional<std::int64_t> sequence_of(std::int64_t seq) const
    {
        if (!playlist_) return std::nullopt;
        const int i = playlist_->timeline.index_of(seq);
        if (i < 0) return std::nullopt;
        return playlist_->timeline.segments[static_cast<std::size_t>(i)].disc_seq;
    }

    // How many discontinuity sequences the playlist has.
    std::size_t sequences() const
    {
        if (!playlist_) return 1;
        std::vector<std::int64_t> seen;
        for (const hls::Segment& s : playlist_->timeline.segments)
            if (std::find(seen.begin(), seen.end(), s.disc_seq) == seen.end()) seen.push_back(s.disc_seq);
        return seen.size();
    }

    // The input's frames in discontinuity sequence `disc` of its playlist,
    // in index order; every frame the demux pass gave when `disc` is
    // nullopt.
    std::vector<std::int64_t> sequence_frames(std::optional<std::int64_t> disc) const
    {
        std::vector<std::int64_t> out;
        if (!media_) return out;
        const auto count = static_cast<std::int64_t>(media_->pts.size());
        if (!disc) {
            for (std::int64_t i = 0; i < count; ++i) out.push_back(i);
            return out;
        }
        for (const SegmentFrames& s : media_->segments) {
            if (sequence_of(s.seq) != disc) continue;
            for (std::int64_t i = std::max<std::int64_t>(s.first_index, 0);
                 i < s.first_index + s.frames && i < count; ++i)
                out.push_back(i);
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    // Frame `index`'s PTS in 90 kHz ticks modulo 2^33.
    std::int64_t pts90(std::int64_t index) const
    {
        return wrap33(av_rescale_q(media_->pts[static_cast<std::size_t>(index)], time_base_,
                                   AVRational{ 1, static_cast<int>(scte35::kClock) }));
    }

private:
    std::int64_t            frames_ = 0;
    const PlaylistEvents*   playlist_ = nullptr;
    const VariantMedia*     media_ = nullptr;
    const PairScores*       pair_ = nullptr;   // null: the reference
    AVRational              time_base_ = { 0, 1 };

    std::size_t size() const { return std::min(pair_->dis_index.size(), pair_->ref_index.size()); }
    std::size_t paired_from(std::int64_t index) const
    {
        const auto end = pair_->dis_index.begin() + static_cast<std::ptrdiff_t>(size());
        return static_cast<std::size_t>(std::lower_bound(pair_->dis_index.begin(), end, index)
                                        - pair_->dis_index.begin());
    }
    std::optional<std::int64_t> on_axis(std::int64_t ref) const
    {
        if (ref < 0 || ref >= frames_) return std::nullopt;
        return ref;
    }
};

// The frames of one discontinuity sequence of a variant, in index order,
// with their PTS in 90 kHz ticks modulo 2^33: what a splice time is
// searched in, made once per sequence.
struct SequenceFrames {
    std::vector<std::int64_t> index;
    std::vector<std::int64_t> pts90;
};

SequenceFrames sequence_frames(const VariantFrames& f, std::optional<std::int64_t> disc)
{
    SequenceFrames s;
    s.index = f.sequence_frames(disc);
    s.pts90.reserve(s.index.size());
    for (std::int64_t i : s.index) s.pts90.push_back(f.pts90(i));
    return s;
}

// The first of the sequence's frames, in index order, whose PTS is at or
// after `t` (90 kHz ticks modulo 2^33), both measured from the first frame's
// PTS modulo 2^33 (design §5.1); nullopt, with `why` set, when there are no
// frames or `t` is before the first or after the last.
std::optional<std::int64_t> first_at_or_after(const SequenceFrames& s, std::int64_t t, std::string& why)
{
    if (s.index.empty()) {
        why = "has no frame of its variant to land on: the demux pass gave its discontinuity sequence none";
        return std::nullopt;
    }
    const std::int64_t p0 = s.pts90.front();
    const std::int64_t dt = wrap33(t - p0);
    if (dt >= kHalfWrap) {
        why = std::format("is before the first frame of its discontinuity sequence, frame {} of its "
                          "variant at PTS {} (90 kHz), so outside the sequence", s.index.front(), p0);
        return std::nullopt;
    }
    for (std::size_t k = 0; k < s.index.size(); ++k) {
        const std::int64_t d = wrap33(s.pts90[k] - p0);
        if (d < kHalfWrap && d >= dt) return s.index[k];
    }
    why = std::format("is after the last frame of its discontinuity sequence, frame {} of its variant at "
                      "PTS {} (90 kHz), so outside the sequence", s.index.back(), s.pts90.back());
    return std::nullopt;
}

// ── Text ─────────────────────────────────────────────────────────────────────

bool starts_with_hex_prefix(std::string_view s)
{
    return s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
}

std::optional<double> decimal(std::string_view s)
{
    double v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || end != s.data() + s.size() || !std::isfinite(v)) return std::nullopt;
    return v;
}

// Unix seconds in whole microseconds (see the rules).
std::int64_t micros(double unix) { return std::llround(unix * 1e6); }

std::string tag_line(const PlaylistTag& tag)
{
    return tag.value ? std::format("#{}:{}", tag.name, *tag.value) : "#" + tag.name;
}

std::string plural(std::size_t n, std::string_view one, std::string_view many)
{
    return std::format("{} {}", n, n == 1 ? one : many);
}

// ── One run ──────────────────────────────────────────────────────────────────

// The name of the member beside duration `name` that gives its ticks
// (src/PlaylistEvents.h, D-109 (d)).
std::string ticks_name(std::string_view name) { return std::string(name) + std::string(kTicksSuffix); }

// The durations a cue declares, by kDurationNames: in seconds, and beside
// break_duration and segmentation_duration, which a message gives in 90 kHz
// ticks, in those ticks too (D-109 (d)).
struct Durations {
    std::optional<double> duration, end_date, planned;
    std::optional<Ticked> break_duration, segmentation;

    // Duration `name`'s seconds, and its ticks where it is given in them.
    std::pair<std::optional<double>, std::optional<std::int64_t>> by_name(std::string_view name) const
    {
        const auto of = [](const std::optional<Ticked>& t) {
            return std::pair{ t ? std::optional<double>(t->seconds) : std::nullopt,
                              t ? std::optional<std::int64_t>(t->ticks) : std::nullopt };
        };
        if (name == "duration") return { duration, std::nullopt };
        if (name == "end_date_duration") return { end_date, std::nullopt };
        if (name == "planned_duration") return { planned, std::nullopt };
        if (name == "break_duration") return of(break_duration);
        if (name == "segmentation_duration") return of(segmentation);
        return {};
    }
    // Whether duration `name` is one a message gives in ticks.
    static bool in_ticks(std::string_view name)
    {
        return name == "break_duration" || name == "segmentation_duration";
    }

    // Every duration in a cue's payload, null where it isn't given, each
    // given in ticks with its ticks beside it.
    void put(Payload& p) const
    {
        for (std::string_view n : kDurationNames) {
            const auto [seconds, ticks] = by_name(n);
            p.emplace_back(std::string(n), opt_number(seconds));
            if (in_ticks(n)) p.emplace_back(ticks_name(n), opt_int(ticks));
        }
    }
    // Into the payload of a cue that takes a message: each duration the
    // cue lacks or has null, with its ticks beside it.
    void fill(Payload& p) const
    {
        for (std::string_view n : kDurationNames) {
            const auto [seconds, ticks] = by_name(n);
            if (!seconds) continue;
            const auto named = [&p](std::string_view name) {
                return std::find_if(p.begin(), p.end(), [name](const auto& m) { return m.first == name; });
            };
            auto at = named(n);
            if (at != p.end() && is_number(at->second)) continue;
            if (at != p.end()) at->second = number_value(*seconds);
            else at = p.emplace(p.end(), std::string(n), number_value(*seconds));
            if (!ticks) continue;
            const std::string t = ticks_name(n);
            if (const auto now = named(t); now != p.end()) now->second = int_value(*ticks);
            else p.emplace(at + 1, t, int_value(*ticks));
        }
    }
};

// One splice time of a message: its splice_point, where the time is placed,
// and the warning written of it, if any (design §5.1; D-106 (a)), made as
// the message is read and written in its turn.
struct Splice {
    scte35::SplicePoint  time;
    std::optional<Event> point;
    std::string          warning;
};

// A message as it is read: what its anchor and its tag give.
struct Read {
    const CueInput* input = nullptr;
    int             variant = -1;
    std::string     where;                       // how a warning names it
    bool            usable = false;              // read through, decoded and anchored: it makes events
    std::vector<std::string> warnings;           // its own as it was read, written in its turn
    SpliceInfo      info;
    std::vector<std::uint8_t> section;           // the section's bytes (D-109 (a))
    Classes         classes;
    std::optional<std::int64_t> line;            // its tag's
    std::optional<std::string>  raw;             // its tag's line as written
    std::optional<std::int64_t> segment;         // its anchor's
    std::optional<std::int64_t> position;        // where it is placed
    std::string                 unplaced;        // why it has no position, as a warning says it
    const SequenceFrames*       frames = nullptr;   // its discontinuity sequence's, to search; null: none
    std::string                 unsearchable;    // why it has none
    Durations                   tag_durations;   // an EXT-X-DATERANGE's
    Value::List                 anchors;
    std::vector<Splice>         splices;         // its splice times, in splice_times()' order
    // For a cue-out message that no plain cue takes, the index of the first
    // message of its signal, its own when it is the first (D-109 (a));
    // kNoSignal for any other.
    std::size_t                 signal = kNoSignal;
    std::vector<std::size_t>    held;            // for an SCTE35-OUT, the SCTE35-IN its tag holds
    bool                        held_by_out = false;   // an SCTE35-IN its tag's SCTE35-OUT holds
};

// Whether cue-out messages `a` and `b` are one signal (D-109 (a)): the same
// section's bytes, the same Out Point's splice_event_id, or a
// segmentation_event_id of a descriptor that makes each a cue_out in common.
bool one_signal(const Read& a, const Read& b)
{
    if (a.section == b.section) return true;
    if (a.classes.out_splice_event_id && a.classes.out_splice_event_id == b.classes.out_splice_event_id)
        return true;
    const std::vector<std::int64_t>& theirs = b.classes.out_segmentation_event_ids;
    return std::any_of(a.classes.out_segmentation_event_ids.begin(),
                       a.classes.out_segmentation_event_ids.end(), [&theirs](std::int64_t id) {
                           return std::find(theirs.begin(), theirs.end(), id) != theirs.end();
                       });
}

bool is_form(const Read& m, std::string_view form)
{
    return m.input->carrier == CueCarrier::PlaylistTag && m.input->form == form;
}

// The type of the plain cue whose tag embeds `m`, its own SCTE35=; empty
// for any other message.
std::string_view embedded_type(const Read& m)
{
    if (is_form(m, kCueOutForm)) return kTypeCueOut;
    if (is_form(m, kContForm)) return kTypeCueOutCont;
    if (is_form(m, kCueInForm)) return kTypeCueIn;
    return {};
}

// The messages are read in their order, then linked (D-109 (a), (b)), then
// each takes its turn in their order again, writing its warnings and making
// its events, so that the warnings and the events come in the messages'
// order, as they would in one pass.
class Run {
public:
    Run(std::vector<Event>& plain, const MessagePlacement& pl, const MessageOptions& o)
        : plain_(plain), pl_(pl), options_(o) {}

    void message(const CueInput& in)
    {
        Read m;
        read(m, in);
        reads_.push_back(std::move(m));
    }

    void finish()
    {
        link();
        for (std::size_t i = 0; i < reads_.size(); ++i) turn(i);
        for (const auto& [v, count] : refused_) {
            const VariantFrames& f = frames(v);
            warn(std::format("{}: no segment map, {}: its {} left out (D-081 (c), D-086)", name(v),
                             f.media()->refused,
                             plural(count, "SCTE-35 message is", "SCTE-35 messages are")));
        }
    }

    MessageEvents out;

private:
    std::vector<Event>&         plain_;
    const MessagePlacement&     pl_;
    const MessageOptions&       options_;
    std::map<int, VariantFrames> frames_;
    // Each variant's sequences searched so far, by discontinuity sequence
    // number; nullopt: every frame of an input without a playlist.
    std::map<std::pair<int, std::optional<std::int64_t>>, SequenceFrames> sequences_;
    std::map<int, std::size_t>  refused_;
    std::vector<Read>           reads_;
    // The splice times each signal has made its splice_point of, by the
    // index of its first message (D-109 (a)).
    std::map<std::size_t, std::vector<std::pair<std::int64_t, std::optional<std::uint8_t>>>> times_;

    void warn(std::string w) { out.warnings.push_back(std::move(w)); }

    const SequenceFrames* sequence(int v, std::optional<std::int64_t> disc)
    {
        const auto key = std::pair{ v, disc };
        auto it = sequences_.find(key);
        if (it == sequences_.end()) it = sequences_.emplace(key, sequence_frames(frames(v), disc)).first;
        return &it->second;
    }

    bool known_variant(int v) const
    {
        if (v < 0 || static_cast<std::size_t>(v) >= pl_.variants.size()) return false;
        return v == 0 || static_cast<std::size_t>(v - 1) < pl_.scores->pairs.size();
    }

    const VariantFrames& frames(int v)
    {
        auto it = frames_.find(v);
        if (it == frames_.end()) it = frames_.emplace(v, VariantFrames(pl_, v)).first;
        return it->second;
    }

    // "variant 2 (http://…/v180/index.m3u8)", as build() names a variant.
    std::string name(int v) const
    {
        const std::string& url = v == 0 ? pl_.scores->reference_url
                                        : pl_.scores->pairs[static_cast<std::size_t>(v - 1)].dis_url;
        return std::format("variant {} ({})", v, url);
    }

    Event event(const Read& m, std::string_view type, std::optional<std::int64_t> segment,
                std::int64_t at) const
    {
        Event e;
        e.source  = Source::Scte35;
        e.type.assign(type);
        e.variant = m.variant;
        e.segment = segment;
        e.ref_index = at;
        if (at >= 0 && static_cast<std::size_t>(at) < pl_.axis->keys.size())
            e.key = pl_.axis->keys[static_cast<std::size_t>(at)];
        return e;
    }

    // ── Reading ──────────────────────────────────────────────────────────

    // `in` decoded, classed and placed, with its anchors and its splice
    // times; what can't be read is a warning, kept for its turn.
    void read(Read& m, const CueInput& in)
    {
        m.input = &in;
        if (const auto* period = std::get_if<PeriodAnchor>(&in.anchor)) {
            m.warnings.push_back(std::format(
                "the {} of period '{}' at {}/{} has a DASH period-time anchor, which nothing places, "
                "since no DASH timeline is scored: it makes no event (design §5.5)",
                in.form, period->period, period->presentation_time, period->timescale));
            return;
        }
        const auto* tag_anchor   = std::get_if<PlaylistAnchor>(&in.anchor);
        const auto* media_anchor = std::get_if<MediaPtsAnchor>(&in.anchor);
        m.variant = tag_anchor ? tag_anchor->variant : media_anchor->variant;
        if (!known_variant(m.variant)) {
            m.warnings.push_back(std::format("the {} names variant {}, which the build doesn't have ({}): "
                                             "it makes no event", in.form, m.variant,
                                             plural(pl_.variants.size(), "variant", "variants")));
            return;
        }
        const VariantFrames& f = frames(m.variant);
        m.where = tag_anchor
                      ? std::format("{}: the {} at line {}", name(m.variant), in.form, tag_anchor->line)
                      : std::format("{}: the {} at PTS {} ({}/{})", name(m.variant), in.form,
                                    media_anchor->pts, media_anchor->time_base.num,
                                    media_anchor->time_base.den);
        if (f.media() && !f.media()->refused.empty()) {
            ++refused_[m.variant];   // one warning per variant, at the end
            return;
        }
        if (!decode(m)) return;
        m.classes = classify(m.info, options_);
        for (std::size_t i = 0; i < m.info.descriptors.size(); ++i)
            if (!m.info.descriptors[i].error.empty())
                m.warnings.push_back(std::format("{}: its descriptor {} counts for no class: {}", m.where,
                                                 i + 1, m.info.descriptors[i].error));

        // A message whose own position isn't found still goes on: its cues
        // take their turn in the pairing, unplaced, and its splice times are
        // searched in its sequence when that is known (design §5.1).
        if (tag_anchor && !tag_place(m, *tag_anchor)) return;
        if (media_anchor) media_place(m, *media_anchor);
        place_splices(m);
        m.usable = true;
    }

    // The section's bytes, from the text a playlist tag carries or as
    // another carrier gives them, decoded; false, with a warning, when they
    // don't decode or the CRC policy drops them.
    bool decode(Read& m)
    {
        const CueInput& in = *m.input;
        std::optional<std::vector<std::uint8_t>> bytes;
        std::string err;
        if (in.carrier == CueCarrier::PlaylistTag) {
            const std::string text(in.bytes.begin(), in.bytes.end());
            const bool hex = in.form.starts_with("DATERANGE ") || starts_with_hex_prefix(text);
            bytes = hex ? scte35::from_hex(text, err) : scte35::from_base64(text, err);
            if (!bytes) {
                m.warnings.push_back(std::format("{}: its text isn't {}, {}: it makes no event (design "
                                                 "§5.5)", m.where, hex ? "hex" : "base64", err));
                return false;
            }
        } else {
            bytes = in.bytes;
        }
        m.info = scte35::decode(*bytes);
        if (!m.info.decoded()) {
            m.warnings.push_back(std::format("{}: it doesn't decode, {}: it makes no event (design §5.5)",
                                             m.where, m.info.error));
            return false;
        }
        const auto size = static_cast<std::ptrdiff_t>(m.info.input_bytes - m.info.bytes_after);
        m.section.assign(bytes->begin(), bytes->begin() + size);
        if (!m.info.crc_valid) {
            const std::string crc = std::format("its CRC_32 is 0x{:08X} where its bytes give 0x{:08X}",
                                                m.info.crc_32, m.info.crc_computed);
            if (options_.bad_crc == BadCrc::Drop) {
                m.warnings.push_back(std::format("{}: {}: it is dropped, as the bad-CRC policy drop says "
                                                 "(D-106 (c))", m.where, crc));
                return false;
            }
            m.warnings.push_back(std::format("{}: {}: it is kept, marked crc_valid false (D-106 (c))",
                                             m.where, crc));
        }
        return true;
    }

    // A playlist tag's message: its tag, its position and its sequence, and
    // an EXT-X-DATERANGE's attributes.
    bool tag_place(Read& m, const PlaylistAnchor& a)
    {
        const VariantFrames& f = frames(m.variant);
        const PlaylistEvents* p = f.playlist();
        if (!p) {
            m.warnings.push_back(std::format("{}: its variant has no playlist to hold its tag: it makes "
                                             "no event", m.where));
            return false;
        }
        m.line = a.line;
        const PlaylistTag* tag = nullptr;
        for (const PlaylistTag& t : p->tags)
            if (t.line == a.line) tag = &t;
        if (tag) m.raw = tag_line(*tag);
        if (a.segment >= 0) {
            m.segment  = a.segment;
            m.position = f.segment_start(a.segment);
            if (!m.position) m.unplaced = std::format("its segment {} isn't placed", a.segment);
            if (const auto disc = f.sequence_of(a.segment)) {
                m.frames = sequence(m.variant, disc);
            } else {
                m.unsearchable = std::format("can't be placed: its segment {} isn't one its playlist "
                                             "lists", a.segment);
            }
        } else {
            m.unplaced     = "it follows the last segment's URI, and no segment holds it";
            m.unsearchable = "can't be placed: its tag follows the last segment's URI, so it has no "
                             "discontinuity sequence";
        }
        m.anchors.push_back(Value{ Value::Object{
            { "anchor", text_value("position") },
            { "segment", opt_int(m.segment) },
            { "line", int_value(a.line) },
            { "ref_index", opt_int(m.position) },
        } });
        if (m.input->form.starts_with("DATERANGE ") && tag) daterange(m, *p, *tag);
        return true;
    }

    // An EXT-X-DATERANGE's dates and durations; for a malformed one, which
    // hands on its well-formed SCTE35-* only (D-106 (e)), a warning instead,
    // none of its other attributes read (D-109 (e)). The date set against
    // the tag's segment is the one RFC 8216 §4.3.2.7.1 gives the message's
    // point: START-DATE, the time of an SCTE35-OUT's splice or of an
    // SCTE35-CMD's command; for an SCTE35-IN its in point, END-DATE or,
    // without one, START-DATE plus DURATION, both of its own tag (D-109 (e)),
    // since the tags of one ID share START-DATE, the break's start
    // (scte35.md §2.5). An SCTE35-IN's START-DATE is recorded all the same,
    // as the range's start, and set against nothing.
    void daterange(Read& m, const PlaylistEvents& p, const PlaylistTag& tag)
    {
        // A DATERANGE that hands on a message has a daterange event only
        // when it is malformed, which then says why (src/PlaylistEvents.h).
        for (const Event& e : p.events) {
            if (e.type != kTypeDaterange || int_member(e.payload, "line") != m.line) continue;
            const Value* error = member(e.payload, "error");
            const auto*  why   = error ? std::get_if<std::string>(&error->v) : nullptr;
            if (!why) continue;
            m.warnings.push_back(std::format(
                "{}: its EXT-X-DATERANGE is malformed, {}: its {}, a well-formed hexadecimal-sequence, is "
                "decoded all the same, and none of the tag's other attributes is read (D-106 (e))",
                m.where, *why, m.input->form.substr(std::string_view("DATERANGE ").size())));
            return;
        }
        std::string err;
        const std::vector<Attribute> attrs = parse_attributes(tag.value.value_or(""), err);
        if (!err.empty()) return;
        const auto find = [&](std::string_view n) -> const Attribute* {
            for (const Attribute& a : attrs)
                if (a.name == n) return &a;
            return nullptr;
        };
        const Attribute* duration = find("DURATION");
        if (duration) m.tag_durations.duration = decimal(duration->value);
        if (const Attribute* d = find("PLANNED-DURATION")) m.tag_durations.planned = decimal(d->value);
        const Attribute* start      = find("START-DATE");
        const Attribute* end        = find("END-DATE");
        const double     start_unix = start ? hls::parse_iso8601(start->value) : hls::kNaN;
        const double     end_unix   = end ? hls::parse_iso8601(end->value) : hls::kNaN;
        if (start && end && !std::isnan(start_unix) && !std::isnan(end_unix))
            m.tag_durations.end_date = static_cast<double>(micros(end_unix) - micros(start_unix)) / 1e6;
        const auto in_micros = [](double unix) {
            return std::isnan(unix) ? std::nullopt : std::optional<std::int64_t>(micros(unix));
        };
        const bool        in     = m.input->form == kRangeInForm;
        const int         index  = m.segment ? p.timeline.index_of(*m.segment) : -1;
        const std::string unread = "it isn't a date hls::parse_iso8601() reads";

        if (start) {
            Value::Object range_start;
            range_start.emplace_back("anchor", text_value("start_date"));
            range_start.emplace_back("date", text_value(start->value));
            range_start.emplace_back("unix", std::isnan(start_unix) ? Value{} : number_value(start_unix));
            set_date(m, p, index, range_start, std::format("its START-DATE {}", start->value),
                     in_micros(start_unix), unread, !in);
            m.anchors.push_back(Value{ std::move(range_start) });
        }
        if (!in || (!end && !(start && duration))) return;
        Value::Object in_point;
        in_point.emplace_back("anchor", text_value("end_date"));
        if (end) {
            in_point.emplace_back("from", text_value("END-DATE"));
            in_point.emplace_back("date", text_value(end->value));
            in_point.emplace_back("unix", std::isnan(end_unix) ? Value{} : number_value(end_unix));
            set_date(m, p, index, in_point, std::format("its END-DATE {}", end->value), in_micros(end_unix),
                     unread, true);
        } else {
            std::optional<std::int64_t> at;
            std::string                 why;
            if (std::isnan(start_unix)) why = "its START-DATE isn't a date hls::parse_iso8601() reads";
            else if (!m.tag_durations.duration) why = "its DURATION isn't a finite number";
            else at = micros(start_unix) + std::llround(*m.tag_durations.duration * 1e6);
            in_point.emplace_back("from", text_value("START-DATE + DURATION"));
            in_point.emplace_back("date", Value{});
            in_point.emplace_back("unix", at ? number_value(static_cast<double>(*at) / 1e6) : Value{});
            set_date(m, p, index, in_point,
                     std::format("its START-DATE {} plus its DURATION of {} s", start->value,
                                 duration->value),
                     at, why, true);
        }
        m.anchors.push_back(Value{ std::move(in_point) });
    }

    // A date of an EXT-X-DATERANGE, `at` in Unix microseconds (nullopt, with
    // `bad` saying why there is none), set against the EXT-X-PROGRAM-DATE-TIME
    // and EXTINF of the tag's segment, of index `index` (scte35.md §4.1): the
    // anchor's pdt, offset_seconds and in_segment. Where `checked` is false,
    // in_segment is null and no warning is written; otherwise a warning says
    // where the date falls outside the segment, or why it can't be set
    // against it (D-106 (a)).
    void set_date(Read& m, const PlaylistEvents& p, int index, Value::Object& anchor,
                  std::string_view what, std::optional<std::int64_t> at, std::string_view bad, bool checked)
    {
        std::optional<double> pdt, offset;
        std::optional<bool>   inside;
        std::string           why;
        if (!at) {
            why = bad;
        } else if (index < 0) {
            why = "no segment follows its tag";
        } else if (const hls::Segment& s = p.timeline.segments[static_cast<std::size_t>(index)];
                   std::isnan(s.pdt)) {
            why = "the segment has no EXT-X-PROGRAM-DATE-TIME, its own or carried on";
        } else {
            pdt    = s.pdt;
            const std::int64_t off = *at - micros(s.pdt);
            offset = static_cast<double>(off) / 1e6;
            if (checked) inside = off >= 0 && off < micros(s.duration);
            if (inside == false)
                m.warnings.push_back(std::format("{}: {} is {} s from the EXT-X-PROGRAM-DATE-TIME of its "
                                                 "segment {}, outside the segment's {} s (D-106 (a))",
                                                 m.where, what, *offset, *m.segment, s.duration));
        }
        if (checked && !why.empty())
            m.warnings.push_back(std::format("{}: {} can't be set against its segment: {} (D-106 (a))",
                                             m.where, what, why));
        anchor.emplace_back("pdt", opt_number(pdt));
        anchor.emplace_back("offset_seconds", opt_number(offset));
        anchor.emplace_back("in_segment", inside ? flag_value(*inside) : Value{});
    }

    // A message read from the media: placed at its PTS, as a splice time.
    // One whose PTS isn't placed keeps the reason, for the warning of each
    // of its events (emit()), and its sequence where that is known, in which
    // its splice times are searched; without a known segment, a PTS is
    // searched only where the playlist has one sequence (D-109 (e)).
    void media_place(Read& m, const MediaPtsAnchor& a)
    {
        const VariantFrames&  f = frames(m.variant);
        const PlaylistEvents* p = f.playlist();
        m.segment = a.segment;
        if (!p) {
            m.frames = sequence(m.variant, std::nullopt);   // one run of frames, no sequences
        } else if (a.segment) {
            if (const auto disc = f.sequence_of(*a.segment)) {
                m.frames = sequence(m.variant, disc);
            } else {
                m.unsearchable = std::format("can't be placed: its segment {} isn't one its playlist "
                                             "lists", *a.segment);
            }
        } else if (f.sequences() == 1 && !p->timeline.segments.empty()) {
            m.frames = sequence(m.variant, p->timeline.segments.front().disc_seq);
        } else {
            m.unsearchable = std::format("can't be placed: its segment isn't known, and its playlist has "
                                         "{} discontinuity sequences", f.sequences());
        }
        std::optional<std::int64_t> index;
        std::string why = m.unsearchable;
        if (!valid_base(a.time_base) || !valid_base(f.time_base())) {
            why = "can't be placed: its time base or its variant's isn't known";
        } else if (m.frames) {
            const AVRational   clock{ 1, static_cast<int>(scte35::kClock) };
            const std::int64_t t = wrap33(av_rescale_q(a.pts, a.time_base, clock));
            index = first_at_or_after(*m.frames, t, why);
        }
        if (index) m.position = f.at(*index);
        if (index && !m.position)
            why = std::format("lands on frame {} of its variant, which is after its last paired frame or "
                              "off the axis", *index);
        if (!m.segment && index) m.segment = f.segment_of(*index);
        if (!m.position) m.unplaced = "its PTS " + why;
        m.anchors.push_back(Value{ Value::Object{
            { "anchor", text_value("media_pts") },
            { "pts", int_value(a.pts) },
            { "time_base", text_value(std::format("{}/{}", a.time_base.num, a.time_base.den)) },
            { "segment", opt_int(m.segment) },
            { "index", opt_int(index) },
            { "ref_index", opt_int(m.position) },
        } });
    }

    // Each splice time: an anchor of the message (D-106 (a)), and the
    // splice_point it makes on the carrying variant's frames, with the
    // warning of one outside the tag's segment or left out, for its turn
    // (splice_points()). A splice time before its sequence's first frame is
    // outside the sequence (D-109 (e)).
    void place_splices(Read& m)
    {
        const VariantFrames& f = frames(m.variant);
        for (const scte35::SplicePoint& t : scte35::splice_times(m.info)) {
            Splice& s = m.splices.emplace_back();
            s.time = t;
            std::optional<std::int64_t> index, at, segment;
            std::string why = m.unsearchable;
            if (!valid_base(f.time_base())) why = "can't be placed: its variant's time base isn't known";
            else if (m.frames) index = first_at_or_after(*m.frames, t.adjusted, why);
            if (index) {
                at      = f.at(*index);
                segment = f.segment_of(*index);
                if (!at)
                    why = std::format("lands on frame {} of its variant, which is after its last paired "
                                      "frame or off the axis", *index);
            }
            std::optional<bool> inside;
            if (index && m.segment) {
                const SegmentFrames* g = f.segment(*m.segment);
                inside = g && *index >= g->first_index && *index < g->first_index + g->frames;
            } else if (!index && m.segment) {
                inside = false;
            }
            const Value       component = t.component_tag ? int_value(*t.component_tag) : Value{};
            const std::string what      = std::format(
                "its splice time {} (90 kHz){}", t.adjusted,
                t.component_tag ? std::format(", of component {}", *t.component_tag) : std::string());
            if (!at) {
                s.warning = std::format("{}: {} {}: its splice_point is left out (design §5.1)", m.where,
                                        what, why);
            } else if (inside == false) {
                s.warning = std::format("{}: {} falls on frame {} of its variant, in segment {}, outside "
                                        "the tag's segment {} (D-106 (a))", m.where, what, *index,
                                        segment ? std::to_string(*segment) : std::string("none"),
                                        *m.segment);
            }
            const auto offset_frames =
                at && m.position ? std::optional<std::int64_t>(*at - *m.position) : std::nullopt;
            const bool seconds = pl_.axis->frame_duration.num > 0 && pl_.axis->frame_duration.den > 0;
            m.anchors.push_back(Value{ Value::Object{
                { "anchor", text_value("splice_time") },
                { "pts_time", int_value(t.pts_time) },
                { "pts_adjusted", int_value(t.adjusted) },
                { "component_tag", component },
                { "index", opt_int(index) },
                { "segment", opt_int(segment) },
                { "ref_index", opt_int(at) },
                { "offset_frames", opt_int(offset_frames) },
                { "offset_seconds", offset_frames && seconds
                                        ? number_value(pl_.axis->seconds_between(*m.position, *at))
                                        : Value{} },
                { "in_segment", inside ? flag_value(*inside) : Value{} },
            } });
            if (!at) continue;
            Event e = event(m, kTypeSplicePoint, segment, *at);
            e.payload.emplace_back("line", opt_int(m.line));
            e.payload.emplace_back("form", text_value(m.input->form));
            e.payload.emplace_back("command", command_value(m));
            e.payload.emplace_back("pts_time", int_value(t.pts_time));
            e.payload.emplace_back("pts_adjustment", int_value(m.info.pts_adjustment));
            e.payload.emplace_back("pts_adjusted", int_value(t.adjusted));
            e.payload.emplace_back("component_tag", component);
            e.payload.emplace_back("index", int_value(*index));
            e.payload.emplace_back("in_segment", inside ? flag_value(*inside) : Value{});
            e.payload.emplace_back("crc_valid", flag_value(m.info.crc_valid));
            s.point = std::move(e);
        }
    }

    // ── Linking (D-109 (a), (b)) ─────────────────────────────────────────

    // Which SCTE35-IN each EXT-X-DATERANGE's SCTE35-OUT holds in its tag,
    // and which cue-out messages that no plain cue takes are one signal, of
    // one variant and one segment, each signal's first message in the
    // messages' order making its cues (signal()).
    void link()
    {
        // A tag's SCTE35-OUT, the first it hands on, by variant and line.
        std::map<std::pair<int, std::int64_t>, std::size_t> outs;
        for (std::size_t j = 0; j < reads_.size(); ++j) {
            const Read& o = reads_[j];
            if (o.usable && is_form(o, kRangeOutForm) && o.classes.out && o.line)
                outs.emplace(std::pair{ o.variant, *o.line }, j);
        }
        // The tag's SCTE35-IN, of the class cue_in alone, is held by that
        // SCTE35-OUT (D-109 (b)). That pair alone is folded: two other
        // messages of one tag that make a cue_out and then a cue_in, such as
        // an SCTE35-CMD that starts a break and an SCTE35-IN, or an
        // SCTE35-OUT and an SCTE35-CMD that ends it, are still a cue_out and
        // a cue_in at the tag's line, of which pair_breaks() makes an
        // ad_break of no frames, the tag's DURATION ending nothing: as
        // built, pending the owner.
        for (std::size_t i = 0; i < reads_.size(); ++i) {
            Read& in = reads_[i];
            if (!in.usable || !is_form(in, kRangeInForm) || !in.classes.in || in.classes.out || !in.line)
                continue;
            const auto o = outs.find(std::pair{ in.variant, *in.line });
            if (o == outs.end()) continue;
            reads_[o->second].held.push_back(i);
            in.held_by_out = true;
        }
        // The first message of each signal, as the smallest index of a
        // union of signals, so that it doesn't depend on which pairs are
        // compared first; only messages of one variant and one segment are.
        std::vector<std::size_t> first(reads_.size());
        for (std::size_t i = 0; i < first.size(); ++i) first[i] = i;
        const auto root = [&first](std::size_t i) {
            while (first[i] != i) i = first[i];
            return i;
        };
        std::map<std::pair<int, std::int64_t>, std::vector<std::size_t>> by_segment;
        std::vector<std::size_t> own;
        for (std::size_t i = 0; i < reads_.size(); ++i) {
            const Read& m = reads_[i];
            if (!m.usable || !m.classes.out || !embedded_type(m).empty() || plain_in_segment(m, kTypeCueOut))
                continue;
            own.push_back(i);
            if (!m.segment) continue;
            std::vector<std::size_t>& before = by_segment[std::pair{ m.variant, *m.segment }];
            for (std::size_t j : before) {
                if (!one_signal(reads_[j], m)) continue;
                const std::size_t a = root(i), b = root(j);
                first[std::max(a, b)] = std::min(a, b);
            }
            before.push_back(i);
        }
        for (std::size_t i : own) reads_[i].signal = root(i);
    }

    // ── Events ───────────────────────────────────────────────────────────

    // Message `i`'s turn: the warnings of its reading, its splice points,
    // and its events.
    void turn(std::size_t i)
    {
        Read& m = reads_[i];
        for (std::string& w : m.warnings) warn(std::move(w));
        if (!m.usable) return;
        splice_points(m);
        emit(i);
    }

    // The splice_points of `m`'s splice times, each with its warning; none,
    // and no warning, for the message of an EXT-X-CUE-OUT-CONT, which
    // repeats its break's message, whose splice time lies before the tag's
    // segment by its nature (D-109 (c)); and for a signal one per splice
    // time, the first message's that gives it (D-109 (a)).
    void splice_points(Read& m)
    {
        if (is_form(m, kContForm)) return;
        for (Splice& s : m.splices) {
            if (m.signal != kNoSignal) {
                auto&      made = times_[m.signal];
                const auto time = std::pair{ s.time.adjusted, s.time.component_tag };
                if (std::find(made.begin(), made.end(), time) != made.end()) continue;
                made.push_back(time);
            }
            if (!s.warning.empty()) warn(std::move(s.warning));
            if (s.point) out.splice_points.push_back(std::move(*s.point));
        }
    }

    // The name of the message's command (Table 7).
    static Value command_value(const Read& m)
    {
        return text_value(std::string(scte35::command_name(m.info.splice_command_type)));
    }

    // The message's record: its classes, its CRC, its fields and its anchors.
    void put_record(Payload& p, const Read& m) const
    {
        p.emplace_back("classes", m.classes.names());
        p.emplace_back("crc_valid", flag_value(m.info.crc_valid));
        p.emplace_back("message", message_value(m.info));
        p.emplace_back("anchors", Value{ m.anchors });
    }

    void put_head(Payload& p, const Read& m) const
    {
        p.emplace_back("line", opt_int(m.line));
        p.emplace_back("form", text_value(m.input->form));
        p.emplace_back("raw", m.raw ? text_value(*m.raw) : Value{});
    }

    Durations durations(const Read& m, SegmentationRole role) const
    {
        Durations d = m.tag_durations;
        if (const auto* c = std::get_if<scte35::SpliceInsert>(&m.info.command); c && c->break_duration)
            d.break_duration = ticked(c->break_duration->duration);
        d.segmentation = segmentation_seconds(m.info, options_, role);
        return d;
    }

    // `m`'s record in the payload of cue `cue`, a plain one or a signal's,
    // and with `d`, the durations of kDurationNames that the cue lacks or
    // has null, each with its ticks beside it (see the rules).
    void merge(Event& cue, const Read& m, const std::optional<Durations>& d) const
    {
        Value::Object record;
        put_head(record, m);
        put_record(record, m);
        Value* list = member(cue.payload, "messages");
        if (!list) {
            cue.payload.emplace_back("messages", Value{ Value::List{} });
            list = &cue.payload.back().second;
        }
        if (!std::holds_alternative<Value::List>(list->v)) *list = Value{ Value::List{} };
        std::get<Value::List>(list->v).push_back(Value{ std::move(record) });
        if (d) d->fill(cue.payload);
    }

    // The SCTE35-INs that `m`'s tag holds beside it, taken by `cue`, the
    // cue_out that took `m` (D-109 (b)): their records, and the line of the
    // first in kHeldCueInLine, by which pair_breaks() ends the cue_out's
    // break at its declared duration; one that declares none makes no
    // ad_break, with a warning, the cue_in after it ending nothing: as
    // built, pending the owner (src/PlaylistEvents.cpp's make_break()).
    void hold(Event& cue, const Read& m) const
    {
        for (std::size_t h : m.held) {
            merge(cue, reads_[h], std::nullopt);
            if (!member(cue.payload, kHeldCueInLine))
                cue.payload.emplace_back(std::string(kHeldCueInLine), opt_int(reads_[h].line));
        }
    }

    // The plain cue of `type` at `line` of `m`'s variant, or the first by
    // line of `type` in its segment; null when none is.
    Event* plain_at_line(const Read& m, std::string_view type) const
    {
        for (Event& c : plain_)
            if (c.variant == m.variant && c.type == type && int_member(c.payload, "line") == m.line)
                return &c;
        return nullptr;
    }
    Event* plain_in_segment(const Read& m, std::string_view type) const
    {
        Event* best = nullptr;
        for (Event& c : plain_) {
            if (c.variant != m.variant || c.type != type || !m.segment || c.segment != m.segment) continue;
            if (!best || int_member(c.payload, "line") < int_member(best->payload, "line")) best = &c;
        }
        return best;
    }

    // The events of message `i`: merged into its plain cue, or its own, or
    // its signal's (see the rules).
    void emit(std::size_t i)
    {
        const Read&            m        = reads_[i];
        const std::string_view embedded = embedded_type(m);
        if (!embedded.empty()) {
            Event* tagged = plain_at_line(m, embedded);
            if (!tagged) {
                warn(std::format("{}: no {} of its tag was given to take it: it makes no event", m.where,
                                 embedded));
                return;
            }
            std::optional<Durations> d;
            if (embedded == kTypeCueOut) d = durations(m, SegmentationRole::Start);
            merge(*tagged, m, d);
            return;
        }

        const auto unplaced = [&](std::string_view what) {
            if (!m.position)
                warn(std::format("{}: {}, so its {} isn't placed (design §5.1)", m.where, m.unplaced,
                                 what));
        };
        if (m.classes.cancel) {
            unplaced(kTypeCancel);
            if (m.position) {
                Event e = event(m, kTypeCancel, m.segment, *m.position);
                put_head(e.payload, m);
                e.payload.emplace_back("command", command_value(m));
                e.payload.emplace_back("splice_event_id", opt_int(m.classes.splice_event_id));
                Value::List ids;
                for (std::int64_t id : m.classes.segmentation_event_ids) ids.push_back(int_value(id));
                e.payload.emplace_back("segmentation_event_ids", Value{ std::move(ids) });
                put_record(e.payload, m);
                out.others.push_back(std::move(e));
            }
        }
        if (m.classes.out) {
            if (Event* joins = plain_in_segment(m, kTypeCueOut)) {
                // A cue_out message in the segment of a plain EXT-X-CUE-OUT
                // of its variant is that cue's, whatever carries it, once
                // its segment is known (D-106 (d)), and so is the SCTE35-IN
                // its tag holds (D-109 (b)); its cue_in, if it has one, is
                // its own and joins that cue_out.
                if (m.classes.in)
                    out.cues.push_back(cue(m, kTypeCueIn, SegmentationRole::End,
                                           int_member(joins->payload, "line")));
                // Where it holds that SCTE35-IN, the plain cue_out ends at the
                // first duration it then declares, by D-076's order: its own
                // EXT-X-CUE-OUT's before the tag's DURATION, its ad_break's
                // cue_in_line the tag's line, before its cue_out_line where
                // the tag is written first. An EXT-X-CUE-IN after it ends
                // nothing, and where nothing is declared it makes no
                // ad_break, with pair_breaks()' warning, so that the break
                // that EXT-X-CUE-IN would end is not made: as built, pending
                // the owner, D-109 (b) saying nothing of a tag that declares
                // no duration.
                merge(*joins, m, durations(m, SegmentationRole::Start));
                hold(*joins, m);
            } else if (m.signal == i) {
                signal(i);
            }
            // A later message of a signal makes no cue: its signal's first
            // took it (signal()).
        } else if (m.classes.in && !m.held_by_out) {
            out.cues.push_back(cue(m, kTypeCueIn, SegmentationRole::End));
        }
        if (!m.classes.any_cue() && !m.classes.cancel) {
            unplaced(kTypeMessage);
            if (!m.position) return;
            Event e = event(m, kTypeMessage, m.segment, *m.position);
            put_head(e.payload, m);
            e.payload.emplace_back("command", command_value(m));
            Value::List types;
            for (std::int64_t t : m.classes.segmentation_types) types.push_back(int_value(t));
            e.payload.emplace_back("segmentation_types", Value{ std::move(types) });
            put_record(e.payload, m);
            out.others.push_back(std::move(e));
        }
    }

    // The cues of the signal whose first message is `f` (D-109 (a)): one
    // cue_out, f's, which takes the others' records and the durations it
    // lacks from them as a plain cue does (merge()), and the SCTE35-INs
    // their tags hold (hold()); and before it one cue_in, of the first of its
    // messages that also ends a break (classify()), at the cue_out's line
    // where its own is later or null, so that it ends that break before this
    // one starts (see cue()).
    void signal(std::size_t f)
    {
        std::vector<std::size_t> members;
        for (std::size_t i = f; i < reads_.size(); ++i)
            if (reads_[i].signal == f) members.push_back(i);
        const Read& first = reads_[f];
        for (std::size_t i : members) {
            if (!reads_[i].classes.in) continue;
            out.cues.push_back(cue(reads_[i], kTypeCueIn, SegmentationRole::End, first.line));
            break;
        }
        Event e = cue(first, kTypeCueOut, SegmentationRole::Start);
        for (std::size_t i : members) {
            if (i != f) merge(e, reads_[i], durations(reads_[i], SegmentationRole::Start));
            hold(e, reads_[i]);
        }
        out.cues.push_back(std::move(e));
    }

    // A cue of its own, placed or kUnplaced, for the pairing. A cue_in that
    // joins a cue_out whose line is `joins`, that of the plain cue (D-106
    // (d)) or the signal's cue_out (D-109 (a)) that took its message's
    // cue_out part, ends the break before that one starts, an end and a
    // start at one splice point, in either order of their tags (Wowza writes
    // the OATCLS after the EXT-X-CUE-OUT, MediaLive before it; scte35.md
    // §2.4): it takes that line where its own is later, or null (D-109 (e)),
    // so that pair_breaks(), given the returned cues first (cues_to_pair()),
    // takes it before that cue_out. Its own line stays in its position
    // anchor.
    Event cue(const Read& m, std::string_view type, SegmentationRole role,
              std::optional<std::int64_t> joins = std::nullopt)
    {
        if (!m.position)
            warn(std::format("{}: {}, so its {} isn't placed: it still takes its turn in the pairing "
                             "(design §5.1)", m.where, m.unplaced, type));
        Event e = event(m, type, m.segment, m.position.value_or(kUnplaced));
        put_head(e.payload, m);
        if (joins && (!m.line || *m.line > *joins)) *member(e.payload, "line") = int_value(*joins);
        durations(m, role).put(e.payload);
        put_record(e.payload, m);
        return e;
    }
};

} // namespace

std::optional<SegmentationRole> segmentation_role(std::uint8_t type)
{
    const std::string_view name = scte35::segmentation_type_name(type);
    const auto ends = [&](std::string_view suffix) {
        return name.ends_with(suffix) || name.ends_with(std::string(suffix) + " (deprecated)");
    };
    if (ends(" Start")) return SegmentationRole::Start;
    if (ends(" End")) return SegmentationRole::End;
    return std::nullopt;
}

std::string check_message_options(const MessageOptions& options)
{
    for (std::uint8_t t : options.cue_types)
        if (!segmentation_role(t))
            return std::format("the segmentation_type_id 0x{:02X} ({}) is neither a Start nor an End type "
                               "of SCTE 35 2023r1's Table 23, so it can't make a time_signal a cue-out or "
                               "a cue-in", t, scte35::segmentation_type_name(t));
    return {};
}

std::vector<CueInput> playlist_messages(const std::vector<MessageVariant>& variants)
{
    std::vector<CueInput> out;
    for (const MessageVariant& v : variants)
        if (v.playlist) out.insert(out.end(), v.playlist->messages.begin(), v.playlist->messages.end());
    return out;
}

MessageEvents scte35_events(const std::vector<CueInput>& messages, std::vector<Event>& plain_cues,
                            const MessagePlacement& placement, const MessageOptions& options)
{
    if (!placement.scores || !placement.axis) {
        MessageEvents out;
        if (!messages.empty())
            out.warnings.push_back(
                std::format("{} can't be placed: no ladder report or axis was given",
                            plural(messages.size(), "SCTE-35 message", "SCTE-35 messages")));
        return out;
    }
    Run run(plain_cues, placement, options);
    for (const CueInput& m : messages) run.message(m);
    run.finish();
    return std::move(run.out);
}

std::vector<Event> cues_to_pair(const MessageEvents& events, const std::vector<Event>& plain_cues)
{
    std::vector<Event> cues = events.cues;
    cues.insert(cues.end(), plain_cues.begin(), plain_cues.end());
    return cues;
}

} // namespace timeline
