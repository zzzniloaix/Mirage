// scte35::decode() and the text a message comes in (src/Scte35.h), on
// splice_info_sections the test builds from field values with the writer of
// tests/Scte35TestSections.h, which computes their CRC_32 bit by bit
// (eval/phase3-survey/design.md P-M): every command, every descriptor the
// decoder reads and every field of the section, CRC_32 good and bad,
// truncation at every byte, the legacy splice_command_length, alignment
// stuffing, the pts_adjustment wrap mod 2^33, the encrypted flag, cancel
// indicators, base64 and hex. No published message is committed
// (DECISIONS.md D-107). An optional test case reads a local file of published
// messages named by MIRAGE_SCTE35_VECTORS, in the format of the survey's
// scte35_vectors.json, and is skipped without one.

#include <catch2/catch_test_macros.hpp>

#include "JsonRead.h"
#include "Scte35.h"
#include "Scte35TestSections.h"

#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace scte35test;

namespace {

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

scte35::SpliceInfo decoded(const Bytes& b)
{
    scte35::SpliceInfo s = scte35::decode(b);
    INFO(s.error);
    REQUIRE(s.decoded());
    return s;
}

template <typename T>
const T& command(const scte35::SpliceInfo& s)
{
    REQUIRE(std::holds_alternative<T>(s.command));
    return std::get<T>(s.command);
}

template <typename T>
const T& body(const scte35::Descriptor& d)
{
    INFO(d.error);
    REQUIRE(std::holds_alternative<T>(d.body));
    return std::get<T>(d.body);
}

// A section with every part the decoder reads: a splice_insert with a
// splice time and a break, an avail and a segmentation descriptor.
Section full_section()
{
    Section s;
    s.pts_adjustment = 900000;
    s.tier           = 0x123;
    s.command_type   = scte35::kSpliceInsert;
    s.command        = insert_command({ .event_id = 0x10, .pts = 2700000,
                                        .duration = std::pair{ true, 2700000 }, .program_id = 7,
                                        .avail_num = 1, .avails_expected = 2 });
    s.descriptors    = concat({ descriptor(scte35::kAvailDescriptor, scte35::kCuei, { 0, 0, 0, 9 }),
                                segmentation({ .event_id = 5, .duration = 2700000, .upid_type = 0x08,
                                               .upid = { 1, 2, 3, 4, 5, 6, 7, 8 }, .type = 0x34, .num = 1,
                                               .expected = 2, .sub = std::pair{ 1, 3 } }) });
    return s;
}

} // namespace

TEST_CASE("scte35: CRC-32/MPEG-2 through av_crc, against the test's own CRC on the same bytes",
          "[timeline][scte35]")
{
    // The CRC catalogue's check value of CRC-32/MPEG-2, the CRC of the nine
    // ASCII digits "123456789", which the test's own CRC gives too.
    const Bytes digits = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    CHECK(crc_bitwise(digits) == 0x0376E6E7u);
    CHECK(scte35::crc32_mpeg2(digits) == 0x0376E6E7u);
    CHECK(scte35::crc32_mpeg2({}) == 0xFFFFFFFFu);

    std::mt19937 random(35);
    std::uniform_int_distribution<int> byte(0, 255), length(0, 300);
    for (int i = 0; i < 200; ++i) {
        Bytes b(static_cast<std::size_t>(length(random)));
        for (std::uint8_t& x : b) x = static_cast<std::uint8_t>(byte(random));
        INFO(hex(b));
        CHECK(scte35::crc32_mpeg2(b) == crc_bitwise(b));
    }

    // A section's CRC_32 is the CRC of the bytes before it, and checks.
    const Bytes b = section(full_section());
    const scte35::SpliceInfo s = decoded(b);
    CHECK(s.has_crc);
    CHECK(s.crc_valid);
    CHECK(s.crc_32 == crc_bitwise(Bytes(b.begin(), b.end() - 4)));
    CHECK(s.crc_computed == s.crc_32);
}

TEST_CASE("scte35: base64 and hex text give the section's bytes, and other text is refused",
          "[timeline][scte35]")
{
    std::string err;
    // Sections of each length modulo 3, so with no, two and one '=' of padding.
    for (std::size_t extra = 0; extra < 3; ++extra) {
        Section s   = full_section();
        s.stuffing  = extra;
        const Bytes b = section(s);
        INFO("length " << b.size());
        const std::string text = base64(b);
        const auto from_b64 = scte35::from_base64(text, err);
        INFO(err);
        REQUIRE(from_b64);
        CHECK(*from_b64 == b);
        CHECK(text.front() == '/');   // 0xFC's first six bits
        for (const std::string& h : { hex(b), hex(b, "0X", false), hex(b, "", true), hex(b, "", false) }) {
            const auto from_h = scte35::from_hex(h, err);
            INFO(h << ": " << err);
            REQUIRE(from_h);
            CHECK(*from_h == b);
        }
    }

    const std::vector<std::pair<std::string, std::string>> bad_base64 = {
        { "", "it is empty" },
        { "QUJ", "its 3 characters aren't a multiple of 4" },
        { "QU JD", "its 5 characters aren't a multiple of 4" },
        { "QU J", "its character 3, byte 0x20, isn't base64's" },
        { "QUJD\nQUJD", "its 9 characters aren't a multiple of 4" },
        { "QUI=QUJD", "its character 4, '=', isn't base64's" },
        { "Q===", "its character 2, '=', isn't base64's" },
        { "-_-_", "its character 1, '-', isn't base64's" },
    };
    for (const auto& [text, why] : bad_base64) {
        INFO(text);
        CHECK_FALSE(scte35::from_base64(text, err));
        CHECK(contains(err, why));
    }
    const std::vector<std::pair<std::string, std::string>> bad_hex = {
        { "", "it has no hex digits" },
        { "0x", "it has no hex digits" },
        { "0xFC3", "its 3 hex digits are an odd number" },
        { "0xFG", "its character 4, 'G', isn't a hex digit" },
        { "0x FC", "its character 3, byte 0x20, isn't a hex digit" },
    };
    for (const auto& [text, why] : bad_hex) {
        INFO(text);
        CHECK_FALSE(scte35::from_hex(text, err));
        CHECK(contains(err, why));
    }
}

