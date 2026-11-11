#pragma once

// SCTE-35 sections for the tests, built from field values with their CRC_32
// (eval/phase3-survey/design.md P-M), and testgen's ad ladder built by hand
// around them (eval/phase3-survey/testgen.md §2.3), as
// tests/test_timeline_build.cpp builds it: the writer of the decoder's tests
// (tests/test_scte35.cpp), and the sections, the base64 text and the Ladder of
// the message events' tests (tests/test_scte35_events.cpp), in one header so
// that the tests of decoded messages in the build include them rather than
// copy them. No published message is in it (DECISIONS.md D-107). Header-only,
// so that the test target's sources stay as they are.

#include <catch2/catch_test_macros.hpp>

#include "FileHash.h"
#include "LadderReportRead.h"
#include "PlaylistEvents.h"
#include "Scte35.h"
#include "Scte35Events.h"
#include "TimelineAxis.h"
#include "TimelineBuild.h"
#include "TimelineModel.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace scte35test {

using Bytes = std::vector<std::uint8_t>;

// ── The writer (P-M) ─────────────────────────────────────────────────────────

// Bit fields, most significant bit first.
class Writer {
public:
    // The low `n` bits of `v`.
    Writer& put(std::uint64_t v, int n)
    {
        for (int i = n - 1; i >= 0; --i) bit(static_cast<unsigned>((v >> i) & 1u));
        return *this;
    }
    Writer& bytes(const Bytes& b)
    {
        for (std::uint8_t x : b) put(x, 8);
        return *this;
    }
    Bytes done() const
    {
        REQUIRE(bits_ % 8 == 0);
        return out_;
    }

private:
    Bytes       out_;
    std::size_t bits_ = 0;

    void bit(unsigned b)
    {
        if (bits_ % 8 == 0) out_.push_back(0);
        if (b) out_.back() = static_cast<std::uint8_t>(out_.back() | (0x80u >> (bits_ % 8)));
        ++bits_;
    }
};

// CRC-32/MPEG-2, the tests' own, bit by bit: polynomial 0x04C11DB7, from
// 0xFFFFFFFF, neither reflected nor inverted (scte35.md §3.6).
inline std::uint32_t crc_bitwise(const Bytes& b)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::uint8_t x : b) {
        crc ^= std::uint32_t{ x } << 24;
        for (int k = 0; k < 8; ++k) crc = crc & 0x80000000u ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    }
    return crc;
}

// A splice_time(): time_specified with `pts`, or not; reserved bits as 1s.
inline void put_time(Writer& w, std::optional<std::int64_t> pts)
{
    if (pts) w.put(1, 1).put(0x3F, 6).put(static_cast<std::uint64_t>(*pts), 33);
    else w.put(0, 1).put(0x7F, 7);
}

// A splice_info_section's fields; its lengths and CRC_32 are computed.
struct Section {
    std::uint8_t  table_id = scte35::kTableId;
    bool          section_syntax_indicator = false;
    bool          private_indicator = false;
    std::uint8_t  sap_type = 3;
    std::uint8_t  protocol_version = 0;
    bool          encrypted = false;
    std::uint8_t  encryption_algorithm = 0;
    std::int64_t  pts_adjustment = 0;
    std::uint8_t  cw_index = 0;
    std::uint16_t tier = 0xFFF;
    std::uint8_t  command_type = scte35::kSpliceNull;
    Bytes         command;                   // the command's bytes
    bool          legacy = false;            // splice_command_length 0xFFF
    std::size_t   command_padding = 0;       // bytes splice_command_length counts after the command
    Bytes         descriptors;               // the loop's bytes
    std::size_t   stuffing = 0;              // alignment stuffing bytes
    bool          bad_crc = false;           // CRC_32 off by its last bit
};

