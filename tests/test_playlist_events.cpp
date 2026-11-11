// timeline::parse_playlist_events(), segment_event() and pair_breaks()
// (src/PlaylistEvents.h) on media playlists the tests write: the segment a
// tag belongs to, the playlist source's events, the plain cue tags in the
// forms eval/phase3-survey/scte35.md §2 describes, the messages handed on to
// the decoder, a live playlist refused (DECISIONS.md D-081 (a)), and the
// pairing of cues into ad breaks by D-076's end rule, for plain cues and for
// cues from a stub decoder alike, a break of no frames kept with a warning
// (D-110 (c)). Nothing is read but the text, and no message is decoded: the
// messages here are made up, in the tags' shapes.

#include <catch2/catch_test_macros.hpp>

#include "PlaylistEvents.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using timeline::Attribute;
using timeline::Breaks;
using timeline::CueInput;
using timeline::Event;
using timeline::Payload;
using timeline::PlaylistAnchor;
using timeline::PlaylistEvents;
using timeline::Source;
using timeline::Value;
using timeline::kUnplaced;

namespace {

const std::string kBase = "http://example.com/ladder/v180/index.m3u8";

// ── Payloads ─────────────────────────────────────────────────────────────────

bool has(const Payload& p, std::string_view name)
{
    return std::any_of(p.begin(), p.end(), [&](const auto& m) { return m.first == name; });
}

// The member `name` of a payload; the test fails without it.
const Value& at(const Payload& p, std::string_view name)
{
    for (const auto& [n, v] : p)
        if (n == name) return v;
    FAIL("no payload member " << name);
    static const Value none;
    return none;
}

std::vector<std::string> names(const Payload& p)
{
    std::vector<std::string> out;
    for (const auto& m : p) out.push_back(m.first);
    return out;
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

// An object's members as text, for the cue tags' attributes, which are kept
// as written.
std::vector<std::pair<std::string, std::string>> texts(const Value& v)
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [n, m] : object(v)) out.emplace_back(n, text(m));
    return out;
}

std::int64_t line_of(const Event& e) { return integer(at(e.payload, "line")); }

std::vector<const Event*> of_type(const std::vector<Event>& events, std::string_view type)
{
    std::vector<const Event*> out;
    for (const Event& e : events)
        if (e.type == type) out.push_back(&e);
    return out;
}

std::string bytes_text(const CueInput& c) { return std::string(c.bytes.begin(), c.bytes.end()); }

const PlaylistAnchor& anchor(const CueInput& c)
{
    REQUIRE(std::holds_alternative<PlaylistAnchor>(c.anchor));
    return std::get<PlaylistAnchor>(c.anchor);
}

// ── Placement ────────────────────────────────────────────────────────────────

// `events` placed as the build places them (design §5.1) on a reference whose
// segments are `frames_per_segment` frames long from media sequence 0 and
// whose frames every variant's segments share: a segment-bound event at its
// segment's first frame. testgen's ladders are 1 s segments of 25 frames
// (eval/phase3-survey/testgen.md §2.3).
std::vector<Event> placed(std::vector<Event> events, std::int64_t frames_per_segment = 25)
{
    for (Event& e : events)
        if (e.segment) e.ref_index = *e.segment * frames_per_segment;
    return events;
}

// The axis of a reference at 25 fps with `frames` frames: `d` seconds after
// frame f end at the first frame at or after f / 25 + d, computed exactly,
// or at `frames` when no frame is. Each duration asked for goes to `seen`,
// and is checked to be positive, as FrameAfter's contract has it.
timeline::FrameAfter grid_25(std::int64_t frames = 200, std::vector<AVRational>* seen = nullptr)
{
    return [frames, seen](std::int64_t f, AVRational d) -> std::optional<std::int64_t> {
        CHECK(d.num > 0);
        CHECK(d.den > 0);
        if (seen) seen->push_back(d);
        const std::int64_t num = std::int64_t{ d.num } * 25, den = d.den;   // frames: num / den
        return std::min(f + (num + den - 1) / den, frames);
    };
}

// A cue as a decoder would give one (src/Scte35Events.h): placed, with its
// line and the members pair_breaks() reads.
Event cue(std::string_view type, int variant, std::int64_t segment, std::int64_t line,
          std::int64_t ref_index, Payload more = {})
{
    Event e;
    e.source = Source::Scte35;
    e.type.assign(type);
    e.variant   = variant;
    e.segment   = segment;
    e.ref_index = ref_index;
    e.payload.emplace_back("line", Value{ line });
    for (auto& m : more) e.payload.push_back(std::move(m));
    return e;
}

// testgen's ad playlist (eval/phase3-survey/testgen.md §2.3), as its
// prototype writes the low-quality ad rung: the ad in segments 3 and 4,
// between two EXT-X-DISCONTINUITY tags, with the plain cue tags.
const std::string kAdPlaylist =
    "#EXTM3U\n"                              //  1
    "#EXT-X-VERSION:3\n"                     //  2
    "#EXT-X-TARGETDURATION:1\n"              //  3
    "#EXT-X-MEDIA-SEQUENCE:0\n"              //  4
    "#EXT-X-DISCONTINUITY-SEQUENCE:0\n"      //  5
    "#EXT-X-PLAYLIST-TYPE:VOD\n"             //  6
    "#EXTINF:1.000000,\n"                    //  7
    "main_000.ts\n"                          //  8
    "#EXTINF:1.000000,\n"                    //  9
    "main_001.ts\n"                          // 10
    "#EXTINF:1.000000,\n"                    // 11
    "main_002.ts\n"                          // 12
    "#EXT-X-DISCONTINUITY\n"                 // 13
    "#EXT-X-CUE-OUT:2.000\n"                 // 14
    "#EXTINF:1.000000,\n"                    // 15
    "ad_000.ts\n"                            // 16
    "#EXT-X-CUE-OUT-CONT:1.000/2.000\n"      // 17
    "#EXTINF:1.000000,\n"                    // 18
    "ad_001.ts\n"                            // 19
    "#EXT-X-DISCONTINUITY\n"                 // 20
    "#EXT-X-CUE-IN\n"                        // 21
    "#EXTINF:1.000000,\n"                    // 22
    "main_005.ts\n"                          // 23
    "#EXTINF:1.000000,\n"                    // 24
    "main_006.ts\n"                          // 25
    "#EXTINF:1.000000,\n"                    // 26
    "main_007.ts\n"                          // 27
    "#EXT-X-ENDLIST\n";                      // 28

// A VOD playlist of 1 s segments, one per element of `body`, which holds the
// tags before its URI: "#EXTINF:1,\nsN.ts\n" follows each element, N from 0.
std::string vod(const std::vector<std::string>& body, const std::string& header = {})
{
    std::string s = "#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXT-X-PLAYLIST-TYPE:VOD\n" + header;
    for (std::size_t i = 0; i < body.size(); ++i)
        s += body[i] + std::format("#EXTINF:1,\ns{}.ts\n", i);
    return s + "#EXT-X-ENDLIST\n";
}

} // namespace

TEST_CASE("playlist_events: testgen's ad playlist, its segments, discontinuities and plain cues",
          "[timeline][playlist_events]")
{
    const PlaylistEvents p = timeline::parse_playlist_events(kAdPlaylist, kBase, 2);
    REQUIRE(p.error.empty());
    CHECK(p.variant == 2);
    REQUIRE(p.timeline.segments.size() == 8);
    CHECK(p.uris == std::vector<std::string>{ "main_000.ts", "main_001.ts", "main_002.ts", "ad_000.ts",
                                              "ad_001.ts", "main_005.ts", "main_006.ts", "main_007.ts" });
    // Every tag but the header line: the playlist's 28 lines less #EXTM3U
    // and the 8 URIs.
    CHECK(p.tags.size() == 19);
    CHECK(p.messages.empty());

    // The header's tags and the segments' own make no event: only the two
    // discontinuities, each in the segment it starts, with the sequence it
    // starts.
    REQUIRE(p.events.size() == 2);
    for (const Event& e : p.events) {
        CHECK(e.source == Source::Playlist);
        CHECK(e.type == "discontinuity");
        CHECK(e.variant == 2);
        CHECK(e.ref_index == kUnplaced);
        CHECK_FALSE(e.ref_end.has_value());
        CHECK(names(e.payload) == std::vector<std::string>{ "line", "discontinuity_sequence" });
    }
    CHECK(p.events[0].segment == 3);
    CHECK(line_of(p.events[0]) == 13);
    CHECK(integer(at(p.events[0].payload, "discontinuity_sequence")) == 1);
    CHECK(p.events[1].segment == 5);
    CHECK(line_of(p.events[1]) == 20);
    CHECK(integer(at(p.events[1].payload, "discontinuity_sequence")) == 2);

    // The plain cues, each in the segment whose URI follows it.
    REQUIRE(p.cues.size() == 3);
    const Event& out = p.cues[0];
    CHECK(out.source == Source::Scte35);
    CHECK(out.type == "cue_out");
    CHECK(out.segment == 3);
    CHECK(out.ref_index == kUnplaced);
    CHECK(names(out.payload) == std::vector<std::string>{ "line", "form", "raw", "duration", "attributes" });
    CHECK(line_of(out) == 14);
    CHECK(text(at(out.payload, "form")) == "CUE-OUT");
    CHECK(text(at(out.payload, "raw")) == "#EXT-X-CUE-OUT:2.000");
    CHECK(number(at(out.payload, "duration")) == 2.0);
    CHECK(object(at(out.payload, "attributes")).empty());

    const Event& cont = p.cues[1];
    CHECK(cont.type == "cue_out_cont");
    CHECK(cont.segment == 4);
    CHECK(names(cont.payload)
          == std::vector<std::string>{ "line", "form", "raw", "elapsed", "duration", "attributes" });
    CHECK(line_of(cont) == 17);
    CHECK(text(at(cont.payload, "form")) == "CUE-OUT-CONT");
    CHECK(number(at(cont.payload, "elapsed")) == 1.0);
    CHECK(number(at(cont.payload, "duration")) == 2.0);

    const Event& in = p.cues[2];
    CHECK(in.type == "cue_in");
    CHECK(in.segment == 5);
    CHECK(names(in.payload) == std::vector<std::string>{ "line", "form", "raw", "attributes" });
    CHECK(line_of(in) == 21);
    CHECK(text(at(in.payload, "raw")) == "#EXT-X-CUE-IN");

    // The break: from the cue-out's segment to the cue-in's, on testgen's
    // 25 frames per segment, the ad's frames 75 to 124.
    const Breaks b = timeline::pair_breaks(placed(p.cues), grid_25());
    CHECK(b.warnings.empty());
    REQUIRE(b.events.size() == 1);
    const Event& br = b.events[0];
    CHECK(br.source == Source::Scte35);
    CHECK(br.type == "ad_break");
    CHECK(br.variant == 2);
    CHECK(br.segment == 3);
    CHECK(br.ref_index == 75);
    CHECK(br.ref_end == 125);
    CHECK(names(br.payload) == std::vector<std::string>{ "cue_out_line", "cue_in_line", "end", "durations",
                                                         "cue_out_cont_durations" });
    CHECK(integer(at(br.payload, "cue_out_line")) == 14);
    CHECK(integer(at(br.payload, "cue_in_line")) == 21);
    CHECK(text(at(br.payload, "end")) == "cue_in");
    const Value::Object& durations = object(at(br.payload, "durations"));
    REQUIRE(durations.size() == 1);
    CHECK(durations[0].first == "duration");
    CHECK(number(durations[0].second) == 2.0);
    REQUIRE(list(at(br.payload, "cue_out_cont_durations")).size() == 1);
    CHECK(number(list(at(br.payload, "cue_out_cont_durations"))[0]) == 2.0);

    // Without its EXT-X-CUE-IN, the break ends at its declared 2 s: the same
    // frames on a 25 fps axis.
    std::string no_in = kAdPlaylist;
    no_in.erase(no_in.find("#EXT-X-CUE-IN\n"), std::string_view("#EXT-X-CUE-IN\n").size());
    const PlaylistEvents q = timeline::parse_playlist_events(no_in, kBase, 2);
    REQUIRE(q.error.empty());
    REQUIRE(q.cues.size() == 2);
    std::vector<AVRational> seen;
    const Breaks d = timeline::pair_breaks(placed(q.cues), grid_25(200, &seen));
    CHECK(d.warnings.empty());
    REQUIRE(d.events.size() == 1);
    CHECK(d.events[0].ref_index == 75);
    CHECK(d.events[0].ref_end == 125);
    CHECK(is_null(at(d.events[0].payload, "cue_in_line")));
    CHECK(text(at(d.events[0].payload, "end")) == "duration");
    REQUIRE(seen.size() == 1);
    CHECK(av_cmp_q(seen[0], AVRational{ 2, 1 }) == 0);
}

