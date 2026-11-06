#include "Scte35.h"

extern "C" {
#include <libavutil/base64.h>
#include <libavutil/bswap.h>
#include <libavutil/crc.h>
}

#include <algorithm>
#include <cctype>
#include <climits>
#include <format>
#include <utility>

namespace scte35 {

namespace {

// ── Bits ─────────────────────────────────────────────────────────────────────

// A reader of bit fields, most significant bit first, over the bytes of one
// structure: a field that runs past them reads as 0 and fails the reader,
// whose first failure names that field and the structure, so that a
// structure is checked once, after its reads, and nothing past its bytes is
// ever read (the rules' bounds).
class Bits {
public:
    Bits(std::span<const std::uint8_t> bytes, std::string scope)
        : bytes_(bytes), scope_(std::move(scope)) {}

    // The next `n` bits, n at most 64.
    std::uint64_t read(int n, std::string_view field)
    {
        if (!error_.empty()) return 0;
        if (static_cast<std::size_t>(n) > left_bits()) {
            error_ = std::format("{} runs past the end of {}", field, scope_);
            return 0;
        }
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i, ++pos_)
            v = (v << 1) | ((bytes_[pos_ / 8] >> (7 - pos_ % 8)) & 1u);
        return v;
    }
    bool flag(std::string_view field) { return read(1, field) != 0; }
    // Bits the syntax reserves, read past.
    void reserved(int n) { read(n, "a reserved field"); }

    // The next `n` whole bytes; the reader must be on a byte.
    std::span<const std::uint8_t> take(std::size_t n, std::string_view field)
    {
        if (!error_.empty()) return {};
        if (pos_ % 8 != 0) {
            error_ = std::format("{} doesn't start on a byte in {}", field, scope_);
            return {};
        }
        if (n > left_bits() / 8) {
            error_ = std::format("{} runs past the end of {}", field, scope_);
            return {};
        }
        const auto out = bytes_.subspan(pos_ / 8, n);
        pos_ += n * 8;
        return out;
    }