// The bytes from protocol_version to the end of the stuffing.
inline Bytes body_of(const Section& s)
{
    Writer w;
    w.put(s.protocol_version, 8)
        .put(s.encrypted, 1)
        .put(s.encryption_algorithm, 6)
        .put(static_cast<std::uint64_t>(s.pts_adjustment), 33)
        .put(s.cw_index, 8)
        .put(s.tier, 12)
        .put(s.legacy ? 0xFFF : s.command.size() + s.command_padding, 12)
        .put(s.command_type, 8)
        .bytes(s.command);
    for (std::size_t i = 0; i < s.command_padding; ++i) w.put(0xFF, 8);
    w.put(s.descriptors.size(), 16).bytes(s.descriptors);
    for (std::size_t i = 0; i < s.stuffing; ++i) w.put(0xFF, 8);
    return w.done();
}

// A section of `body`, the bytes after section_length, with its CRC_32.
inline Bytes seal(const Section& s, const Bytes& body)
{
    Writer head;
    head.put(s.table_id, 8)
        .put(s.section_syntax_indicator, 1)
        .put(s.private_indicator, 1)
        .put(s.sap_type, 2)
        .put(body.size() + 4, 12);
    Bytes out = head.done();
    out.insert(out.end(), body.begin(), body.end());
    std::uint32_t crc = crc_bitwise(out);
    if (s.bad_crc) crc ^= 1u;
    for (int k = 3; k >= 0; --k) out.push_back(static_cast<std::uint8_t>(crc >> (8 * k)));
    return out;
}

// The bytes of section `s`.
inline Bytes section(const Section& s) { return seal(s, body_of(s)); }

// A splice_insert()'s fields (Table 10).
struct Insert {
    std::uint32_t event_id = 1;
    bool          cancel = false;
    bool          out = true;
    bool          program = true;
    bool          immediate = false;
    bool          compliance = true;
    std::optional<std::int64_t> pts;                                    // time_specified when set
    std::vector<std::pair<std::uint8_t, std::optional<std::int64_t>>> components;   // component mode
    std::optional<std::pair<bool, std::int64_t>> duration;             // auto_return, ticks
    std::uint16_t program_id = 0;
    std::uint8_t  avail_num = 0, avails_expected = 0;
};

// The bytes of splice_insert `c`, the command.
inline Bytes insert_command(const Insert& c)
{
    Writer w;
    w.put(c.event_id, 32).put(c.cancel, 1).put(0x7F, 7);
    if (!c.cancel) {
        w.put(c.out, 1).put(c.program, 1).put(c.duration.has_value(), 1).put(c.immediate, 1)
            .put(c.compliance, 1).put(0x7, 3);
        if (c.program && !c.immediate) put_time(w, c.pts);
        if (!c.program) {
            w.put(c.components.size(), 8);
            for (const auto& [tag, pts] : c.components) {
                w.put(tag, 8);
                if (!c.immediate) put_time(w, pts);
            }
        }
        if (c.duration)
            w.put(c.duration->first, 1).put(0x3F, 6)
                .put(static_cast<std::uint64_t>(c.duration->second), 33);
        w.put(c.program_id, 16).put(c.avail_num, 8).put(c.avails_expected, 8);
    }
    return w.done();
}

// A section of splice_insert `c`, `adjustment` its pts_adjustment.
inline Section splice_insert(const Insert& c, std::int64_t adjustment = 0)
{
    Section s;
    s.pts_adjustment = adjustment;
    s.command_type   = scte35::kSpliceInsert;
    s.command        = insert_command(c);
    return s;
}

// The bytes of a time_signal at `pts`, or immediate, the command.
inline Bytes signal_command(std::optional<std::int64_t> pts)
{
    Writer w;
    put_time(w, pts);
    return w.done();
}

// A segmentation_descriptor()'s fields after its identifier (Table 20).
struct Segmentation {
    std::uint32_t event_id = 1;
    bool          cancel = false;
    bool          compliance = true;
    bool          program = true;
    std::optional<std::int64_t> duration;                  // 40 bits
    bool          restricted = false;                      // delivery_not_restricted_flag 0
    bool          web = false, no_blackout = false, archive = false;
    std::uint8_t  device = 0;
    std::vector<std::pair<std::uint8_t, std::int64_t>> components;   // tag, pts_offset
    std::uint8_t  upid_type = 0;
    Bytes         upid;
    std::uint8_t  type = 0x22;
    std::uint8_t  num = 0, expected = 0;
    std::optional<std::pair<std::uint8_t, std::uint8_t>> sub;        // sub_segment_num, _expected
    Bytes         extra;                                   // bytes after the fields
};