TEST_CASE("playlist_events: a tag belongs to the segment whose URI follows it",
          "[timeline][playlist_events]")
{
    // A tag before #EXTINF and one between #EXTINF and the URI (Wowza's
    // placement, scte35.md §2.4) belong to the same segment; a tag after the
    // last URI belongs to none.
    const std::string playlist =
        "#EXTM3U\n"                                  //  1
        "#EXT-X-TARGETDURATION:6\n"                  //  2
        "#EXT-X-PLAYLIST-TYPE:VOD\n"                 //  3
        "#EXTINF:6.0,\n"                             //  4
        "a.ts\n"                                     //  5
        "#EXT-X-CUE-OUT:60.000\n"                    //  6
        "#EXTINF:6.0,\n"                             //  7
        "#EXT-OATCLS-SCTE35:/DARAAAAAAAAAP/wAAAAAAAAAAA=\n"   //  8
        "b.ts\n"                                     //  9
        "#EXTINF:6.0,\n"                             // 10
        "#EXT-X-DISCONTINUITY\n"                     // 11
        "c.ts\n"                                     // 12
        "\n"                                         // 13
        "#EXT-X-CUE-IN\n"                            // 14
        "#EXT-OATCLS-SCTE35:/DATAAAAAAAAAP/wAgI=\n"  // 15
        "#EXT-X-ENDLIST\n";                          // 16
    const PlaylistEvents p = timeline::parse_playlist_events(playlist, kBase, 0);
    REQUIRE(p.error.empty());
    REQUIRE(p.timeline.segments.size() == 3);

    REQUIRE(p.cues.size() == 2);
    CHECK(p.cues[0].type == "cue_out");
    CHECK(line_of(p.cues[0]) == 6);
    CHECK(p.cues[0].segment == 1);
    CHECK(p.cues[1].type == "cue_in");
    CHECK(line_of(p.cues[1]) == 14);
    CHECK_FALSE(p.cues[1].segment.has_value());

    REQUIRE(p.messages.size() == 2);
    CHECK(anchor(p.messages[0]).segment == 1);
    CHECK(anchor(p.messages[0]).line == 8);
    CHECK(anchor(p.messages[1]).segment == -1);
    CHECK(anchor(p.messages[1]).line == 15);

    // The discontinuity between #EXTINF and c.ts is c.ts's, as
    // hls::parse_media_timeline has it.
    REQUIRE(p.events.size() == 1);
    CHECK(p.events[0].type == "discontinuity");
    CHECK(p.events[0].segment == 2);
    CHECK(p.timeline.segments[2].discontinuity);
    CHECK(integer(at(p.events[0].payload, "discontinuity_sequence")) == p.timeline.segments[2].disc_seq);

    // The tags record their segment's index, -1 for none.
    std::map<int, int> segment_of_line;
    for (const auto& t : p.tags) segment_of_line[t.line] = t.segment_index;
    CHECK(segment_of_line[4] == 0);
    CHECK(segment_of_line[6] == 1);
    CHECK(segment_of_line[7] == 1);
    CHECK(segment_of_line[8] == 1);
    CHECK(segment_of_line[11] == 2);
    CHECK(segment_of_line[14] == -1);
    CHECK(segment_of_line[16] == -1);

    // The same text with CRLF line ends and a byte order mark: the same
    // lines, as parse_media_timeline counts them.
    std::string crlf = "\xEF\xBB\xBF";
    for (char c : playlist) crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    const PlaylistEvents r = timeline::parse_playlist_events(crlf, kBase, 0);
    REQUIRE(r.error.empty());
    REQUIRE(r.tags.size() == p.tags.size());
    for (std::size_t i = 0; i < p.tags.size(); ++i) {
        CHECK(r.tags[i].line == p.tags[i].line);
        CHECK(r.tags[i].segment_index == p.tags[i].segment_index);
        CHECK(r.tags[i].name == p.tags[i].name);
        CHECK(r.tags[i].value == p.tags[i].value);
    }
    CHECK(r.uris == p.uris);
    CHECK(r.timeline.segments[1].line == 9);

    // A URI that no #EXTINF precedes makes no segment, so the tags before it
    // belong to the next segment.
    const PlaylistEvents s = timeline::parse_playlist_events(
        "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-PLAYLIST-TYPE:VOD\n"
        "#EXT-X-CUE-OUT:4\nstray.ts\n#EXTINF:2,\na.ts\n#EXT-X-ENDLIST\n",
        kBase, 0);
    REQUIRE(s.error.empty());
    REQUIRE(s.timeline.segments.size() == 1);
    CHECK(s.uris == std::vector<std::string>{ "a.ts" });
    REQUIRE(s.cues.size() == 1);
    CHECK(s.cues[0].segment == 0);
    CHECK(line_of(s.cues[0]) == 4);
}

TEST_CASE("playlist_events: discontinuities are numbered from EXT-X-DISCONTINUITY-SEQUENCE",
          "[timeline][playlist_events]")
{
    const std::string playlist =
        "#EXTM3U\n"
        "#EXT-X-TARGETDURATION:2\n"
        "#EXT-X-MEDIA-SEQUENCE:100\n"
        "#EXT-X-DISCONTINUITY-SEQUENCE:7\n"
        "#EXT-X-PLAYLIST-TYPE:VOD\n"
        "#EXTINF:2,\n"
        "s100.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:2,\n"
        "s101.ts\n"
        "#EXTINF:2,\n"
        "#EXT-X-DISCONTINUITY\n"
        "s102.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:2,\n"
        "s103.ts\n"
        "#EXT-X-ENDLIST\n";
    const PlaylistEvents p = timeline::parse_playlist_events(playlist, kBase, 1);
    REQUIRE(p.error.empty());
    const auto discs = of_type(p.events, "discontinuity");
    REQUIRE(discs.size() == 4);
    // Events name their segment by media sequence number, from 100.
    CHECK(discs[0]->segment == 101);
    CHECK(integer(at(discs[0]->payload, "discontinuity_sequence")) == 8);
    CHECK(discs[1]->segment == 102);
    CHECK(integer(at(discs[1]->payload, "discontinuity_sequence")) == 9);
    // Two tags before one segment: hls::parse_media_timeline starts one
    // sequence there, which both events give.
    CHECK(discs[2]->segment == 103);
    CHECK(discs[3]->segment == 103);
    CHECK(integer(at(discs[2]->payload, "discontinuity_sequence")) == 10);
    CHECK(integer(at(discs[3]->payload, "discontinuity_sequence")) == 10);

    std::vector<std::int64_t> seqs;
    for (std::size_t i = 0; i < p.timeline.segments.size(); ++i)
        seqs.push_back(integer(at(timeline::segment_event(p, i, std::nullopt).payload, "discontinuity_sequence")));
    CHECK(seqs == std::vector<std::int64_t>{ 7, 8, 9, 10 });

    // A trailing discontinuity starts no segment's sequence.
    const PlaylistEvents t = timeline::parse_playlist_events(
        "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXTINF:2,\na.ts\n"
        "#EXT-X-DISCONTINUITY\n#EXT-X-ENDLIST\n",
        kBase, 1);
    REQUIRE(t.events.size() == 1);
    CHECK_FALSE(t.events[0].segment.has_value());
    CHECK(is_null(at(t.events[0].payload, "discontinuity_sequence")));
}

TEST_CASE("playlist_events: EXT-X-PROGRAM-DATE-TIME as written and as Unix seconds",
          "[timeline][playlist_events]")
{
    const PlaylistEvents p = timeline::parse_playlist_events(
        vod({ "#EXT-X-PROGRAM-DATE-TIME:2026-03-01T12:00:00.000Z\n", "",
              "#EXT-X-PROGRAM-DATE-TIME:not a date\n" }),
        kBase, 0);
    REQUIRE(p.error.empty());
    const auto pdts = of_type(p.events, "program_date_time");
    REQUIRE(pdts.size() == 2);
    CHECK(names(pdts[0]->payload) == std::vector<std::string>{ "line", "date", "unix" });
    CHECK(pdts[0]->segment == 0);
    CHECK(text(at(pdts[0]->payload, "date")) == "2026-03-01T12:00:00.000Z");
    // calendar.timegm((2026, 3, 1, 12, 0, 0)) in Python gives 1772366400.
    CHECK(number(at(pdts[0]->payload, "unix")) == 1772366400.0);
    CHECK(pdts[1]->segment == 2);
    CHECK(text(at(pdts[1]->payload, "date")) == "not a date");
    CHECK(is_null(at(pdts[1]->payload, "unix")));

    // The segments' own dates: explicit, then carried on 1 s at a time
    // (src/HlsAlign.cpp), past the date that doesn't parse.
    const Event s0 = timeline::segment_event(p, 0, std::nullopt);
    const Event s1 = timeline::segment_event(p, 1, std::nullopt);
    const Event s2 = timeline::segment_event(p, 2, std::nullopt);
    CHECK(number(at(s0.payload, "pdt")) == 1772366400.0);
    CHECK(flag(at(s0.payload, "pdt_explicit")));
    CHECK(number(at(s1.payload, "pdt")) == 1772366401.0);
    CHECK_FALSE(flag(at(s1.payload, "pdt_explicit")));
    CHECK(number(at(s2.payload, "pdt")) == 1772366402.0);
    CHECK_FALSE(flag(at(s2.payload, "pdt_explicit")));
}

