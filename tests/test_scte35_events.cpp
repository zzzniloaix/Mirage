// timeline::scte35_events() (src/Scte35Events.h): decoded SCTE-35 messages as
// events, classed by D-106 (b), kept or dropped by D-106 (c), placed by their
// anchor's kind as D-106 (a) and design §5.1 say, merged into their plain
// cues by D-106 (d), and paired with them by WP04's pair_breaks(); a
// malformed EXT-X-DATERANGE's message decoded with a warning (D-106 (e)).
// The inputs are built by hand in the shape of testgen's ad ladder
// (eval/phase3-survey/testgen.md §2.3), as tests/test_timeline_build.cpp
// builds them: three rungs of eight 25-frame segments at 25 fps, the
// reference v360 and the pairs v270 and v180, the ad in segments 3 and 4
// restarting its timestamps at the main content's origin. timeline::build()
// gives the axis and the plain cues as the build holds them, and its segment
// and keyframe events are what this module's placement is checked against.
// The messages are sections a writer here builds from field values, with
// their CRC_32 (the design's P-M); none is a published one (D-107).

#include <catch2/catch_approx.hpp>
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
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using timeline::CueInput;
using timeline::Event;
using timeline::MessageEvents;
using timeline::MessageOptions;
using timeline::Payload;
using timeline::Value;
using Bytes = std::vector<std::uint8_t>;

namespace {

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// ── Sections, from field values (P-M) ────────────────────────────────────────

class Writer {
public:
    Writer& put(std::uint64_t v, int n)
    {
        for (int i = n - 1; i >= 0; --i) {
            if (bits_ % 8 == 0) out_.push_back(0);
            if ((v >> i) & 1u)
                out_.back() = static_cast<std::uint8_t>(out_.back() | (0x80u >> (bits_ % 8)));
            ++bits_;
        }
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
};

// CRC-32/MPEG-2 bit by bit (scte35.md §3.6), the test's own.
std::uint32_t crc_bitwise(const Bytes& b)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::uint8_t x : b) {
        crc ^= std::uint32_t{ x } << 24;
        for (int k = 0; k < 8; ++k) crc = crc & 0x80000000u ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    }
    return crc;
}

void put_time(Writer& w, std::optional<std::int64_t> pts)
{
    if (pts) w.put(1, 1).put(0x3F, 6).put(static_cast<std::uint64_t>(*pts), 33);
    else w.put(0, 1).put(0x7F, 7);
}

struct Section {
    std::int64_t pts_adjustment = 0;
    std::uint8_t command_type = scte35::kSpliceNull;
    Bytes        command;
    Bytes        descriptors;
    bool         encrypted = false;
    bool         bad_crc = false;
};

Bytes section(const Section& s)
{
    Writer body;
    body.put(0, 8)   // protocol_version
        .put(s.encrypted, 1)
        .put(0, 6)
        .put(static_cast<std::uint64_t>(s.pts_adjustment), 33)
        .put(0, 8)
        .put(0xFFF, 12)
        .put(s.command.size(), 12)
        .put(s.command_type, 8)
        .bytes(s.command)
        .put(s.descriptors.size(), 16)
        .bytes(s.descriptors);
    const Bytes b = body.done();
    Writer head;
    head.put(scte35::kTableId, 8).put(0, 1).put(0, 1).put(3, 2).put(b.size() + 4, 12);
    Bytes out = head.done();
    out.insert(out.end(), b.begin(), b.end());
    std::uint32_t crc = crc_bitwise(out);
    if (s.bad_crc) crc ^= 1u;
    for (int k = 3; k >= 0; --k) out.push_back(static_cast<std::uint8_t>(crc >> (8 * k)));
    return out;
}

struct Insert {
    std::uint32_t event_id = 1;
    bool          cancel = false;
    bool          out = true;
    bool          immediate = false;
    std::optional<std::int64_t> pts;
    std::optional<std::pair<bool, std::int64_t>> duration;   // auto_return, 90 kHz ticks
};

Section splice_insert(const Insert& c, std::int64_t adjustment = 0)
{
    Writer w;
    w.put(c.event_id, 32).put(c.cancel, 1).put(0x7F, 7);
    if (!c.cancel) {
        w.put(c.out, 1).put(1, 1).put(c.duration.has_value(), 1).put(c.immediate, 1).put(1, 1).put(0x7, 3);
        if (!c.immediate) put_time(w, c.pts);
        if (c.duration)
            w.put(c.duration->first, 1).put(0x3F, 6)
                .put(static_cast<std::uint64_t>(c.duration->second), 33);
        w.put(0, 16).put(0, 8).put(0, 8);
    }
    return { .pts_adjustment = adjustment, .command_type = scte35::kSpliceInsert, .command = w.done() };
}

// A segmentation_descriptor() of program mode with no UPID.
struct Seg {
    std::uint8_t  type = 0x22;
    std::uint32_t event_id = 1;
    bool          cancel = false;
    std::optional<std::int64_t> duration;
};

Bytes segmentation(const Seg& d)
{
    Writer w;
    w.put(scte35::kSegmentationDescriptor, 8);
    Writer b;
    b.put(scte35::kCuei, 32).put(d.event_id, 32).put(d.cancel, 1).put(1, 1).put(0x3F, 6);
    if (!d.cancel) {
        b.put(1, 1).put(d.duration.has_value(), 1).put(1, 1).put(0x1F, 5);
        if (d.duration) b.put(static_cast<std::uint64_t>(*d.duration), 40);
        b.put(0, 8).put(0, 8).put(d.type, 8).put(0, 8).put(0, 8);
    }
    const Bytes body = b.done();
    w.put(body.size(), 8).bytes(body);
    return w.done();
}

Section time_signal(std::optional<std::int64_t> pts, const std::vector<Seg>& descriptors,
                    std::int64_t adjustment = 0)
{
    Writer w;
    put_time(w, pts);
    Bytes loop;
    for (const Seg& d : descriptors) {
        const Bytes b = segmentation(d);
        loop.insert(loop.end(), b.begin(), b.end());
    }
    return { .pts_adjustment = adjustment, .command_type = scte35::kTimeSignal, .command = w.done(),
             .descriptors = loop };
}

std::string base64(const Bytes& b)
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

std::string hex(const Bytes& b)
{
    std::string out = "0x";
    for (std::uint8_t x : b) out += std::format("{:02X}", x);
    return out;
}

std::string oatcls(const Section& s) { return "#EXT-OATCLS-SCTE35:" + base64(section(s)) + "\n"; }

// ── testgen's ad ladder, as tests/test_timeline_build.cpp builds it ──────────

constexpr AVRational   kTs      = { 1, 90000 };
constexpr std::int64_t kOrigin  = 900000;   // 10 s: where the main content starts
constexpr std::int64_t kFrame25 = 3600;     // a frame at 25 fps, in 1/90000
constexpr std::int64_t kFrames  = 200;
constexpr const char*  kRungs[] = { "v360", "v270", "v180" };

std::string url_of(int variant)
{
    return std::format("http://test.invalid/ladder/{}/index.m3u8", kRungs[variant]);
}

// The reference's PTS: the ad's 50 frames from 10 s again, as the main
// content's first 50.
std::int64_t ref_pts(std::int64_t i)
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
std::string ad_playlist(const std::map<int, std::string>& before,
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
std::int64_t line_with(const std::string& text, std::string_view part)
{
    const std::size_t at = text.find(part);
    REQUIRE(at != std::string::npos);
    return 1 + std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(at), '\n');
}

timeline::LadderScores ad_report()
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
timeline::VariantMedia ad_media(const std::function<std::int64_t(std::int64_t)>& pts = ref_pts)
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
// them, for scte35_events().
struct Ladder {
    timeline::BuildInputs in;
    timeline::Timeline    t;
    std::vector<Event>    plain;   // the build's placed cue events, as it pairs them
    std::vector<std::string> playlists;

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
        for (const Event& e : t.events)
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
    MessageEvents run(const MessageOptions& options = {}, const std::vector<CueInput>& extra = {})
    {
        const timeline::MessagePlacement p = placement();
        std::vector<CueInput> messages = timeline::playlist_messages(p.variants);
        messages.insert(messages.end(), extra.begin(), extra.end());
        return timeline::scte35_events(messages, plain, p, options);
    }
    // The ad breaks of every cue, plain and decoded, as the build pairs them.
    timeline::Breaks pair(const MessageEvents& m) const
    {
        const auto frame_after = [this](std::int64_t f, AVRational d) { return t.axis.frame_after(f, d); };
        return timeline::pair_breaks(timeline::cues_to_pair(m, plain), frame_after);
    }
};

// ── Payloads ─────────────────────────────────────────────────────────────────

const Value* member(const Payload& p, std::string_view name)
{
    for (const auto& [n, v] : p)
        if (n == name) return &v;
    return nullptr;
}

const Value& at(const Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    if (!v) FAIL("no payload member " << name);
    return *v;
}

const Value::Object& object(const Value& v)
{
    REQUIRE(std::holds_alternative<Value::Object>(v.v));
    return std::get<Value::Object>(v.v);
}

const Value::List& list(const Value& v)
{
    REQUIRE(std::holds_alternative<Value::List>(v.v));
    return std::get<Value::List>(v.v);
}

bool is_null(const Value& v) { return std::holds_alternative<std::monostate>(v.v); }