inline Bytes segmentation_body(const Segmentation& d)
{
    Writer w;
    w.put(d.event_id, 32).put(d.cancel, 1).put(d.compliance, 1).put(0x3F, 6);
    if (!d.cancel) {
        w.put(d.program, 1).put(d.duration.has_value(), 1).put(!d.restricted, 1);
        if (d.restricted) w.put(d.web, 1).put(d.no_blackout, 1).put(d.archive, 1).put(d.device, 2);
        else w.put(0x1F, 5);
        if (!d.program) {
            w.put(d.components.size(), 8);
            for (const auto& [tag, offset] : d.components)
                w.put(tag, 8).put(0x7F, 7).put(static_cast<std::uint64_t>(offset), 33);
        }
        if (d.duration) w.put(static_cast<std::uint64_t>(*d.duration), 40);
        w.put(d.upid_type, 8).put(d.upid.size(), 8).bytes(d.upid);
        w.put(d.type, 8).put(d.num, 8).put(d.expected, 8);
        if (d.sub) w.put(d.sub->first, 8).put(d.sub->second, 8);
    }
    w.bytes(d.extra);
    return w.done();
}

// A splice_descriptor(): tag, length, identifier, body.
inline Bytes descriptor(std::uint8_t tag, std::uint32_t identifier, const Bytes& body)
{
    Writer w;
    w.put(tag, 8).put(4 + body.size(), 8).put(identifier, 32).bytes(body);
    return w.done();
}

inline Bytes segmentation(const Segmentation& d)
{
    return descriptor(scte35::kSegmentationDescriptor, scte35::kCuei, segmentation_body(d));
}

inline Bytes concat(std::initializer_list<Bytes> parts)
{
    Bytes out;
    for (const Bytes& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

// A section of a time_signal at `pts`, or immediate, with a segmentation
// descriptor for each of `descriptors`, `adjustment` its pts_adjustment.
inline Section time_signal(std::optional<std::int64_t> pts, const std::vector<Segmentation>& descriptors,
                           std::int64_t adjustment = 0)
{
    Section s;
    s.pts_adjustment = adjustment;
    s.command_type   = scte35::kTimeSignal;
    s.command        = signal_command(pts);
    for (const Segmentation& d : descriptors) {
        const Bytes b = segmentation(d);
        s.descriptors.insert(s.descriptors.end(), b.begin(), b.end());
    }
    return s;
}

// ── Text ─────────────────────────────────────────────────────────────────────

// RFC 4648 §4 base64, with its padding, the tests' own.
inline std::string base64(const Bytes& b)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < b.size(); i += 3) {
        const std::uint32_t n = std::uint32_t{ b[i] } << 16
                              | (i + 1 < b.size() ? std::uint32_t{ b[i + 1] } << 8 : 0u)
                              | (i + 2 < b.size() ? std::uint32_t{ b[i + 2] } : 0u);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += i + 1 < b.size() ? kAlphabet[(n >> 6) & 63] : '=';
        out += i + 2 < b.size() ? kAlphabet[n & 63] : '=';
    }
    return out;
}

// Hex digits after `prefix`, two a byte.
inline std::string hex(const Bytes& b, std::string_view prefix = "0x", bool upper = true)
{
    std::string out(prefix);
    for (std::uint8_t x : b) out += upper ? std::format("{:02X}", x) : std::format("{:02x}", x);
    return out;
}

// An #EXT-OATCLS-SCTE35 line of section `s`, in base64, as the tag carries it.
inline std::string oatcls(const Section& s) { return "#EXT-OATCLS-SCTE35:" + base64(section(s)) + "\n"; }

// ── testgen's ad ladder, as tests/test_timeline_build.cpp builds it ──────────

// Three rungs of eight 25-frame segments at 25 fps, the reference v360 and
// the pairs v270 and v180, the ad in segments 3 and 4 restarting its
// timestamps at the main content's origin.
inline constexpr AVRational   kTs      = { 1, 90000 };
inline constexpr std::int64_t kOrigin  = 900000;   // 10 s: where the main content starts
inline constexpr std::int64_t kFrame25 = 3600;     // a frame at 25 fps, in 1/90000
inline constexpr std::int64_t kFrames  = 200;
inline constexpr const char*  kRungs[] = { "v360", "v270", "v180" };