TEST_CASE("playlist_events: vendor tags and the signalings outside D-070's list are kept raw",
          "[timeline][playlist_events]")
{
    const std::string playlist =
        "#EXTM3U\n"                                                                //  1
        "#EXT-X-VERSION:7\n"                                                       //  2
        "#EXT-X-TARGETDURATION:2\n"                                                //  3
        "#EXT-X-PLAYLIST-TYPE:VOD\n"                                               //  4
        "#EXT-X-INDEPENDENT-SEGMENTS\n"                                            //  5
        "#EXT-X-START:TIME-OFFSET=0\n"                                             //  6
        "## splice_insert(auto_return)\n"                                          //  7
        "#USP-X-TIMESTAMP-MAP:MPEGTS=900000,LOCAL=1970-01-01T00:00:00Z\n"          //  8
        "# EXT-X-CUE-OUT-CONT: 12.480/30\n"                                        //  9
        "#EXT-X-MAP:URI=\"init.mp4\"\n"                                            // 10
        "#EXT-X-KEY:METHOD=NONE\n"                                                 // 11
        "#EXT-X-SCTE35:TYPE=0x30,DURATION=30.000,CUE-OUT=YES,CUE=\"/DAUAAAAAAAAAP/wAw==\"\n"   // 12
        "#EXT-X-SPLICEPOINT-SCTE35:/DAUAAAAAAAAAP/wAw==\n"                         // 13
        "#EXT-X-CUE:DURATION=\"30.000\",ID=\"7\",TYPE=\"SpliceOut\",TIME=\"12.000\"\n"   // 14
        "#EXT-X-CUE DURATION=27 ID=cue-1\n"                                        // 15
        "#EXT-X-ASSET:CAID=0x0000000000000001\n"                                   // 16
        "#EXTINF:2,\n"                                                             // 17
        "a.ts\n"                                                                   // 18
        "#EXT-X-VENDOR-ANALYTICS-URL:https://example.com/analytics\n"              // 19
        "#EXTINF:2,\n"                                                             // 20
        "#EXT-X-GAP\n"                                                             // 21
        "b.ts\n"                                                                   // 22
        "#EXT-X-ENDLIST\n";                                                        // 23
    const PlaylistEvents p = timeline::parse_playlist_events(playlist, kBase, 0);
    REQUIRE(p.error.empty());
    // No cue and no message: D-070's list only (D-076).
    CHECK(p.cues.empty());
    CHECK(p.messages.empty());

    // Lines 7 to 9 are comments (RFC 8216 §4.1), not tags; the header's
    // tags, #EXTINF and EXT-X-GAP make no event.
    struct Want {
        int line;
        std::int64_t segment;
        std::string name;
        std::optional<std::string> value;
    };
    const std::vector<Want> want = {
        { 6, 0, "EXT-X-START", "TIME-OFFSET=0" },
        { 10, 0, "EXT-X-MAP", "URI=\"init.mp4\"" },
        { 11, 0, "EXT-X-KEY", "METHOD=NONE" },
        { 12, 0, "EXT-X-SCTE35", "TYPE=0x30,DURATION=30.000,CUE-OUT=YES,CUE=\"/DAUAAAAAAAAAP/wAw==\"" },
        { 13, 0, "EXT-X-SPLICEPOINT-SCTE35", "/DAUAAAAAAAAAP/wAw==" },
        { 14, 0, "EXT-X-CUE", "DURATION=\"30.000\",ID=\"7\",TYPE=\"SpliceOut\",TIME=\"12.000\"" },
        { 15, 0, "EXT-X-CUE DURATION=27 ID=cue-1", std::nullopt },   // no ':': all of it the name
        { 16, 0, "EXT-X-ASSET", "CAID=0x0000000000000001" },
        { 19, 1, "EXT-X-VENDOR-ANALYTICS-URL", "https://example.com/analytics" },
    };
    REQUIRE(p.events.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        const Event& e = p.events[i];
        INFO("line " << want[i].line);
        CHECK(e.source == Source::Playlist);
        CHECK(e.type == "tag");
        CHECK(e.segment == want[i].segment);
        CHECK(names(e.payload) == std::vector<std::string>{ "line", "name", "value" });
        CHECK(line_of(e) == want[i].line);
        CHECK(text(at(e.payload, "name")) == want[i].name);
        if (want[i].value) CHECK(text(at(e.payload, "value")) == *want[i].value);
        else CHECK(is_null(at(e.payload, "value")));
    }
    // The tags list holds every tag, but not the comments.
    CHECK(std::none_of(p.tags.begin(), p.tags.end(), [](const timeline::PlaylistTag& t) {
        return t.line >= 7 && t.line <= 9;
    }));
    // EXT-X-GAP and EXT-X-MAP are flags of the segments they apply to.
    CHECK_FALSE(flag(at(timeline::segment_event(p, 0, std::nullopt).payload, "gap")));
    CHECK(flag(at(timeline::segment_event(p, 1, std::nullopt).payload, "gap")));
    CHECK(flag(at(timeline::segment_event(p, 1, std::nullopt).payload, "map")));
}