TEST_CASE("scte35: splice_insert out and in, immediate, with a break_duration and auto_return, and in "
          "component mode", "[timeline][scte35]")
{
    // An Out Point with its splice time and a break that returns by itself.
    Section out = full_section();
    const scte35::SpliceInfo s = decoded(section(out));
    CHECK(s.table_id == scte35::kTableId);
    CHECK(s.sap_type == 3);
    CHECK(s.protocol_version == 0);
    CHECK_FALSE(s.encrypted_packet);
    CHECK(s.pts_adjustment == 900000);
    CHECK(s.tier == 0x123);
    CHECK(s.splice_command_type == scte35::kSpliceInsert);
    CHECK(s.splice_command_length == out.command.size());
    CHECK(scte35::command_name(s.splice_command_type) == "splice_insert");
    const auto& c = command<scte35::SpliceInsert>(s);
    CHECK(c.splice_event_id == 0x10);
    CHECK_FALSE(c.cancel);
    CHECK(c.out_of_network);
    CHECK(c.program_splice);
    CHECK(c.duration_flag);
    CHECK_FALSE(c.splice_immediate);
    CHECK(c.event_id_compliance);
    REQUIRE(c.splice_time);
    CHECK(c.splice_time->time_specified);
    CHECK(c.splice_time->pts_time == 2700000);
    REQUIRE(c.break_duration);
    CHECK(c.break_duration->auto_return);
    CHECK(c.break_duration->duration == 2700000);
    CHECK(c.unique_program_id == 7);
    CHECK(c.avail_num == 1);
    CHECK(c.avails_expected == 2);
    const auto times = scte35::splice_times(s);
    REQUIRE(times.size() == 1);
    CHECK(times[0].pts_time == 2700000);
    CHECK(times[0].adjusted == 2700000 + 900000);
    CHECK_FALSE(times[0].component_tag);
    CHECK(s.descriptors.size() == 2);
    CHECK(s.alignment_stuffing == 0);
    CHECK(s.command_unused == 0);
    CHECK(s.bytes_after == 0);

    // An In Point without a break_duration, whose splice time isn't specified.
    Section in        = out;
    in.command        = insert_command({ .event_id = 0x10, .out = false, .compliance = false });
    in.descriptors    = {};
    const scte35::SpliceInfo si = decoded(section(in));
    const auto& ci = command<scte35::SpliceInsert>(si);
    CHECK_FALSE(ci.out_of_network);
    CHECK_FALSE(ci.duration_flag);
    CHECK_FALSE(ci.break_duration);
    CHECK_FALSE(ci.event_id_compliance);
    REQUIRE(ci.splice_time);
    CHECK_FALSE(ci.splice_time->time_specified);
    CHECK(scte35::splice_times(si).empty());

    // Immediate: no splice_time() at all, and a break that doesn't return by itself.
    Section now = out;
    now.command = insert_command({ .event_id = 0x11, .immediate = true,
                                   .duration = std::pair{ false, 450000 } });
    const scte35::SpliceInfo sn = decoded(section(now));
    const auto& cn = command<scte35::SpliceInsert>(sn);
    CHECK(cn.splice_immediate);
    CHECK_FALSE(cn.splice_time);
    REQUIRE(cn.break_duration);
    CHECK_FALSE(cn.break_duration->auto_return);
    CHECK(cn.break_duration->duration == 450000);
    CHECK(scte35::splice_times(sn).empty());

    // Component mode (deprecated): each component's splice time, one not
    // specified; then the same immediate, with no splice_time() at all.
    Section parts = out;
    parts.command = insert_command({ .event_id = 0x12, .program = false,
                                     .components = { { 0x21, 1000 }, { 0x22, std::nullopt },
                                                     { 0x23, 3000 } } });
    const scte35::SpliceInfo sp = decoded(section(parts));
    const auto& cp = command<scte35::SpliceInsert>(sp);
    CHECK_FALSE(cp.program_splice);
    CHECK_FALSE(cp.splice_time);
    REQUIRE(cp.components.size() == 3);
    CHECK(cp.components[1].tag == 0x22);
    REQUIRE(cp.components[1].splice_time);
    CHECK_FALSE(cp.components[1].splice_time->time_specified);
    const auto tp = scte35::splice_times(sp);
    REQUIRE(tp.size() == 2);
    CHECK((tp[0].component_tag == 0x21 && tp[0].adjusted == 1000 + 900000));
    CHECK((tp[1].component_tag == 0x23 && tp[1].adjusted == 3000 + 900000));

    parts.command = insert_command({ .event_id = 0x13, .program = false, .immediate = true,
                                     .components = { { 0x21, std::nullopt }, { 0x22, std::nullopt } } });
    const scte35::SpliceInfo sq = decoded(section(parts));
    const auto& cq = command<scte35::SpliceInsert>(sq);
    REQUIRE(cq.components.size() == 2);
    CHECK_FALSE(cq.components[0].splice_time);
    CHECK(scte35::splice_times(sq).empty());
}