std::int64_t integer(const Value& v)
{
    REQUIRE(std::holds_alternative<std::int64_t>(v.v));
    return std::get<std::int64_t>(v.v);
}

double number(const Value& v)
{
    REQUIRE(std::holds_alternative<double>(v.v));
    return std::get<double>(v.v);
}

std::string text(const Value& v)
{
    REQUIRE(std::holds_alternative<std::string>(v.v));
    return std::get<std::string>(v.v);
}

bool flag(const Value& v)
{
    REQUIRE(std::holds_alternative<bool>(v.v));
    return std::get<bool>(v.v);
}

std::vector<std::string> names(const Payload& p)
{
    std::vector<std::string> out;
    for (const auto& m : p) out.push_back(m.first);
    return out;
}

std::vector<std::string> texts(const Value& v)
{
    std::vector<std::string> out;
    for (const Value& x : list(v)) out.push_back(text(x));
    return out;
}

// An object member `name` of an object value.
const Value& field(const Value& o, std::string_view name) { return at(object(o), name); }

// The anchor of kind `kind` of an event's or record's anchors.
const Value& anchor(const Payload& p, std::string_view kind, std::size_t nth = 0)
{
    for (const Value& a : list(at(p, "anchors")))
        if (text(field(a, "anchor")) == kind && nth-- == 0) return a;
    FAIL("no anchor " << kind);
    static const Value none;
    return none;
}

std::vector<const Event*> of_type(const std::vector<Event>& events, std::string_view type, int variant = -2)
{
    std::vector<const Event*> out;
    for (const Event& e : events)
        if (e.type == type && (variant == -2 || e.variant == variant)) out.push_back(&e);
    return out;
}

std::string warnings_text(const MessageEvents& m)
{
    std::string out;
    for (const std::string& w : m.warnings) out += w + "\n";
    return out;
}

bool any_warning(const MessageEvents& m, std::string_view part)
{
    return std::any_of(m.warnings.begin(), m.warnings.end(),
                       [part](const std::string& w) { return contains(w, part); });
}

std::string name(int v) { return std::format("variant {} ({})", v, url_of(v)); }

} // namespace