TEST_CASE("playlist_events: a live playlist isn't read (D-081 (a)), nor a text that isn't a media playlist",
          "[timeline][playlist_events]")
{
    const std::string segments = "#EXTINF:2,\na.ts\n#EXT-X-CUE-OUT:2\n#EXTINF:2,\nb.ts\n";
    const std::string head = "#EXTM3U\n#EXT-X-TARGETDURATION:2\n";

    // Neither EXT-X-ENDLIST nor EXT-X-PLAYLIST-TYPE:VOD: live, refused.
    for (const std::string& live : { head + segments, head + "#EXT-X-PLAYLIST-TYPE:EVENT\n" + segments }) {
        const PlaylistEvents p = timeline::parse_playlist_events(live, kBase, 0);
        CHECK(p.error.find("live playlist") != std::string::npos);
        CHECK(p.error.find("D-081 (a)") != std::string::npos);
        CHECK(p.timeline.ok);
        CHECK(p.tags.empty());
        CHECK(p.events.empty());
        CHECK(p.cues.empty());
        CHECK(p.messages.empty());
    }
    // An EVENT playlist that has ended, and a VOD one without
    // EXT-X-ENDLIST, aren't live (hls::MediaTimeline::live()).
    for (const std::string& vod_text : { head + "#EXT-X-PLAYLIST-TYPE:EVENT\n" + segments + "#EXT-X-ENDLIST\n",
                                         head + "#EXT-X-PLAYLIST-TYPE:VOD\n" + segments }) {
        const PlaylistEvents p = timeline::parse_playlist_events(vod_text, kBase, 0);
        CHECK(p.error.empty());
        CHECK(p.cues.size() == 1);
    }

    // A master playlist, a text that isn't a playlist, a playlist without
    // segments: each refused with parse_media_timeline's reason.
    const PlaylistEvents master = timeline::parse_playlist_events(
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000\nv1/index.m3u8\n", kBase, 0);
    CHECK(master.error == "it isn't a media playlist with segments: a master playlist, not a media playlist");
    const PlaylistEvents html = timeline::parse_playlist_events("<html></html>\n", kBase, 0);
    CHECK(html.error == "it isn't a media playlist with segments: not a playlist: no #EXTM3U header");
    const PlaylistEvents empty = timeline::parse_playlist_events(
        "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-ENDLIST\n", kBase, 0);
    CHECK(empty.error == "it isn't a media playlist with segments: no media segments");
}

TEST_CASE("playlist_events: EXT-X-CUE-OUT in the forms scte35.md §2.1 describes",
          "[timeline][playlist_events]")
{
    struct Want {
        std::string tag;
        std::optional<double> duration;
        std::vector<std::pair<std::string, std::string>> attributes;
        bool error = false;
    };
    const std::vector<Want> want = {
        { "#EXT-X-CUE-OUT", std::nullopt, {} },
        { "#EXT-X-CUE-OUT:", std::nullopt, {} },
        { "#EXT-X-CUE-OUT:30.000", 30.0, {} },
        { "#EXT-X-CUE-OUT:24", 24.0, {} },
        { "#EXT-X-CUE-OUT:19.2", 19.2, {} },
        { "#EXT-X-CUE-OUT:DURATION=120", 120.0, { { "DURATION", "120" } } },
        { "#EXT-X-CUE-OUT:DURATION=10.50,K=V", 10.5, { { "DURATION", "10.50" }, { "K", "V" } } },
        { "#EXT-X-CUE-OUT:10.50,K=V", 10.5, { { "K", "V" } } },
        { "#EXT-X-CUE-OUT:ID=\"1001\",X-TYPE=\"EABN\",DURATION=29.988", 29.988,
          { { "ID", "1001" }, { "X-TYPE", "EABN" }, { "DURATION", "29.988" } } },
        { "#EXT-X-CUE-OUT:ID=\"1001\",DURATION=29.988", 29.988, { { "ID", "1001" }, { "DURATION", "29.988" } } },
        { "#EXT-X-CUE-OUT:Duration=\"45.500\"", 45.5, { { "Duration", "45.500" } } },   // any case
        { "#EXT-X-CUE-OUT:soon", std::nullopt, {}, true },
        { "#EXT-X-CUE-OUT:DURATION=abc", std::nullopt, { { "DURATION", "abc" } }, true },
        { "#EXT-X-CUE-OUT:-5", std::nullopt, {}, true },
    };
    std::vector<std::string> body;
    for (const Want& w : want) body.push_back(w.tag + "\n");
    body.push_back("#EXT-X-CUE-OUT-CONT:10/30\n");   // never a cue-out (scte35.md §6)
    const PlaylistEvents p = timeline::parse_playlist_events(vod(body), kBase, 0);
    REQUIRE(p.error.empty());
    REQUIRE(p.cues.size() == want.size() + 1);
    for (std::size_t i = 0; i < want.size(); ++i) {
        const Event& e = p.cues[i];
        INFO(want[i].tag);
        CHECK(e.type == "cue_out");
        CHECK(e.segment == static_cast<std::int64_t>(i));
        CHECK(text(at(e.payload, "raw")) == want[i].tag);
        if (want[i].duration) CHECK(number(at(e.payload, "duration")) == *want[i].duration);
        else CHECK(is_null(at(e.payload, "duration")));
        CHECK(texts(at(e.payload, "attributes")) == want[i].attributes);
        CHECK(has(e.payload, "error") == want[i].error);
    }
    CHECK(text(at(p.cues[11].payload, "error")).find("attributes don't parse") != std::string::npos);
    CHECK(text(at(p.cues[12].payload, "error")) == "its DURATION 'abc' isn't a decimal number");
    CHECK(p.cues.back().type == "cue_out_cont");
    CHECK(p.messages.empty());
}

TEST_CASE("playlist_events: EXT-X-CUE-OUT-CONT in both forms, its SCTE35= handed on",
          "[timeline][playlist_events]")
{
    const std::string b64 = "/DASAAAAAAAAAP/wAQEBAQ==";
    const PlaylistEvents p = timeline::parse_playlist_events(
        vod({ "#EXT-X-CUE-OUT-CONT: 12.480/30\n", "#EXT-X-CUE-OUT-CONT:10/30\n",
              "#EXT-X-CUE-OUT-CONT:ElapsedTime=2.480,Duration=60.000,SCTE35=" + b64 + "\n",
              "#EXT-X-CUE-OUT-CONT:5\n", "#EXT-X-CUE-OUT-CONT:ElapsedTime=x,Duration=30\n",
              "#EXT-X-CUE-OUT-CONT\n" }),
        kBase, 3);
    REQUIRE(p.error.empty());
    REQUIRE(p.cues.size() == 6);
    const auto elapsed  = [&](std::size_t i) { return at(p.cues[i].payload, "elapsed"); };
    const auto duration = [&](std::size_t i) { return at(p.cues[i].payload, "duration"); };
    for (const Event& e : p.cues) CHECK(e.type == "cue_out_cont");

    // elapsed/duration, a blank after the ':' as AWS prints it (scte35.md §2.2).
    CHECK(number(elapsed(0)) == 12.48);
    CHECK(number(duration(0)) == 30.0);
    CHECK(text(at(p.cues[0].payload, "raw")) == "#EXT-X-CUE-OUT-CONT: 12.480/30");
    CHECK(number(elapsed(1)) == 10.0);
    CHECK(number(duration(1)) == 30.0);
    CHECK_FALSE(has(p.cues[1].payload, "error"));

    // The attribute form: its message handed on, as written, at its tag.
    CHECK(number(elapsed(2)) == 2.48);
    CHECK(number(duration(2)) == 60.0);
    CHECK(texts(at(p.cues[2].payload, "attributes"))
          == std::vector<std::pair<std::string, std::string>>{
              { "ElapsedTime", "2.480" }, { "Duration", "60.000" }, { "SCTE35", b64 } });
    REQUIRE(p.messages.size() == 1);
    const CueInput& m = p.messages[0];
    CHECK(m.carrier == timeline::CueCarrier::PlaylistTag);
    CHECK(m.form == "CUE-OUT-CONT SCTE35");
    CHECK(bytes_text(m) == b64);
    CHECK(anchor(m).variant == 3);
    CHECK(anchor(m).segment == 2);
    CHECK(anchor(m).line == line_of(p.cues[2]));

    // Malformed: kept, with the reason, and nothing invented.
    CHECK(is_null(elapsed(3)));
    CHECK(is_null(duration(3)));
    CHECK(has(p.cues[3].payload, "error"));
    CHECK(is_null(elapsed(4)));
    CHECK(number(duration(4)) == 30.0);
    CHECK(text(at(p.cues[4].payload, "error")) == "its ElapsedTime 'x' isn't a decimal number");
    // Bare: nothing declared.
    CHECK(is_null(elapsed(5)));
    CHECK(is_null(duration(5)));
    CHECK_FALSE(has(p.cues[5].payload, "error"));
}

TEST_CASE("playlist_events: EXT-X-CUE-IN bare and with attributes", "[timeline][playlist_events]")
{
    const std::string b64 = "/DATAAAAAAAAAP/wAgI=";
    const PlaylistEvents p = timeline::parse_playlist_events(
        vod({ "#EXT-X-CUE-IN\n", "#EXT-X-CUE-IN:ID=\"1001\"\n",
              "#EXT-X-CUE-IN:ID=\"1002\",SCTE35=" + b64 + "\n", "#EXT-X-CUE-IN:\"x\"\n" }),
        kBase, 0);
    REQUIRE(p.error.empty());
    REQUIRE(p.cues.size() == 4);
    for (const Event& e : p.cues) CHECK(e.type == "cue_in");
    CHECK(object(at(p.cues[0].payload, "attributes")).empty());
    CHECK(texts(at(p.cues[1].payload, "attributes"))
          == std::vector<std::pair<std::string, std::string>>{ { "ID", "1001" } });
    CHECK_FALSE(has(p.cues[1].payload, "error"));
    REQUIRE(p.messages.size() == 1);
    CHECK(p.messages[0].form == "CUE-IN SCTE35");
    CHECK(bytes_text(p.messages[0]) == b64);
    CHECK(anchor(p.messages[0]).segment == 2);
    CHECK(has(p.cues[3].payload, "error"));
    CHECK(object(at(p.cues[3].payload, "attributes")).empty());
}

TEST_CASE("playlist_events: #EXT-OATCLS-SCTE35 handed on as a CueInput with its anchor",
          "[timeline][playlist_events]")
{
    // Before the CUE-OUT of its segment, as MediaLive writes it, and after a
    // cue tag of its segment, each in its segment (scte35.md §2.4).
    const std::string out_msg = "/DARAAAAAAAAAP/wAAAAAAAAAAA=", in_msg = "/DAUAAAAAAAAAP/wAw==";
    const std::string playlist = vod(
        { "", "#EXT-OATCLS-SCTE35:" + out_msg + "\n#EXT-X-CUE-OUT:2\n", "",
          "#EXT-X-CUE-IN\n#EXT-OATCLS-SCTE35: " + in_msg + "\n" },
        "#EXT-X-MEDIA-SEQUENCE:40\n");
    const PlaylistEvents p = timeline::parse_playlist_events(playlist, kBase, 5);
    REQUIRE(p.error.empty());
    REQUIRE(p.messages.size() == 2);
    CHECK(p.messages[0].form == "OATCLS");
    CHECK(bytes_text(p.messages[0]) == out_msg);
    CHECK(anchor(p.messages[0]).variant == 5);
    CHECK(anchor(p.messages[0]).segment == 41);   // media sequence numbers, from 40
    CHECK(anchor(p.messages[0]).line == 7);
    CHECK(std::any_of(p.tags.begin(), p.tags.end(), [](const timeline::PlaylistTag& t) {
        return t.line == 7 && t.name == "EXT-OATCLS-SCTE35" && t.segment_index == 1;
    }));
    // The value as written, without the blank before it.
    CHECK(bytes_text(p.messages[1]) == in_msg);
    CHECK(anchor(p.messages[1]).segment == 43);
    // The plain cues are there as well: merging a message into its
    // CUE-OUT's cue is the decoder's (design §5.5).
    REQUIRE(p.cues.size() == 2);
    CHECK(p.cues[0].segment == 41);
    CHECK(p.cues[1].segment == 43);
    CHECK(p.events.empty());
}

TEST_CASE("playlist_events: EXT-X-DATERANGE's attributes, its SCTE35-* messages, a malformed one kept raw",
          "[timeline][playlist_events]")
{
    const std::vector<std::string> ranges = {
        // 0: quoted, decimal and hex values, a comma inside quotes.
        "ID=\"chapter-2\",CLASS=\"com.example.chapter\",START-DATE=\"2026-03-01T12:00:10Z\","
        "PLANNED-DURATION=20.5,X-COM-EXAMPLE-ID=0xAB12,X-COM-EXAMPLE-RANK=3,X-COM-EXAMPLE-NOTE=\"a, b\","
        "END-ON-NEXT=YES",
        // 1, 2: SCTE35-OUT, then SCTE35-IN and SCTE35-CMD together: messages only.
        "ID=\"ad-1\",START-DATE=\"2026-03-01T12:00:20Z\",PLANNED-DURATION=30,SCTE35-OUT=0xFC3011",
        "ID=\"ad-1\",START-DATE=\"2026-03-01T12:00:20Z\",DURATION=30,SCTE35-IN=0xFC3012,SCTE35-CMD=0xfc3013",
        // 3 to 9: malformed.
        "ID=\"0001\",START-DATE=\"2026-03-01T12:00:30Z\",PLANNED-DURATION=60.000,SCTE35-OUT0xFC30",
        "ID=\"0002\",START-DATE=2026-03-01T12:00:30Z,PLANNED-DURATION=60.000",
        "ID=\"0003\",DURATION=\"30\"",
        "ID=\"0004\",ID=\"0005\"",
        "ID=\"0006,START-DATE=\"2026-03-01T12:00:30Z\"",
        "ID=\"0007\",SCTE35-CMD=\"0xFC30\"",
        "ID=\"0008\",X-COM-EXAMPLE-WORD=word",
    };
    std::vector<std::string> body;
    for (const std::string& r : ranges) body.push_back("#EXT-X-DATERANGE:" + r + "\n");
    const PlaylistEvents p = timeline::parse_playlist_events(
        vod(body, "#EXT-X-PROGRAM-DATE-TIME:2026-03-01T12:00:00Z\n"), kBase, 0);
    REQUIRE(p.error.empty());

    // The messages of 1 and 2, one per attribute, in their order, as written.
    REQUIRE(p.messages.size() == 3);
    CHECK(p.messages[0].form == "DATERANGE SCTE35-OUT");
    CHECK(bytes_text(p.messages[0]) == "0xFC3011");
    CHECK(anchor(p.messages[0]).segment == 1);
    CHECK(p.messages[1].form == "DATERANGE SCTE35-IN");
    CHECK(bytes_text(p.messages[1]) == "0xFC3012");
    CHECK(p.messages[2].form == "DATERANGE SCTE35-CMD");
    CHECK(bytes_text(p.messages[2]) == "0xfc3013");
    CHECK(anchor(p.messages[2]).segment == 2);
    CHECK(anchor(p.messages[2]).line == anchor(p.messages[1]).line);

    // A daterange event for each of the others; the PDT is the other event.
    const auto dr = of_type(p.events, "daterange");
    REQUIRE(dr.size() == 8);
    CHECK(of_type(p.events, "program_date_time").size() == 1);
    CHECK(names(dr[0]->payload) == std::vector<std::string>{ "line", "raw", "attributes" });
    CHECK(dr[0]->segment == 0);
    CHECK(text(at(dr[0]->payload, "raw")) == ranges[0]);
    const Value::Object& a = object(at(dr[0]->payload, "attributes"));
    REQUIRE(a.size() == 8);
    CHECK(a[0].first == "ID");
    CHECK(text(a[0].second) == "chapter-2");
    CHECK(text(a[1].second) == "com.example.chapter");
    CHECK(text(a[2].second) == "2026-03-01T12:00:10Z");
    CHECK(a[3].first == "PLANNED-DURATION");
    CHECK(number(a[3].second) == 20.5);
    CHECK(text(a[4].second) == "0xAB12");
    CHECK(number(a[5].second) == 3.0);
    CHECK(text(a[6].second) == "a, b");
    CHECK(text(a[7].second) == "YES");

    // Malformed ones: the value as written, the reason, no attributes, and no
    // message handed on: the two SCTE35-* here aren't well-formed
    // hexadecimal-sequences themselves (D-106 (e)).
    const std::vector<std::string> errors = {
        "attribute SCTE35-OUT0xFC30 has no '='",
        "START-DATE isn't a quoted-string (RFC 8216 §4.3.2.7)",
        "DURATION isn't a decimal-floating-point (RFC 8216 §4.3.2.7)",
        "attribute ID is given twice",
        "'2' follows the value of ID, where a ',' or the end belongs",
        "SCTE35-CMD isn't a hexadecimal-sequence (RFC 8216 §4.3.2.7)",
        "X-COM-EXAMPLE-WORD is neither a quoted-string, a hexadecimal-sequence nor a decimal-floating-point "
        "(RFC 8216 §4.3.2.7)",
    };
    for (std::size_t i = 0; i < errors.size(); ++i) {
        const Event& e = *dr[i + 1];
        INFO(ranges[i + 3]);
        CHECK(names(e.payload) == std::vector<std::string>{ "line", "raw", "attributes", "error" });
        CHECK(e.segment == static_cast<std::int64_t>(i + 3));
        CHECK(text(at(e.payload, "raw")) == ranges[i + 3]);
        CHECK(object(at(e.payload, "attributes")).empty());
        CHECK(text(at(e.payload, "error")) == errors[i]);
    }
}

TEST_CASE("playlist_events: a malformed EXT-X-DATERANGE hands on each SCTE35-* that is a well-formed "
          "hexadecimal-sequence (D-106 (e))", "[timeline][playlist_events]")
{
    const std::vector<std::string> ranges = {
        // 0: Wowza's unquoted START-DATE, a type error; its SCTE35-OUT is
        // well-formed.
        "ID=\"0001\",START-DATE=2026-03-01T12:00:30Z,PLANNED-DURATION=60.000,SCTE35-OUT=0xFC3011",
        // 1: a list that doesn't parse after a well-formed SCTE35-OUT, with a
        // well-formed SCTE35-IN after the bad value and blanks around its ','.
        "ID=\"0002\",SCTE35-OUT=0xfc3012,X-COM-EXAMPLE-NOTE=a b , SCTE35-IN=0xFC3013",
        // 2: two of one name, each well-formed, and a quoted SCTE35-OUT, which
        // isn't a hexadecimal-sequence.
        "ID=\"0003\",SCTE35-CMD=0xFC3014,SCTE35-CMD=0xFC3015,SCTE35-OUT=\"0xFC3016\"",
        // 3: a quote that never closes holds the rest: nothing can be told apart.
        "ID=\"0004,SCTE35-OUT=0xFC3017",
        // 4: an SCTE35-OUT that isn't hex, and an SCTE35-IN without '='.
        "ID=\"0005\",START-DATE=x,SCTE35-OUT=0xFC30G1,SCTE35-IN0xFC3018",
    };
    std::vector<std::string> body;
    for (const std::string& r : ranges) body.push_back("#EXT-X-DATERANGE:" + r + "\n");
    const PlaylistEvents p = timeline::parse_playlist_events(
        vod(body, "#EXT-X-PROGRAM-DATE-TIME:2026-03-01T12:00:00Z\n"), kBase, 2);
    REQUIRE(p.error.empty());

    std::vector<std::pair<std::string, std::string>> handed;
    for (const CueInput& c : p.messages) {
        handed.emplace_back(c.form, bytes_text(c));
        CHECK(anchor(c).variant == 2);
    }
    CHECK(handed == std::vector<std::pair<std::string, std::string>>{
                        { "DATERANGE SCTE35-OUT", "0xFC3011" },
                        { "DATERANGE SCTE35-OUT", "0xfc3012" },
                        { "DATERANGE SCTE35-IN", "0xFC3013" },
                        { "DATERANGE SCTE35-CMD", "0xFC3014" },
                        { "DATERANGE SCTE35-CMD", "0xFC3015" },
                    });
    REQUIRE(p.messages.size() == 5);
    CHECK(anchor(p.messages[0]).segment == 0);
    CHECK(anchor(p.messages[1]).segment == 1);
    CHECK(anchor(p.messages[2]).segment == 1);
    CHECK(anchor(p.messages[3]).segment == 2);
    CHECK(anchor(p.messages[4]).segment == 2);

    // Each is still a daterange event kept raw, with its reason, which the
    // decoder's warning names (src/Scte35Events.h), at its messages' line.
    const auto dr = of_type(p.events, "daterange");
    REQUIRE(dr.size() == ranges.size());
    const std::vector<std::string> errors = {
        "START-DATE isn't a quoted-string (RFC 8216 §4.3.2.7)",
        "the unquoted value of X-COM-EXAMPLE-NOTE has a '\"' or a blank in it",
        "attribute SCTE35-CMD is given twice",
        "the quoted value of ID has no closing '\"'",
        "attribute SCTE35-IN0xFC3018 has no '='",
    };
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        INFO(ranges[i]);
        CHECK(text(at(dr[i]->payload, "raw")) == ranges[i]);
        CHECK(object(at(dr[i]->payload, "attributes")).empty());
        CHECK(text(at(dr[i]->payload, "error")) == errors[i]);
    }
    CHECK(line_of(*dr[0]) == anchor(p.messages[0]).line);
    CHECK(line_of(*dr[1]) == anchor(p.messages[2]).line);
    CHECK(line_of(*dr[2]) == anchor(p.messages[4]).line);
}