TEST_CASE("scte35: time_signal with segmentation descriptors, sub-segments, a UPID, restrictions, the "
          "other CUEI descriptors, and descriptors whose identifier isn't CUEI", "[timeline][scte35]")
{
    Section s;
    s.command_type = scte35::kTimeSignal;
    s.command      = signal_command(5400000);
    const Bytes dtmf_body  = { 50, static_cast<std::uint8_t>(3 << 5 | 0x1F), '1', '2', '*' };
    const Bytes time_body  = { 0, 0, 0x5F, 0x5E, 0x10, 0x00, 0x00, 0x00, 0x00, 0x2A, 0x00, 0x25 };
    const Bytes audio_body = { static_cast<std::uint8_t>(1 << 4 | 0xF), 0x31, 'e', 'n', 'g',
                               static_cast<std::uint8_t>(2 << 5 | 5 << 1 | 1) };
    s.descriptors = concat({
        // A private descriptor whose tag is that of a segmentation descriptor:
        // its identifier isn't CUEI, so it is skipped by its length.
        descriptor(scte35::kSegmentationDescriptor, 0x41424344, { 0xDE, 0xAD }),
        descriptor(scte35::kAvailDescriptor, scte35::kCuei, { 0x00, 0x00, 0x01, 0x2C }),
        // A Provider Placement Opportunity Start with sub-segments, a TI UPID
        // and a duration.
        segmentation({ .event_id = 0xA5A50001, .duration = 2700000, .upid_type = 0x08,
                       .upid = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF }, .type = 0x34,
                       .num = 2, .expected = 4, .sub = std::pair{ 1, 3 } }),
        // A Provider Advertisement Start whose descriptor_length doesn't hold
        // the sub-segment fields: they are absent.
        segmentation({ .event_id = 0xA5A50002, .upid_type = 0x00, .type = 0x30, .num = 1,
                       .expected = 1 }),
        // A Break End with delivery restricted, a component loop, and a byte
        // its fields leave; a Break Start (0x22) doesn't take sub-segments,
        // so their two bytes here are bytes left.
        segmentation({ .event_id = 0x01020304, .program = false, .restricted = true, .web = true,
                       .no_blackout = false, .archive = true, .device = 2,
                       .components = { { 0x40, 90000 } }, .type = 0x23, .extra = { 0xAB } }),
        segmentation({ .event_id = 0x01020305, .type = 0x22, .sub = std::pair{ 1, 2 } }),
        descriptor(scte35::kDtmfDescriptor, scte35::kCuei, dtmf_body),
        descriptor(scte35::kTimeDescriptor, scte35::kCuei, time_body),
        descriptor(scte35::kAudioDescriptor, scte35::kCuei, audio_body),
        // A tag SCTE reserves (Table 16), skipped by its length.
        descriptor(0x05, scte35::kCuei, { 1, 2, 3 }),
    });
    const scte35::SpliceInfo d = decoded(section(s));
    const auto& t = command<scte35::TimeSignal>(d);
    CHECK(t.splice_time.time_specified);
    CHECK(t.splice_time.pts_time == 5400000);
    REQUIRE(d.descriptors.size() == 10);
    CHECK(d.descriptor_loop_length == s.descriptors.size());

    const scte35::Descriptor& priv = d.descriptors[0];
    CHECK(priv.tag == scte35::kSegmentationDescriptor);
    CHECK(priv.identifier == 0x41424344u);
    CHECK(std::holds_alternative<std::monostate>(priv.body));
    CHECK(priv.bytes == Bytes{ 0xDE, 0xAD });
    CHECK(priv.error.empty());

    CHECK(body<scte35::AvailDescriptor>(d.descriptors[1]).provider_avail_id == 300);

    const auto& po = body<scte35::SegmentationDescriptor>(d.descriptors[2]);
    CHECK(po.event_id == 0xA5A50001);
    CHECK_FALSE(po.cancel);
    CHECK(po.event_id_compliance);
    CHECK(po.program_segmentation);
    CHECK(po.duration_flag);
    CHECK(po.delivery_not_restricted);
    CHECK(po.duration == 2700000);
    CHECK(po.upid_type == 0x08);
    CHECK(po.upid == Bytes{ 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF });
    CHECK(po.type_id == 0x34);
    CHECK(scte35::segmentation_type_name(po.type_id) == "Provider Placement Opportunity Start");
    CHECK(po.segment_num == 2);
    CHECK(po.segments_expected == 4);
    CHECK(po.sub_segment_num == 1);
    CHECK(po.sub_segments_expected == 3);
    CHECK(d.descriptors[2].unparsed == 0);

    const auto& ad = body<scte35::SegmentationDescriptor>(d.descriptors[3]);
    CHECK(ad.type_id == 0x30);
    CHECK_FALSE(ad.duration);
    CHECK(ad.upid.empty());
    CHECK_FALSE(ad.sub_segment_num);
    CHECK_FALSE(ad.sub_segments_expected);

    const auto& end = body<scte35::SegmentationDescriptor>(d.descriptors[4]);
    CHECK(end.type_id == 0x23);
    CHECK_FALSE(end.program_segmentation);
    CHECK_FALSE(end.delivery_not_restricted);
    CHECK(end.web_delivery_allowed);
    CHECK_FALSE(end.no_regional_blackout);
    CHECK(end.archive_allowed);
    CHECK(end.device_restrictions == 2);
    REQUIRE(end.components.size() == 1);
    CHECK(end.components[0].tag == 0x40);
    CHECK(end.components[0].pts_offset == 90000);
    CHECK_FALSE(end.sub_segment_num);
    CHECK(d.descriptors[4].unparsed == 1);

    const auto& start = body<scte35::SegmentationDescriptor>(d.descriptors[5]);
    CHECK(start.type_id == 0x22);
    CHECK_FALSE(start.sub_segment_num);
    CHECK(d.descriptors[5].unparsed == 2);

    const auto& dtmf = body<scte35::DtmfDescriptor>(d.descriptors[6]);
    CHECK(dtmf.preroll == 50);
    CHECK(dtmf.chars == "12*");
    const auto& time = body<scte35::TimeDescriptor>(d.descriptors[7]);
    CHECK(time.tai_seconds == 0x5F5E1000);
    CHECK(time.tai_ns == 42);
    CHECK(time.utc_offset == 37);
    const auto& audio = body<scte35::AudioDescriptor>(d.descriptors[8]);
    REQUIRE(audio.channels.size() == 1);
    CHECK(audio.channels[0].component_tag == 0x31);
    CHECK(audio.channels[0].iso_code == "eng");
    CHECK(audio.channels[0].bit_stream_mode == 2);
    CHECK(audio.channels[0].num_channels == 5);
    CHECK(audio.channels[0].full_srvc_audio);

    const scte35::Descriptor& reserved = d.descriptors[9];
    CHECK(reserved.tag == 0x05);
    CHECK(reserved.identifier == scte35::kCuei);
    CHECK(std::holds_alternative<std::monostate>(reserved.body));
    CHECK(reserved.bytes == Bytes{ 1, 2, 3 });
    CHECK(reserved.error.empty());

    // A time_signal whose time isn't specified: an immediate one, no splice time.
    s.command = signal_command(std::nullopt);
    const scte35::SpliceInfo n = decoded(section(s));
    CHECK_FALSE(command<scte35::TimeSignal>(n).splice_time.time_specified);
    CHECK(scte35::splice_times(n).empty());
}