    std::size_t left_bits() const { return bytes_.size() * 8 - pos_; }
    std::size_t left_bytes() const { return left_bits() / 8; }
    bool        aligned() const { return pos_ % 8 == 0; }
    bool        ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }
    void fail(std::string why)
    {
        if (error_.empty()) error_ = std::move(why);
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::string                   scope_;
    std::size_t                   pos_ = 0;   // in bits
    std::string                   error_;
};

template <typename T>
T narrow(std::uint64_t v)
{
    return static_cast<T>(v);
}

std::vector<std::uint8_t> copy(std::span<const std::uint8_t> s) { return { s.begin(), s.end() }; }

std::uint32_t be32(std::span<const std::uint8_t> b)
{
    return (std::uint32_t{ b[0] } << 24) | (std::uint32_t{ b[1] } << 16) | (std::uint32_t{ b[2] } << 8)
         | std::uint32_t{ b[3] };
}

// A character as an error names it: itself when it prints, else its code.
std::string char_text(char c)
{
    const auto u = static_cast<unsigned char>(c);
    if (u >= 0x21 && u < 0x7F) return std::format("'{}'", c);
    return std::format("byte 0x{:02X}", u);
}

// ── Commands ─────────────────────────────────────────────────────────────────

SpliceTime read_splice_time(Bits& r)
{
    SpliceTime t;
    t.time_specified = r.flag("time_specified_flag");
    if (t.time_specified) {
        r.reserved(6);
        t.pts_time = narrow<std::int64_t>(r.read(33, "pts_time"));
    } else {
        r.reserved(7);
    }
    return t;
}

BreakDuration read_break_duration(Bits& r)
{
    BreakDuration b;
    b.auto_return = r.flag("auto_return");
    r.reserved(6);
    b.duration = narrow<std::int64_t>(r.read(33, "duration"));
    return b;
}

SpliceInsert read_splice_insert(Bits& r)
{
    SpliceInsert c;
    c.splice_event_id = narrow<std::uint32_t>(r.read(32, "splice_event_id"));
    c.cancel          = r.flag("splice_event_cancel_indicator");
    r.reserved(7);
    if (c.cancel) return c;
    c.out_of_network      = r.flag("out_of_network_indicator");
    c.program_splice      = r.flag("program_splice_flag");
    c.duration_flag       = r.flag("duration_flag");
    c.splice_immediate    = r.flag("splice_immediate_flag");
    c.event_id_compliance = r.flag("event_id_compliance_flag");
    r.reserved(3);
    if (c.program_splice && !c.splice_immediate) c.splice_time = read_splice_time(r);
    if (!c.program_splice) {
        const auto count = r.read(8, "component_count");
        for (std::uint64_t i = 0; i < count && r.ok(); ++i) {
            SpliceInsert::Component k;
            k.tag = narrow<std::uint8_t>(r.read(8, "component_tag"));
            if (!c.splice_immediate) k.splice_time = read_splice_time(r);
            c.components.push_back(k);
        }
    }
    if (c.duration_flag) c.break_duration = read_break_duration(r);
    c.unique_program_id = narrow<std::uint16_t>(r.read(16, "unique_program_id"));
    c.avail_num         = narrow<std::uint8_t>(r.read(8, "avail_num"));
    c.avails_expected   = narrow<std::uint8_t>(r.read(8, "avails_expected"));
    return c;
}

SpliceSchedule read_splice_schedule(Bits& r)
{
    SpliceSchedule s;
    const auto count = r.read(8, "splice_count");
    for (std::uint64_t i = 0; i < count && r.ok(); ++i) {
        ScheduledEvent e;
        e.splice_event_id     = narrow<std::uint32_t>(r.read(32, "splice_event_id"));
        e.cancel              = r.flag("splice_event_cancel_indicator");
        e.event_id_compliance = r.flag("event_id_compliance_flag");
        r.reserved(6);
        if (!e.cancel) {
            e.out_of_network = r.flag("out_of_network_indicator");
            e.program_splice = r.flag("program_splice_flag");
            e.duration_flag  = r.flag("duration_flag");
            r.reserved(5);
            if (e.program_splice) {
                e.utc_splice_time = narrow<std::uint32_t>(r.read(32, "utc_splice_time"));
            } else {
                const auto components = r.read(8, "component_count");
                for (std::uint64_t k = 0; k < components && r.ok(); ++k) {
                    ScheduledEvent::Component c;
                    c.tag             = narrow<std::uint8_t>(r.read(8, "component_tag"));
                    c.utc_splice_time = narrow<std::uint32_t>(r.read(32, "utc_splice_time"));
                    e.components.push_back(c);
                }
            }
            if (e.duration_flag) e.break_duration = read_break_duration(r);
            e.unique_program_id = narrow<std::uint16_t>(r.read(16, "unique_program_id"));
            e.avail_num         = narrow<std::uint8_t>(r.read(8, "avail_num"));
            e.avails_expected   = narrow<std::uint8_t>(r.read(8, "avails_expected"));
        }
        s.events.push_back(std::move(e));
    }
    return s;
}

// The command of `s`, read from `r`, which holds its bytes: all of them
// under a splice_command_length, the rest of the section under 0xFFF.
void read_command(SpliceInfo& s, Bits& r, bool legacy)
{
    switch (s.splice_command_type) {
    case kSpliceNull:
    case kBandwidthReservation:
        return;   // empty (Tables 8 and 12)
    case kSpliceInsert:
        s.command = read_splice_insert(r);
        return;
    case kTimeSignal:
        s.command = TimeSignal{ read_splice_time(r) };
        return;
    case kSpliceSchedule:
        s.command = read_splice_schedule(r);
        return;
    case kPrivateCommand: {
        if (legacy) {
            r.fail("a private_command under splice_command_length 0xFFF has no length: its private bytes "
                   "and the descriptor loop after them can't be found");
            return;
        }
        PrivateCommand p;
        p.identifier = narrow<std::uint32_t>(r.read(32, "identifier"));
        p.bytes      = copy(r.take(r.left_bytes(), "private_byte"));
        s.command    = std::move(p);
        return;
    }
    default:
        // A reserved command (Table 7): skipped by its length, which 0xFFF
        // doesn't give.
        if (legacy) {
            r.fail(std::format("the reserved splice_command_type 0x{:02X} under splice_command_length "
                               "0xFFF has no length: the descriptor loop after it can't be found",
                               s.splice_command_type));
            return;
        }
        s.command_bytes = copy(r.take(r.left_bytes(), "the command's bytes"));
        return;
    }
}

// ── Descriptors ──────────────────────────────────────────────────────────────

bool has_sub_segments(std::uint8_t type)
{
    // Table 20: the types after which sub_segment_num and
    // sub_segments_expected may follow, when descriptor_length holds them.
    static constexpr std::uint8_t kTypes[] = { 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x44, 0x46 };
    return std::find(std::begin(kTypes), std::end(kTypes), type) != std::end(kTypes);
}

SegmentationDescriptor read_segmentation(Bits& r)
{
    SegmentationDescriptor d;
    d.event_id            = narrow<std::uint32_t>(r.read(32, "segmentation_event_id"));
    d.cancel              = r.flag("segmentation_event_cancel_indicator");
    d.event_id_compliance = r.flag("segmentation_event_id_compliance_indicator");
    r.reserved(6);
    if (d.cancel) return d;
    d.program_segmentation    = r.flag("program_segmentation_flag");
    d.duration_flag           = r.flag("segmentation_duration_flag");
    d.delivery_not_restricted = r.flag("delivery_not_restricted_flag");
    if (!d.delivery_not_restricted) {
        d.web_delivery_allowed = r.flag("web_delivery_allowed_flag");
        d.no_regional_blackout = r.flag("no_regional_blackout_flag");
        d.archive_allowed      = r.flag("archive_allowed_flag");
        d.device_restrictions  = narrow<std::uint8_t>(r.read(2, "device_restrictions"));
    } else {
        r.reserved(5);
    }
    if (!d.program_segmentation) {
        const auto count = r.read(8, "component_count");
        for (std::uint64_t i = 0; i < count && r.ok(); ++i) {
            SegmentationDescriptor::Component c;
            c.tag = narrow<std::uint8_t>(r.read(8, "component_tag"));
            r.reserved(7);
            c.pts_offset = narrow<std::int64_t>(r.read(33, "pts_offset"));
            d.components.push_back(c);
        }
    }
    if (d.duration_flag) d.duration = narrow<std::int64_t>(r.read(40, "segmentation_duration"));
    d.upid_type = narrow<std::uint8_t>(r.read(8, "segmentation_upid_type"));
    const auto upid_length = r.read(8, "segmentation_upid_length");
    d.upid              = copy(r.take(upid_length, "segmentation_upid"));
    d.type_id           = narrow<std::uint8_t>(r.read(8, "segmentation_type_id"));
    d.segment_num       = narrow<std::uint8_t>(r.read(8, "segment_num"));
    d.segments_expected = narrow<std::uint8_t>(r.read(8, "segments_expected"));
    // Present only where descriptor_length holds them (scte35.md §3.4).
    if (r.ok() && has_sub_segments(d.type_id) && r.left_bytes() >= 2) {
        d.sub_segment_num       = narrow<std::uint8_t>(r.read(8, "sub_segment_num"));
        d.sub_segments_expected = narrow<std::uint8_t>(r.read(8, "sub_segments_expected"));
    }
    return d;
}

std::string_view descriptor_name(std::uint8_t tag)
{
    switch (tag) {
    case kAvailDescriptor:        return "avail_descriptor";
    case kDtmfDescriptor:         return "DTMF_descriptor";
    case kSegmentationDescriptor: return "segmentation_descriptor";
    case kTimeDescriptor:         return "time_descriptor";
    case kAudioDescriptor:        return "audio_descriptor";
    default:                      return "a reserved descriptor";
    }
}

// Descriptor `number` of the loop, from 1: its tag and the descriptor_length
// bytes after its length, `body`.
Descriptor read_descriptor(std::uint8_t tag, std::span<const std::uint8_t> body, std::size_t number)
{
    Descriptor d;
    d.tag    = tag;
    d.length = static_cast<std::uint8_t>(body.size());
    if (body.size() < 4) {
        d.bytes = copy(body);
        d.error = std::format("its descriptor_length of {} holds no identifier", body.size());
        return d;
    }
    d.identifier    = be32(body);
    const auto rest = body.subspan(4);
    d.bytes         = copy(rest);
    // Skipped by length: a private descriptor, or a tag SCTE reserves
    // (§10.1).
    if (*d.identifier != kCuei || tag > kAudioDescriptor) return d;

    Bits r(rest, std::format("descriptor {} ({}, descriptor_length {})", number, descriptor_name(tag),
                             body.size()));
    switch (tag) {
    case kAvailDescriptor:
        d.body = AvailDescriptor{ narrow<std::uint32_t>(r.read(32, "provider_avail_id")) };
        break;
    case kDtmfDescriptor: {
        DtmfDescriptor t;
        t.preroll        = narrow<std::uint8_t>(r.read(8, "preroll"));
        const auto count = r.read(3, "dtmf_count");
        r.reserved(5);
        const auto chars = r.take(count, "DTMF_char");
        t.chars.assign(chars.begin(), chars.end());
        d.body = std::move(t);
        break;
    }
    case kSegmentationDescriptor:
        d.body = read_segmentation(r);
        break;
    case kTimeDescriptor: {
        TimeDescriptor t;
        t.tai_seconds = narrow<std::int64_t>(r.read(48, "TAI_seconds"));
        t.tai_ns      = narrow<std::uint32_t>(r.read(32, "TAI_ns"));
        t.utc_offset  = narrow<std::uint16_t>(r.read(16, "UTC_offset"));
        d.body        = t;
        break;
    }
    case kAudioDescriptor: {
        AudioDescriptor a;
        const auto count = r.read(4, "audio_count");
        r.reserved(4);
        for (std::uint64_t i = 0; i < count && r.ok(); ++i) {
            AudioChannel c;
            c.component_tag   = narrow<std::uint8_t>(r.read(8, "component_tag"));
            const auto iso    = r.take(3, "ISO_code");
            c.iso_code.assign(iso.begin(), iso.end());
            c.bit_stream_mode = narrow<std::uint8_t>(r.read(3, "Bit_Stream_Mode"));
            c.num_channels    = narrow<std::uint8_t>(r.read(4, "Num_Channels"));
            c.full_srvc_audio = r.flag("Full_Srvc_Audio");
            a.channels.push_back(std::move(c));
        }
        d.body = std::move(a);
        break;
    }
    default:
        break;
    }
    if (!r.ok()) {
        d.body  = std::monostate{};
        d.error = r.error();
        return d;
    }
    d.unparsed = r.left_bytes();
    return d;
}

} // namespace