TEST_CASE("playlist_events: parse_attributes", "[timeline][playlist_events]")
{
    std::string err = "left over";
    std::vector<Attribute> a = timeline::parse_attributes("ID=\"a,b\",X=0x1F,D=1.5,E=YES", err);
    CHECK(err.empty());
    REQUIRE(a.size() == 4);
    CHECK(a[0].name == "ID");
    CHECK(a[0].value == "a,b");
    CHECK(a[0].quoted);
    CHECK(a[1].value == "0x1F");
    CHECK_FALSE(a[1].quoted);
    CHECK(a[2].value == "1.5");
    CHECK(a[3].value == "YES");

    // Blanks around a ',' are allowed; an empty list has no attribute.
    a = timeline::parse_attributes("A=1, B=2 ,C=\"x\"  ", err);
    CHECK(err.empty());
    REQUIRE(a.size() == 3);
    CHECK(a[1].name == "B");
    CHECK(a[1].value == "2");
    CHECK(timeline::parse_attributes("", err).empty());
    CHECK(err.empty());
    CHECK(timeline::parse_attributes("  ", err).empty());
    CHECK(err.empty());

    const std::vector<std::pair<std::string, std::string>> bad = {
        { "=1", "attribute 1 has no name" },
        { "A=1,,B=2", "attribute 2 has no name" },
        { "A", "attribute A has no '='" },
        { "A B=1", "attribute A has no '='" },
        { "A=", "attribute A has no value" },
        { "A=1,B= ", "attribute B has no value" },
        { "A=\"x", "the quoted value of A has no closing '\"'" },
        { "A=\"x\"y", "'y' follows the value of A, where a ',' or the end belongs" },
        { "A=x y", "the unquoted value of A has a '\"' or a blank in it" },
        { "A=x\"y\"", "the unquoted value of A has a '\"' or a blank in it" },
        { "A=1,A=2", "attribute A is given twice" },
        { "A=1,", "the list ends with a ','" },
        { "A.B=1", "the attribute name 'A.B' has a character other than a letter, a digit, '-' or '_'" },
    };
    for (const auto& [list, why] : bad) {
        INFO(list);
        CHECK(timeline::parse_attributes(list, err).empty());
        CHECK(err == why);
    }
    // A success after a failure clears the reason.
    CHECK(timeline::parse_attributes("A=1", err).size() == 1);
    CHECK(err.empty());
}

TEST_CASE("playlist_events: a segment's event", "[timeline][playlist_events]")
{
    const std::string playlist =
        "#EXTM3U\n"                       // 1
        "#EXT-X-TARGETDURATION:4\n"       // 2
        "#EXT-X-PLAYLIST-TYPE:VOD\n"      // 3
        "#EXT-X-MAP:URI=\"init.mp4\"\n"   // 4
        "#EXTINF:4.004,\n"                // 5
        "#EXT-X-BYTERANGE:1000@0\n"       // 6
        "main.mp4\n"                      // 7
        "#EXTINF:4.004,\n"                // 8
        "#EXT-X-BYTERANGE:2000\n"         // 9
        "main.mp4\n"                      // 10
        "#EXT-X-ENDLIST\n";               // 11
    const PlaylistEvents p = timeline::parse_playlist_events(playlist, kBase, 4);
    REQUIRE(p.error.empty());
    REQUIRE(p.timeline.segments.size() == 2);

    const Event s0 = timeline::segment_event(p, 0, 1000);
    CHECK(s0.source == Source::Playlist);
    CHECK(s0.type == "segment");
    CHECK(s0.variant == 4);
    CHECK(s0.segment == 0);
    CHECK(s0.ref_index == kUnplaced);
    CHECK_FALSE(s0.ref_end.has_value());
    CHECK(names(s0.payload)
          == std::vector<std::string>{ "line", "uri", "url", "media_sequence", "extinf", "byte_range",
                                       "discontinuity_sequence", "pdt", "pdt_explicit", "playlist_time",
                                       "bytes", "gap", "map" });
    CHECK(line_of(s0) == 7);
    CHECK(text(at(s0.payload, "uri")) == "main.mp4");
    CHECK(text(at(s0.payload, "url")) == "http://example.com/ladder/v180/main.mp4");
    CHECK(integer(at(s0.payload, "media_sequence")) == 0);
    CHECK(number(at(s0.payload, "extinf")) == 4.004);
    const Value::Object& r0 = object(at(s0.payload, "byte_range"));
    REQUIRE(r0.size() == 2);
    CHECK(r0[0].first == "length");
    CHECK(integer(r0[0].second) == 1000);
    CHECK(r0[1].first == "offset");
    CHECK(integer(r0[1].second) == 0);
    CHECK(integer(at(s0.payload, "discontinuity_sequence")) == 0);
    CHECK(is_null(at(s0.payload, "pdt")));
    CHECK_FALSE(flag(at(s0.payload, "pdt_explicit")));
    CHECK(number(at(s0.payload, "playlist_time")) == 0.0);
    CHECK(integer(at(s0.payload, "bytes")) == 1000);
    CHECK_FALSE(flag(at(s0.payload, "gap")));
    CHECK(flag(at(s0.payload, "map")));

    // A range without an offset follows the one before it in the same file.
    const Event s1 = timeline::segment_event(p, 1, std::nullopt);
    CHECK(s1.segment == 1);
    CHECK(line_of(s1) == 10);
    const Value::Object& r1 = object(at(s1.payload, "byte_range"));
    CHECK(integer(r1[0].second) == 2000);
    CHECK(integer(r1[1].second) == 1000);
    CHECK(number(at(s1.payload, "playlist_time")) == 4.004);
    CHECK(is_null(at(s1.payload, "bytes")));

    // A whole file has no range; EXT-X-MAP is a tag event of its own.
    const PlaylistEvents w = timeline::parse_playlist_events(kAdPlaylist, kBase, 0);
    CHECK(is_null(at(timeline::segment_event(w, 3, std::nullopt).payload, "byte_range")));
    CHECK_FALSE(flag(at(timeline::segment_event(w, 3, std::nullopt).payload, "map")));
    REQUIRE(p.events.size() == 1);
    CHECK(text(at(p.events[0].payload, "name")) == "EXT-X-MAP");
}