TEST_CASE("scte35: splice_command_length 0xFFF, alignment stuffing, and a command shorter than its "
          "splice_command_length", "[timeline][scte35]")
{
    // The legacy length: the command is as long as its syntax.
    Section s = full_section();
    s.legacy  = true;
    const scte35::SpliceInfo l = decoded(section(s));
    CHECK(l.splice_command_length == scte35::kLegacyCommandLength);
    CHECK(command<scte35::SpliceInsert>(l).splice_time->pts_time == 2700000);
    REQUIRE(l.descriptors.size() == 2);
    CHECK(body<scte35::SegmentationDescriptor>(l.descriptors[1]).type_id == 0x34);
    s.command_type = scte35::kTimeSignal;
    s.command      = signal_command(1234);
    CHECK(command<scte35::TimeSignal>(decoded(section(s))).splice_time.pts_time == 1234);

    // Three bytes of stuffing, which an unencrypted section may have.
    Section st  = full_section();
    st.stuffing = 3;
    const scte35::SpliceInfo a = decoded(section(st));
    CHECK(a.alignment_stuffing == 3);
    CHECK(a.descriptors.size() == 2);
    CHECK(a.crc_valid);

    // A splice_command_length two bytes past the command: the descriptor loop
    // is found after them.
    Section pad         = full_section();
    pad.command_padding = 2;
    const scte35::SpliceInfo p = decoded(section(pad));
    CHECK(p.command_unused == 2);
    CHECK(p.splice_command_length == pad.command.size() + 2);
    CHECK(p.descriptors.size() == 2);

    // Under 0xFFF, a command whose syntax gives no length can't be passed.
    Section priv      = full_section();
    priv.legacy       = true;
    priv.command_type = scte35::kPrivateCommand;
    priv.command      = { 0x41, 0x42, 0x43, 0x44, 9, 9 };
    scte35::SpliceInfo e = scte35::decode(section(priv));
    CHECK(contains(e.error, "a private_command under splice_command_length 0xFFF has no length"));
    priv.command_type = 0x10;
    e = scte35::decode(section(priv));
    CHECK(contains(e.error, "the reserved splice_command_type 0x10 under splice_command_length 0xFFF has "
                            "no length"));
}

TEST_CASE("scte35: pts_adjustment is added to a splice time mod 2^33", "[timeline][scte35]")
{
    constexpr std::int64_t kTop = scte35::kPtsWrap - 1;
    CHECK(scte35::add_pts(0, 0) == 0);
    CHECK(scte35::add_pts(kTop, 1) == 0);
    CHECK(scte35::add_pts(kTop, kTop) == kTop - 1);
    CHECK(scte35::add_pts(scte35::kPtsWrap - 90000, 180000) == 90000);

    Section s;
    s.command_type   = scte35::kTimeSignal;
    s.command        = signal_command(scte35::kPtsWrap - 90000);
    s.pts_adjustment = 180000;
    const scte35::SpliceInfo d = decoded(section(s));
    CHECK(d.pts_adjustment == 180000);
    const auto t = scte35::splice_times(d);
    REQUIRE(t.size() == 1);
    CHECK(t[0].pts_time == scte35::kPtsWrap - 90000);
    CHECK(t[0].adjusted == 90000);

    // Both at their 33-bit maximum.
    s.command        = signal_command(kTop);
    s.pts_adjustment = kTop;
    const auto top = scte35::splice_times(decoded(section(s)));
    REQUIRE(top.size() == 1);
    CHECK(top[0].pts_time == kTop);
    CHECK(top[0].adjusted == kTop - 1);
}