// ── Public ───────────────────────────────────────────────────────────────────

std::string_view command_name(std::uint8_t type)
{
    switch (type) {
    case kSpliceNull:           return "splice_null";
    case kSpliceSchedule:       return "splice_schedule";
    case kSpliceInsert:         return "splice_insert";
    case kTimeSignal:           return "time_signal";
    case kBandwidthReservation: return "bandwidth_reservation";
    case kPrivateCommand:       return "private_command";
    default:                    return "reserved";
    }
}

std::string_view segmentation_type_name(std::uint8_t type)
{
    switch (type) {
    case 0x00: return "Not Indicated";
    case 0x01: return "Content Identification";
    case 0x02: return "Private";
    case 0x10: return "Program Start";
    case 0x11: return "Program End";
    case 0x12: return "Program Early Termination";
    case 0x13: return "Program Breakaway";
    case 0x14: return "Program Resumption";
    case 0x15: return "Program Runover Planned";
    case 0x16: return "Program Runover Unplanned";
    case 0x17: return "Program Overlap Start";
    case 0x18: return "Program Blackout Override";
    case 0x19: return "Program Join";
    case 0x1A: return "Program Immediate Resumption";
    case 0x20: return "Chapter Start";
    case 0x21: return "Chapter End";
    case 0x22: return "Break Start";
    case 0x23: return "Break End";
    case 0x24: return "Opening Credit Start (deprecated)";
    case 0x25: return "Opening Credit End (deprecated)";
    case 0x26: return "Closing Credit Start (deprecated)";
    case 0x27: return "Closing Credit End (deprecated)";
    case 0x30: return "Provider Advertisement Start";
    case 0x31: return "Provider Advertisement End";
    case 0x32: return "Distributor Advertisement Start";
    case 0x33: return "Distributor Advertisement End";
    case 0x34: return "Provider Placement Opportunity Start";
    case 0x35: return "Provider Placement Opportunity End";
    case 0x36: return "Distributor Placement Opportunity Start";
    case 0x37: return "Distributor Placement Opportunity End";
    case 0x38: return "Provider Overlay Placement Opportunity Start";
    case 0x39: return "Provider Overlay Placement Opportunity End";
    case 0x3A: return "Distributor Overlay Placement Opportunity Start";
    case 0x3B: return "Distributor Overlay Placement Opportunity End";
    case 0x3C: return "Provider Promo Start";
    case 0x3D: return "Provider Promo End";
    case 0x3E: return "Distributor Promo Start";
    case 0x3F: return "Distributor Promo End";
    case 0x40: return "Unscheduled Event Start";
    case 0x41: return "Unscheduled Event End";
    case 0x42: return "Alternate Content Opportunity Start";
    case 0x43: return "Alternate Content Opportunity End";
    case 0x44: return "Provider Ad Block Start";
    case 0x45: return "Provider Ad Block End";
    case 0x46: return "Distributor Ad Block Start";
    case 0x47: return "Distributor Ad Block End";
    case 0x50: return "Network Start";
    case 0x51: return "Network End";
    default:   return "reserved";
    }
}