TEST_CASE("playlist_events: pair_breaks pairs each variant's cues in play order",
          "[timeline][playlist_events]")
{
    // Two breaks in each of two variants; the second variant's playlist has
    // a cue-out and a cue-in in one segment, which make an empty span, kept
    // with a warning (D-110 (c)).
    const std::string v0 = vod({ "", "#EXT-X-CUE-OUT:1\n", "#EXT-X-CUE-IN\n", "#EXT-X-CUE-OUT:2\n", "",
                                 "#EXT-X-CUE-IN\n" });
    const std::string v1 = vod({ "#EXT-X-CUE-OUT\n", "#EXT-X-CUE-OUT-CONT:1/3\n", "#EXT-X-CUE-OUT-CONT\n",
                                 "#EXT-X-CUE-IN\n#EXT-X-CUE-OUT:0\n#EXT-X-CUE-IN\n" });
    const PlaylistEvents p0 = timeline::parse_playlist_events(v0, kBase, 0);
    const PlaylistEvents p1 = timeline::parse_playlist_events(v1, kBase, 1);
    REQUIRE(p0.error.empty());
    REQUIRE(p1.error.empty());

    // In any order: the cues are sorted by variant, segment and line.
    std::vector<Event> all = placed(p1.cues);
    for (const Event& e : placed(p0.cues)) all.push_back(e);
    std::reverse(all.begin(), all.end());
    const Breaks b = timeline::pair_breaks(all, grid_25());
    CHECK(b.warnings == std::vector<std::string>{
        "variant 1: the ad_break of the cue_out at line 14 has no frames, the cue_in at line 15 ending it "
        "at reference frame 75, where it starts: a drop that starts at that frame doesn't list it (D-078, "
        "D-110 (c))" });
    REQUIRE(b.events.size() == 4);
    CHECK(b.events[0].variant == 0);
    CHECK(b.events[0].ref_index == 25);
    CHECK(b.events[0].ref_end == 50);
    CHECK(b.events[1].variant == 0);
    CHECK(b.events[1].ref_index == 75);
    CHECK(b.events[1].ref_end == 125);
    CHECK(b.events[2].variant == 1);
    CHECK(b.events[2].ref_index == 0);
    CHECK(b.events[2].ref_end == 75);
    CHECK(object(at(b.events[2].payload, "durations")).empty());   // a bare CUE-OUT declares none
    const Value::List& conts = list(at(b.events[2].payload, "cue_out_cont_durations"));
    REQUIRE(conts.size() == 2);
    CHECK(number(conts[0]) == 3.0);
    CHECK(is_null(conts[1]));
    CHECK(b.events[3].variant == 1);
    CHECK(b.events[3].ref_index == 75);
    CHECK(b.events[3].ref_end == 75);
    for (const Event& e : b.events) CHECK(text(at(e.payload, "end")) == "cue_in");

    // The same cues give the same breaks.
    const Breaks again = timeline::pair_breaks(all, grid_25());
    REQUIRE(again.events.size() == b.events.size());
    for (std::size_t i = 0; i < b.events.size(); ++i) {
        CHECK(again.events[i].ref_index == b.events[i].ref_index);
        CHECK(again.events[i].ref_end == b.events[i].ref_end);
        CHECK(names(again.events[i].payload) == names(b.events[i].payload));
    }
}

TEST_CASE("playlist_events: pair_breaks ends a break without a cue-in at its first declared duration",
          "[timeline][playlist_events]")
{
    using timeline::kTypeCueOut;
    // D-076's order: the tag's own, END-DATE less START-DATE, PLANNED-DURATION,
    // break_duration, segmentation_duration; each value is kept.
    const auto run = [](Payload durations) {
        std::vector<AVRational> seen;
        const Breaks b = timeline::pair_breaks({ cue(kTypeCueOut, 0, 1, 10, 25, std::move(durations)) },
                                               grid_25(200, &seen));
        REQUIRE(b.warnings.empty());
        REQUIRE(b.events.size() == 1);
        return std::pair{ b.events[0], seen };
    };
    {
        const auto [e, seen] = run({ { "duration", Value{ 2.0 } }, { "planned_duration", Value{ 3.0 } } });
        CHECK(text(at(e.payload, "end")) == "duration");
        CHECK(e.ref_end == 75);
        CHECK(object(at(e.payload, "durations")).size() == 2);
    }
    {
        const auto [e, seen] = run({ { "duration", Value{} }, { "end_date_duration", Value{ 1.0 } },
                                     { "planned_duration", Value{ 3.0 } } });
        CHECK(text(at(e.payload, "end")) == "end_date_duration");
        CHECK(e.ref_end == 50);
        // A null is no declared duration, and isn't kept as one.
        const Value::Object& d = object(at(e.payload, "durations"));
        REQUIRE(d.size() == 2);
        CHECK(d[0].first == "end_date_duration");
        CHECK(d[1].first == "planned_duration");
    }
    {
        const auto [e, seen] = run({ { "break_duration", Value{ 4.0 } }, { "planned_duration", Value{ 3.0 } } });
        CHECK(text(at(e.payload, "end")) == "planned_duration");
        CHECK(e.ref_end == 100);
        // In kDurationNames' order, whatever the payload's.
        const Value::Object& d = object(at(e.payload, "durations"));
        REQUIRE(d.size() == 2);
        CHECK(d[0].first == "planned_duration");
        CHECK(d[1].first == "break_duration");
    }
    {
        const auto [e, seen] = run({ { "segmentation_duration", Value{ 5.0 } }, { "break_duration", Value{ 4.0 } } });
        CHECK(text(at(e.payload, "end")) == "break_duration");
        CHECK(e.ref_end == 125);
    }
    {
        // An integer is a number too.
        const auto [e, seen] = run({ { "segmentation_duration", Value{ std::int64_t{ 5 } } } });
        CHECK(text(at(e.payload, "end")) == "segmentation_duration");
        CHECK(e.ref_end == 150);
    }
    {
        // Exact: 29.988 s as 7497/250, and 0.28 s at 25 fps is 7 frames,
        // where the double 0.28 times 25 is above 7.
        const auto [e, seen] = run({ { "duration", Value{ 29.988 } } });
        REQUIRE(seen.size() == 1);
        CHECK(av_cmp_q(seen[0], AVRational{ 7497, 250 }) == 0);
        CHECK(e.ref_end == 200);   // past the reference's last frame: its end
        const auto [f, seen_f] = run({ { "duration", Value{ 0.28 } } });
        CHECK(0.28 * 25 > 7.0);
        REQUIRE(seen_f.size() == 1);
        CHECK(av_cmp_q(seen_f[0], AVRational{ 7, 25 }) == 0);
        CHECK(f.ref_end == 25 + 7);
        // A zero duration ends where the break starts, without the axis,
        // which is given positive durations only (FrameAfter); the break of
        // no frames is kept, with a warning (D-110 (c)).
        std::vector<AVRational> seen_g;
        const Breaks g =
            timeline::pair_breaks({ cue(kTypeCueOut, 0, 1, 10, 25, { { "duration", Value{ 0.0 } } }) },
                                  grid_25(200, &seen_g));
        REQUIRE(g.events.size() == 1);
        CHECK(g.events[0].ref_end == 25);
        CHECK(text(at(g.events[0].payload, "end")) == "duration");
        CHECK(seen_g.empty());
        CHECK(g.warnings.size() == 1);
        // INT_MAX s, the largest duration an AVRational holds; and a decimal
        // of 19 places whose reduced fraction one holds.
        const auto [h, seen_h] = run({ { "duration", Value{ 2147483647.0 } } });
        REQUIRE(seen_h.size() == 1);
        CHECK(av_cmp_q(seen_h[0], AVRational{ INT_MAX, 1 }) == 0);
        CHECK(h.ref_end == 200);
        const auto [k, seen_k] = run({ { "duration", Value{ 0.0000000006103515625 } } });
        REQUIRE(seen_k.size() == 1);
        CHECK(av_cmp_q(seen_k[0], AVRational{ 1, 1638400000 }) == 0);
        CHECK(k.ref_end == 26);   // the first frame after its start
    }
    {
        // A negative or not finite duration is passed over, and kept.
        const auto [e, seen] = run({ { "duration", Value{ -1.0 } }, { "planned_duration", Value{ 2.0 } } });
        CHECK(text(at(e.payload, "end")) == "planned_duration");
        CHECK(object(at(e.payload, "durations")).size() == 2);
    }
}

TEST_CASE("playlist_events: pair_breaks makes no break it can't place, and says why",
          "[timeline][playlist_events]")
{
    using timeline::kTypeCueIn;
    using timeline::kTypeCueOut;
    using timeline::kTypeCueOutCont;
    const Payload two_s = { { "duration", Value{ 2.0 } } };

    // Neither a cue-in nor a declared duration.
    Breaks b = timeline::pair_breaks({ cue(kTypeCueOut, 1, 3, 14, 75) }, grid_25());
    CHECK(b.events.empty());
    CHECK(b.warnings == std::vector<std::string>{
        "variant 1: no ad_break for the cue_out at line 14: no cue_in follows it, and it declares no "
        "duration (D-076)" });

    // Breaks don't nest, the owner's answer to Q46 (D-087): a cue-out that
    // another follows before any cue-in ends at its declared duration, or
    // makes no break, and the second pairs with the next cue-in.
    b = timeline::pair_breaks({ cue(kTypeCueOut, 0, 1, 10, 25, two_s), cue(kTypeCueOut, 0, 2, 12, 50),
                                cue(kTypeCueIn, 0, 4, 16, 100) },
                              grid_25());
    CHECK(b.warnings.empty());
    REQUIRE(b.events.size() == 2);
    CHECK(b.events[0].ref_index == 25);
    CHECK(b.events[0].ref_end == 75);
    CHECK(text(at(b.events[0].payload, "end")) == "duration");
    CHECK(b.events[1].ref_index == 50);
    CHECK(b.events[1].ref_end == 100);
    b = timeline::pair_breaks({ cue(kTypeCueOut, 0, 1, 10, 25), cue(kTypeCueOut, 0, 2, 12, 50, two_s) }, grid_25());
    REQUIRE(b.events.size() == 1);
    CHECK(b.events[0].ref_index == 50);
    CHECK(b.warnings == std::vector<std::string>{
        "variant 0: no ad_break for the cue_out at line 10: the cue_out at line 12 follows it before "
        "any cue_in, and it declares no duration (D-076)" });

    // A cue-in or a cue_out_cont outside a break, and events of other types,
    // make nothing; a cue-in before a cue-out in its segment ends no break.
    Event disc = cue("discontinuity", 0, 1, 9, 25);
    disc.source = Source::Playlist;
    b = timeline::pair_breaks({ cue(kTypeCueIn, 0, 1, 9, 25), cue(kTypeCueOutCont, 0, 1, 10, 25), disc,
                                cue(kTypeCueIn, 0, 2, 12, 50), cue(kTypeCueOut, 0, 2, 13, 50),
                                cue(kTypeCueIn, 0, 3, 15, 75) },
                              grid_25());
    CHECK(b.warnings.empty());
    REQUIRE(b.events.size() == 1);
    CHECK(b.events[0].ref_index == 50);
    CHECK(b.events[0].ref_end == 75);
    CHECK(integer(at(b.events[0].payload, "cue_out_line")) == 13);
    CHECK(list(at(b.events[0].payload, "cue_out_cont_durations")).empty());

    // A cue the build couldn't place still takes its turn: the cue-out
    // doesn't pair with the next break's cue-in.
    b = timeline::pair_breaks({ cue(kTypeCueOut, 0, 1, 10, 25, two_s), cue(kTypeCueIn, 0, 2, 12, kUnplaced),
                                cue(kTypeCueOut, 0, 4, 16, 100), cue(kTypeCueIn, 0, 6, 20, 150) },
                              grid_25());
    REQUIRE(b.events.size() == 1);
    CHECK(b.events[0].ref_index == 100);
    CHECK(b.warnings == std::vector<std::string>{
        "variant 0: no ad_break for the cue_out at line 10: the cue_in at line 12, which ends it, has no "
        "reference frame (design §5.1)" });
    b = timeline::pair_breaks({ cue(kTypeCueOut, 0, 1, 10, kUnplaced), cue(kTypeCueIn, 0, 2, 12, 50) }, grid_25());
    CHECK(b.events.empty());
    CHECK(b.warnings == std::vector<std::string>{
        "variant 0: no ad_break for the cue_out at line 10: it has no reference frame (design §5.1)" });
    b = timeline::pair_breaks({ cue(kTypeCueOut, 0, 1, 10, kUnplaced, two_s) }, grid_25());
    CHECK(b.events.empty());
    CHECK(b.warnings.size() == 1);

    // An axis that can't place the end, or one that breaks FrameAfter's
    // contract by ending a break before it starts.
    b = timeline::pair_breaks({ cue(kTypeCueOut, 2, 1, 10, 25, two_s) },
                              [](std::int64_t, AVRational) { return std::optional<std::int64_t>{}; });
    CHECK(b.events.empty());
    CHECK(b.warnings == std::vector<std::string>{
        "variant 2: no ad_break for the cue_out at line 10: the axis gives no reference frame for the end "
        "of its duration of 2 s" });
    b = timeline::pair_breaks({ cue(kTypeCueOut, 2, 1, 10, 25, two_s) },
                              [](std::int64_t, AVRational) { return std::optional<std::int64_t>{ 10 }; });
    CHECK(b.events.empty());
    CHECK(b.warnings == std::vector<std::string>{
        "variant 2: no ad_break for the cue_out at line 10: it would end at reference frame 10, before its "
        "start at 25" });

    // A declared duration that no AVRational holds exactly makes no break,
    // and the axis isn't asked for it: a numerator above INT_MAX, beyond an
    // int64 or not, or a denominator above INT_MAX once reduced. Each is the
    // value of an EXT-X-CUE-OUT that no cue-in follows, in segment 1, so at
    // reference frame 25.
    const std::vector<std::pair<std::string, std::string>> inexact = {
        { "99999999999999999999999", "1e+23" },
        { "3000000000", "3e+09" },
        { "12345678901.5", "12345678901.5" },
        { "2147483648", "2147483648" },
        { "0.0000000001", "1e-10" },
    };
    for (const auto& [declared, shown] : inexact) {
        INFO(declared);
        const PlaylistEvents p =
            timeline::parse_playlist_events(vod({ "", "#EXT-X-CUE-OUT:" + declared + "\n" }), kBase, 0);
        REQUIRE(p.error.empty());
        REQUIRE(p.cues.size() == 1);
        CHECK_FALSE(has(p.cues[0].payload, "error"));
        std::vector<AVRational> seen;
        b = timeline::pair_breaks(placed(p.cues), grid_25(200, &seen));
        CHECK(b.events.empty());
        CHECK(seen.empty());
        CHECK(b.warnings == std::vector<std::string>{ std::format(
            "variant 0: no ad_break for the cue_out at line 6: no AVRational holds its duration of {} s "
            "exactly, so the axis isn't asked for its end", shown) });
    }

    // A cue without a line or a segment is named as it can be.
    Event lineless = cue(kTypeCueOut, 0, 7, 0, 175);
    lineless.payload.clear();
    b = timeline::pair_breaks({ lineless }, grid_25());
    CHECK(b.warnings == std::vector<std::string>{
        "variant 0: no ad_break for the cue_out of segment 7: no cue_in follows it, and it declares no "
        "duration (D-076)" });
}