TEST_CASE("scte35: a CRC_32 that fails, in its field or in the bytes before it, is reported and the "
          "section still decoded", "[timeline][scte35]")
{
    Section s  = full_section();
    s.bad_crc  = true;
    const Bytes b = section(s);
    const scte35::SpliceInfo d = decoded(b);
    CHECK(d.has_crc);
    CHECK_FALSE(d.crc_valid);
    CHECK(d.crc_computed == crc_bitwise(Bytes(b.begin(), b.end() - 4)));
    CHECK(d.crc_32 == (d.crc_computed ^ 1u));
    CHECK(command<scte35::SpliceInsert>(d).splice_event_id == 0x10);

    // A byte of the section changed after its CRC_32 was computed: avail_num,
    // the second byte from the end of the command, which starts at byte 14.
    Bytes changed = section(full_section());
    const std::size_t at = 14 + full_section().command.size() - 2;
    changed[at] = static_cast<std::uint8_t>(changed[at] ^ 0x04);
    const scte35::SpliceInfo c = decoded(changed);
    CHECK_FALSE(c.crc_valid);
    CHECK(command<scte35::SpliceInsert>(c).avail_num == (1 ^ 0x04));
}

TEST_CASE("scte35: input cut at every byte is refused, and no field is read past its structure",
          "[timeline][scte35]")
{
    // Every prefix of a whole section: section_length asks for more.
    const Bytes whole = section(full_section());
    for (std::size_t n = 0; n < whole.size(); ++n) {
        const Bytes cut(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(n));
        const scte35::SpliceInfo d = scte35::decode(cut);
        INFO("the first " << n << " bytes: " << d.error);
        CHECK_FALSE(d.decoded());
        CHECK(d.input_bytes == n);
    }
    // Trailing bytes after a section are counted, not read.
    Bytes longer = whole;
    longer.insert(longer.end(), { 0xFF, 0xFF, 0xFF });
    const scte35::SpliceInfo tail = decoded(longer);
    CHECK(tail.bytes_after == 3);
    CHECK(tail.crc_valid);

    // The section's own bytes cut at every byte, with section_length and
    // CRC_32 made to fit, so that each inner structure runs past its end:
    // only the whole body decodes, and its error names what ran past.
    const Section s  = full_section();
    const Bytes body = body_of(s);
    for (std::size_t n = 0; n <= body.size(); ++n) {
        const Bytes sealed = seal(s, Bytes(body.begin(), body.begin() + static_cast<std::ptrdiff_t>(n)));
        const scte35::SpliceInfo d = scte35::decode(sealed);
        INFO("the body's first " << n << " of " << body.size() << " bytes: " << d.error);
        CHECK(d.decoded() == (n == body.size()));
        if (n >= 10) CHECK(d.crc_valid);   // the header is there, with its CRC_32
    }
    CHECK(contains(scte35::decode(seal(s, Bytes(body.begin(), body.begin() + 12))).error,
                   "its splice_command_length of 20 runs past the end of the section"));

    // A descriptor whose fields run past its descriptor_length keeps the
    // reason, without failing the section; so does one too short for its
    // identifier.
    Section d   = full_section();
    Bytes avail = descriptor(scte35::kAvailDescriptor, scte35::kCuei, { 0, 0 });
    Bytes seg   = segmentation({ .event_id = 9, .upid = { 1, 2, 3 }, .type = 0x22 });
    seg[1] = static_cast<std::uint8_t>(seg[1] - 4);   // its upid's bytes and after: past its length
    seg.resize(seg.size() - 4);
    d.descriptors = concat({ avail, seg, Bytes{ 0x00, 0x02, 0xAA, 0xBB } });
    const scte35::SpliceInfo k = decoded(section(d));
    REQUIRE(k.descriptors.size() == 3);
    CHECK(std::holds_alternative<std::monostate>(k.descriptors[0].body));
    CHECK(contains(k.descriptors[0].error, "provider_avail_id runs past the end of descriptor 1"));
    CHECK(contains(k.descriptors[1].error, "segmentation_upid runs past the end of descriptor 2"));
    CHECK(k.descriptors[1].bytes.size() + 4 == k.descriptors[1].length);
    CHECK_FALSE(k.descriptors[2].identifier);
    CHECK(contains(k.descriptors[2].error, "its descriptor_length of 2 holds no identifier"));
    CHECK(k.descriptors[2].bytes == Bytes{ 0xAA, 0xBB });
}

TEST_CASE("scte35: an encrypted section is reported undecoded, with its header and CRC_32 read",
          "[timeline][scte35]")
{
    Section s              = full_section();
    s.encrypted            = true;
    s.encryption_algorithm = 1;
    s.cw_index             = 5;
    const scte35::SpliceInfo d = scte35::decode(section(s));
    CHECK_FALSE(d.decoded());
    CHECK(contains(d.error, "it is encrypted (encrypted_packet 1, encryption_algorithm 1)"));
    CHECK(d.encrypted_packet);
    CHECK(d.encryption_algorithm == 1);
    CHECK(d.cw_index == 5);
    CHECK(d.pts_adjustment == 900000);
    CHECK(d.tier == 0x123);
    CHECK(d.crc_valid);
    CHECK(std::holds_alternative<std::monostate>(d.command));
    CHECK(d.descriptors.empty());
    CHECK(scte35::splice_times(d).empty());
}