inline std::string url_of(int variant)
{
    return std::format("http://test.invalid/ladder/{}/index.m3u8", kRungs[variant]);
}

// The reference's PTS: the ad's 50 frames from 10 s again, as the main
// content's first 50.
inline std::int64_t ref_pts(std::int64_t i)
{
    return i < 75 || i >= 125 ? kOrigin + i * kFrame25 : kOrigin + (i - 75) * kFrame25;
}

// testgen's ad playlist (lines 1 to 6 its header, then `header`), with
// `before[k]` written before segment k's plain cue tag and `after[k]` after
// it, ahead of its EXTINF, and `tail` after the last URI; its plain cue tags
// (EXT-X-CUE-OUT:2.000, EXT-X-CUE-OUT-CONT and EXT-X-CUE-IN before segments
// 3, 4 and 5) only with `plain`. Its EXT-X-DISCONTINUITY tags before
// segments 3 and 5 make the discontinuity sequences 0 (segments 0 to 2), 1
// (the ad) and 2.
inline std::string ad_playlist(const std::map<int, std::string>& before,
                               const std::map<int, std::string>& after = {}, bool plain = true,
                               const std::string& header = "", const std::string& tail = "")
{
    static constexpr const char* kUris[] = { "main_000.ts", "main_001.ts", "main_002.ts", "ad_000.ts",
                                             "ad_001.ts",   "main_005.ts", "main_006.ts", "main_007.ts" };
    std::string s = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:0\n"
                    "#EXT-X-DISCONTINUITY-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n" + header;
    for (int k = 0; k < 8; ++k) {
        if (k == 3 || k == 5) s += "#EXT-X-DISCONTINUITY\n";
        if (const auto it = before.find(k); it != before.end()) s += it->second;
        if (plain && k == 3) s += "#EXT-X-CUE-OUT:2.000\n";
        if (plain && k == 4) s += "#EXT-X-CUE-OUT-CONT:1.000/2.000\n";
        if (plain && k == 5) s += "#EXT-X-CUE-IN\n";
        if (const auto it = after.find(k); it != after.end()) s += it->second;
        s += std::format("#EXTINF:1.000000,\n{}\n", kUris[k]);
    }
    return s + tail + "#EXT-X-ENDLIST\n";
}

// The line of `text` holding `part`, from 1.
inline std::int64_t line_with(const std::string& text, std::string_view part)
{
    const std::size_t at = text.find(part);
    REQUIRE(at != std::string::npos);
    return 1 + std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(at), '\n');
}

inline timeline::LadderScores ad_report()
{
    timeline::LadderScores s;
    s.status         = "complete";
    s.reference_mode = "top-rung";
    s.model          = "vmaf_v0.6.1";
    s.model_source   = "built-in";
    s.master_url     = "http://test.invalid/ladder/master.m3u8";
    s.master_sha256  = filehash::sha256_hex("the master's bytes");
    const std::int64_t bandwidth[] = { 1000000, 600000, 350000 };
    for (int k = 0; k < 3; ++k) {
        timeline::MasterEntry e;
        e.index     = static_cast<std::size_t>(k);
        e.line      = static_cast<std::size_t>(3 + 2 * k);
        e.uri       = std::format("{}/index.m3u8", kRungs[k]);
        e.url       = url_of(k);
        e.bandwidth = bandwidth[k];
        e.video     = true;
        s.entries.push_back(e);
    }
    s.reference_url     = url_of(0);
    s.reference_entries = { 0 };
    for (int pair = 0; pair < 2; ++pair) {
        timeline::PairScores p;
        p.index         = static_cast<std::size_t>(pair);
        p.entries       = { static_cast<std::size_t>(pair + 1) };
        p.ref_url       = url_of(0);
        p.dis_url       = url_of(pair + 1);
        p.ref_time_base = kTs;
        p.dis_time_base = kTs;
        p.ref_rate      = { 25, 1 };
        p.dis_rate      = { 25, 1 };
        for (std::int64_t i = 0; i < kFrames; ++i) {
            p.ref_index.push_back(i);
            p.dis_index.push_back(i);
            p.ref_pts.push_back(ref_pts(i));
            p.dis_pts.push_back(ref_pts(i));
            p.vmaf.push_back(90.0);
        }
        p.context_first = { 0, 75 };
        p.ref_sequences = { { .first_index = 0, .first_pts = kOrigin, .frames = 75 },
                            { .first_index = 75, .first_pts = kOrigin, .frames = 125 } };
        p.dis_sequences = p.ref_sequences;
        s.pairs.push_back(std::move(p));
    }
    return s;
}