std::optional<std::vector<std::uint8_t>> from_base64(std::string_view text, std::string& err)
{
    err.clear();
    if (text.empty()) {
        err = "it is empty";
        return std::nullopt;
    }
    if (text.size() % 4 != 0) {
        err = std::format("its {} characters aren't a multiple of 4, as base64 with its padding is "
                          "(RFC 4648 §4)", text.size());
        return std::nullopt;
    }
    if (text.size() / 4 * 3 > static_cast<std::size_t>(INT_MAX)) {
        err = std::format("its {} characters are more than av_base64_decode() takes", text.size());
        return std::nullopt;
    }
    // At most two '=' of padding, at the end only.
    std::size_t pad = 0;
    while (pad < 2 && text[text.size() - 1 - pad] == '=') ++pad;
    for (std::size_t i = 0; i + pad < text.size(); ++i) {
        const char c = text[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '/') {
            err = std::format("its character {}, {}, isn't base64's (RFC 4648 §4)", i + 1, char_text(c));
            return std::nullopt;
        }
    }
    // av_base64_decode() reads a NUL-terminated string.
    const std::string in(text);
    std::vector<std::uint8_t> out(text.size() / 4 * 3);
    const int n = av_base64_decode(out.data(), in.c_str(), static_cast<int>(out.size()));
    if (n < 0) {
        err = "av_base64_decode() refuses it";
        return std::nullopt;
    }
    out.resize(static_cast<std::size_t>(n));
    return out;
}