TEST_CASE("scte35: cancel indicators end a splice_insert and a segmentation descriptor",
          "[timeline][scte35]")
{
    Section s      = full_section();
    s.command      = insert_command({ .event_id = 0xA5A50002, .cancel = true });
    s.descriptors  = segmentation({ .event_id = 0xA5A50001, .cancel = true, .compliance = false });
    const scte35::SpliceInfo d = decoded(section(s));
    const auto& c = command<scte35::SpliceInsert>(d);
    CHECK(c.splice_event_id == 0xA5A50002);
    CHECK(c.cancel);
    CHECK_FALSE(c.splice_time);
    CHECK_FALSE(c.break_duration);
    CHECK(s.command.size() == 5);   // the id and one byte of the indicator
    CHECK(scte35::splice_times(d).empty());
    REQUIRE(d.descriptors.size() == 1);
    const auto& g = body<scte35::SegmentationDescriptor>(d.descriptors[0]);
    CHECK(g.event_id == 0xA5A50001);
    CHECK(g.cancel);
    CHECK_FALSE(g.event_id_compliance);
    CHECK(d.descriptors[0].length == 9);   // the identifier, the id and one byte
    CHECK(d.descriptors[0].unparsed == 0);
}

TEST_CASE("scte35: splice_null, bandwidth_reservation, splice_schedule, private_command and a reserved "
          "command", "[timeline][scte35]")
{
    Section s;
    const scte35::SpliceInfo null = decoded(section(s));
    CHECK(null.splice_command_type == scte35::kSpliceNull);
    CHECK(std::holds_alternative<std::monostate>(null.command));
    CHECK(scte35::command_name(null.splice_command_type) == "splice_null");

    s.command_type = scte35::kBandwidthReservation;
    CHECK(std::holds_alternative<std::monostate>(decoded(section(s)).command));
    CHECK(scte35::command_name(scte35::kBandwidthReservation) == "bandwidth_reservation");

    // splice_schedule: an event in program mode with a break, a cancelled
    // one, and one in component mode.
    Writer w;
    w.put(3, 8);
    w.put(0x100, 32).put(0, 1).put(1, 1).put(0x3F, 6).put(1, 1).put(1, 1).put(1, 1).put(0x1F, 5)
        .put(1300000000, 32).put(1, 1).put(0x3F, 6).put(2700000, 33).put(9, 16).put(1, 8).put(1, 8);
    w.put(0x101, 32).put(1, 1).put(0, 1).put(0x3F, 6);
    w.put(0x102, 32).put(0, 1).put(1, 1).put(0x3F, 6).put(0, 1).put(0, 1).put(0, 1).put(0x1F, 5)
        .put(2, 8).put(0x31, 8).put(1300000010, 32).put(0x32, 8).put(1300000020, 32)
        .put(10, 16).put(2, 8).put(3, 8);
    s.command_type = scte35::kSpliceSchedule;
    s.command      = w.done();
    const scte35::SpliceInfo sched = decoded(section(s));
    const auto& e = command<scte35::SpliceSchedule>(sched).events;
    REQUIRE(e.size() == 3);
    CHECK(e[0].splice_event_id == 0x100);
    CHECK(e[0].out_of_network);
    CHECK(e[0].utc_splice_time == 1300000000u);
    REQUIRE(e[0].break_duration);
    CHECK(e[0].break_duration->duration == 2700000);
    CHECK(e[0].unique_program_id == 9);
    CHECK(e[1].cancel);
    CHECK_FALSE(e[1].utc_splice_time);
    CHECK_FALSE(e[2].program_splice);
    REQUIRE(e[2].components.size() == 2);
    CHECK((e[2].components[1].tag == 0x32 && e[2].components[1].utc_splice_time == 1300000020u));
    CHECK(e[2].avails_expected == 3);
    CHECK(scte35::splice_times(sched).empty());   // UTC times, no PTS

    s.command_type = scte35::kPrivateCommand;
    s.command      = { 0x41, 0x42, 0x43, 0x44, 1, 2, 3 };
    const scte35::SpliceInfo priv = decoded(section(s));
    const auto& p = command<scte35::PrivateCommand>(priv);
    CHECK(p.identifier == 0x41424344u);
    CHECK(p.bytes == Bytes{ 1, 2, 3 });

    s.command_type = 0x10;   // reserved (Table 7)
    s.command      = { 7, 8 };
    const scte35::SpliceInfo r = decoded(section(s));
    CHECK(std::holds_alternative<std::monostate>(r.command));
    CHECK(r.command_bytes == Bytes{ 7, 8 });
    CHECK(scte35::command_name(0x10) == "reserved");
}

TEST_CASE("scte35: what isn't a splice_info_section of version 0 doesn't decode, and says why",
          "[timeline][scte35]")
{
    Section s   = full_section();
    s.table_id  = 0xFD;
    CHECK(contains(scte35::decode(section(s)).error,
                   "its table_id is 0xFD, not a splice_info_section's 0xFC"));

    s = full_section();
    s.protocol_version = 1;
    const scte35::SpliceInfo v = scte35::decode(section(s));
    CHECK(contains(v.error, "its protocol_version is 1"));
    CHECK(v.crc_valid);

    // section_length past the input, and too short for the header.
    Bytes b = section(full_section());
    b[2] = static_cast<std::uint8_t>(b[2] + 1);
    CHECK(contains(scte35::decode(b).error, "makes a section of"));
    const Bytes tiny = { 0xFC, 0x30, 0x05, 0, 0, 0, 0, 0 };
    CHECK(contains(scte35::decode(tiny).error, "its section_length of 5 leaves no room for the header"));

    // A descriptor loop past CRC_32, and a descriptor past the loop's end.
    s = full_section();
    Bytes body = body_of(s);
    const std::size_t loop_at = 11 + s.command.size();   // descriptor_loop_length, after the command
    body[loop_at + 1] = static_cast<std::uint8_t>(body[loop_at + 1] + 1);
    CHECK(contains(scte35::decode(seal(s, body)).error, "runs past CRC_32"));
    s.descriptors = { 0x00, 0x09, 0x43, 0x55 };   // a descriptor_length of 9 in a loop of 4
    CHECK(contains(scte35::decode(section(s)).error,
                   "descriptor 1 (splice_descriptor_tag 0x00, descriptor_length 9) runs past the end of "
                   "the descriptor loop"));
    s.descriptors = { 0x00 };
    CHECK(contains(scte35::decode(section(s)).error, "descriptor 1 has 1 of its 2 header bytes"));
}