TEST_CASE("playlist_events: pair_breaks pairs cues from a stub decoder with the plain ones",
          "[timeline][playlist_events]")
{
    // A message-only cue-out (an OATCLS without a CUE-OUT) that a plain
    // EXT-X-CUE-IN ends; a DATERANGE's SCTE35-OUT without a cue-in, whose
    // break ends at its PLANNED-DURATION before the message's
    // break_duration (D-076); and a plain break after both.
    const std::string oatcls = "/DARAAAAAAAAAP/wAAAAAAAAAAA=";
    const std::string playlist = vod(
        { "", "#EXT-OATCLS-SCTE35:" + oatcls + "\n", "", "#EXT-X-CUE-IN\n",
          "#EXT-X-DATERANGE:ID=\"b2\",START-DATE=\"2026-03-01T12:00:04Z\",PLANNED-DURATION=1.5,"
          "SCTE35-OUT=0xFC3011\n",
          "", "#EXT-X-CUE-OUT:1\n", "#EXT-X-CUE-IN\n" },
        "#EXT-X-PROGRAM-DATE-TIME:2026-03-01T12:00:00Z\n");
    const PlaylistEvents p = timeline::parse_playlist_events(playlist, kBase, 1);
    REQUIRE(p.error.empty());
    REQUIRE(p.messages.size() == 2);
    REQUIRE(p.cues.size() == 3);

    // The stub: what the decoder would say of each message, by its text, as
    // a cue at its tag with the message's break_duration; a DATERANGE's
    // durations come from its tag, which the anchor's line finds.
    const std::map<std::string, std::pair<std::string_view, double>> decoded = {
        { oatcls, { timeline::kTypeCueOut, 2.0 } },
        { "0xFC3011", { timeline::kTypeCueOut, 1.0 } },
    };
    std::vector<Event> stub;
    for (const CueInput& m : p.messages) {
        const auto it = decoded.find(bytes_text(m));
        REQUIRE(it != decoded.end());
        const PlaylistAnchor& at_tag = anchor(m);
        Payload more = { { "form", Value{ m.form } } };
        for (const timeline::PlaylistTag& t : p.tags) {
            if (t.line != at_tag.line || t.name != "EXT-X-DATERANGE") continue;
            std::string err;
            for (const Attribute& a : timeline::parse_attributes(*t.value, err))
                if (a.name == "PLANNED-DURATION") more.emplace_back("planned_duration", Value{ std::stod(a.value) });
        }
        more.emplace_back("break_duration", Value{ it->second.second });
        stub.push_back(cue(it->second.first, at_tag.variant, at_tag.segment, at_tag.line,
                           at_tag.segment * 25, std::move(more)));
    }

    // Plain cues first, the decoder's after them: play order is restored.
    std::vector<Event> all = placed(p.cues);
    all.insert(all.end(), stub.begin(), stub.end());
    const Breaks b = timeline::pair_breaks(all, grid_25());
    CHECK(b.warnings.empty());
    REQUIRE(b.events.size() == 3);

    CHECK(b.events[0].ref_index == 25);
    CHECK(b.events[0].ref_end == 75);
    CHECK(text(at(b.events[0].payload, "end")) == "cue_in");
    CHECK(integer(at(b.events[0].payload, "cue_out_line")) == anchor(p.messages[0]).line);
    CHECK(integer(at(b.events[0].payload, "cue_in_line")) == line_of(p.cues[0]));
    const Value::Object& d0 = object(at(b.events[0].payload, "durations"));
    REQUIRE(d0.size() == 1);
    CHECK(d0[0].first == "break_duration");
    CHECK(number(d0[0].second) == 2.0);

    // The DATERANGE's break: 1.5 s from frame 100, 37.5 frames, so to frame 138.
    CHECK(b.events[1].ref_index == 100);
    CHECK(b.events[1].ref_end == 138);
    CHECK(text(at(b.events[1].payload, "end")) == "planned_duration");
    const Value::Object& d = object(at(b.events[1].payload, "durations"));
    REQUIRE(d.size() == 2);
    CHECK(d[0].first == "planned_duration");
    CHECK(number(d[0].second) == 1.5);
    CHECK(d[1].first == "break_duration");
    CHECK(number(d[1].second) == 1.0);

    CHECK(b.events[2].ref_index == 150);
    CHECK(b.events[2].ref_end == 175);
    CHECK(text(at(b.events[2].payload, "end")) == "cue_in");
}

TEST_CASE("playlist_events: pair_breaks takes a duration's 90 kHz ticks before its seconds (D-109 (d))",
          "[timeline][playlist_events]")
{
    using timeline::kTypeCueOut;
    const auto ticks = [](std::int64_t t) { return Value{ t }; };
    const auto run = [](Payload durations) {
        std::vector<AVRational> seen;
        const Breaks b = timeline::pair_breaks({ cue(kTypeCueOut, 0, 1, 10, 25, std::move(durations)) },
                                               grid_25(200, &seen));
        REQUIRE(b.warnings.empty());
        REQUIRE(b.events.size() == 1);
        return std::pair{ b.events[0], seen };
    };
    CHECK(timeline::kDurationClock == 90000);
    CHECK(timeline::kTicksSuffix == "_ticks");
    {
        // 180001 ticks, which 9 doesn't divide, so that no decimal of seconds
        // holds them: 50 frames and a tick at 25 fps, ending at the 51st
        // frame after the start. The ticks are kept beside their seconds.
        const auto [e, seen] = run({ { "break_duration", Value{ 180001.0 / 90000 } },
                                     { "break_duration_ticks", ticks(180001) } });
        REQUIRE(seen.size() == 1);
        CHECK(av_cmp_q(seen[0], AVRational{ 180001, 90000 }) == 0);
        CHECK(e.ref_end == 25 + 51);
        CHECK(text(at(e.payload, "end")) == "break_duration");
        const Value::Object& d = object(at(e.payload, "durations"));
        REQUIRE(d.size() == 2);
        CHECK(d[0].first == "break_duration");
        CHECK(d[1].first == "break_duration_ticks");
        CHECK(integer(d[1].second) == 180001);
    }
    {
        // The ticks first, whatever the seconds say, and on their own too;
        // a segmentation_duration of 150000 ticks, 50 frames at 30 fps, is
        // 5/3 s, 41 frames and two thirds at 25 fps.
        const auto [e, seen] = run({ { "break_duration", Value{ 3.0 } },
                                     { "break_duration_ticks", ticks(90000) } });
        REQUIRE(seen.size() == 1);
        CHECK(av_cmp_q(seen[0], AVRational{ 1, 1 }) == 0);
        CHECK(e.ref_end == 50);
        const auto [f, seen_f] = run({ { "segmentation_duration_ticks", ticks(150000) } });
        REQUIRE(seen_f.size() == 1);
        CHECK(av_cmp_q(seen_f[0], AVRational{ 5, 3 }) == 0);
        CHECK(f.ref_end == 25 + 42);
        CHECK(text(at(f.payload, "end")) == "segmentation_duration");
        CHECK(object(at(f.payload, "durations")).size() == 1);
    }
    {
        // A duration in seconds alone, or with null ticks, or ticks that
        // aren't a whole number from 0, is taken by its seconds, as before.
        const auto [e, seen] = run({ { "break_duration", Value{ 2.0 } } });
        REQUIRE(seen.size() == 1);
        CHECK(av_cmp_q(seen[0], AVRational{ 2, 1 }) == 0);
        CHECK(e.ref_end == 75);
        const auto [f, seen_f] = run({ { "break_duration", Value{ 2.0 } },
                                       { "break_duration_ticks", Value{} } });
        CHECK(f.ref_end == 75);
        CHECK(object(at(f.payload, "durations")).size() == 1);
        const auto [g, seen_g] = run({ { "segmentation_duration", Value{ 1.0 } },
                                       { "segmentation_duration_ticks", ticks(-1) } });
        REQUIRE(seen_g.size() == 1);
        CHECK(av_cmp_q(seen_g[0], AVRational{ 1, 1 }) == 0);
        CHECK(g.ref_end == 50);
        const auto [h, seen_h] = run({ { "break_duration", Value{ 1.0 } },
                                       { "break_duration_ticks", Value{ 90000.0 } } });
        CHECK(av_cmp_q(seen_h[0], AVRational{ 1, 1 }) == 0);
        CHECK(h.ref_end == 50);
    }
    {
        // D-076's order holds over the ticks: the tag's own duration first.
        const auto [e, seen] = run({ { "duration", Value{ 1.0 } }, { "break_duration", Value{ 2.0 } },
                                     { "break_duration_ticks", ticks(180000) } });
        CHECK(text(at(e.payload, "end")) == "duration");
        CHECK(e.ref_end == 50);
        CHECK(object(at(e.payload, "durations")).size() == 3);
    }
    // The seconds of 180001 ticks alone make no break, as before; nor do
    // ticks that no AVRational holds, a 40-bit segmentation_duration whose
    // numerator stays above INT_MAX once reduced.
    Breaks b = timeline::pair_breaks({ cue(kTypeCueOut, 0, 1, 10, 25, { { "break_duration",
                                                                           Value{ 180001.0 / 90000 } } }) },
                                     grid_25());
    CHECK(b.events.empty());
    CHECK(b.warnings == std::vector<std::string>{
        "variant 0: no ad_break for the cue_out at line 10: no AVRational holds its break_duration of "
        "2.000011111111111 s exactly, so the axis isn't asked for its end" });
    const std::int64_t widest = (std::int64_t{ 1 } << 40) - 1;
    std::vector<AVRational> seen;
    b = timeline::pair_breaks(
        { cue(kTypeCueOut, 0, 1, 10, 25, { { "segmentation_duration_ticks", ticks(widest) } }) },
        grid_25(200, &seen));
    CHECK(b.events.empty());
    CHECK(seen.empty());
    CHECK(b.warnings == std::vector<std::string>{
        "variant 0: no ad_break for the cue_out at line 10: no AVRational holds its "
        "segmentation_duration of 1099511627775 ticks (90 kHz) exactly, so the axis isn't asked for its "
        "end" });
    // A cue-in still ends a break before any duration, ticks or not.
    b = timeline::pair_breaks({ cue(kTypeCueOut, 0, 1, 10, 25, { { "break_duration_ticks",
                                                                  ticks(180001) } }),
                                cue(timeline::kTypeCueIn, 0, 2, 12, 50) },
                              grid_25());
    REQUIRE(b.events.size() == 1);
    CHECK(b.events[0].ref_end == 50);
    CHECK(text(at(b.events[0].payload, "end")) == "cue_in");
}