std::optional<std::vector<std::uint8_t>> from_hex(std::string_view text, std::string& err)
{
    err.clear();
    std::size_t at = 0;   // where the digits start
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) at = 2;
    const std::size_t digits = text.size() - at;
    if (digits == 0) {
        err = "it has no hex digits";
        return std::nullopt;
    }
    for (std::size_t i = at; i < text.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(text[i]))) {
            err = std::format("its character {}, {}, isn't a hex digit", i + 1, char_text(text[i]));
            return std::nullopt;
        }
    if (digits % 2 != 0) {
        err = std::format("its {} hex digits are an odd number, which no bytes are", digits);
        return std::nullopt;
    }
    const auto value = [](char c) {
        return static_cast<std::uint8_t>(std::isdigit(static_cast<unsigned char>(c))
                                             ? c - '0'
                                             : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
    };
    std::vector<std::uint8_t> out;
    out.reserve(digits / 2);
    for (std::size_t i = at; i < text.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(value(text[i]) << 4 | value(text[i + 1])));
    return out;
}

std::uint32_t crc32_mpeg2(std::span<const std::uint8_t> bytes)
{
    // No bytes leave the CRC at its start. av_crc() isn't called for them:
    // given the null pointer an empty span may hold, it reads from it.
    if (bytes.empty()) return 0xFFFFFFFFu;
    // libavutil holds a CRC that isn't reflected with its bytes swapped
    // (av_crc_init() in libavutil/crc.c), so av_crc() returns it swapped;
    // FFmpeg's MPEG-TS muxer swaps it back, as here, before writing a
    // section's CRC_32 (libavformat/mpegtsenc.c). The zero check of a whole
    // section (decode()) is the same either way.
    return av_bswap32(av_crc(av_crc_get_table(AV_CRC_32_IEEE), UINT32_MAX, bytes.data(), bytes.size()));
}