TEST_CASE("scte35_events: decoded messages are classed by D-106 (b), and the cue types can be set",
          "[timeline][scte35][scte35_events]")
{
    // v270's playlist, without plain cue tags: one OATCLS per segment.
    const std::map<int, std::string> messages = {
        { 0, oatcls(splice_insert({ .event_id = 11, .pts = kOrigin })) },
        { 1, oatcls(splice_insert({ .event_id = 11, .out = false, .immediate = true })) },
        { 2, oatcls(splice_insert({ .event_id = 12, .cancel = true })) },
        { 3, oatcls(time_signal(std::nullopt, { { .type = 0x22, .duration = 180000 } })) },
        // An advertisement's end and the next one's start at one splice point.
        { 4, oatcls(time_signal(std::nullopt, { { .type = 0x31 }, { .type = 0x30 } })) },
        { 5, oatcls(time_signal(std::nullopt, { { .type = 0x10 } })) },
        { 6, oatcls(time_signal(std::nullopt, { { .event_id = 77, .cancel = true } })) },
        { 7, oatcls(Section{}) },   // splice_null
    };
    const std::string v270 = ad_playlist(messages, {}, false);
    Ladder ladder({ ad_playlist({}), v270, ad_playlist({}) });
    const MessageEvents m = ladder.run();
    INFO(warnings_text(m));
    CHECK(m.warnings.empty());

    // Each at the start of its tag's segment (D-106 (a)): segment k of v270
    // starts at reference frame 25k.
    const auto at_segment = [](const Event* e, std::int64_t k) {
        return e->variant == 1 && e->segment == k && e->ref_index == 25 * k && !e->ref_end
            && e->source == timeline::Source::Scte35;
    };
    REQUIRE(m.cues.size() == 5);
    CHECK((m.cues[0].type == "cue_out" && at_segment(&m.cues[0], 0)));
    CHECK((m.cues[1].type == "cue_in" && at_segment(&m.cues[1], 1)));
    CHECK((m.cues[2].type == "cue_out" && at_segment(&m.cues[2], 3)));
    // The end first, then the start, at one frame and one line.
    CHECK((m.cues[3].type == "cue_in" && at_segment(&m.cues[3], 4)));
    CHECK((m.cues[4].type == "cue_out" && at_segment(&m.cues[4], 4)));
    CHECK(texts(at(m.cues[3].payload, "classes")) == std::vector<std::string>{ "cue_in", "cue_out" });
    CHECK(integer(at(m.cues[3].payload, "line")) == integer(at(m.cues[4].payload, "line")));
    CHECK(m.cues[0].key.pts == ref_pts(0));
    CHECK(m.cues[2].key.sequence == 1);

    // The payload of a cue of its own, in its order.
    const Payload& out = m.cues[2].payload;
    CHECK(names(out) == std::vector<std::string>{ "line", "form", "raw", "duration", "end_date_duration",
                                                  "planned_duration", "break_duration",
                                                  "segmentation_duration", "classes", "crc_valid",
                                                  "message", "anchors" });
    const Section brk = time_signal(std::nullopt, { { .type = 0x22, .duration = 180000 } });
    CHECK(integer(at(out, "line")) == line_with(v270, "#EXT-OATCLS-SCTE35:" + base64(section(brk))));
    CHECK(text(at(out, "form")) == "OATCLS");
    CHECK(text(at(out, "raw")).starts_with("#EXT-OATCLS-SCTE35:/DA"));
    CHECK(is_null(at(out, "duration")));
    CHECK(is_null(at(out, "break_duration")));
    CHECK(number(at(out, "segmentation_duration")) == 2.0);
    CHECK(flag(at(out, "crc_valid")));
    const Value& msg = at(out, "message");
    CHECK(text(field(msg, "command")) == "time_signal");
    const Value& seg = list(field(msg, "descriptors"))[0];
    CHECK(text(field(seg, "name")) == "segmentation_descriptor");
    CHECK(integer(field(seg, "segmentation_type_id")) == 0x22);
    CHECK(text(field(seg, "segmentation_type_name")) == "Break Start");
    CHECK(integer(field(seg, "segmentation_duration")) == 180000);
    const Value& pos = anchor(out, "position");
    CHECK(integer(field(pos, "segment")) == 3);
    CHECK(integer(field(pos, "ref_index")) == 75);
    CHECK(list(at(out, "anchors")).size() == 1);   // an immediate time_signal: no splice time

    // A splice_insert's fields.
    const Value& insert = field(at(m.cues[0].payload, "message"), "splice_insert");
    CHECK(integer(field(insert, "splice_event_id")) == 11);
    CHECK(flag(field(insert, "out_of_network_indicator")));

    // A cancellation, of a splice_insert and of a segmentation descriptor.
    REQUIRE(m.others.size() == 4);
    const Event& cancel = m.others[0];
    CHECK((cancel.type == "cancel" && at_segment(&cancel, 2)));
    CHECK(names(cancel.payload) == std::vector<std::string>{ "line", "form", "raw", "command",
                                                             "splice_event_id", "segmentation_event_ids",
                                                             "classes", "crc_valid", "message",
                                                             "anchors" });
    CHECK(integer(at(cancel.payload, "splice_event_id")) == 12);
    CHECK(list(at(cancel.payload, "segmentation_event_ids")).empty());
    // Every other message is a message event, with its segmentation types.
    const Event& program = m.others[1];
    CHECK((program.type == "message" && at_segment(&program, 5)));
    CHECK(names(program.payload) == std::vector<std::string>{ "line", "form", "raw", "command",
                                                              "segmentation_types", "classes",
                                                              "crc_valid", "message", "anchors" });
    CHECK(text(at(program.payload, "command")) == "time_signal");
    REQUIRE(list(at(program.payload, "segmentation_types")).size() == 1);
    CHECK(integer(list(at(program.payload, "segmentation_types"))[0]) == 0x10);
    const Event& cancelled = m.others[2];
    CHECK((cancelled.type == "cancel" && at_segment(&cancelled, 6)));
    CHECK(is_null(at(cancelled.payload, "splice_event_id")));
    CHECK(integer(list(at(cancelled.payload, "segmentation_event_ids"))[0]) == 77);
    const Event& null = m.others[3];
    CHECK((null.type == "message" && at_segment(&null, 7)));
    CHECK(text(at(null.payload, "command")) == "splice_null");
    CHECK(list(at(null.payload, "segmentation_types")).empty());

    // A splice time is its own splice_point (segment 0's), the cues none.
    REQUIRE(m.splice_points.size() == 1);
    CHECK(m.splice_points[0].ref_index == 0);

    // The cue types set to the Program pair: Break Start becomes a message,
    // Program Start a cue_out; the advertisements a message.
    MessageOptions program_types;
    program_types.cue_types = { 0x10, 0x11 };
    CHECK(timeline::check_message_options(program_types).empty());
    Ladder again({ ad_playlist({}), v270, ad_playlist({}) });
    const MessageEvents p = again.run(program_types);
    std::vector<std::pair<std::string, std::int64_t>> classes;
    for (const Event& e : p.cues) classes.emplace_back(e.type, *e.segment);
    for (const Event& e : p.others) classes.emplace_back(e.type, *e.segment);
    std::sort(classes.begin(), classes.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    CHECK(classes == std::vector<std::pair<std::string, std::int64_t>>{
                         { "cue_out", 0 }, { "cue_in", 1 }, { "cancel", 2 }, { "message", 3 },
                         { "message", 4 }, { "cue_out", 5 }, { "cancel", 6 }, { "message", 7 } });

    // Table 23's roles, by the names of its types; a type that is neither
    // can't be a cue type.
    CHECK(timeline::segmentation_role(0x22) == timeline::SegmentationRole::Start);
    CHECK(timeline::segmentation_role(0x23) == timeline::SegmentationRole::End);
    CHECK(timeline::segmentation_role(0x17) == timeline::SegmentationRole::Start);
    CHECK(timeline::segmentation_role(0x24) == timeline::SegmentationRole::Start);
    CHECK(timeline::segmentation_role(0x27) == timeline::SegmentationRole::End);
    CHECK_FALSE(timeline::segmentation_role(0x12));
    CHECK_FALSE(timeline::segmentation_role(0x00));
    CHECK_FALSE(timeline::segmentation_role(0x99));
    for (std::uint8_t t : timeline::kDefaultCueTypes) CHECK(timeline::segmentation_role(t));
    MessageOptions bad;
    bad.cue_types = { 0x22, 0x13 };
    CHECK(contains(timeline::check_message_options(bad),
                   "0x13 (Program Breakaway) is neither a Start nor an End"));
    CHECK(timeline::check_message_options(MessageOptions{}).empty());
}

TEST_CASE("scte35_events: a message whose CRC_32 fails is kept and marked, or dropped (D-106 (c))",
          "[timeline][scte35][scte35_events]")
{
    Section bad = splice_insert({ .event_id = 5, .pts = kOrigin + 3 * kFrame25 });
    bad.bad_crc = true;
    Ladder ladder({ ad_playlist({}), ad_playlist({ { 3, oatcls(bad) } }, {}, false), ad_playlist({}) });

    const MessageEvents kept = ladder.run();
    REQUIRE(kept.cues.size() == 1);
    CHECK(kept.cues[0].type == "cue_out");
    CHECK_FALSE(flag(at(kept.cues[0].payload, "crc_valid")));
    REQUIRE(kept.splice_points.size() == 1);
    CHECK_FALSE(flag(at(kept.splice_points[0].payload, "crc_valid")));
    const Bytes         bytes     = section(bad);
    const std::uint32_t field_crc = crc_bitwise(Bytes(bytes.begin(), bytes.end() - 4)) ^ 1u;
    CHECK(kept.warnings == std::vector<std::string>{ std::format(
                               "{}: the OATCLS at line {}: its CRC_32 is 0x{:08X} where its bytes give "
                               "0x{:08X}: it is kept, marked crc_valid false (D-106 (c))", name(1),
                               line_with(ladder.playlists[1], "#EXT-OATCLS"), field_crc, field_crc ^ 1u) });

    MessageOptions drop;
    drop.bad_crc = timeline::BadCrc::Drop;
    Ladder again({ ad_playlist({}), ad_playlist({ { 3, oatcls(bad) } }, {}, false), ad_playlist({}) });
    const MessageEvents dropped = again.run(drop);
    CHECK(dropped.cues.empty());
    CHECK(dropped.splice_points.empty());
    CHECK(dropped.others.empty());
    REQUIRE(dropped.warnings.size() == 1);
    CHECK(contains(dropped.warnings[0], "it is dropped, as the bad-CRC policy drop says (D-106 (c))"));
}

TEST_CASE("scte35_events: a message in an EXT-X-CUE-OUT's segment is that cue's (D-106 (d)), and a cue "
          "tag's own SCTE35= is its cue's, a CUE-OUT-CONT's included", "[timeline][scte35][scte35_events]")
{
    // MediaLive's order in v180: the OATCLS before the CUE-OUT. The
    // message is the break's out, with a 2 s break_duration; the
    // CUE-OUT-CONT carries it again in SCTE35=; an OATCLS in message sits
    // with the CUE-IN.
    const Section out = splice_insert({ .event_id = 9, .immediate = true,
                                        .duration = std::pair{ true, 180000 } });
    const Section in  = splice_insert({ .event_id = 9, .out = false, .immediate = true });
    const std::string cont_tag = "#EXT-X-CUE-OUT-CONT:ElapsedTime=1.000,Duration=2.000,SCTE35=";
    const std::string v180 = ad_playlist({ { 3, oatcls(out) + "#EXT-X-CUE-OUT:2.000\n" },
                                           { 4, cont_tag + base64(section(out)) + "\n" },
                                           { 5, "#EXT-X-CUE-IN\n" + oatcls(in) } },
                                         {}, false);
    Ladder ladder({ ad_playlist({}), ad_playlist({}), v180 });
    const MessageEvents m = ladder.run();
    INFO(warnings_text(m));
    CHECK(m.warnings.empty());

    // The OATCLS out makes no cue of its own: the CUE-OUT takes it, and
    // declares its break_duration, which it doesn't itself.
    REQUIRE(m.cues.size() == 1);
    CHECK(m.cues[0].type == "cue_in");
    CHECK(m.cues[0].segment == 5);
    const auto outs = of_type(ladder.plain, "cue_out", 2);
    REQUIRE(outs.size() == 1);
    const Payload& cue = outs[0]->payload;
    CHECK(names(cue) == std::vector<std::string>{ "line", "form", "raw", "duration", "attributes",
                                                  "messages", "break_duration" });
    CHECK(number(at(cue, "duration")) == 2.0);
    CHECK(number(at(cue, "break_duration")) == 2.0);
    const Value::List& records = list(at(cue, "messages"));
    REQUIRE(records.size() == 1);
    const Value& r = records[0];
    CHECK(names(object(r)) == std::vector<std::string>{ "line", "form", "raw", "classes", "crc_valid",
                                                        "message", "anchors" });
    CHECK(text(field(r, "form")) == "OATCLS");
    CHECK(integer(field(r, "line")) == line_with(v180, "#EXT-OATCLS-SCTE35:" + base64(section(out))));
    CHECK(texts(field(r, "classes")) == std::vector<std::string>{ "cue_out" });
    const Value& insert = field(field(r, "message"), "splice_insert");
    CHECK(integer(field(field(insert, "break_duration"), "duration")) == 180000);

    // The CUE-OUT-CONT's own message, decoded into it; no other cue.
    const auto conts = of_type(ladder.plain, "cue_out_cont", 2);
    REQUIRE(conts.size() == 1);
    const Value::List& cont = list(at(conts[0]->payload, "messages"));
    REQUIRE(cont.size() == 1);
    CHECK(text(field(cont[0], "form")) == "CUE-OUT-CONT SCTE35");
    CHECK(integer(field(cont[0], "line")) == integer(at(conts[0]->payload, "line")));
    CHECK(texts(field(cont[0], "classes")) == std::vector<std::string>{ "cue_out" });
    CHECK_FALSE(member(conts[0]->payload, "break_duration"));

    // The other variants' cues take nothing.
    for (const Event& e : ladder.plain)
        if (e.variant != 2) CHECK_FALSE(member(e.payload, "messages"));

    // Paired with the plain cues, one break over the ad, ended by a cue_in.
    const timeline::Breaks b = ladder.pair(m);
    INFO(b.warnings.size());
    const auto breaks = of_type(b.events, "ad_break", 2);
    REQUIRE(breaks.size() == 1);
    CHECK(breaks[0]->ref_index == 75);
    CHECK(breaks[0]->ref_end == 125);
    CHECK(text(at(breaks[0]->payload, "end")) == "cue_in");
    CHECK(number(field(at(breaks[0]->payload, "durations"), "break_duration")) == 2.0);

    // Wowza's order, the OATCLS after the CUE-OUT: the same one cue.
    Ladder wowza({ ad_playlist({}), ad_playlist({}), ad_playlist({}, { { 3, oatcls(out) } }) });
    const MessageEvents w = wowza.run();
    CHECK(w.cues.empty());
    REQUIRE(of_type(wowza.plain, "cue_out", 2).size() == 1);
    CHECK(list(at(of_type(wowza.plain, "cue_out", 2)[0]->payload, "messages")).size() == 1);

    // A bare CUE-OUT takes a DATERANGE's DURATION for its own null one, and
    // its PLANNED-DURATION; a message that ends one advertisement and starts
    // the next gives a CUE-OUT its start and makes a cue_in of its own.
    const std::string daterange = "#EXT-X-PROGRAM-DATE-TIME:2026-03-01T12:00:03.000Z\n"
                                  "#EXT-X-DATERANGE:ID=\"d\",START-DATE=\"2026-03-01T12:00:03.000Z\","
                                  "DURATION=2.0,PLANNED-DURATION=2.5,SCTE35-OUT="
                                  + hex(section(out)) + "\n";
    const Section ends = time_signal(std::nullopt, { { .type = 0x31 }, { .type = 0x30 } });
    Ladder bare({ ad_playlist({}),
                  ad_playlist({ { 3, daterange + "#EXT-X-CUE-OUT\n" },
                                { 6, oatcls(ends) + "#EXT-X-CUE-OUT\n" } },
                              {}, false),
                  ad_playlist({}) });
    const MessageEvents d = bare.run();
    INFO(warnings_text(d));
    CHECK(d.warnings.empty());
    const auto bare_outs = of_type(bare.plain, "cue_out", 1);
    REQUIRE(bare_outs.size() == 2);
    const Payload& filled = bare_outs[0]->payload;
    CHECK(number(at(filled, "duration")) == 2.0);
    CHECK(number(at(filled, "planned_duration")) == 2.5);
    CHECK(number(at(filled, "break_duration")) == 2.0);
    CHECK(names(filled) == std::vector<std::string>{ "line", "form", "raw", "duration", "attributes",
                                                     "messages", "planned_duration", "break_duration" });
    CHECK(text(field(list(at(filled, "messages"))[0], "form")) == "DATERANGE SCTE35-OUT");
    REQUIRE(d.cues.size() == 1);
    CHECK(d.cues[0].type == "cue_in");
    CHECK(d.cues[0].segment == 6);
    CHECK(texts(field(list(at(bare_outs[1]->payload, "messages"))[0], "classes"))
          == std::vector<std::string>{ "cue_in", "cue_out" });
}

TEST_CASE("scte35_events: a message that ends a break and starts the next ends the first before its "
          "EXT-X-CUE-OUT starts the next, in either order of their tags",
          "[timeline][scte35][scte35_events]")
{
    // v270: a break that an OATCLS out opens in segment 1; in segment 3 the
    // next break's EXT-X-CUE-OUT and a time_signal that ends one
    // advertisement and starts the next, the OATCLS after the CUE-OUT as
    // Wowza writes it or before it as MediaLive does (scte35.md §2.4); the
    // CUE-IN in segment 5.
    const Section     first   = splice_insert({ .event_id = 1, .immediate = true });
    const Section     ends    = time_signal(std::nullopt, { { .type = 0x31 }, { .type = 0x30 } });
    const std::string cue_out = "#EXT-X-CUE-OUT:2.000\n";
    for (const bool wowza : { true, false }) {
        INFO((wowza ? "Wowza's order" : "MediaLive's order"));
        const std::string v270 =
            ad_playlist({ { 1, oatcls(first) },
                          { 3, wowza ? cue_out + oatcls(ends) : oatcls(ends) + cue_out },
                          { 5, "#EXT-X-CUE-IN\n" } },
                        {}, false);
        Ladder ladder({ ad_playlist({}), v270, ad_playlist({}) });
        const MessageEvents m = ladder.run();
        INFO(warnings_text(m));
        CHECK(m.warnings.empty());

        // The CUE-OUT takes the message's cue_out part; its cue_in is its
        // own, at the CUE-OUT's line where its own comes after it, its own
        // line kept in its position anchor.
        const std::int64_t out_line = line_with(v270, cue_out);
        const std::int64_t own_line = line_with(v270, oatcls(ends));
        CHECK(own_line == (wowza ? out_line + 1 : out_line - 1));
        REQUIRE(m.cues.size() == 2);
        CHECK((m.cues[0].type == "cue_out" && m.cues[0].ref_index == 25));
        const Event& in = m.cues[1];
        CHECK(in.type == "cue_in");
        CHECK(in.segment == 3);
        CHECK(in.ref_index == 75);
        CHECK(integer(at(in.payload, "line")) == std::min(out_line, own_line));
        CHECK(integer(field(anchor(in.payload, "position"), "line")) == own_line);
        CHECK(text(at(in.payload, "raw")) == oatcls(ends).substr(0, oatcls(ends).size() - 1));
        const auto outs = of_type(ladder.plain, "cue_out", 1);
        REQUIRE(outs.size() == 1);
        CHECK(texts(field(list(at(outs[0]->payload, "messages"))[0], "classes"))
              == std::vector<std::string>{ "cue_in", "cue_out" });

        // The returned cues go first, so that the pairing takes the cue_in
        // before the CUE-OUT at its line: the first break ends where the
        // second starts, and the second ends at the CUE-IN.
        const std::vector<Event> cues = timeline::cues_to_pair(m, ladder.plain);
        REQUIRE(cues.size() == m.cues.size() + ladder.plain.size());
        CHECK(cues[1].type == "cue_in");
        const timeline::Breaks b = ladder.pair(m);
        CHECK(b.warnings.empty());
        const auto breaks = of_type(b.events, "ad_break", 1);
        REQUIRE(breaks.size() == 2);
        CHECK((breaks[0]->ref_index == 25 && breaks[0]->ref_end == 75));
        CHECK(text(at(breaks[0]->payload, "end")) == "cue_in");
        CHECK(integer(at(breaks[0]->payload, "cue_in_line")) == std::min(out_line, own_line));
        CHECK((breaks[1]->ref_index == 75 && breaks[1]->ref_end == 125));
        CHECK(integer(at(breaks[1]->payload, "cue_out_line")) == out_line);
        CHECK(text(at(breaks[1]->payload, "end")) == "cue_in");
    }
}

TEST_CASE("scte35_events: an OATCLS or SCTE35= value that starts with 0x is read as hex",
          "[timeline][scte35][scte35_events]")
{
    // v270: an OATCLS out in hex in segment 1, and in segment 6 one whose
    // hex has a character that isn't a hex digit; v180: a break whose
    // CUE-OUT-CONT carries its out in SCTE35=, in hex with 0X.
    const Section     out = splice_insert({ .event_id = 21, .immediate = true,
                                            .duration = std::pair{ true, 180000 } });
    const std::string bad = "0xFC30ZZ";
    const std::string v270 = ad_playlist({ { 1, "#EXT-OATCLS-SCTE35:" + hex(section(out)) + "\n" },
                                           { 6, "#EXT-OATCLS-SCTE35:" + bad + "\n" } },
                                         {}, false);
    const std::string upper = "0X" + hex(section(out)).substr(2);
    const std::string v180 =
        ad_playlist({ { 3, "#EXT-X-CUE-OUT:2.000\n" },
                      { 4, "#EXT-X-CUE-OUT-CONT:ElapsedTime=1.000,Duration=2.000,SCTE35=" + upper + "\n" },
                      { 5, "#EXT-X-CUE-IN\n" } },
                    {}, false);
    Ladder ladder({ ad_playlist({}), v270, v180 });
    const MessageEvents m = ladder.run();
    INFO(warnings_text(m));

    // The OATCLS in hex: a cue_out of its own, its break_duration decoded.
    REQUIRE(m.cues.size() == 1);
    const Event& cue = m.cues[0];
    CHECK((cue.type == "cue_out" && cue.variant == 1 && cue.ref_index == 25));
    CHECK(text(at(cue.payload, "form")) == "OATCLS");
    CHECK(text(at(cue.payload, "raw")).starts_with("#EXT-OATCLS-SCTE35:0xFC"));
    CHECK(number(at(cue.payload, "break_duration")) == 2.0);
    CHECK(integer(field(field(at(cue.payload, "message"), "splice_insert"), "splice_event_id")) == 21);

    // The CUE-OUT-CONT's SCTE35= in hex, decoded into it.
    const auto conts = of_type(ladder.plain, "cue_out_cont", 2);
    REQUIRE(conts.size() == 1);
    const Value::List& records = list(at(conts[0]->payload, "messages"));
    REQUIRE(records.size() == 1);
    CHECK(text(field(records[0], "form")) == "CUE-OUT-CONT SCTE35");
    CHECK(texts(field(records[0], "classes")) == std::vector<std::string>{ "cue_out" });
    CHECK(integer(field(field(field(field(records[0], "message"), "splice_insert"), "break_duration"),
                        "duration"))
          == 180000);

    // A value starting 0x is read as hex only: one that isn't is no message.
    CHECK(m.warnings
          == std::vector<std::string>{ std::format(
              "{}: the OATCLS at line {}: its text isn't hex, its character 7, 'Z', isn't a hex digit: it "
              "makes no event (design §5.5)", name(1), line_with(v270, bad)) });
    CHECK(m.others.empty());
}

TEST_CASE("scte35_events: cues from messages alone are paired by pair_breaks(), by their cue_in or their "
          "declared duration", "[timeline][scte35][scte35_events]")
{
    const Section out     = splice_insert({ .event_id = 1, .immediate = true });
    const Section in      = splice_insert({ .event_id = 1, .out = false, .immediate = true });
    const Section timed   = splice_insert({ .event_id = 2, .immediate = true,
                                            .duration = std::pair{ true, 180000 } });
    // v270: an OATCLS out in segment 3 and in in segment 5. v180: an out
    // with a 2 s break_duration and no in; the reference: a DATERANGE out
    // whose PLANNED-DURATION is 1 s, at the date of its segment, which
    // follows a discontinuity and so has a PDT of its own.
    const std::string daterange = "#EXT-X-PROGRAM-DATE-TIME:2026-03-01T12:00:03.000Z\n"
                                  "#EXT-X-DATERANGE:ID=\"b\",START-DATE=\"2026-03-01T12:00:03.000Z\","
                                  "PLANNED-DURATION=1.0,SCTE35-OUT=" + hex(section(out)) + "\n";
    Ladder ladder({ ad_playlist({ { 3, daterange } }, {}, false),
                    ad_playlist({ { 3, oatcls(out) }, { 5, oatcls(in) } }, {}, false),
                    ad_playlist({ { 3, oatcls(timed) } }, {}, false) });
    const MessageEvents m = ladder.run();
    INFO(warnings_text(m));
    CHECK(m.warnings.empty());
    CHECK(ladder.plain.empty());
    REQUIRE(m.cues.size() == 4);

    const timeline::Breaks b = ladder.pair(m);
    CHECK(b.warnings.empty());
    const auto reference = of_type(b.events, "ad_break", 0);
    REQUIRE(reference.size() == 1);
    CHECK(reference[0]->ref_index == 75);
    CHECK(reference[0]->ref_end == 100);
    CHECK(text(at(reference[0]->payload, "end")) == "planned_duration");
    const auto v270 = of_type(b.events, "ad_break", 1);
    REQUIRE(v270.size() == 1);
    CHECK(v270[0]->ref_index == 75);
    CHECK(v270[0]->ref_end == 125);
    CHECK(text(at(v270[0]->payload, "end")) == "cue_in");
    const auto v180 = of_type(b.events, "ad_break", 2);
    REQUIRE(v180.size() == 1);
    CHECK(v180[0]->ref_end == 125);
    CHECK(text(at(v180[0]->payload, "end")) == "break_duration");

    // A duration of 90 kHz ticks is written in seconds, which pair_breaks()
    // takes as the decimal its shortest text gives (src/PlaylistEvents.h):
    // ticks that 9 doesn't divide are no decimal of seconds, so such a break
    // without its cue_in isn't made, and pair_breaks() says why.
    Ladder odd({ ad_playlist({}, {}, false),
                 ad_playlist({ { 3, oatcls(splice_insert({ .event_id = 2, .immediate = true,
                                                           .duration = std::pair{ true, 180001 } })) } },
                             {}, false),
                 ad_playlist({}, {}, false) });
    const MessageEvents o = odd.run();
    REQUIRE(o.cues.size() == 1);
    CHECK(number(at(o.cues[0].payload, "break_duration")) == 180001.0 / 90000);
    const timeline::Breaks ob = odd.pair(o);
    CHECK(ob.events.empty());
    REQUIRE(ob.warnings.size() == 1);
    CHECK(contains(ob.warnings[0], "no AVRational holds its break_duration"));
}

TEST_CASE("scte35_events: a splice time is a splice_point on the carrying variant's own frames, within its "
          "playlist's discontinuity sequence, and every anchor is recorded (D-106 (a))",
          "[timeline][scte35][scte35_events]")
{
    // Program Start messages on v270, whose PTS are the reference's: the
    // ad's frames 75 to 124 repeat the PTS of frames 0 to 49.
    const auto program = [](std::int64_t pts, std::int64_t adjustment = 0) {
        return oatcls(time_signal(pts - adjustment, { { .type = 0x10 } }, adjustment));
    };
    const std::int64_t frame80 = kOrigin + 5 * kFrame25;   // the PTS of frames 5 and 80
    const std::map<int, std::string> before = {
        { 0, program(frame80) },
        { 3, program(frame80, 450000) + program(frame80 + 1) + program(kOrigin + 35 * kFrame25)
                 + program(kOrigin + 60 * kFrame25) + program(kOrigin - kFrame25) },
    };
    const std::string v270 = ad_playlist(before, {}, false);
    Ladder ladder({ ad_playlist({}), v270, ad_playlist({}) });
    const MessageEvents m = ladder.run();
    INFO(warnings_text(m));

    // Segment 0's: frame 5, in the main content's sequence.
    REQUIRE(m.splice_points.size() == 4);
    const Event& main = m.splice_points[0];
    CHECK(main.type == "splice_point");
    CHECK(main.variant == 1);
    CHECK(main.ref_index == 5);
    CHECK(main.segment == 0);
    CHECK(names(main.payload) == std::vector<std::string>{ "line", "form", "command", "pts_time",
                                                           "pts_adjustment", "pts_adjusted",
                                                           "component_tag", "index", "in_segment",
                                                           "crc_valid" });
    CHECK(integer(at(main.payload, "pts_adjusted")) == frame80);
    CHECK(flag(at(main.payload, "in_segment")));

    // Segment 3's, in the ad's sequence: the same PTS is frame 80, its
    // pts_adjustment added; a tick later, frame 81; frame 110, in segment 4,
    // with a warning; after the sequence's last frame and before its first,
    // none, with a warning each.
    const Event& ad = m.splice_points[1];
    CHECK(ad.ref_index == 80);
    CHECK(ad.segment == 3);
    CHECK(integer(at(ad.payload, "pts_time")) == frame80 - 450000);
    CHECK(integer(at(ad.payload, "pts_adjustment")) == 450000);
    CHECK(integer(at(ad.payload, "index")) == 80);
    CHECK(m.splice_points[2].ref_index == 81);
    const Event& later = m.splice_points[3];
    CHECK(later.ref_index == 110);
    CHECK(later.segment == 4);
    CHECK_FALSE(flag(at(later.payload, "in_segment")));
    const auto where = [&](std::int64_t pts) {
        return std::format("{}: the OATCLS at line {}: its splice time {} (90 kHz)", name(1),
                           line_with(v270, program(pts).substr(0, program(pts).size() - 1)), pts);
    };
    CHECK(any_warning(m, where(kOrigin + 35 * kFrame25)
                             + " falls on frame 110 of its variant, in segment 4, outside the tag's "
                               "segment 3 (D-106 (a))"));
    CHECK(any_warning(m, where(kOrigin + 60 * kFrame25)
                             + " is after the last frame of its discontinuity sequence, frame 124 of its "
                               "variant at PTS 1076400 (90 kHz), so outside the sequence: its splice_point "
                               "is left out (design §5.1)"));
    CHECK(any_warning(m, where(kOrigin - kFrame25)
                             + " is before the first frame of its discontinuity sequence, frame 75 of its "
                               "variant at PTS 900000 (90 kHz), so outside the sequence: its splice_point "
                               "is left out (design §5.1)"));
    CHECK(m.warnings.size() == 3);

    // The message's anchors: its position, and its splice time with its
    // offset from it.
    REQUIRE(m.others.size() == 6);
    const Payload& p = m.others[1].payload;
    CHECK(m.others[1].ref_index == 75);
    const Value& pos = anchor(p, "position");
    CHECK(integer(field(pos, "ref_index")) == 75);
    CHECK(integer(field(pos, "line")) == integer(at(p, "line")));
    const Value& splice = anchor(p, "splice_time");
    CHECK(integer(field(splice, "pts_adjusted")) == frame80);
    CHECK(integer(field(splice, "index")) == 80);
    CHECK(integer(field(splice, "ref_index")) == 80);
    CHECK(integer(field(splice, "offset_frames")) == 5);
    CHECK(number(field(splice, "offset_seconds")) == Catch::Approx(0.2).margin(1e-12));
    CHECK(flag(field(splice, "in_segment")));
    CHECK(is_null(field(splice, "component_tag")));
    const Value& outside = anchor(m.others[4].payload, "splice_time");
    CHECK(is_null(field(outside, "ref_index")));
    CHECK(is_null(field(outside, "offset_frames")));
    CHECK_FALSE(flag(field(outside, "in_segment")));
}

TEST_CASE("scte35_events: a splice time on a distorted variant whose PTS origin differs from the "
          "reference's lands on the right reference frame", "[timeline][scte35][scte35_events]")
{
    // v180 (pair 1) runs on a clock of its own, which passes 2^33 at its
    // frame 85 and goes on, as a demuxer gives PTS past a wrap; its frame 90
    // didn't pair.
    constexpr std::int64_t kShift = scte35::kPtsWrap - kOrigin - 10 * kFrame25;
    const auto dis_pts = [](std::int64_t i) { return ref_pts(i) + kShift; };
    const auto program = [](std::int64_t pts) { return oatcls(time_signal(pts, { { .type = 0x10 } })); };
    const auto wrapped = [&](std::int64_t i) { return dis_pts(i) % scte35::kPtsWrap; };
    const std::string v180 =
        ad_playlist({ { 3, program(wrapped(80)) + program(wrapped(87)) + program(wrapped(90)) } }, {},
                    false);
    Ladder ladder({ ad_playlist({}), ad_playlist({}), v180 }, [&](timeline::BuildInputs& in) {
        timeline::PairScores& p = in.scores.pairs[1];
        for (std::int64_t i = 0; i < kFrames; ++i) p.dis_pts[static_cast<std::size_t>(i)] = dis_pts(i);
        const auto drop = [](auto& v) { v.erase(v.begin() + 90); };
        drop(p.ref_index);
        drop(p.dis_index);
        drop(p.ref_pts);
        drop(p.dis_pts);
        drop(p.vmaf);
        in.variants[2].media = ad_media(dis_pts);
    });
    const MessageEvents m = ladder.run();
    INFO(warnings_text(m));
    CHECK(m.warnings.empty());
    REQUIRE(m.splice_points.size() == 3);
    // Frame 80 of v180, not frame 105, where the reference's PTS are nearest.
    CHECK(wrapped(80) == scte35::kPtsWrap - 5 * kFrame25);
    CHECK(m.splice_points[0].ref_index == 80);
    CHECK(m.splice_points[0].variant == 2);
    CHECK(m.splice_points[0].key.pts == ref_pts(80));
    // Past the wrap: 2 frames after it, frame 87.
    CHECK(wrapped(87) == 2 * kFrame25);
    CHECK(m.splice_points[1].ref_index == 87);
    // Frame 90 isn't paired: the next paired frame's (D-081 (d)).
    CHECK(integer(at(m.splice_points[2].payload, "index")) == 90);
    CHECK(m.splice_points[2].ref_index == 91);
}

TEST_CASE("scte35_events: a section anchored by media PTS goes through the same interface, and a "
          "period-time anchor goes to the warnings", "[timeline][scte35][scte35_events]")
{
    // No plain cue tags, which would take a cue_out message of their segment.
    Ladder ladder({ ad_playlist({}, {}, false), ad_playlist({}, {}, false), ad_playlist({}, {}, false) });
    const auto section_at = [](int variant, std::int64_t pts, AVRational tb,
                               std::optional<std::int64_t> segment, const Section& s) {
        CueInput c;
        c.carrier = timeline::CueCarrier::TsSection;
        c.anchor  = timeline::MediaPtsAnchor{ .variant = variant, .pts = pts, .time_base = tb,
                                              .segment = segment };
        c.form    = "TS section";
        c.bytes   = section(s);
        return c;
    };
    const Section out = splice_insert({ .event_id = 3, .immediate = true });
    const Section timed = time_signal(ref_pts(90), { { .type = 0x22 } });
    CueInput period;
    period.carrier = timeline::CueCarrier::DashEventStream;
    period.anchor  = timeline::PeriodAnchor{ .period = "p1", .presentation_time = 9000,
                                             .timescale = 90000 };
    period.form    = "EventStream";
    period.bytes   = section(out);
    const std::vector<CueInput> extra = {
        // In v270's ad, at the PTS of its frame 80, in 90 kHz and in ms.
        section_at(1, ref_pts(80), kTs, 3, out),
        section_at(1, ref_pts(80) / 90, AVRational{ 1, 1000 }, 3, out),
        // With a splice time of its own, ten frames on.
        section_at(1, ref_pts(80), kTs, 3, timed),
        // Its segment unknown: v270's playlist has three sequences.
        section_at(1, ref_pts(80), kTs, std::nullopt, out),
        // Its PTS a frame before the ad's sequence, its splice time in it.
        section_at(1, kOrigin - kFrame25, kTs, 3, timed),
        period,
    };
    const MessageEvents m = ladder.run({}, extra);
    REQUIRE(m.cues.size() == 5);
    for (const Event& c : m.cues) {
        CHECK(c.type == "cue_out");
        CHECK(c.variant == 1);
        CHECK(is_null(at(c.payload, "line")));
        CHECK(is_null(at(c.payload, "raw")));
        CHECK(text(at(c.payload, "form")) == "TS section");
    }
    for (std::size_t k = 0; k < 3; ++k) {
        CHECK(m.cues[k].segment == 3);
        CHECK(m.cues[k].ref_index == 80);
    }
    const Value& a = anchor(m.cues[1].payload, "media_pts");
    CHECK(integer(field(a, "pts")) == ref_pts(80) / 90);
    CHECK(text(field(a, "time_base")) == "1/1000");
    CHECK(integer(field(a, "index")) == 80);
    CHECK(integer(field(a, "ref_index")) == 80);
    CHECK(integer(field(anchor(m.cues[2].payload, "splice_time"), "offset_frames")) == 10);

    // A PTS that isn't placed: its cue still takes its turn in the pairing,
    // unplaced, with its segment where it is known, and its splice time is
    // searched in its sequence all the same.
    CHECK(m.cues[3].ref_index == timeline::kUnplaced);
    CHECK_FALSE(m.cues[3].segment);
    CHECK(is_null(field(anchor(m.cues[3].payload, "media_pts"), "ref_index")));
    CHECK(m.cues[4].ref_index == timeline::kUnplaced);
    CHECK(m.cues[4].segment == 3);
    REQUIRE(m.splice_points.size() == 2);
    for (const Event& s : m.splice_points) {
        CHECK(s.ref_index == 90);
        CHECK(flag(at(s.payload, "in_segment")));
    }
    CHECK(is_null(field(anchor(m.cues[4].payload, "splice_time"), "offset_frames")));
    REQUIRE(m.warnings.size() == 3);
    CHECK(m.warnings[0]
          == std::format("{}: the TS section at PTS {} (1/90000): its PTS can't be placed: its segment "
                         "isn't known, and its playlist has 3 discontinuity sequences, so its cue_out "
                         "isn't placed: it still takes its turn in the pairing (design §5.1)", name(1),
                         ref_pts(80)));
    CHECK(m.warnings[1]
          == std::format("{}: the TS section at PTS {} (1/90000): its PTS is before the first frame of "
                         "its discontinuity sequence, frame 75 of its variant at PTS {} (90 kHz), so "
                         "outside the sequence, so its cue_out isn't placed: it still takes its turn in "
                         "the pairing (design §5.1)", name(1), kOrigin - kFrame25, kOrigin));
    CHECK(m.warnings[2] == "the EventStream of period 'p1' at 9000/90000 has a DASH period-time anchor, "
                           "which nothing places, since no DASH timeline is scored: it makes no event "
                           "(design §5.5)");

    // Without a playlist, a variant's frames are one run: the reference as a
    // mezzanine would be, its frame 130 found among all 200.
    timeline::MessagePlacement mezzanine = ladder.placement();
    mezzanine.variants[0].playlist = nullptr;
    std::vector<Event> none;
    const MessageEvents z = timeline::scte35_events({ section_at(0, ref_pts(130), kTs, std::nullopt, out) },
                                                    none, mezzanine, {});
    INFO(warnings_text(z));
    REQUIRE(z.cues.size() == 1);
    CHECK(z.cues[0].ref_index == 130);
    CHECK(z.cues[0].segment == 5);

    // A section in the segment of an EXT-X-CUE-OUT is that cue's, whatever
    // carries it (D-106 (d)).
    Ladder cued({ ad_playlist({}), ad_playlist({}), ad_playlist({}) });
    const MessageEvents q = cued.run({}, { section_at(1, ref_pts(80), kTs, 3, out) });
    INFO(warnings_text(q));
    CHECK(q.warnings.empty());
    CHECK(q.cues.empty());
    const auto outs = of_type(cued.plain, "cue_out", 1);
    REQUIRE(outs.size() == 1);
    const Value::List& records = list(at(outs[0]->payload, "messages"));
    REQUIRE(records.size() == 1);
    CHECK(text(field(records[0], "form")) == "TS section");
    CHECK(is_null(field(records[0], "line")));
    CHECK(integer(field(anchor(object(records[0]), "media_pts"), "ref_index")) == 80);
}

TEST_CASE("scte35_events: segment starts and frames are placed where timeline::build() places them",
          "[timeline][scte35][scte35_events]")
{
    // v270 (pair 0) pairs none of its segment 1 and nothing after its frame
    // 189, and its frame 60 didn't pair; v180 (pair 1) is on a coarser grid,
    // its frame j paired with reference frame 2j, in segments of 13 and 12
    // frames, on continuous PTS.
    constexpr std::int64_t kFrame125 = 7200;
    const auto coarse_pts = [](std::int64_t j) { return kOrigin + j * kFrame125; };
    std::map<int, std::string> each;
    for (int k = 0; k < 8; ++k) each[k] = oatcls(Section{});
    const std::string playlist = ad_playlist(each, {}, false);
    Ladder ladder({ playlist, playlist, playlist }, [&](timeline::BuildInputs& in) {
        timeline::PairScores& p = in.scores.pairs[0];
        const auto keep = [&](auto pred) {
            const auto filter = [&](auto& v) {
                std::size_t w = 0;
                for (std::size_t k = 0; k < p.dis_index.size(); ++k)
                    if (pred(p.dis_index[k])) v[w++] = v[k];
                v.resize(w);
            };
            filter(p.ref_pts);
            filter(p.dis_pts);
            filter(p.vmaf);
            filter(p.ref_index);
            filter(p.dis_index);   // last: the others read it
        };
        keep([](std::int64_t i) { return (i < 25 || i >= 50) && i != 60 && i < 190; });
        p.context_first = { 0, 50 };
        timeline::PairScores& q = in.scores.pairs[1];
        q.ref_index.clear(), q.dis_index.clear(), q.ref_pts.clear(), q.dis_pts.clear(), q.vmaf.clear();
        for (std::int64_t j = 0; j < 100; ++j) {
            q.ref_index.push_back(2 * j);
            q.dis_index.push_back(j);
            q.ref_pts.push_back(ref_pts(2 * j));
            q.dis_pts.push_back(coarse_pts(j));
            q.vmaf.push_back(90.0);
        }
        q.dis_rate      = { 25, 2 };
        q.context_first = { 0 };
        q.dis_sequences = { { .first_index = 0, .first_pts = kOrigin, .frames = 100 } };
        timeline::VariantMedia& m = in.variants[2].media;
        m = {};
        std::int64_t first = 0;
        for (std::int64_t k = 0; k < 8; ++k) {
            const std::int64_t n = k % 2 == 0 ? 13 : 12;
            m.segments.push_back({ .seq = k, .first_index = first, .frames = n });
            first += n;
        }
        for (std::int64_t j = 0; j < 100; ++j) {
            m.keyframes.push_back(j);
            m.pts.push_back(coarse_pts(j));
        }
    });

    // A message in each segment of each variant, and one at each frame's PTS
    // in its segment, read from the media.
    std::vector<CueInput> at_frames;
    for (int v = 0; v < 3; ++v) {
        const timeline::VariantMedia& m = ladder.in.variants[static_cast<std::size_t>(v)].media;
        for (const timeline::SegmentFrames& s : m.segments)
            for (std::int64_t i = s.first_index; i < s.first_index + s.frames; ++i) {
                CueInput c;
                c.carrier = timeline::CueCarrier::TsSection;
                c.anchor  = timeline::MediaPtsAnchor{ .variant   = v,
                                                      .pts       = m.pts[static_cast<std::size_t>(i)],
                                                      .time_base = kTs,
                                                      .segment   = s.seq };
                c.form    = std::format("frame {}", i);
                c.bytes   = section(Section{});
                at_frames.push_back(std::move(c));
            }
    }
    const MessageEvents m = ladder.run({}, at_frames);

    for (int v = 0; v < 3; ++v) {
        INFO("variant " << v);
        // Each segment's message where the build's segment event starts, or
        // left out with the segment, which the build doesn't place either.
        const auto segments = of_type(ladder.t.events, "segment", v);
        for (std::int64_t k = 0; k < 8; ++k) {
            INFO("segment " << k);
            const auto seg = std::find_if(segments.begin(), segments.end(),
                                          [k](const Event* e) { return e->segment == k; });
            const auto msg = std::find_if(m.others.begin(), m.others.end(), [&](const Event& e) {
                return e.variant == v && e.segment == k && text(at(e.payload, "form")) == "OATCLS";
            });
            if (seg == segments.end()) {
                CHECK(msg == m.others.end());
                const std::int64_t line = line_with(playlist, std::format("main_00{}", k)) - 2;
                CHECK(any_warning(m, std::format("{}: the OATCLS at line {}: its segment {} isn't "
                                                 "placed, so its message isn't placed (design §5.1)",
                                                 name(v), line, k)));
            } else {
                REQUIRE(msg != m.others.end());
                CHECK(msg->ref_index == (*seg)->ref_index);
            }
        }
        // Each frame's message where the build places that frame's keyframe.
        const auto keys = of_type(ladder.t.events, "keyframe", v);
        const timeline::VariantMedia& media = ladder.in.variants[static_cast<std::size_t>(v)].media;
        std::size_t placed = 0;
        for (std::int64_t i = 0; i < static_cast<std::int64_t>(media.pts.size()); ++i) {
            const auto key = std::find_if(keys.begin(), keys.end(), [&](const Event* e) {
                return std::get<std::int64_t>(at(e->payload, "index").v) == i;
            });
            const auto msg = std::find_if(m.others.begin(), m.others.end(), [&](const Event& e) {
                return e.variant == v && text(at(e.payload, "form")) == std::format("frame {}", i);
            });
            INFO("frame " << i);
            if (key == keys.end()) {
                CHECK(msg == m.others.end());
            } else {
                REQUIRE(msg != m.others.end());
                CHECK(msg->ref_index == (*key)->ref_index);
                ++placed;
            }
        }
        CHECK(placed == keys.size());
    }
    // v270's segment 1 and frames 190 to 199, which no frame places.
    CHECK(of_type(ladder.t.events, "segment", 1).size() == 7);
    CHECK(of_type(ladder.t.events, "keyframe", 1).size() == 190);
    CHECK(any_warning(m, std::format("{}: the frame 190 at PTS {} (1/90000): its PTS lands on frame 190 "
                                     "of its variant, which is after its last paired frame or off the "
                                     "axis, so its message isn't placed (design §5.1)", name(1),
                                     ref_pts(190))));
}

TEST_CASE("scte35_events: what can't be decoded or placed is a warning, and nothing is left out silently",
          "[timeline][scte35][scte35_events]")
{
    Section encrypted = splice_insert({ .event_id = 1, .immediate = true });
    encrypted.encrypted = true;
    Bytes cut = section(time_signal(kOrigin, { { .type = 0x22 } }));
    cut.resize(cut.size() - 10);
    Bytes truncated_descriptor =
        section(Section{ .command_type = scte35::kTimeSignal, .command = { 0x7F },
                         .descriptors = { 0x02, 0x05, 0x43, 0x55, 0x45, 0x49, 0x00 } });
    const std::string after_last = oatcls(splice_insert({ .event_id = 2, .immediate = true }));
    const std::string range = "#EXT-X-DATERANGE:ID=\"x\",START-DATE=\"2026-03-01T12:00:00Z\",SCTE35-CMD=";
    const std::string v270 = ad_playlist(
        { { 0, "#EXT-OATCLS-SCTE35:not base64!!\n" },
          { 1, oatcls(encrypted) },
          { 2, range + hex(cut) + "\n" },
          { 4, "#EXT-OATCLS-SCTE35:" + base64(truncated_descriptor) + "\n" } },
        {}, false, "#EXT-X-PROGRAM-DATE-TIME:2026-03-01T12:00:00.000Z\n", after_last);
    const std::string v180 =
        ad_playlist({ { 3, oatcls(splice_insert({ .event_id = 3, .immediate = true })) } });
    Ladder ladder({ ad_playlist({}), v270, v180 }, [](timeline::BuildInputs& in) {
        in.variants[2].media = {};
        in.variants[2].media.refused = "frame 5's PTS isn't the report's";
    });
    CueInput stray;
    stray.anchor = timeline::PlaylistAnchor{ .variant = 7, .segment = 0, .line = 1 };
    stray.form   = "OATCLS";
    const MessageEvents m = ladder.run({}, { stray });
    INFO(warnings_text(m));
    const auto at_line = [&](std::string_view part) {
        const bool command = contains(part, "DATERANGE");
        return std::format("{}: the {} at line {}", name(1), command ? "DATERANGE SCTE35-CMD" : "OATCLS",
                           line_with(v270, part));
    };
    CHECK(any_warning(m, at_line("not base64!!")
                             + ": its text isn't base64, its character 4, byte 0x20, isn't base64's (RFC "
                               "4648 §4): it makes no event (design §5.5)"));
    CHECK(any_warning(m, at_line(base64(section(encrypted)))
                             + ": it doesn't decode, it is encrypted (encrypted_packet 1, "
                               "encryption_algorithm 0): its command and descriptors aren't decoded: it "
                               "makes no event (design §5.5)"));
    CHECK(any_warning(m, at_line("#EXT-X-DATERANGE")
                             + ": it doesn't decode, its section_length of 39 makes a section of 42 bytes, "
                               "and the input has 32"));
    CHECK(any_warning(m, at_line(base64(truncated_descriptor))
                             + ": its descriptor 1 counts for no class: segmentation_event_id runs past "
                               "the end of descriptor 1 (segmentation_descriptor, descriptor_length 5)"));
    // After the last URI: no segment holds it, and its cue takes its turn unplaced.
    CHECK(any_warning(m, at_line(after_last.substr(0, after_last.size() - 1))
                             + ": it follows the last segment's URI, and no segment holds it, so its "
                               "cue_out isn't placed: it still takes its turn in the pairing (design "
                               "§5.1)"));
    // A refused variant's messages, in one warning (D-081 (c), D-086).
    CHECK(any_warning(m, name(2) + ": no segment map, frame 5's PTS isn't the report's: its 1 SCTE-35 "
                                   "message is left out (D-081 (c), D-086)"));
    CHECK(any_warning(m, "the OATCLS names variant 7, which the build doesn't have (3 variants): it makes "
                         "no event"));
    CHECK(m.warnings.size() == 7);

    // The message whose descriptor is cut is still a message; the cue after
    // the last URI is returned unplaced.
    REQUIRE(m.others.size() == 1);
    CHECK(m.others[0].segment == 4);
    REQUIRE(m.cues.size() == 1);
    CHECK(m.cues[0].ref_index == timeline::kUnplaced);
    CHECK_FALSE(m.cues[0].segment);
    CHECK(is_null(field(anchor(m.cues[0].payload, "position"), "ref_index")));
}

TEST_CASE("scte35_events: an EXT-X-DATERANGE's dates and durations are read, an SCTE35-IN's in point by "
          "its END-DATE or DURATION, and a malformed one's well-formed SCTE35-* is decoded with a warning "
          "(D-106 (e))", "[timeline][scte35][scte35_events]")
{
    const Section out = splice_insert({ .event_id = 4, .immediate = true });
    const std::string message = hex(section(out));
    // The playlist's dates: segment k from 12:00:00 + k s.
    const std::string pdt = "#EXT-X-PROGRAM-DATE-TIME:2026-03-01T12:00:00.000Z\n";
    const auto range = [&](std::string_view id, std::string_view start, std::string_view rest) {
        return std::format("#EXT-X-DATERANGE:ID=\"{}\",START-DATE=\"{}\",{}SCTE35-OUT={}\n", id, start,
                           rest, message);
    };
    const std::string v270 = ad_playlist(
        { { 0, range("in", "2026-03-01T12:00:00.200Z",
                     "PLANNED-DURATION=2.0,END-DATE=\"2026-03-01T12:00:02.200Z\",") },
          { 1, range("end", "2026-03-01T12:00:02.000Z", "DURATION=1.5,") },
          { 2, range("before", "2026-03-01T12:00:01.999Z", "") },
          // Wowza's unquoted START-DATE: malformed, its message decoded all the same.
          { 6, std::format("#EXT-X-DATERANGE:ID=\"w\",START-DATE=2026-03-01T12:00:06.000Z,"
                           "PLANNED-DURATION=60.000,SCTE35-OUT={}\n", message) } },
        {}, false, pdt);
    const std::string nodate =
        ad_playlist({ { 0, range("n", "2026-03-01T12:00:00.000Z", "") } }, {}, false);
    Ladder ladder({ nodate, v270, ad_playlist({}) });
    const MessageEvents m = ladder.run();
    INFO(warnings_text(m));
    REQUIRE(m.cues.size() == 5);

    // Segment 0's: 0.2 s into its segment, its END-DATE 2 s after its START-DATE.
    const Payload& in = m.cues[1].payload;
    CHECK(m.cues[1].variant == 1);
    CHECK(text(at(in, "form")) == "DATERANGE SCTE35-OUT");
    CHECK(is_null(at(in, "duration")));
    CHECK(number(at(in, "end_date_duration")) == 2.0);
    CHECK(number(at(in, "planned_duration")) == 2.0);
    const Value& date = anchor(in, "start_date");
    CHECK(text(field(date, "date")) == "2026-03-01T12:00:00.200Z");
    CHECK(number(field(date, "offset_seconds")) == Catch::Approx(0.2).margin(1e-9));
    CHECK(flag(field(date, "in_segment")));
    // Segment 1's: a START-DATE at the segment's end is outside it, and
    // segment 2's, a millisecond before its start, too.
    CHECK(number(at(m.cues[2].payload, "duration")) == 1.5);
    CHECK_FALSE(flag(field(anchor(m.cues[2].payload, "start_date"), "in_segment")));
    CHECK(number(field(anchor(m.cues[2].payload, "start_date"), "offset_seconds"))
          == Catch::Approx(1.0).margin(1e-9));
    CHECK(any_warning(m, std::format("{}: the DATERANGE SCTE35-OUT at line {}: its START-DATE "
                                     "2026-03-01T12:00:02.000Z is 1 s from the EXT-X-PROGRAM-DATE-TIME of "
                                     "its segment 1, outside the segment's 1 s (D-106 (a))",
                                     name(1), line_with(v270, "ID=\"end\""))));
    CHECK_FALSE(flag(field(anchor(m.cues[3].payload, "start_date"), "in_segment")));
    CHECK(number(field(anchor(m.cues[3].payload, "start_date"), "offset_seconds"))
          == Catch::Approx(-0.001).margin(1e-9));

    // The malformed one: placed by its position, no attribute read.
    const Event& wowza = m.cues[4];
    CHECK(wowza.segment == 6);
    CHECK(wowza.ref_index == 150);
    CHECK(is_null(at(wowza.payload, "planned_duration")));
    CHECK(list(at(wowza.payload, "anchors")).size() == 1);
    CHECK(any_warning(m, std::format("{}: the DATERANGE SCTE35-OUT at line {}: its EXT-X-DATERANGE is "
                                     "malformed, START-DATE isn't a quoted-string (RFC 8216 §4.3.2.7): its "
                                     "SCTE35-OUT, a well-formed hexadecimal-sequence, is decoded all the "
                                     "same, and none of the tag's other attributes is read (D-106 (e))",
                                     name(1), line_with(v270, "ID=\"w\""))));
    // Its tag is still a daterange event of the playlist, with its error.
    const timeline::PlaylistEvents& p = *ladder.in.variants[1].playlist;
    CHECK(std::count_if(p.events.begin(), p.events.end(),
                        [](const Event& e) { return e.type == "daterange"; })
          == 1);

    // Without an EXT-X-PROGRAM-DATE-TIME, a START-DATE can't be set against
    // its segment.
    CHECK(m.cues[0].variant == 0);
    CHECK(is_null(field(anchor(m.cues[0].payload, "start_date"), "offset_seconds")));
    CHECK(any_warning(m, std::format("{}: the DATERANGE SCTE35-OUT at line {}: its START-DATE "
                                     "2026-03-01T12:00:00.000Z can't be set against its segment: the "
                                     "segment has no EXT-X-PROGRAM-DATE-TIME, its own or carried on (D-106 "
                                     "(a))", name(0), line_with(nodate, "ID=\"n\""))));
    CHECK(m.warnings.size() == 4);

    // RFC 8216 §4.3.2.7.1's pair of tags of one ID, on v180, segment k from
    // 12:00:00 + k s, a PDT after each discontinuity: the out before segment
    // 3, at its splice; the in before segment 5, with the out's START-DATE,
    // the break's start, and its actual END-DATE and DURATION, which date its
    // in point. Then a pair whose in gives DURATION alone: START-DATE plus
    // DURATION, a second after the start of segment 7, its tag's.
    const std::string in_message = hex(section(splice_insert({ .event_id = 4, .out = false,
                                                               .immediate = true })));
    const auto rfc_range = [&](std::string_view id, std::string_view start, std::string_view rest,
                               std::string_view attr, const std::string& hex_message) {
        return std::format("#EXT-X-DATERANGE:ID=\"{}\",START-DATE=\"{}\",{}{}={}\n", id, start, rest, attr,
                           hex_message);
    };
    const std::string at3 = "2026-03-01T12:00:03.000Z", at6 = "2026-03-01T12:00:06.000Z";
    const std::string v180 = ad_playlist(
        { { 3, "#EXT-X-PROGRAM-DATE-TIME:" + at3 + "\n"
                   + rfc_range("splice-1", at3, "PLANNED-DURATION=2.0,", "SCTE35-OUT", message) },
          { 5, "#EXT-X-PROGRAM-DATE-TIME:2026-03-01T12:00:05.000Z\n"
                   + rfc_range("splice-1", at3, "END-DATE=\"2026-03-01T12:00:05.000Z\",DURATION=2.0,",
                               "SCTE35-IN", in_message) },
          { 6, rfc_range("splice-2", at6, "PLANNED-DURATION=1.0,", "SCTE35-OUT", message) },
          { 7, rfc_range("splice-2", at6, "DURATION=2.0,", "SCTE35-IN", in_message) } },
        {}, false);
    Ladder rfc({ ad_playlist({}), ad_playlist({}), v180 });
    const MessageEvents r = rfc.run();
    INFO(warnings_text(r));
    REQUIRE(r.cues.size() == 4);
    CHECK((r.cues[0].type == "cue_out" && r.cues[0].ref_index == 75));
    CHECK(flag(field(anchor(r.cues[0].payload, "start_date"), "in_segment")));

    // The in: its END-DATE is its segment's start; its START-DATE, 2 s
    // before it, is recorded as the range's start and set against nothing.
    const Payload& closing = r.cues[1].payload;
    CHECK((r.cues[1].type == "cue_in" && r.cues[1].ref_index == 125));
    CHECK(number(at(closing, "duration")) == 2.0);
    CHECK(number(at(closing, "end_date_duration")) == 2.0);
    const Value& range_start = anchor(closing, "start_date");
    CHECK(number(field(range_start, "offset_seconds")) == Catch::Approx(-2.0).margin(1e-9));
    CHECK(is_null(field(range_start, "in_segment")));
    const Value& in_point = anchor(closing, "end_date");
    CHECK(names(object(in_point)) == std::vector<std::string>{ "anchor", "from", "date", "unix", "pdt",
                                                               "offset_seconds", "in_segment" });
    CHECK(text(field(in_point, "from")) == "END-DATE");
    CHECK(text(field(in_point, "date")) == "2026-03-01T12:00:05.000Z");
    CHECK(number(field(in_point, "offset_seconds")) == Catch::Approx(0.0).margin(1e-9));
    CHECK(flag(field(in_point, "in_segment")));
    CHECK_FALSE(member(r.cues[0].payload, "end_date"));

    // The in without END-DATE: START-DATE plus DURATION, outside its
    // segment, with a warning, the only one.
    const Value& late = anchor(r.cues[3].payload, "end_date");
    CHECK(text(field(late, "from")) == "START-DATE + DURATION");
    CHECK(is_null(field(late, "date")));
    CHECK(number(field(late, "offset_seconds")) == Catch::Approx(1.0).margin(1e-9));
    CHECK_FALSE(flag(field(late, "in_segment")));
    const std::int64_t late_line = line_with(v180, "ID=\"splice-2\",START-DATE=\"" + at6 + "\",DURATION");
    CHECK(r.warnings == std::vector<std::string>{ std::format(
              "{}: the DATERANGE SCTE35-IN at line {}: its START-DATE 2026-03-01T12:00:06.000Z plus its "
              "DURATION of 2.0 s is 1 s from the EXT-X-PROGRAM-DATE-TIME of its segment 7, outside the "
              "segment's 1 s (D-106 (a))", name(2), late_line) });

    // Paired by their cue_ins.
    const timeline::Breaks b = rfc.pair(r);
    CHECK(b.warnings.empty());
    const auto breaks = of_type(b.events, "ad_break", 2);
    REQUIRE(breaks.size() == 2);
    CHECK((breaks[0]->ref_index == 75 && breaks[0]->ref_end == 125));
    CHECK((breaks[1]->ref_index == 150 && breaks[1]->ref_end == 175));
}