// ── Published messages, from a local file (P-M; D-107) ───────────────────────

namespace {

// The bytes of a vector's value in its encoding, as the survey's checker
// reads them: hex with or without 0x, or base64.
std::optional<Bytes> vector_bytes(const json::Value& encoding, const json::Value& value, std::string& err)
{
    const std::string* e = encoding.string();
    const std::string* v = value.string();
    if (!e || !v) {
        err = "no encoding or value";
        return std::nullopt;
    }
    return *e == "hex" ? scte35::from_hex(*v, err) : scte35::from_base64(*v, err);
}

// A member of the survey's summary as an integer; nullopt for none, for
// null and for any other value.
std::optional<std::int64_t> json_int(const json::Value* v)
{
    if (!v || v->is_null() || !v->integer()) return std::nullopt;
    return *v->integer();
}

std::optional<std::int64_t> hex_number(const json::Value* v)
{
    const std::string* s = v ? v->string() : nullptr;
    if (!s) return std::nullopt;
    std::string err;
    std::string digits = s->starts_with("0x") ? s->substr(2) : *s;
    if (digits.size() % 2) digits = "0" + digits;
    const auto b = scte35::from_hex(digits, err);
    if (!b) return std::nullopt;
    std::int64_t n = 0;
    for (std::uint8_t x : *b) n = n << 8 | x;
    return n;
}

// The survey's reference decoder's name of a descriptor, which its summary
// of a decode gives (scte35_evidence/tools/scte35_decode.py).
std::optional<std::string> reference_name(const scte35::Descriptor& d)
{
    if (!d.identifier) return std::nullopt;
    if (*d.identifier != scte35::kCuei) return "private descriptor";
    switch (d.tag) {
    case scte35::kAvailDescriptor:        return "avail_descriptor";
    case scte35::kDtmfDescriptor:         return "DTMF_descriptor";
    case scte35::kSegmentationDescriptor: return "segmentation_descriptor";
    case scte35::kTimeDescriptor:         return "time_descriptor";
    case scte35::kAudioDescriptor:        return "audio_descriptor";
    default:                              return "reserved descriptor";
    }
}

// Each field of the survey's summary of a decode (agent_decode) against the
// decoder's; each disagreement named in `out`.
void compare_summary(const json::Value& want, const scte35::SpliceInfo& got, std::vector<std::string>& out)
{
    const auto eq_int = [&](std::string_view key, std::optional<std::int64_t> mine) {
        const json::Value* w = want.find(key);
        if (!w) return;
        const std::optional<std::int64_t> theirs = json_int(w);
        if (theirs != mine)
            out.push_back(std::format("{}: the survey's {}, the decoder's {}", key,
                                      theirs ? std::to_string(*theirs) : "null",
                                      mine ? std::to_string(*mine) : "null"));
    };
    if (const json::Value* name = want.find("splice_command"); name && name->string()
        && *name->string() != scte35::command_name(got.splice_command_type))
        out.push_back(std::format("splice_command: the survey's {}, the decoder's {}", *name->string(),
                                  scte35::command_name(got.splice_command_type)));
    eq_int("pts_adjustment", got.pts_adjustment);
    if (const auto tier = hex_number(want.find("tier")); tier && *tier != got.tier)
        out.push_back(std::format("tier: the survey's {}, the decoder's {}", *tier, got.tier));
    if (const auto crc = hex_number(want.find("crc_32")); crc && *crc != got.crc_32)
        out.push_back(std::format("crc_32: the survey's {}, the decoder's {}", *crc, got.crc_32));
    if (const auto* c = std::get_if<scte35::SpliceInsert>(&got.command)) {
        eq_int("splice_event_id", c->splice_event_id);
        eq_int("splice_event_cancel_indicator", c->cancel ? 1 : 0);
        if (!c->cancel) {
            eq_int("out_of_network_indicator", c->out_of_network ? 1 : 0);
            eq_int("splice_immediate_flag", c->splice_immediate ? 1 : 0);
        }
        if (c->splice_time)
            eq_int("pts_time", c->splice_time->time_specified
                                   ? std::optional<std::int64_t>(c->splice_time->pts_time)
                                   : std::nullopt);
        if (c->break_duration) {
            eq_int("break_duration", c->break_duration->duration);
            eq_int("auto_return", c->break_duration->auto_return ? 1 : 0);
        }
    }
    if (const auto* t = std::get_if<scte35::TimeSignal>(&got.command))
        eq_int("pts_time", t->splice_time.time_specified
                               ? std::optional<std::int64_t>(t->splice_time.pts_time)
                               : std::nullopt);
    // The survey's summary has the splice time of a time_signal and of a
    // splice_insert in program mode only.
    std::optional<std::int64_t> adjusted;
    for (const scte35::SplicePoint& p : scte35::splice_times(got))
        if (!p.component_tag && !adjusted) adjusted = p.adjusted;
    if (want.find("splice_pts_adjusted")) eq_int("splice_pts_adjusted", adjusted);
    else if (adjusted)
        out.push_back(std::format("splice_pts_adjusted: the survey's none, the decoder's {}", *adjusted));

    const json::Value* descriptors = want.find("descriptors");
    const json::Value::Array none;
    const json::Value::Array& theirs = descriptors && descriptors->array() ? *descriptors->array() : none;
    if (theirs.size() != got.descriptors.size()) {
        out.push_back(std::format("descriptors: the survey's {}, the decoder's {}", theirs.size(),
                                  got.descriptors.size()));
        return;
    }
    for (std::size_t i = 0; i < theirs.size(); ++i) {
        const json::Value&         w = theirs[i];
        const scte35::Descriptor&  d = got.descriptors[i];
        const auto*                seg = std::get_if<scte35::SegmentationDescriptor>(&d.body);
        const std::string          at = std::format("descriptor {}: ", i + 1);
        if (w.find("segmentation_event_id")) {
            if (!seg) {
                out.push_back(at + "the survey's a segmentation_descriptor, the decoder's not: " + d.error);
                continue;
            }
            std::vector<std::string> sub;
            const auto field = [&](std::string_view key, std::optional<std::int64_t> mine) {
                const json::Value* v = w.find(key);
                if (v && json_int(v) != mine) sub.push_back(std::string(key));
            };
            field("segmentation_event_id", seg->event_id);
            field("segmentation_type_id",
                  seg->cancel ? std::nullopt : std::optional<std::int64_t>(seg->type_id));
            field("segmentation_duration", seg->duration);
            field("cancel", seg->cancel ? std::optional<std::int64_t>(1) : std::nullopt);
            if (!w.find("cancel") && seg->cancel) sub.push_back("cancel");
            if (!w.find("segmentation_duration") && seg->duration) sub.push_back("segmentation_duration");
            for (const std::string& f : sub) out.push_back(at + f);
        } else {
            const json::Value* name = w.find("descriptor");
            const json::Value* tag  = w.find("tag");
            const std::optional<std::string> mine = reference_name(d);
            const std::optional<std::string> theirs_name =
                name && name->string() ? std::optional<std::string>(*name->string()) : std::nullopt;
            if (theirs_name != mine)
                out.push_back(at + std::format("the survey's {}, the decoder's {}",
                                               theirs_name.value_or("null"), mine.value_or("null")));
            if (tag && tag->integer() && *tag->integer() != d.tag) out.push_back(at + "tag");
            if (seg) out.push_back(at + "the decoder's a segmentation_descriptor, the survey's not");
        }
    }
}

} // namespace