std::int64_t add_pts(std::int64_t a, std::int64_t b)
{
    const std::int64_t sum = (a + b) % kPtsWrap;
    return sum < 0 ? sum + kPtsWrap : sum;
}

SpliceInfo decode(std::span<const std::uint8_t> input)
{
    SpliceInfo s;
    s.input_bytes = input.size();
    if (input.size() < 3) {
        s.error = std::format("its {} {} fewer than a section header's 3", input.size(),
                              input.size() == 1 ? "byte is" : "bytes are");
        return s;
    }
    Bits head(input.first(3), "the section's first 3 bytes");
    s.table_id                 = narrow<std::uint8_t>(head.read(8, "table_id"));
    s.section_syntax_indicator = head.flag("section_syntax_indicator");
    s.private_indicator        = head.flag("private_indicator");
    s.sap_type                 = narrow<std::uint8_t>(head.read(2, "sap_type"));
    s.section_length           = narrow<std::uint16_t>(head.read(12, "section_length"));
    if (s.table_id != kTableId) {
        s.error = std::format("its table_id is 0x{:02X}, not a splice_info_section's 0xFC (Table 5)",
                              s.table_id);
        return s;
    }
    const std::size_t total = 3 + std::size_t{ s.section_length };
    if (total > input.size()) {
        s.error = std::format("its section_length of {} makes a section of {} bytes, and the input has {}",
                              s.section_length, total, input.size());
        return s;
    }
    s.bytes_after = input.size() - total;
    // The fields from protocol_version through splice_command_length, in
    // the clear, are 10 bytes; CRC_32 is the last 4.
    constexpr std::size_t kClear = 3 + 10;
    if (total < kClear + 4) {
        s.error = std::format("its section_length of {} leaves no room for the header's {} bytes after "
                              "it and CRC_32", s.section_length, kClear - 3);
        return s;
    }
    const auto section = input.first(total);
    s.has_crc      = true;
    s.crc_32       = be32(section.last(4));
    s.crc_computed = crc32_mpeg2(section.first(total - 4));
    s.crc_valid    =
        av_crc(av_crc_get_table(AV_CRC_32_IEEE), UINT32_MAX, section.data(), section.size()) == 0;

    // From protocol_version to CRC_32, which isn't read again.
    Bits r(section.subspan(3, total - 3 - 4),
           std::format("the section (section_length {})", s.section_length));
    s.protocol_version      = narrow<std::uint8_t>(r.read(8, "protocol_version"));
    s.encrypted_packet      = r.flag("encrypted_packet");
    s.encryption_algorithm  = narrow<std::uint8_t>(r.read(6, "encryption_algorithm"));
    s.pts_adjustment        = narrow<std::int64_t>(r.read(33, "pts_adjustment"));
    s.cw_index              = narrow<std::uint8_t>(r.read(8, "cw_index"));
    s.tier                  = narrow<std::uint16_t>(r.read(12, "tier"));
    s.splice_command_length = narrow<std::uint16_t>(r.read(12, "splice_command_length"));
    if (s.protocol_version != 0) {
        s.error = std::format("its protocol_version is {}, and SCTE 35 2023r1 defines the structure of "
                              "version 0 only (§9.6.1)", s.protocol_version);
        return s;
    }
    if (s.encrypted_packet) {
        s.error = std::format("it is encrypted (encrypted_packet 1, encryption_algorithm {}): its command "
                              "and descriptors aren't decoded", s.encryption_algorithm);
        return s;
    }
    s.splice_command_type = narrow<std::uint8_t>(r.read(8, "splice_command_type"));
    if (!r.ok()) {
        s.error = r.error();
        return s;
    }

    // The command, within its splice_command_length, or as long as its
    // syntax under 0xFFF. Every field so far ends on a byte.
    const bool legacy = s.splice_command_length == kLegacyCommandLength;
    const std::string name(command_name(s.splice_command_type));
    const std::string scope = std::format("the section (section_length {})", s.section_length);
    std::span<const std::uint8_t> after;   // the bytes after the command, to CRC_32
    if (legacy) {
        const auto rest = r.take(r.left_bytes(), "the command");
        Bits c(rest,
               std::format("the section, which holds the {} under splice_command_length 0xFFF", name));
        read_command(s, c, true);
        if (c.ok() && !c.aligned()) c.fail(std::format("the {} doesn't end on a byte", name));
        if (!c.ok()) {
            s.error = c.error();
            return s;
        }
        after = rest.last(c.left_bytes());
    } else {
        if (s.splice_command_length > r.left_bytes()) {
            s.error = std::format("its splice_command_length of {} runs past the end of {}, which has {} "
                                  "after splice_command_type", s.splice_command_length, scope,
                                  r.left_bytes() == 1 ? std::string("1 byte")
                                                      : std::format("{} bytes", r.left_bytes()));
            return s;
        }
        const auto bytes = r.take(s.splice_command_length, "the command");
        Bits c(bytes, std::format("the {} (splice_command_length {})", name, s.splice_command_length));
        read_command(s, c, false);
        if (c.ok() && !c.aligned()) c.fail(std::format("the {} doesn't end on a byte", name));
        if (!c.ok()) {
            s.error = c.error();
            return s;
        }
        s.command_unused = c.left_bytes();
        after            = r.take(r.left_bytes(), "the descriptor loop");
    }

    // The descriptor loop, then alignment stuffing up to CRC_32.
    Bits tail(after, scope);
    s.descriptor_loop_length = narrow<std::uint16_t>(tail.read(16, "descriptor_loop_length"));
    if (!tail.ok()) {
        s.error = tail.error();
        return s;
    }
    if (s.descriptor_loop_length > tail.left_bytes()) {
        s.error = std::format("its descriptor_loop_length of {} runs past CRC_32, {} bytes after it",
                              s.descriptor_loop_length, tail.left_bytes());
        return s;
    }
    const auto loop = tail.take(s.descriptor_loop_length, "the descriptor loop");
    for (std::size_t at = 0, number = 1; at < loop.size(); ++number) {
        if (loop.size() - at < 2) {
            s.error = std::format("descriptor {} has {} of its 2 header bytes before the end of the "
                                  "descriptor loop (descriptor_loop_length {})", number, loop.size() - at,
                                  s.descriptor_loop_length);
            return s;
        }
        const std::uint8_t tag    = loop[at];
        const std::size_t  length = loop[at + 1];
        if (length > loop.size() - at - 2) {
            s.error = std::format("descriptor {} (splice_descriptor_tag 0x{:02X}, descriptor_length {}) "
                                  "runs past the end of the descriptor loop (descriptor_loop_length {})",
                                  number, tag, length, s.descriptor_loop_length);
            return s;
        }
        s.descriptors.push_back(read_descriptor(tag, loop.subspan(at + 2, length), number));
        at += 2 + length;
    }
    s.alignment_stuffing = tail.left_bytes();
    return s;
}

std::vector<SplicePoint> splice_times(const SpliceInfo& info)
{
    std::vector<SplicePoint> out;
    if (!info.decoded()) return out;
    const auto add = [&](const SpliceTime& t, std::optional<std::uint8_t> component) {
        if (!t.time_specified) return;
        out.push_back({ .pts_time      = t.pts_time,
                        .adjusted      = add_pts(t.pts_time, info.pts_adjustment),
                        .component_tag = component });
    };
    if (const auto* t = std::get_if<TimeSignal>(&info.command)) {
        add(t->splice_time, std::nullopt);
    } else if (const auto* c = std::get_if<SpliceInsert>(&info.command);
               c && !c->cancel && !c->splice_immediate) {
        if (c->splice_time) add(*c->splice_time, std::nullopt);
        for (const SpliceInsert::Component& k : c->components)
            if (k.splice_time) add(*k.splice_time, k.tag);
    }
    return out;
}

} // namespace scte35