// The demux pass of an input of the ad ladder's shape: 25-frame segments,
// a keyframe at every frame, so that the build places each one, and `pts`.
inline timeline::VariantMedia ad_media(const std::function<std::int64_t(std::int64_t)>& pts = ref_pts)
{
    timeline::VariantMedia m;
    for (std::int64_t k = 0; k < 8; ++k)
        m.segments.push_back({ .seq = k, .first_index = 25 * k, .frames = 25 });
    for (std::int64_t i = 0; i < kFrames; ++i) {
        m.keyframes.push_back(i);
        m.pts.push_back(pts(i));
    }
    return m;
}

// The ladder's inputs, its build, and the plain cues as the build holds
// them, for scte35_events(): timeline::build() gives the axis and the plain
// cues, and its segment and keyframe events are what the messages'
// placement is checked against.
struct Ladder {
    timeline::BuildInputs           in;
    timeline::Timeline              t;
    std::vector<timeline::Event>    plain;   // the build's placed cue events, as it pairs them
    std::vector<std::string>        playlists;

    // Each variant's playlist; `adjust` changes the inputs before the build.
    explicit Ladder(std::vector<std::string> texts,
                    const std::function<void(timeline::BuildInputs&)>& adjust = {})
        : playlists(std::move(texts))
    {
        in.scores = ad_report();
        for (int v = 0; v < 3; ++v) {
            timeline::VariantRead r;
            const std::string& text = playlists[static_cast<std::size_t>(v)];
            r.playlist = timeline::parse_playlist_events(text, url_of(v), v);
            REQUIRE(r.playlist->error.empty());
            r.playlist_location = url_of(v);
            r.playlist_sha256   = filehash::sha256_hex(text);
            r.media             = ad_media();
            in.variants.push_back(std::move(r));
        }
        if (adjust) adjust(in);
        std::string err;
        t = timeline::build(in, err);
        INFO(err);
        REQUIRE(err.empty());
        for (const timeline::Event& e : t.events)
            if (e.type == timeline::kTypeCueOut || e.type == timeline::kTypeCueOutCont
                || e.type == timeline::kTypeCueIn)
                plain.push_back(e);
    }
    Ladder(const Ladder&) = delete;

    timeline::MessagePlacement placement() const
    {
        timeline::MessagePlacement p{ .scores = &in.scores, .axis = &t.axis, .variants = {} };
        for (const timeline::VariantRead& r : in.variants)
            p.variants.push_back({ .playlist = r.playlist ? &*r.playlist : nullptr, .media = &r.media });
        return p;
    }
    // Every playlist's messages, then `extra`.
    timeline::MessageEvents run(const timeline::MessageOptions& options = {},
                                const std::vector<timeline::CueInput>& extra = {})
    {
        const timeline::MessagePlacement p = placement();
        std::vector<timeline::CueInput> messages = timeline::playlist_messages(p.variants);
        messages.insert(messages.end(), extra.begin(), extra.end());
        return timeline::scte35_events(messages, plain, p, options);
    }
    // The ad breaks of every cue, plain and decoded, as the build pairs them;
    // each duration the axis is asked for goes to `asked`.
    timeline::Breaks pair(const timeline::MessageEvents& m, std::vector<AVRational>* asked = nullptr) const
    {
        const auto frame_after = [this, asked](std::int64_t f, AVRational d) {
            if (asked) asked->push_back(d);
            return t.axis.frame_after(f, d);
        };
        return timeline::pair_breaks(timeline::cues_to_pair(m, plain), frame_after);
    }
};

} // namespace scte35test