TEST_CASE("scte35: published messages from a local vectors file that MIRAGE_SCTE35_VECTORS names",
          "[timeline][scte35]")
{
    // The survey's scte35_vectors.json, or a file in its format, outside the
    // repository: no published message is committed (D-107).
    const char* path = std::getenv("MIRAGE_SCTE35_VECTORS");
    if (!path || !*path) SKIP("MIRAGE_SCTE35_VECTORS names no vectors file (DECISIONS.md D-107)");
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    json::Error jerr;
    const std::optional<json::Value> doc = json::parse(text, jerr);
    INFO(json::error_text(jerr));
    REQUIRE(doc);
    const json::Value* vectors = doc->find("vectors");
    REQUIRE((vectors && vectors->array()));

    std::size_t compared = 0, decodes = 0, bad_crc = 0, refused = 0;
    std::vector<std::string> disagreements;
    for (const json::Value& v : *vectors->array()) {
        const json::Value* id_value = v.find("id");
        const std::string  id = id_value && id_value->string() ? *id_value->string() : "(no id)";
        const json::Value* encoding = v.find("encoding");
        const json::Value* value = v.find("value");
        const json::Value* check = v.find("agent_check");
        if (!encoding || !value || !check) continue;
        ++compared;
        std::vector<std::string> mine;
        std::string err;
        const std::optional<Bytes> bytes = vector_bytes(*encoding, *value, err);
        const scte35::SpliceInfo s = bytes ? scte35::decode(*bytes) : scte35::SpliceInfo{ .error = err };
        const json::Value* their_decoded = check->find("decoded");
        const bool theirs = their_decoded && their_decoded->boolean() && *their_decoded->boolean();
        if (s.decoded() != theirs)
            mine.push_back(std::format("decoded: the survey's {}, the decoder's {} ({})", theirs,
                                       s.decoded(), s.error));
        if (s.decoded() && theirs) {
            ++decodes;
            const json::Value* crc = check->find("crc_32_valid");
            if (crc && crc->boolean() && *crc->boolean() != s.crc_valid)
                mine.push_back(std::format("crc_32_valid: the survey's {}, the decoder's {}",
                                           *crc->boolean(), s.crc_valid));
            if (!s.crc_valid) ++bad_crc;
            if (const json::Value* summary = v.find("agent_decode")) compare_summary(*summary, s, mine);
        } else if (!s.decoded()) {
            ++refused;
        }
        // The same message in another encoding gives the same bytes, unless
        // its source expects an error.
        const json::Value* also = v.find("also_published_as");
        if (bytes && !v.find("source_expects_error") && also && also->array())
            for (const json::Value& a : *also->array()) {
                const json::Value* e2 = a.find("encoding");
                const json::Value* v2 = a.find("value");
                if (!e2 || !v2) continue;
                std::string err2;
                const std::optional<Bytes> other = vector_bytes(*e2, *v2, err2);
                if (!other || *other != *bytes) mine.push_back("also_published_as: other bytes " + err2);
            }
        for (const std::string& m : mine) disagreements.push_back(id + ": " + m);
    }
    for (const std::string& d : disagreements) UNSCOPED_INFO(d);
    CHECK(disagreements.empty());
    WARN(std::format("{} vectors compared: {} decoded by both, {} of them with a CRC_32 that fails; {} "
                     "not decoded; {} disagreements", compared, decodes, bad_crc, refused,
                     disagreements.size()));
}