TEST_CASE("playlist_events: pair_breaks ends a cue_out that holds its tag's cue_in at its declared "
          "duration (D-109 (b)), and one that declares none pairs as any cue_out (D-110 (a))",
          "[timeline][playlist_events]")
{
    using timeline::kTypeCueIn;
    using timeline::kTypeCueOut;
    using timeline::kTypeCueOutCont;
    const std::string held(timeline::kHeldCueInLine);
    CHECK(held == "held_cue_in_line");

    // The cue_out of an EXT-X-DATERANGE at line 14 that holds its SCTE35-IN:
    // its break ends at its DURATION, with the CUE-OUT-CONT after it; the
    // cue_in after it ends nothing, and makes no break of its own.
    const Value line_14{ std::int64_t{ 14 } };
    Breaks b = timeline::pair_breaks(
        { cue(kTypeCueOut, 0, 3, 14, 75, { { "duration", Value{ 2.0 } }, { held, line_14 } }),
          cue(kTypeCueOutCont, 0, 4, 17, 100, { { "duration", Value{ 2.0 } } }),
          cue(kTypeCueIn, 0, 6, 23, 150) },
        grid_25());
    CHECK(b.warnings.empty());
    REQUIRE(b.events.size() == 1);
    CHECK(b.events[0].ref_index == 75);
    CHECK(b.events[0].ref_end == 125);
    CHECK(text(at(b.events[0].payload, "end")) == "duration");
    CHECK(integer(at(b.events[0].payload, "cue_out_line")) == 14);
    CHECK(integer(at(b.events[0].payload, "cue_in_line")) == 14);
    REQUIRE(list(at(b.events[0].payload, "cue_out_cont_durations")).size() == 1);
    CHECK(number(list(at(b.events[0].payload, "cue_out_cont_durations"))[0]) == 2.0);

    // By D-076's order: its END-DATE where it has no DURATION.
    b = timeline::pair_breaks({ cue(kTypeCueOut, 0, 3, 14, 75, { { "duration", Value{} },
                                                                  { "end_date_duration", Value{ 1.0 } },
                                                                  { held, line_14 } }) },
                              grid_25());
    REQUIRE(b.events.size() == 1);
    CHECK(b.events[0].ref_end == 100);
    CHECK(text(at(b.events[0].payload, "end")) == "end_date_duration");

    // Breaks still don't nest (D-087): a cue_out after it ends it at its
    // duration, and pairs with the next cue_in.
    b = timeline::pair_breaks({ cue(kTypeCueOut, 0, 1, 10, 25, { { "duration", Value{ 3.0 } },
                                                                  { held, Value{ std::int64_t{ 10 } } } }),
                                cue(kTypeCueOut, 0, 2, 12, 50), cue(kTypeCueIn, 0, 4, 16, 100) },
                              grid_25());
    CHECK(b.warnings.empty());
    REQUIRE(b.events.size() == 2);
    CHECK((b.events[0].ref_index == 25 && b.events[0].ref_end == 100));
    CHECK(text(at(b.events[0].payload, "end")) == "duration");
    CHECK((b.events[1].ref_index == 50 && b.events[1].ref_end == 100));
    CHECK(text(at(b.events[1].payload, "end")) == "cue_in");

    // A cue_out with the member that declares no duration, which the decoder
    // never gives (D-110 (a)), holds nothing: the cue_in after it ends it, as
    // it ends any cue_out's break; without one it makes no break, and says
    // why, as a cue_out that declares nothing does.
    b = timeline::pair_breaks({ cue(kTypeCueOut, 1, 3, 14, 75, { { held, line_14 } }),
                                cue(kTypeCueIn, 1, 5, 21, 125) },
                              grid_25());
    CHECK(b.warnings.empty());
    REQUIRE(b.events.size() == 1);
    CHECK((b.events[0].ref_index == 75 && b.events[0].ref_end == 125));
    CHECK(text(at(b.events[0].payload, "end")) == "cue_in");
    CHECK(integer(at(b.events[0].payload, "cue_in_line")) == 21);
    b = timeline::pair_breaks(
        { cue(kTypeCueOut, 1, 3, 14, 75, { { "duration", Value{} }, { held, line_14 } }) }, grid_25());
    CHECK(b.events.empty());
    CHECK(b.warnings == std::vector<std::string>{
        "variant 1: no ad_break for the cue_out at line 14: no cue_in follows it, and it declares no "
        "duration (D-076)" });

    // One that declares 0 s ends where it starts, the cue_in after it ending
    // nothing: a break of no frames, kept with a warning (D-110 (c)).
    b = timeline::pair_breaks(
        { cue(kTypeCueOut, 0, 3, 14, 75, { { "duration", Value{ 0.0 } }, { held, line_14 } }),
          cue(kTypeCueIn, 0, 5, 21, 125) },
        grid_25());
    REQUIRE(b.events.size() == 1);
    CHECK((b.events[0].ref_index == 75 && b.events[0].ref_end == 75));
    CHECK(text(at(b.events[0].payload, "end")) == "duration");
    CHECK(integer(at(b.events[0].payload, "cue_in_line")) == 14);
    CHECK(b.warnings == std::vector<std::string>{
        "variant 0: the ad_break of the cue_out at line 14 has no frames, its duration of 0 s ending it at "
        "reference frame 75, where it starts: a drop that starts at that frame doesn't list it (D-078, "
        "D-110 (c))" });
}

TEST_CASE("playlist_events: pair_breaks keeps an ad_break of no frames, with a warning (D-110 (c))",
          "[timeline][playlist_events]")
{
    // A cue-out and a cue-in of one segment, two plain tags: a break of no
    // frames at its segment's first frame, ended by the cue-in.
    const PlaylistEvents p =
        timeline::parse_playlist_events(vod({ "", "", "", "#EXT-X-CUE-OUT\n#EXT-X-CUE-IN\n" }), kBase, 2);
    REQUIRE(p.error.empty());
    REQUIRE(p.cues.size() == 2);
    Breaks b = timeline::pair_breaks(placed(p.cues), grid_25());
    REQUIRE(b.events.size() == 1);
    const Event& empty = b.events[0];
    CHECK((empty.variant == 2 && empty.ref_index == 75 && empty.ref_end == 75));
    CHECK(text(at(empty.payload, "end")) == "cue_in");
    CHECK(integer(at(empty.payload, "cue_out_line")) == 10);
    CHECK(integer(at(empty.payload, "cue_in_line")) == 11);
    CHECK(b.warnings == std::vector<std::string>{
        "variant 2: the ad_break of the cue_out at line 10 has no frames, the cue_in at line 11 ending it "
        "at reference frame 75, where it starts: a drop that starts at that frame doesn't list it (D-078, "
        "D-110 (c))" });
    // What the warning says: a drop that starts at its frame doesn't meet it
    // (D-078), nor one that ends there; one around its frame does.
    CHECK_FALSE(timeline::meets(empty, { .ref_index = 75, .ref_end = 100 }));
    CHECK_FALSE(timeline::meets(empty, { .ref_index = 50, .ref_end = 75 }));
    CHECK(timeline::meets(empty, { .ref_index = 50, .ref_end = 100 }));

    // A declared duration of 0, without a cue-in, in each of D-076's forms:
    // EXT-X-CUE-OUT:0, DURATION=0, an END-DATE at its START-DATE and a
    // break_duration of 0 ticks; each a break of no frames with its warning,
    // which names its cue_out's line and how it ended.
    const std::vector<std::pair<Payload, std::string>> zero = {
        { { { "duration", Value{ 0.0 } } }, "its duration of 0 s" },
        { { { "end_date_duration", Value{ 0.0 } } }, "its end_date_duration of 0 s" },
        { { { "break_duration", Value{ 0.0 } }, { "break_duration_ticks", Value{ std::int64_t{ 0 } } } },
          "its break_duration of 0 ticks (90 kHz)" },
    };
    for (const auto& [durations, how] : zero) {
        INFO(how);
        b = timeline::pair_breaks({ cue(timeline::kTypeCueOut, 1, 3, 14, 75, durations) }, grid_25());
        REQUIRE(b.events.size() == 1);
        CHECK((b.events[0].ref_index == 75 && b.events[0].ref_end == 75));
        CHECK(b.warnings == std::vector<std::string>{ std::format(
                  "variant 1: the ad_break of the cue_out at line 14 has no frames, {} ending it at "
                  "reference frame 75, where it starts: a drop that starts at that frame doesn't list it "
                  "(D-078, D-110 (c))", how) });
    }
    const PlaylistEvents q = timeline::parse_playlist_events(vod({ "", "#EXT-X-CUE-OUT:0\n" }), kBase, 0);
    REQUIRE(q.cues.size() == 1);
    b = timeline::pair_breaks(placed(q.cues), grid_25());
    REQUIRE(b.events.size() == 1);
    CHECK((b.events[0].ref_index == 25 && b.events[0].ref_end == 25));
    CHECK(b.warnings == std::vector<std::string>{
        "variant 0: the ad_break of the cue_out at line 6 has no frames, its duration of 0 s ending it at "
        "reference frame 25, where it starts: a drop that starts at that frame doesn't list it (D-078, "
        "D-110 (c))" });

    // A break of frames has no such warning.
    b = timeline::pair_breaks(
        { cue(timeline::kTypeCueOut, 1, 3, 14, 75), cue(timeline::kTypeCueIn, 1, 4, 17, 100) }, grid_25());
    REQUIRE(b.events.size() == 1);
    CHECK(b.warnings.empty());
}
