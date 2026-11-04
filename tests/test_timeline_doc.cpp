// timeline::to_json(), from_json() and write_document() (src/TimelineDoc.h):
// the timeline document of DECISIONS.md D-072, its ladder part, on
// timelines the test builds with the round-1 modules from inputs of its own
// in the shape of testgen's ad ladder (eval/phase3-survey/testgen.md §2.3):
// three rungs of eight 25-frame segments at 25 fps, the reference v360 and
// the pairs v270 and v180, the ad in segments 3 and 4 restarting its
// timestamps at the main content's origin, v180's ad scored low. The
// writer's output is checked against docs/schema/mirage-timeline-1.schema.json
// by a subset JSON Schema validator written here (the design's P-AI); then a
// reader round trip, byte-identical output, whole numbers written as integer
// literals in fields the schema types "number" and read back with
// number_of(), detection: null when no detection ran, the reader's refusals,
// an unknown schema_version among them, what the writer writes without
// checking and the reader refuses, the axis's stretches, those of the
// 24000/1001 and 60000/1001 rates among them, the reference the axis is built
// on, and the writing through a temporary file.

#include <catch2/catch_test_macros.hpp>

#include "FileHash.h"
#include "JsonRead.h"
#include "JsonText.h"
#include "LadderReportRead.h"
#include "PlaylistEvents.h"
#include "TestTemp.h"
#include "TimelineAxis.h"
#include "TimelineBuild.h"
#include "TimelineDetect.h"
#include "TimelineDoc.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

using timeline::Attribution;
using timeline::CurveRef;
using timeline::Event;
using timeline::Found;
using timeline::Source;
using timeline::Value;

namespace {

// ── JSON trees ───────────────────────────────────────────────────────────────

json::Value parsed(std::string_view text)
{
    json::Error e;
    std::optional<json::Value> v = json::parse(text, e);
    INFO(json::error_text(e));
    REQUIRE(v);
    return std::move(*v);
}

// A tree as compact JSON text, numbers as json::number writes a double.
std::string text_of(const json::Value& v)
{
    if (v.is_null()) return "null";
    if (const bool* b = v.boolean()) return *b ? "true" : "false";
    if (const std::int64_t* i = v.integer()) return std::to_string(*i);
    if (const std::string* s = v.string()) return json::quote(*s);
    if (const json::Value::Array* a = v.array()) {
        std::string out = "[";
        for (std::size_t i = 0; i < a->size(); ++i) out += (i == 0 ? "" : ",") + text_of((*a)[i]);
        return out + "]";
    }
    if (const json::Value::Object* o = v.object()) {
        std::string out = "{";
        for (std::size_t i = 0; i < o->size(); ++i)
            out += (i == 0 ? "" : ",") + json::quote((*o)[i].first) + ":" + text_of((*o)[i].second);
        return out + "}";
    }
    return json::number(*v.number());
}

// The value at `path` in `root`, as "timeline[3].payload.line" names it;
// nullptr when there is none.
json::Value* walk(json::Value& root, std::string_view path)
{
    json::Value* v = &root;
    std::size_t  i = 0;
    while (v && i < path.size()) {
        if (path[i] == '.') {
            ++i;
        } else if (path[i] == '[') {
            const std::size_t close = path.find(']', i);
            const std::size_t n     = std::stoul(std::string(path.substr(i + 1, close - i - 1)));
            auto* a = std::get_if<json::Value::Array>(&v->v);
            v = a && n < a->size() ? &(*a)[n] : nullptr;
            i = close + 1;
        } else {
            std::size_t end = path.find_first_of(".[", i);
            if (end == std::string_view::npos) end = path.size();
            const std::string_view key = path.substr(i, end - i);
            json::Value* next = nullptr;
            if (auto* o = std::get_if<json::Value::Object>(&v->v))
                for (auto& [k, x] : *o)
                    if (k == key) next = &x;
            v = next;
            i = end;
        }
    }
    return v;
}

json::Value& at(json::Value& root, std::string_view path)
{
    json::Value* v = walk(root, path);
    INFO(path);
    REQUIRE(v);
    return *v;
}

// Takes member `key` out of the object at `path`.
void erase(json::Value& root, std::string_view path, std::string_view key)
{
    auto& o = std::get<json::Value::Object>(at(root, path).v);
    const auto it = std::find_if(o.begin(), o.end(), [&](const auto& m) { return m.first == key; });
    REQUIRE(it != o.end());
    o.erase(it);
}

json::Value integer_value(std::int64_t n) { return json::Value{ n }; }
json::Value string_value(std::string s) { return json::Value{ std::move(s) }; }

// ── The subset JSON Schema validator (the design's P-AI) ─────────────────────

// The keywords of JSON Schema draft 2020-12
// (https://json-schema.org/draft/2020-12/json-schema-validation) that the
// subset checks: type, enum, required, properties, additionalProperties and
// items, as the draft defines each; and the annotations $schema, title and
// description, which check nothing. Any other keyword is an error, so that
// no part of a schema file goes unchecked. One departure from the draft:
// "integer" is an integer literal, as src/JsonRead.h keeps one, where the
// draft also takes a number whose fraction is zero (1.0); to_json() writes
// every integer as a literal and every double through json::number.
constexpr std::string_view kAnnotations[] = { "$schema", "title", "description" };
constexpr std::string_view kKeywords[]    = { "type", "enum", "required", "properties",
                                              "additionalProperties", "items" };
constexpr std::string_view kTypeNames[]   = { "null", "boolean", "object", "array",
                                              "number", "string", "integer" };

template <typename List>
bool among(const List& list, std::string_view s)
{
    return std::find(std::begin(list), std::end(list), s) != std::end(list);
}

bool has_type(const json::Value& v, std::string_view type)
{
    if (type == "null") return v.is_null();
    if (type == "boolean") return v.boolean() != nullptr;
    if (type == "object") return v.object() != nullptr;
    if (type == "array") return v.array() != nullptr;
    if (type == "number") return v.number().has_value();
    if (type == "string") return v.string() != nullptr;
    if (type == "integer") return v.integer() != nullptr;
    return false;
}

// JSON's equality, as enum compares: numbers by value, 1 and 1.0 alike.
bool same(const json::Value& a, const json::Value& b)
{
    if (a.number() && b.number()) {
        if (a.integer() && b.integer()) return *a.integer() == *b.integer();
        return *a.number() == *b.number();
    }
    return a == b;
}

// What is wrong with a schema itself: a node that is neither an object nor a
// boolean, a keyword outside the subset, a keyword's value of another form.
void subset_errors(const json::Value& schema, const std::string& where, std::vector<std::string>& out)
{
    if (schema.boolean()) return;
    const json::Value::Object* o = schema.object();
    if (!o) {
        out.push_back(where + ": a schema is an object or a boolean");
        return;
    }
    for (const auto& [k, x] : *o) {
        const std::string here = where + "." + k;
        if (among(kAnnotations, k)) {
            if (!x.string()) out.push_back(here + ": an annotation is a string");
        } else if (k == "type") {
            std::vector<const json::Value*> names;
            if (x.string()) names.push_back(&x);
            if (const json::Value::Array* a = x.array())
                for (const json::Value& n : *a) names.push_back(&n);
            if (names.empty()) out.push_back(here + ": a type name or a list of them");
            for (const json::Value* n : names)
                if (!n->string() || !among(kTypeNames, *n->string())) out.push_back(here + ": no type name");
        } else if (k == "enum") {
            if (!x.array()) out.push_back(here + ": a list of values");
        } else if (k == "required") {
            const json::Value::Array* a = x.array();
            if (!a) out.push_back(here + ": a list of names");
            else
                for (const json::Value& n : *a)
                    if (!n.string()) out.push_back(here + ": a name is a string");
        } else if (k == "properties") {
            const json::Value::Object* p = x.object();
            if (!p) out.push_back(here + ": an object of schemas");
            else
                for (const auto& [name, s] : *p) subset_errors(s, here + "." + name, out);
        } else if (k == "additionalProperties" || k == "items") {
            subset_errors(x, here, out);
        } else {
            out.push_back(here + ": a keyword outside the subset");
        }
    }
}

// Where `v` fails `schema`, as "timeline[3].t: ..." lines.
void check(const json::Value& schema, const json::Value& v, const std::string& where,
           std::vector<std::string>& out)
{
    if (const bool* b = schema.boolean()) {
        if (!*b) out.push_back(where + ": the schema allows nothing here");
        return;
    }
    const json::Value::Object* keywords = schema.object();
    if (!keywords) {
        out.push_back(where + ": the schema is neither an object nor a boolean");
        return;
    }
    const json::Value* properties = schema.find("properties");
    for (const auto& [k, x] : *keywords) {
        if (among(kAnnotations, k)) continue;
        if (!among(kKeywords, k)) {
            out.push_back(where + ": the schema's keyword " + k + " is outside the subset");
        } else if (k == "type") {
            bool ok = x.string() && has_type(v, *x.string());
            if (const json::Value::Array* a = x.array())
                for (const json::Value& n : *a) ok = ok || (n.string() && has_type(v, *n.string()));
            if (!ok)
                out.push_back(where + ": " + std::string(json::kind_name(v)) + ", not of type " + text_of(x));
        } else if (k == "enum") {
            const json::Value::Array* a = x.array();
            if (!a || std::none_of(a->begin(), a->end(), [&](const json::Value& e) { return same(e, v); }))
                out.push_back(where + ": " + text_of(v) + ", not one of " + text_of(x));
        } else if (k == "required") {
            if (v.object() && x.array())
                for (const json::Value& n : *x.array())
                    if (n.string() && !v.find(*n.string())) out.push_back(where + ": no " + *n.string());
        } else if (k == "properties") {
            if (const json::Value::Object* o = v.object())
                for (const auto& [name, member] : *o)
                    if (const json::Value* s = x.find(name)) check(*s, member, where + "." + name, out);
        } else if (k == "additionalProperties") {
            if (const json::Value::Object* o = v.object())
                for (const auto& [name, member] : *o)
                    if (!properties || !properties->find(name)) check(x, member, where + "." + name, out);
        } else if (k == "items") {
            if (const json::Value::Array* a = v.array())
                for (std::size_t i = 0; i < a->size(); ++i)
                    check(x, (*a)[i], std::format("{}[{}]", where, i), out);
        }
    }
}

std::vector<std::string> failures(const json::Value& schema, std::string_view instance)
{
    std::vector<std::string> out;
    check(schema, parsed(instance), "$", out);
    return out;
}

// The schema file, read from the source tree, which this file's own path
// names: CMake compiles it by its absolute path.
const json::Value& schema_file()
{
    static const json::Value schema = [] {
        const fs::path file = fs::path(__FILE__).parent_path().parent_path() / "docs" / "schema" /
                              "mirage-timeline-1.schema.json";
        std::ifstream in(file, std::ios::binary);
        INFO(file.string());
        REQUIRE(in);
        std::ostringstream text;
        text << in.rdbuf();
        return parsed(text.str());
    }();
    return schema;
}

std::vector<std::string> schema_failures(const std::string& document)
{
    std::vector<std::string> out;
    check(schema_file(), parsed(document), "$", out);
    return out;
}

// ── A ladder's timeline, built with the round-1 modules ──────────────────────

constexpr AVRational   kTs      = { 1, 90000 };   // MPEG-TS's time base
constexpr std::int64_t kOrigin  = 900000;         // 10 s: where testgen's main content starts
constexpr std::int64_t kFrame25 = 3600;           // a frame at 25 fps, in 1/90000
constexpr std::int64_t kFrames  = 200;            // eight segments of 25 frames
constexpr const char*  kRungs[] = { "v360", "v270", "v180" };

// testgen's ad playlist (eval/phase3-survey/testgen.md §2.3), the same text
// in every rung: the ad in segments 3 and 4, between two EXT-X-DISCONTINUITY
// tags, with the plain cue tags (D-069).
const std::string kAdPlaylist =
    "#EXTM3U\n"
    "#EXT-X-VERSION:3\n"
    "#EXT-X-TARGETDURATION:1\n"
    "#EXT-X-MEDIA-SEQUENCE:0\n"
    "#EXT-X-DISCONTINUITY-SEQUENCE:0\n"
    "#EXT-X-PLAYLIST-TYPE:VOD\n"
    "#EXTINF:1.000000,\nmain_000.ts\n"
    "#EXTINF:1.000000,\nmain_001.ts\n"
    "#EXTINF:1.000000,\nmain_002.ts\n"
    "#EXT-X-DISCONTINUITY\n"
    "#EXT-X-CUE-OUT:2.000\n"
    "#EXTINF:1.000000,\nad_000.ts\n"
    "#EXT-X-CUE-OUT-CONT:1.000/2.000\n"
    "#EXTINF:1.000000,\nad_001.ts\n"
    "#EXT-X-DISCONTINUITY\n"
    "#EXT-X-CUE-IN\n"
    "#EXTINF:1.000000,\nmain_005.ts\n"
    "#EXTINF:1.000000,\nmain_006.ts\n"
    "#EXTINF:1.000000,\nmain_007.ts\n"
    "#EXT-X-ENDLIST\n";

std::string url_of(int variant)
{
    return std::format("http://test.invalid/ladder/{}/index.m3u8", kRungs[variant]);
}

// The reference's PTS: the main content from 10 s, the ad's 50 frames from
// 10 s again, the main content back at 15 s, where it would be without the ad.
std::int64_t ref_pts(std::int64_t i)
{
    return i < 75 || i >= 125 ? kOrigin + i * kFrame25 : kOrigin + (i - 75) * kFrame25;
}

// A pair's score at frame i: v270 (pair 0) near 97, with the one-point dip
// of the first pair of the ad's libvmaf context (D-037); v180 (pair 1) near
// 91, its ad near 25.
double score(int pair, std::int64_t i)
{
    const double wobble = 0.25 * static_cast<double>(i % 4);
    if (pair == 0) return i == 75 ? 80.0 : 97.0 + wobble;
    return i >= 75 && i < 125 ? 25.0 + wobble : 91.0 + wobble;
}

// The ladder report as timeline::read_ladder_report() would give it: the
// master's three entries, the reference v360 and two complete pairs that pair
// every frame, the reference's sequences as the aligner finds them
// (src/VMAFAlign.h): a new one at the ad's step back, none at its 3.04 s step
// forward at frame 125.
timeline::LadderScores ad_report()
{
    timeline::LadderScores s;
    s.status                  = "complete";
    s.reference_mode          = "top-rung";
    s.model                   = "vmaf_v0.6.1";
    s.model_source            = "built-in";
    s.scaler                  = "bicubic";
    s.libvmaf_version         = "1.2.3";
    s.libvmaf_package_version = "1.2.4";
    s.master_url              = "http://test.invalid/ladder/master.m3u8";
    s.master_location         = s.master_url;
    s.master_sha256           = filehash::sha256_hex("the master's bytes");
    const std::int64_t bandwidth[] = { 1000000, 600000, 350000 };
    const char* const  resolution[] = { "640x360", "480x270", "320x180" };
    for (int k = 0; k < 3; ++k) {
        timeline::MasterEntry e;
        e.index      = static_cast<std::size_t>(k);
        e.line       = static_cast<std::size_t>(3 + 2 * k);
        e.uri        = std::format("{}/index.m3u8", kRungs[k]);
        e.url        = url_of(k);
        e.bandwidth  = bandwidth[k];
        e.resolution = resolution[k];
        e.codecs     = "avc1.64001e";
        e.video      = true;
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
            p.vmaf.push_back(score(pair, i));
        }
        p.context_first = { 0, 75 };
        p.ref_sequences = { { .first_index = 0, .first_pts = kOrigin, .frames = 75 },
                            { .first_index = 75, .first_pts = kOrigin, .frames = 125 } };
        p.dis_sequences = p.ref_sequences;
        s.pairs.push_back(std::move(p));
    }
    s.n_threads   = 4;
    s.n_subsample = 1;
    return s;
}

// What a timeline document is written from.
struct Built {
    timeline::LadderScores   scores;
    timeline::Timeline       timeline;
    std::vector<Attribution> attributions;
    timeline::DocInfo        info;
};

struct Shape {
    bool detect = true;                 // W and N given (D-077)
    bool reference_refused = false;     // the reference's segment map refused (D-081 (c))
    timeline::QueryOptions overlap;     // the attributions' margins (D-078)
};

constexpr std::string_view kRefusal = "the PTS of reference frame 40 isn't the report's";

// The ad ladder's timeline, placed as the timeline build places events
// (eval/phase3-survey/design.md §5.1) where every pair pairs each frame with
// the reference frame of its own index: a segment-bound event of any variant
// at its segment's first frame, 25 frames a segment. The segments, cues,
// discontinuities and breaks are WP04's; the keyframes and context starts
// are made here, with payloads of the test's own. With W = 50 and N = 12
// (the acceptance tests' parameters, D-077) v180's ad is its pair's one drop.
Built ad_ladder(const Shape& shape = {})
{
    Built b;
    b.scores = ad_report();
    timeline::Timeline& t = b.timeline;

    timeline::VariantMedia media;
    for (std::int64_t k = 0; k < 8; ++k) {
        media.segments.push_back({ .seq = k, .first_index = 25 * k, .frames = 25, .bytes = 12000 + k });
        media.keyframes.push_back(25 * k);
    }
    for (std::int64_t i = 0; i < kFrames; ++i) media.pts.push_back(ref_pts(i));
    timeline::VariantMedia refused;
    refused.refused = std::string(kRefusal);

    std::vector<timeline::PlaylistEvents> playlists;
    for (int v = 0; v < 3; ++v) {
        playlists.push_back(timeline::parse_playlist_events(kAdPlaylist, url_of(v), v));
        REQUIRE(playlists.back().error.empty());
    }
    t.axis = timeline::build_axis(b.scores, shape.reference_refused ? &refused : &media,
                                  &playlists[0].timeline);
    REQUIRE(t.axis.clock == (shape.reference_refused ? "frame-grid" : "playlist-time"));

    const int top[] = { 1, 2, 3 }, bottom[] = { 3, 2, 1 };
    for (int v = 0; v < 3; ++v) {
        const timeline::MasterEntry& e = b.scores.entries[static_cast<std::size_t>(v)];
        timeline::Variant var;
        var.index             = v;
        var.pair              = v - 1;
        var.entries           = { e.index };
        var.url               = url_of(v);
        var.playlist_location = url_of(v);
        var.playlist_sha256   = filehash::sha256_hex(kAdPlaylist);
        var.bandwidth         = e.bandwidth;
        var.resolution        = e.resolution;
        var.codecs            = e.codecs;
        var.rank_from_top     = top[v];
        var.rank_from_bottom  = bottom[v];
        t.variants.push_back(var);
    }
    if (shape.reference_refused) {
        t.variants[0].segment_map = "refused: " + std::string(kRefusal);
        t.warnings.push_back("v360: no segment map, " + std::string(kRefusal) +
                             ": its segment-bound events are left out (D-081 (c))");
    }
    for (const timeline::PairScores& p : b.scores.pairs) {
        timeline::Curve c;
        c.variant   = static_cast<int>(p.index) + 1;
        c.ref_index = p.ref_index;
        c.vmaf      = p.vmaf;
        for (std::int64_t k : p.context_first)
            c.context_first.push_back(p.ref_index[static_cast<std::size_t>(k)]);
        t.curves.push_back(std::move(c));
    }

    const auto place = [&t](Event e, std::int64_t at, std::optional<std::int64_t> end = std::nullopt) {
        e.ref_index = at;
        e.ref_end   = end;
        e.key       = t.axis.keys[static_cast<std::size_t>(at)];
        return e;
    };
    std::vector<Event> events, cues;
    for (int v = shape.reference_refused ? 1 : 0; v < 3; ++v) {
        const timeline::PlaylistEvents& p = playlists[static_cast<std::size_t>(v)];
        for (std::size_t i = 0; i < p.timeline.segments.size(); ++i) {
            const auto first = static_cast<std::int64_t>(25 * i);
            events.push_back(place(timeline::segment_event(p, i, media.segments[i].bytes), first, first + 25));
        }
        for (const Event& e : p.events) events.push_back(place(e, 25 * *e.segment));
        for (const Event& e : p.cues) cues.push_back(place(e, 25 * *e.segment));
        for (std::int64_t k = 0; k < 8; ++k) {
            Event key;
            key.source  = Source::Decoder;
            key.type    = "keyframe";
            key.variant = v;
            key.segment = k;
            events.push_back(place(key, 25 * k));
        }
    }
    const timeline::Breaks breaks = timeline::pair_breaks(
        cues, [&t](std::int64_t f, AVRational d) { return t.axis.frame_after(f, d); });
    REQUIRE(breaks.warnings.empty());
    REQUIRE(breaks.events.size() == (shape.reference_refused ? 2u : 3u));
    events.insert(events.end(), cues.begin(), cues.end());
    events.insert(events.end(), breaks.events.begin(), breaks.events.end());
    for (int pair = 0; pair < 2; ++pair) {
        Event c;
        c.source  = Source::Vmaf;
        c.type    = "context_start";
        c.variant = pair + 1;
        c.payload.emplace_back("pair_index", Value{ std::int64_t{ 75 } });
        events.push_back(place(c, 75));
    }
    timeline::assign_ids(events);
    t.events = std::move(events);
    t.build_index();

    timeline::DetectOptions options;
    options.window     = 50;
    options.min_points = 12;
    b.info.overlap     = shape.overlap;
    if (shape.detect) {
        b.info.detection = timeline::Detection{ .options = options,
                                                .curves  = { CurveRef{ .index = 0 }, CurveRef{ .index = 1 } } };
        for (int pair = 0; pair < 2; ++pair) {
            const CurveRef curve{ .index = pair };
            std::string    err;
            for (const timeline::Drop& d : timeline::detect(timeline::curve_points(t, curve), options, err)) {
                Attribution a    = timeline::attribute(t, curve, d.interval, b.info.overlap);
                a.detected       = true;
                a.baseline       = d.baseline;
                a.drop_magnitude = d.drop_magnitude;
                a.mean_drop      = d.mean_drop;
                b.attributions.push_back(std::move(a));
            }
            REQUIRE(err.empty());
        }
    }

    b.info.ffmpeg_libraries = { { "libavutil", "1.2.3" }, { "libavcodec", "4.5.6" },
                                { "libavformat", "7.8.9" } };
    b.info.options = { { "report", Value{ std::string("ad.json") } },
                       { "out", Value{ std::string("ad.timeline.json") } },
                       { "threshold", Value{ 6.0 } },
                       { "window", Value{ std::int64_t{ 50 } } },
                       { "min_points", Value{ std::int64_t{ 12 } } } };
    b.info.report               = timeline::report_info(b.scores);
    b.info.report.path          = "ad.json";
    b.info.report.sha256        = filehash::sha256_hex("the report's bytes");
    b.info.report.scores_sha256 = filehash::sha256_hex(timeline::scores_text(b.scores));
    std::string err;
    REQUIRE(timeline::reference_info(b.scores, t.axis, b.info.reference, err));
    REQUIRE(err.empty());
    return b;
}

std::string document(const Built& b) { return timeline::to_json(b.timeline, b.attributions, b.info); }

struct Read {
    bool                     ok = false;
    std::string              err;
    timeline::Timeline       timeline;
    std::vector<Attribution> attributions;
    timeline::DocInfo        info;
};

Read read(std::string_view text)
{
    Read r;
    r.ok = timeline::from_json(text, r.timeline, r.attributions, r.info, r.err);
    return r;
}

// The ids an attribution lists from one source.
std::vector<std::string> listed(const timeline::Timeline& t, const Attribution& a, Source source)
{
    std::vector<std::string> out;
    for (const Found& f : a.by_source[static_cast<std::size_t>(source)]) out.push_back(t.events[f.event].id);
    return out;
}

using Ids = std::vector<std::string>;

// Two payload values alike as JSON reads them back: numbers by value, through
// number_of(), since a whole double is written as an integer literal and read
// back as an integer (from_json()'s contract).
bool alike(const Value& a, const Value& b)
{
    const std::optional<double> x = timeline::number_of(a), y = timeline::number_of(b);
    if (x && y) return *x == *y;
    if (a.v.index() != b.v.index()) return false;
    if (const auto* l = std::get_if<Value::List>(&a.v)) {
        const auto& m = std::get<Value::List>(b.v);
        return l->size() == m.size() &&
               std::equal(l->begin(), l->end(), m.begin(),
                          [](const Value& x, const Value& y) { return alike(x, y); });
    }
    if (const auto* o = std::get_if<Value::Object>(&a.v)) {
        const auto& p = std::get<Value::Object>(b.v);
        return o->size() == p.size() &&
               std::equal(o->begin(), o->end(), p.begin(), [](const auto& x, const auto& y) {
                   return x.first == y.first && alike(x.second, y.second);
               });
    }
    if (const auto* s = std::get_if<std::string>(&a.v)) return *s == std::get<std::string>(b.v);
    if (const auto* f = std::get_if<bool>(&a.v)) return *f == std::get<bool>(b.v);
    return true;   // both null
}

bool alike(const timeline::Payload& a, const timeline::Payload& b)
{
    return alike(Value{ a }, Value{ b });
}

bool same(AVRational a, AVRational b) { return a.num == b.num && a.den == b.den; }

// Every field of `a` and `b` the document holds, compared.
void check_same(const timeline::Timeline& a, const timeline::Timeline& b)
{
    CHECK(a.axis.kind == b.axis.kind);
    CHECK(a.axis.clock == b.axis.clock);
    CHECK(same(a.axis.time_base, b.axis.time_base));
    CHECK(same(a.axis.frame_duration, b.axis.frame_duration));
    CHECK(a.axis.frames == b.axis.frames);
    CHECK(a.axis.checked == b.axis.checked);
    CHECK(a.axis.warnings == b.axis.warnings);
    CHECK(a.axis.tick_den == b.axis.tick_den);
    CHECK(a.axis.ticks == b.axis.ticks);
    REQUIRE(a.axis.keys.size() == b.axis.keys.size());
    for (std::size_t i = 0; i < a.axis.keys.size(); ++i) {
        CHECK(a.axis.keys[i].sequence == b.axis.keys[i].sequence);
        CHECK(a.axis.keys[i].pts == b.axis.keys[i].pts);
    }
    REQUIRE(a.axis.runs.size() == b.axis.runs.size());
    for (std::size_t i = 0; i < a.axis.runs.size(); ++i) {
        const timeline::AxisRun &x = a.axis.runs[i], &y = b.axis.runs[i];
        CHECK(x.test == y.test);
        CHECK(x.sequence == y.sequence);
        CHECK(x.first == y.first);
        CHECK(x.last == y.last);
        CHECK(x.frames == y.frames);
        CHECK(x.largest_at == y.largest_at);
        CHECK(x.largest == y.largest);
        CHECK(x.warning == y.warning);
    }
    REQUIRE(a.variants.size() == b.variants.size());
    for (std::size_t i = 0; i < a.variants.size(); ++i) {
        const timeline::Variant &x = a.variants[i], &y = b.variants[i];
        CHECK(x.index == y.index);
        CHECK(x.pair == y.pair);
        CHECK(x.entries == y.entries);
        CHECK(x.url == y.url);
        CHECK(x.playlist_location == y.playlist_location);
        CHECK(x.playlist_sha256 == y.playlist_sha256);
        CHECK(x.bandwidth == y.bandwidth);
        CHECK(x.resolution == y.resolution);
        CHECK(x.codecs == y.codecs);
        CHECK(x.rank_from_top == y.rank_from_top);
        CHECK(x.rank_from_bottom == y.rank_from_bottom);
        CHECK(x.segment_map == y.segment_map);
    }
    REQUIRE(a.events.size() == b.events.size());
    for (std::size_t i = 0; i < a.events.size(); ++i) {
        const Event &x = a.events[i], &y = b.events[i];
        INFO(x.id);
        CHECK(x.id == y.id);
        CHECK(x.source == y.source);
        CHECK(x.type == y.type);
        CHECK(x.variant == y.variant);
        CHECK(x.session == y.session);
        CHECK(x.segment == y.segment);
        CHECK(x.ref_index == y.ref_index);
        CHECK(x.ref_end == y.ref_end);
        CHECK(x.key.sequence == y.key.sequence);
        CHECK(x.key.pts == y.key.pts);
        CHECK(x.wallclock_us == y.wallclock_us);
        CHECK(alike(x.payload, y.payload));
    }
    CHECK(a.max_end == b.max_end);
    CHECK(a.warnings == b.warnings);
}

void check_same(const std::vector<Attribution>& a, const std::vector<Attribution>& b)
{
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        const Attribution &x = a[i], &y = b[i];
        CHECK(x.curve.index == y.curve.index);
        CHECK(x.interval.ref_index == y.interval.ref_index);
        CHECK(x.interval.ref_end == y.interval.ref_end);
        CHECK(x.searched.ref_index == y.searched.ref_index);
        CHECK(x.searched.ref_end == y.searched.ref_end);
        CHECK(x.detected == y.detected);
        CHECK(x.points == y.points);
        CHECK(x.baseline == y.baseline);
        CHECK(x.min == y.min);
        CHECK(x.mean == y.mean);
        CHECK(x.drop_magnitude == y.drop_magnitude);
        CHECK(x.mean_drop == y.mean_drop);
        for (std::size_t s = 0; s < timeline::kSourceCount; ++s) {
            REQUIRE(x.by_source[s].size() == y.by_source[s].size());
            for (std::size_t k = 0; k < x.by_source[s].size(); ++k) {
                const Found &f = x.by_source[s][k], &g = y.by_source[s][k];
                CHECK(f.event == g.event);
                CHECK(f.offset_frames == g.offset_frames);
                CHECK(f.offset_end_frames == g.offset_end_frames);
                CHECK(f.offset_s == g.offset_s);
                CHECK(f.offset_end_s == g.offset_end_s);
            }
        }
    }
}

// The ad ladder with what its plain shape lacks, each value kind of the
// layout's nullable fields among them: the reference's map refused, so its
// axis is the frame grid with the fact of frames 125 to 199; margins; a
// config file; v360 as a mezzanine would be, with no entry, BANDWIDTH, rank
// or playlist; v180 without a RESOLUTION; and a payload of every value kind.
// An event with a session and a wall clock comes with the session part,
// whose sessions it can name (WP09b): a ladder's document has neither.
Built varied()
{
    Built b = ad_ladder({ .detect = true, .reference_refused = true,
                          .overlap = { .margin_before = { 1, 25 }, .margin_after = { 2, 25 } } });
    timeline::Timeline& t = b.timeline;
    b.info.config_file = timeline::FileRef{ .path   = "timeline.conf",
                                            .sha256 = filehash::sha256_hex("window = 50") };
    b.info.report.model      = "models/vmaf.json";
    b.info.report.model_source = "file";
    b.info.report.model_sha256 = filehash::sha256_hex("a model");
    timeline::Variant& mezzanine = t.variants[0];
    mezzanine.entries.clear();
    mezzanine.bandwidth = 0;
    mezzanine.rank_from_top = mezzanine.rank_from_bottom = 0;
    mezzanine.playlist_location.clear();
    mezzanine.playlist_sha256.clear();
    t.variants[2].resolution.reset();

    Event corrupt;
    corrupt.source    = Source::Decoder;
    corrupt.type      = "corrupt_packet";
    corrupt.variant   = 2;
    corrupt.segment   = 4;
    corrupt.ref_index = 122;
    corrupt.key       = t.axis.keys[122];
    corrupt.payload   = {
        { "none", Value{} },
        { "flag", Value{ true } },
        { "count", Value{ std::int64_t{ -3 } } },
        { "ratio", Value{ 0.1 } },
        { "whole", Value{ 2.0 } },
        { "big", Value{ 1e16 } },
        { "text", Value{ std::string("a \"quoted\" line\n, caf\xc3\xa9") } },
        { "list", Value{ Value::List{ Value{ std::int64_t{ 1 } }, Value{ std::string("x") }, Value{} } } },
        { "next_boundary", Value{ Value::Object{ { "seq", Value{ std::int64_t{ 5 } } },
                                                 { "first_index", Value{ std::int64_t{ 125 } } },
                                                 { "discontinuity", Value{ true } } } } },
    };
    t.events.push_back(corrupt);
    // An id for the new event, the old ones kept; Found indices follow the
    // events they name through the sort.
    std::vector<std::string> before;
    for (const Event& e : t.events) before.push_back(e.id);
    t.events.back().id = "decoder/corrupt_packet/2/0";
    before.back()      = t.events.back().id;
    t.build_index();
    for (Attribution& a : b.attributions)
        for (auto& group : a.by_source)
            for (Found& f : group) {
                const std::string& id = before[f.event];
                f.event = static_cast<std::size_t>(
                    std::find_if(t.events.begin(), t.events.end(), [&](const Event& e) { return e.id == id; }) -
                    t.events.begin());
            }
    t.warnings.push_back("a warning with \"quotes\" and a\ttab");
    return b;
}

} // namespace

// ── The validator ────────────────────────────────────────────────────────────

TEST_CASE("timeline schema: the subset validator takes and refuses as JSON Schema does",
          "[timeline][timeline_doc]")
{
    const json::Value object = parsed(R"({
        "type": "object", "required": ["a", "b"], "additionalProperties": false,
        "properties": {
            "a": {"type": ["integer", "null"]},
            "b": {"type": "array", "items": {"enum": [1, "x"]}},
            "c": {"type": "object", "additionalProperties": {"type": "string"}},
            "d": {"type": "number"},
            "e": {"type": ["object", "null"], "required": ["f"], "properties": {"f": {"type": "boolean"}}}
        }})");
    CHECK(failures(object, R"({"a": 1, "b": []})").empty());
    CHECK(failures(object, R"({"a": null, "b": [1, "x", 1.0], "c": {"k": "v"}, "d": 2.5, "e": null})").empty());
    CHECK(failures(object, R"({"a": 0, "b": [], "d": 2, "e": {"f": false}})").empty());   // 2: a number

    CHECK(failures(object, R"({"b": []})") == std::vector<std::string>{ "$: no a" });
    CHECK(failures(object, R"({"a": 1, "b": [], "z": 0})") ==
          std::vector<std::string>{ "$.z: the schema allows nothing here" });
    CHECK(failures(object, R"({"a": "1", "b": []})") ==
          std::vector<std::string>{ R"($.a: a string, not of type ["integer","null"])" });
    // The departure: a whole number written as a double isn't an integer.
    CHECK(failures(object, R"({"a": 1.0, "b": []})").size() == 1);
    CHECK(failures(object, R"({"a": 1e16, "b": []})").size() == 1);
    CHECK(failures(object, R"({"a": 1, "b": [2]})") ==
          std::vector<std::string>{ R"($.b[0]: 2, not one of [1,"x"])" });
    CHECK(failures(object, R"({"a": 1, "b": [], "c": {"k": 1}})") ==
          std::vector<std::string>{ R"($.c.k: an integer, not of type "string")" });
    CHECK(failures(object, R"({"a": 1, "b": [], "d": "2"})").size() == 1);
    CHECK(failures(object, R"({"a": 1, "b": [], "e": {}})") == std::vector<std::string>{ "$.e: no f" });
    CHECK(failures(object, R"([])") == std::vector<std::string>{ R"($: an array, not of type "object")" });

    // "items": false, as the schema file has it for a ladder's sessions: an
    // array with no item.
    const json::Value none = parsed(R"({"type": "array", "items": false})");
    CHECK(failures(none, "[]").empty());
    CHECK(failures(none, "[{}]") == std::vector<std::string>{ "$[0]: the schema allows nothing here" });
    std::vector<std::string> none_errors;
    subset_errors(none, "schema", none_errors);
    CHECK(none_errors.empty());

    // A keyword outside the subset is an error, wherever it is.
    const json::Value outside = parsed(R"({"properties": {"a": {"minimum": 0}}})");
    CHECK(failures(outside, R"({"a": 1})") ==
          std::vector<std::string>{ "$.a: the schema's keyword minimum is outside the subset" });
    std::vector<std::string> errors;
    subset_errors(outside, "schema", errors);
    CHECK(errors == std::vector<std::string>{ "schema.properties.a.minimum: a keyword outside the subset" });
    errors.clear();
    subset_errors(parsed(R"({"type": "float", "required": [1], "items": 3})"), "schema", errors);
    CHECK(errors.size() == 3);
    errors.clear();
    subset_errors(object, "schema", errors);
    CHECK(errors.empty());
}

TEST_CASE("timeline schema: the schema file is in the validator's subset, with section 5.9's vocabulary",
          "[timeline][timeline_doc]")
{
    const json::Value& schema = schema_file();
    std::vector<std::string> errors;
    subset_errors(schema, "schema", errors);
    CHECK(errors.empty());

    json::Value copy = schema;
    // Every type of every source, in the design's order, and nothing else.
    std::vector<std::string> vocabulary;
    for (std::size_t s = 0; s < timeline::kSourceCount; ++s)
        for (std::string_view type : timeline::event_types(static_cast<Source>(s)))
            vocabulary.emplace_back(type);
    CHECK(vocabulary.size() == 22);
    std::vector<std::string> types;
    for (const json::Value& v : *at(copy, "properties.timeline.items.properties.type.enum").array())
        types.push_back(*v.string());
    CHECK(types == vocabulary);
    // WP04's types are among them, each under its own source.
    const auto has = [](Source s, std::string_view type) {
        const auto list = timeline::event_types(s);
        return std::find(list.begin(), list.end(), type) != list.end();
    };
    for (std::string_view type : { timeline::kTypeSegment, timeline::kTypeDiscontinuity,
                                   timeline::kTypeProgramDateTime, timeline::kTypeDaterange,
                                   timeline::kTypeTag, timeline::kTypePeriod })
        CHECK(has(Source::Playlist, type));
    for (std::string_view type : { timeline::kTypeCueOut, timeline::kTypeCueOutCont, timeline::kTypeCueIn,
                                   timeline::kTypeAdBreak })
        CHECK(has(Source::Scte35, type));
    CHECK(timeline::event_types(static_cast<Source>(6)).empty());

    std::vector<std::string> sources;
    for (const json::Value& v : *at(copy, "properties.timeline.items.properties.source.enum").array())
        sources.push_back(*v.string());
    CHECK(sources == std::vector<std::string>{ "vmaf", "network", "playlist", "scte35", "player", "decoder" });
    const std::string group_source =
        "properties.attributions.items.properties.overlapping_events.items.properties.source";
    CHECK(text_of(at(copy, group_source)) == text_of(at(copy, "properties.timeline.items.properties.source")));

    // Every field written through json::number is typed "number", never
    // "integer", since a whole double is written as an integer literal.
    for (std::string_view path : {
             "properties.axis.properties.reference.properties.sequences.items.properties.first_pts",
             "properties.axis.properties.runs.items.properties.largest",
             "properties.timeline.items.properties.t",
             "properties.timeline.items.properties.t_end",
             "properties.timeline.items.properties.pts",
             "properties.timeline.items.properties.wallclock",
             "properties.detection.properties.threshold",
             "properties.attributions.items.properties.interval.properties.t",
             "properties.attributions.items.properties.interval.properties.t_end",
             "properties.attributions.items.properties.baseline",
             "properties.attributions.items.properties.min",
             "properties.attributions.items.properties.mean",
             "properties.attributions.items.properties.drop_magnitude",
             "properties.attributions.items.properties.mean_drop",
             "properties.attributions.items.properties.overlapping_events.items.properties.events.items."
             "properties.offset",
             "properties.attributions.items.properties.overlapping_events.items.properties.events.items."
             "properties.offset_end",
         }) {
        INFO(path);
        const std::string type = text_of(at(copy, std::string(path) + ".type"));
        CHECK(type.find("\"number\"") != std::string::npos);
        CHECK(type.find("integer") == std::string::npos);
    }
}

// ── Writing ──────────────────────────────────────────────────────────────────

TEST_CASE("timeline::to_json: the ad ladder's document, checked against the schema file",
          "[timeline][timeline_doc]")
{
    const Built b = ad_ladder();
    const std::string text = document(b);
    CHECK(schema_failures(text).empty());
    CHECK(text.back() == '\n');

    // One drop, v180's ad, and the events of v180's and v360's playlists
    // that meet it (D-078), as the query lists them.
    REQUIRE(b.attributions.size() == 1);
    const Attribution& a = b.attributions[0];
    CHECK(a.curve.index == 1);
    CHECK(a.interval.ref_index == 75);
    CHECK(a.interval.ref_end == 125);
    CHECK(a.points == 50);
    CHECK(listed(b.timeline, a, Source::Scte35) ==
          Ids{ "scte35/cue_out/0/0", "scte35/cue_out/2/0", "scte35/ad_break/0/0", "scte35/ad_break/2/0",
               "scte35/cue_out_cont/0/0", "scte35/cue_out_cont/2/0" });

    json::Value doc = parsed(text);
    CHECK(text_of(at(doc, "schema")) == R"("mirage-timeline")");
    CHECK(text_of(at(doc, "schema_version")) == "1");
    CHECK(text_of(at(doc, "tool")) == R"("mirage_vmaf")");
    CHECK(text_of(at(doc, "command")) == R"("timeline")");
    CHECK(text_of(at(doc, "ffmpeg_libraries")) ==
          R"({"libavutil":"1.2.3","libavcodec":"4.5.6","libavformat":"7.8.9"})");
    CHECK(text_of(at(doc, "config_file")) == "null");
    CHECK(text_of(at(doc, "ladder_report")) ==
          std::format(R"({{"path":"ad.json","sha256":"{}","scores_sha256":"{}","status":"complete",)"
                      R"("reference_mode":"top-rung","model":"vmaf_v0.6.1","model_source":"built-in",)"
                      R"("model_sha256":null,"scaler":"bicubic","libvmaf_version":"1.2.3",)"
                      R"("libvmaf_package_version":"1.2.4","master":{{"url":"http://test.invalid/ladder/)"
                      R"(master.m3u8","location":"http://test.invalid/ladder/master.m3u8","sha256":"{}"}}}})",
                      b.info.report.sha256, b.info.report.scores_sha256, b.scores.master_sha256));
    // The session part as a ladder alone has it, until WP09b fills it.
    CHECK(text_of(at(doc, "session_plan")) == "null");
    CHECK(text_of(at(doc, "sessions")) == "[]");

    // The axis: the playlist time with its segment map, equal to the frame
    // grid on this ladder, so one stretch of seconds; three of media keys, the
    // ad's step back and the main content's step forward each starting one.
    CHECK(text_of(at(doc, "axis.kind")) == R"("reference-frames")");
    CHECK(text_of(at(doc, "axis.seconds")) == R"("playlist-time")");
    CHECK(text_of(at(doc, "axis.frame_duration")) == R"("1/25")");
    CHECK(text_of(at(doc, "axis.frames")) == "200");
    CHECK(text_of(at(doc, "axis.reference")) ==
          R"({"url":"http://test.invalid/ladder/v360/index.m3u8","time_base":"1/90000","frame_rate":"25/1",)"
          R"("sequences":[{"first_index":0,"first_pts":10,"first_pts_ticks":900000,"frames":75},)"
          R"({"first_index":75,"first_pts":10,"first_pts_ticks":900000,"frames":125}]})");
    CHECK(text_of(at(doc, "axis.checked")) == R"("segment-map")");
    const std::int64_t den = b.timeline.axis.tick_den;
    CHECK(text_of(at(doc, "axis.tick_den")) == std::to_string(den));
    CHECK(den % 25 == 0);
    CHECK(text_of(at(doc, "axis.ticks")) ==
          std::format(R"([{{"first":0,"frames":200,"first_tick":0,"step":{}}}])", den / 25));
    CHECK(text_of(at(doc, "axis.keys")) ==
          R"([{"first":0,"frames":75,"sequence":0,"first_pts_ticks":900000,"step":3600},)"
          R"({"first":75,"frames":50,"sequence":1,"first_pts_ticks":900000,"step":3600},)"
          R"({"first":125,"frames":75,"sequence":1,"first_pts_ticks":1350000,"step":3600}])");
    CHECK(text_of(at(doc, "axis.runs")) == "[]");
    CHECK(text_of(at(doc, "axis.warnings")) == "[]");

    // Variants by entry and URI, with both ranks (D-075).
    CHECK(text_of(at(doc, "variants[2]")) ==
          std::format(R"({{"id":2,"role":"distorted","pair":1,"entries":[2],)"
                      R"("url":"http://test.invalid/ladder/v180/index.m3u8","bandwidth":350000,)"
                      R"("resolution":"320x180","codecs":"avc1.64001e","rank_from_top":3,)"
                      R"("rank_from_bottom":1,"playlist":{{"url":"http://test.invalid/ladder/v180/)"
                      R"(index.m3u8","location":"http://test.invalid/ladder/v180/index.m3u8",)"
                      R"("sha256":"{}"}},"segment_map":"ok"}})",
                      filehash::sha256_hex(kAdPlaylist)));
    CHECK(text_of(at(doc, "variants[0].role")) == R"("reference")");
    CHECK(text_of(at(doc, "variants[0].pair")) == "null");

    // Every event once, in canonical order; v180's cue-out at the ad's first
    // frame, 3 s on the axis, its media key the ad's sequence and PTS, no wall
    // clock (D-073).
    const json::Value::Array& events = *at(doc, "timeline").array();
    CHECK(events.size() == b.timeline.events.size());
    for (std::size_t i = 0; i < events.size(); ++i)
        CHECK(*events[i].find("id")->string() == b.timeline.events[i].id);
    const auto cue = std::find_if(events.begin(), events.end(), [](const json::Value& e) {
        return *e.find("id")->string() == "scte35/cue_out/2/0";
    });
    REQUIRE(cue != events.end());
    CHECK(text_of(*cue) ==
          R"({"id":"scte35/cue_out/2/0","source":"scte35","type":"cue_out","variant":2,"session":null,)"
          R"("segment":3,"ref_index":75,"ref_end":null,"t":3,"t_end":null,"seq":1,"pts":10,"pts_ticks":900000,)"
          R"("wallclock":null,"payload":{"line":14,"form":"CUE-OUT","raw":"#EXT-X-CUE-OUT:2.000",)"
          R"("duration":2,"attributes":{}}})");

    CHECK(text_of(at(doc, "detection")) ==
          R"({"threshold":6,"window":50,"min_points":12,"baseline":"median-non-low","warm_up":25,)"
          R"("context_first":"no-special-case","curves":[{"kind":"pair","index":0},{"kind":"pair","index":1}],)"
          R"("session_rules":null})");
    CHECK(text_of(at(doc, "overlap")) ==
          R"({"rule":"half-open","margin_before":"0/1","margin_after":"0/1","scope":"curve-inputs"})");

    // The attribution: §7's names, its interval an object, one group per
    // source in section 3.1's order, each event by its id with its offsets.
    CHECK(text_of(at(doc, "attributions[0].curve")) == R"({"kind":"pair","index":1})");
    CHECK(text_of(at(doc, "attributions[0].detected")) == "true");
    CHECK(text_of(at(doc, "attributions[0].interval")) ==
          R"({"ref_index":75,"ref_end":125,"t":3,"t_end":5,"points":50})");
    CHECK(*at(doc, "attributions[0].baseline").number() == a.baseline.value());
    CHECK(*at(doc, "attributions[0].drop_magnitude").number() == a.drop_magnitude.value());
    const json::Value::Array& groups = *at(doc, "attributions[0].overlapping_events").array();
    REQUIRE(groups.size() == timeline::kSourceCount);
    for (std::size_t s = 0; s < groups.size(); ++s)
        CHECK(*groups[s].find("source")->string() == timeline::source_name(static_cast<Source>(s)));
    CHECK(text_of(at(doc, "attributions[0].overlapping_events[0]")) ==
          R"({"source":"vmaf","events":[{"event":"vmaf/context_start/2/0","offset":0,"offset_end":null,)"
          R"("offset_frames":0,"offset_end_frames":null}]})");
    CHECK(text_of(at(doc, "attributions[0].overlapping_events[1]")) == R"({"source":"network","events":[]})");
    CHECK(text_of(at(doc, "attributions[0].overlapping_events[3].events[2]")) ==
          R"({"event":"scte35/ad_break/0/0","offset":0,"offset_end":2,"offset_frames":0,)"
          R"("offset_end_frames":50})");
    CHECK(text_of(at(doc, "attributions[0].overlapping_events[3].events[4]")) ==
          R"({"event":"scte35/cue_out_cont/0/0","offset":1,"offset_end":null,"offset_frames":25,)"
          R"("offset_end_frames":null})");
    CHECK(text_of(at(doc, "warnings")) == "[]");

    // The top level holds §8's keys in its order, the session part's two
    // among them.
    std::vector<std::string> keys;
    for (const auto& m : *doc.object()) keys.push_back(m.first);
    CHECK(keys == std::vector<std::string>{ "schema", "schema_version", "tool", "command", "ffmpeg_libraries",
                                            "options", "config_file", "ladder_report", "session_plan", "axis",
                                            "variants", "sessions", "timeline", "detection", "overlap",
                                            "attributions", "warnings" });
}

TEST_CASE("timeline::to_json: whole numbers are integer literals, in fields the schema types number",
          "[timeline][timeline_doc]")
{
    const Built b = ad_ladder();
    const std::string text = document(b);
    // The cue-out at 3 s, the ad break to 5 s, the PTS at 10 s, threshold 6,
    // the offsets of +1 s and +2 s: each a whole double written as an integer.
    CHECK(text.find(R"("t": 3, "t_end": null, "seq": 1, "pts": 10, "pts_ticks": 900000)") != std::string::npos);
    CHECK(text.find(R"("t": 3, "t_end": 5,)") != std::string::npos);
    CHECK(text.find(R"("threshold": 6,)") != std::string::npos);
    CHECK(text.find(R"("offset": 1, "offset_end": null, "offset_frames": 25)") != std::string::npos);
    CHECK(text.find(R"("offset": 0, "offset_end": 2, "offset_frames": 0, "offset_end_frames": 50)") !=
          std::string::npos);
    CHECK(text.find(".0,") == std::string::npos);
    CHECK(text.find(".0}") == std::string::npos);
    // They read back as integers, which the schema's "number" takes.
    json::Value doc = parsed(text);
    CHECK(at(doc, "attributions[0].interval.t").integer());
    CHECK(at(doc, "detection.threshold").integer());
    CHECK(schema_failures(text).empty());

    // A double that isn't whole is written in full, and one beyond 2^53 in
    // exponent form, which reads back as a double: both numbers.
    Built v = varied();
    const std::string varied_text = document(v);
    CHECK(varied_text.find(R"("ratio": 0.1, "whole": 2, "big": 1e+16,)") != std::string::npos);
    CHECK(schema_failures(varied_text).empty());
}

TEST_CASE("timeline::to_json: detection: null when no detection ran", "[timeline][timeline_doc]")
{
    // Without W and N there is no detection and no attribution (D-077).
    const Built b = ad_ladder({ .detect = false });
    CHECK(b.attributions.empty());
    const std::string text = document(b);
    CHECK(text.find("\n  \"detection\": null,\n") != std::string::npos);
    CHECK(text.find("\n  \"attributions\": [],\n") != std::string::npos);
    CHECK(schema_failures(text).empty());
    const Read r = read(text);
    REQUIRE(r.ok);
    CHECK_FALSE(r.info.detection.has_value());
    CHECK(r.attributions.empty());

    // A record whose options lack W or N is no detection either.
    Built c = ad_ladder({ .detect = false });
    c.info.detection = timeline::Detection{ .options = {}, .curves = { CurveRef{ .index = 0 } } };
    c.info.detection->options.window = 50;
    CHECK(document(c) == text);
}

TEST_CASE("timeline::to_json: byte-identical output for the same timeline, whatever the events' order",
          "[timeline][timeline_doc]")
{
    const Built b = ad_ladder();
    const std::string text = document(b);
    CHECK(document(b) == text);
    CHECK(document(ad_ladder()) == text);

    // The events reversed, each Found following its event: the same document,
    // since the writer writes them in canonical order and names them by id.
    Built r = b;
    std::reverse(r.timeline.events.begin(), r.timeline.events.end());
    const std::size_t n = r.timeline.events.size();
    for (Attribution& a : r.attributions)
        for (auto& group : a.by_source)
            for (Found& f : group) f.event = n - 1 - f.event;
    CHECK(document(r) == text);

    // An index past the events is written null, which the schema refuses.
    Built bad = b;
    bad.attributions[0].by_source[0][0].event = n;
    const std::string bad_text = document(bad);
    CHECK(bad_text.find(R"("event": null)") != std::string::npos);
    CHECK_FALSE(schema_failures(bad_text).empty());
    CHECK_FALSE(read(bad_text).ok);
}

TEST_CASE("timeline::to_json: the axis's stretches, on hand-made axes and exact to the tick",
          "[timeline][timeline_doc]")
{
    // Ticks with steps that change, and two neighbours whose difference
    // doesn't fit in 64 bits; PTS of two sequences, unknown on two frames,
    // and one more pair of neighbours too far apart for a step.
    constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    Built b = ad_ladder({ .detect = false });
    b.timeline.events.clear();
    b.timeline.build_index();
    timeline::Axis& a = b.timeline.axis;
    a.clock          = "frame-grid";
    a.checked        = "report-only";
    a.frames         = 8;
    a.tick_den       = 30;
    a.frame_duration = { 1, 30 };
    a.ticks          = { 0, 1, 2, 10, 11, kMax, kMin, kMin + 1 };
    a.keys = { { 0, 0 }, { 0, 3 }, { 0, 6 }, { 0, std::nullopt }, { 0, std::nullopt },
               { 1, 100 }, { 1, 103 }, { 1, kMin } };
    a.runs = { { .test = timeline::AxisRun::Test::Clocks, .sequence = 0, .first = 3, .last = 4, .frames = 2,
                 .largest_at = 3, .largest = -0.04, .warning = true },
               { .test = timeline::AxisRun::Test::ReportGrid, .sequence = 1, .first = 5, .last = 7, .frames = 3,
                 .largest_at = 6, .largest = 3.0, .warning = false } };
    a.warnings = { timeline::run_text(a.runs[0]) };
    const std::string text = document(b);
    CHECK(schema_failures(text).empty());

    json::Value doc = parsed(text);
    CHECK(text_of(at(doc, "axis.ticks")) ==
          std::format(R"([{{"first":0,"frames":3,"first_tick":0,"step":1}},)"
                      R"({{"first":3,"frames":2,"first_tick":10,"step":1}},)"
                      R"({{"first":5,"frames":1,"first_tick":{},"step":0}},)"
                      R"({{"first":6,"frames":2,"first_tick":{},"step":1}}])",
                      kMax, kMin));
    CHECK(text_of(at(doc, "axis.keys")) ==
          std::format(R"([{{"first":0,"frames":3,"sequence":0,"first_pts_ticks":0,"step":3}},)"
                      R"({{"first":3,"frames":2,"sequence":0,"first_pts_ticks":null,"step":0}},)"
                      R"({{"first":5,"frames":2,"sequence":1,"first_pts_ticks":100,"step":3}},)"
                      R"({{"first":7,"frames":1,"sequence":1,"first_pts_ticks":{},"step":0}}])",
                      kMin));
    CHECK(text_of(at(doc, "axis.runs")) ==
          R"([{"test":"clocks","sequence":0,"first":3,"last":4,"frames":2,"largest_at":3,"largest":-0.04,)"
          R"("warning":true},{"test":"report-grid","sequence":1,"first":5,"last":7,"frames":3,"largest_at":6,)"
          R"("largest":3,"warning":false}])");

    const Read r = read(text);
    INFO(r.err);
    REQUIRE(r.ok);
    check_same(b.timeline, r.timeline);
    CHECK(timeline::to_json(r.timeline, r.attributions, r.info) == text);

    // An axis without frames, as a default Axis has, writes no stretch.
    Built e = ad_ladder({ .detect = false });
    e.timeline.events.clear();
    e.timeline.build_index();
    e.timeline.axis = {};
    e.timeline.axis.clock   = "frame-grid";
    e.timeline.axis.checked = "report-only";
    const std::string empty = document(e);
    json::Value empty_doc = parsed(empty);
    CHECK(text_of(at(empty_doc, "axis.ticks")) == "[]");
    CHECK(text_of(at(empty_doc, "axis.keys")) == "[]");
    CHECK(text_of(at(empty_doc, "axis.frame_duration")) == R"("0/1")");
    const Read er = read(empty);
    INFO(er.err);
    REQUIRE(er.ok);
    CHECK(er.timeline.axis.ticks.empty());
    CHECK(er.timeline.axis.keys.empty());
}

TEST_CASE("timeline::to_json: the axis's stretches where a frame isn't a whole number of ticks",
          "[timeline][timeline_doc]")
{
    // A reference of two 2.002 s segments at 24000/1001 fps (48 frames each)
    // and at 60000/1001 fps (120 each), its PTS on MPEG-TS's 90 kHz clock
    // rounded to the nearest tick, halves up, as a muxer writes them, through
    // build_axis() with its segment map. A frame is 15015/4 ticks (3753.75) at
    // the first rate and 3003/2 (1501.5) at the second, so the PTS step 3754,
    // 3754, 3753, 3754 and 1502, 1501, and the stretches, WP08's encoding
    // pending the owner (src/TimelineDoc.h), start every 4 frames and every 2:
    // they grow with the frames there, not with the segments. The axis still
    // reads back exactly.
    struct Rate {
        AVRational   rate;
        std::int64_t per_segment;
        std::vector<std::int64_t> stretch_frames;   // the frames of each stretch, ticks' and keys'
        std::int64_t key_step;                      // the PTS step of every stretch but a last single frame
    };
    std::vector<std::int64_t> at_24(1, 3);
    at_24.insert(at_24.end(), 23, 4);
    at_24.push_back(1);
    const std::vector<Rate> rates = { { { 24000, 1001 }, 48, at_24, 3754 },
                                      { { 60000, 1001 }, 120, std::vector<std::int64_t>(120, 2), 1502 } };
    for (const Rate& r : rates) {
        INFO(std::format("{}/{} fps", r.rate.num, r.rate.den));
        const std::int64_t frames = 2 * r.per_segment;
        // round(i x 90000 x den / num), halves up, from 10 s.
        const auto pts = [&r](std::int64_t i) {
            const std::int64_t twice = 2 * 90000 * static_cast<std::int64_t>(r.rate.den);
            return kOrigin + (i * twice + r.rate.num) / (2 * static_cast<std::int64_t>(r.rate.num));
        };
        timeline::LadderScores s = ad_report();
        s.pairs.resize(1);
        timeline::PairScores& p = s.pairs[0];
        p.ref_rate = p.dis_rate = r.rate;
        p.ref_index.clear();
        p.dis_index.clear();
        p.ref_pts.clear();
        p.dis_pts.clear();
        p.vmaf.clear();
        for (std::int64_t i = 0; i < frames; ++i) {
            p.ref_index.push_back(i);
            p.dis_index.push_back(i);
            p.ref_pts.push_back(pts(i));
            p.dis_pts.push_back(pts(i));
            p.vmaf.push_back(95.0);
        }
        p.context_first = { 0 };
        p.ref_sequences = { { .first_index = 0, .first_pts = pts(0), .frames = frames } };
        p.dis_sequences = p.ref_sequences;

        timeline::VariantMedia media;
        for (std::int64_t k = 0; k < 2; ++k)
            media.segments.push_back(
                { .seq = k, .first_index = k * r.per_segment, .frames = r.per_segment, .bytes = 1000 });
        for (std::int64_t i = 0; i < frames; ++i) media.pts.push_back(pts(i));
        const std::string playlist = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:3\n"
                                     "#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n"
                                     "#EXTINF:2.002,\ns0.ts\n#EXTINF:2.002,\ns1.ts\n#EXT-X-ENDLIST\n";
        const timeline::PlaylistEvents parsed_playlist =
            timeline::parse_playlist_events(playlist, url_of(0), 0);
        REQUIRE(parsed_playlist.error.empty());

        timeline::Timeline t;
        t.axis = timeline::build_axis(s, &media, &parsed_playlist.timeline);
        const std::string first_warning = t.axis.warnings.empty() ? std::string() : t.axis.warnings.front();
        INFO(first_warning);
        REQUIRE(t.axis.clock == "playlist-time");
        REQUIRE(t.axis.warnings.empty());
        timeline::Variant ref;
        ref.index = 0;
        ref.url   = url_of(0);
        t.variants.push_back(ref);
        timeline::DocInfo info;
        std::string       err;
        REQUIRE(timeline::reference_info(s, t.axis, info.reference, err));
        const std::string text = timeline::to_json(t, {}, info);
        CHECK(schema_failures(text).empty());

        json::Value doc = parsed(text);
        for (const char* name : { "axis.ticks", "axis.keys" }) {
            INFO(name);
            std::vector<std::int64_t> lengths;
            for (const json::Value& stretch : *at(doc, name).array())
                lengths.push_back(*stretch.find("frames")->integer());
            CHECK(lengths == r.stretch_frames);
        }
        const json::Value::Array& keys = *at(doc, "axis.keys").array();
        for (std::size_t k = 0; k < keys.size(); ++k) {
            const bool single = *keys[k].find("frames")->integer() == 1;
            CHECK(*keys[k].find("step")->integer() == (single ? 0 : r.key_step));
        }

        const Read back = read(text);
        INFO(back.err);
        REQUIRE(back.ok);
        CHECK(back.timeline.axis.ticks == t.axis.ticks);
        REQUIRE(back.timeline.axis.keys.size() == t.axis.keys.size());
        for (std::size_t i = 0; i < t.axis.keys.size(); ++i)
            CHECK(back.timeline.axis.keys[i].pts == t.axis.keys[i].pts);
    }
}

// ── Reading ──────────────────────────────────────────────────────────────────

TEST_CASE("timeline::from_json: a reader round trip", "[timeline][timeline_doc]")
{
    for (const Built& b : { ad_ladder(), ad_ladder({ .detect = false }), varied() }) {
        const std::string text = document(b);
        CHECK(schema_failures(text).empty());
        const Read r = read(text);
        INFO(r.err);
        REQUIRE(r.ok);
        CHECK(r.err.empty());
        check_same(b.timeline, r.timeline);
        check_same(b.attributions, r.attributions);
        CHECK(r.timeline.curves.empty());   // the report's, which the caller reads

        const timeline::DocInfo &x = b.info, &y = r.info;
        CHECK(x.tool == y.tool);
        CHECK(x.command == y.command);
        CHECK(x.ffmpeg_libraries == y.ffmpeg_libraries);
        CHECK(alike(x.options, y.options));
        CHECK(x.config_file.has_value() == y.config_file.has_value());
        if (x.config_file && y.config_file) {
            CHECK(x.config_file->path == y.config_file->path);
            CHECK(x.config_file->sha256 == y.config_file->sha256);
        }
        const timeline::ReportInfo &p = x.report, &q = y.report;
        CHECK(std::tie(p.path, p.sha256, p.scores_sha256, p.status, p.reference_mode, p.model, p.model_source,
                       p.model_sha256, p.scaler, p.libvmaf_version, p.libvmaf_package_version, p.master_url,
                       p.master_location, p.master_sha256) ==
              std::tie(q.path, q.sha256, q.scores_sha256, q.status, q.reference_mode, q.model, q.model_source,
                       q.model_sha256, q.scaler, q.libvmaf_version, q.libvmaf_package_version, q.master_url,
                       q.master_location, q.master_sha256));
        CHECK(x.reference.url == y.reference.url);
        CHECK(same(x.reference.frame_rate, y.reference.frame_rate));
        REQUIRE(x.reference.sequences.size() == y.reference.sequences.size());
        for (std::size_t i = 0; i < x.reference.sequences.size(); ++i) {
            CHECK(x.reference.sequences[i].first_index == y.reference.sequences[i].first_index);
            CHECK(x.reference.sequences[i].first_pts == y.reference.sequences[i].first_pts);
            CHECK(x.reference.sequences[i].frames == y.reference.sequences[i].frames);
        }
        CHECK(x.detection.has_value() == y.detection.has_value());
        if (x.detection && y.detection) {
            CHECK(x.detection->options.threshold == y.detection->options.threshold);
            CHECK(x.detection->options.window == y.detection->options.window);
            CHECK(x.detection->options.min_points == y.detection->options.min_points);
            REQUIRE(x.detection->curves.size() == y.detection->curves.size());
            for (std::size_t i = 0; i < x.detection->curves.size(); ++i)
                CHECK(x.detection->curves[i].index == y.detection->curves[i].index);
        }
        CHECK(same(x.overlap.margin_before, y.overlap.margin_before));
        CHECK(same(x.overlap.margin_after, y.overlap.margin_after));

        // Written again, the document is the same bytes.
        CHECK(timeline::to_json(r.timeline, r.attributions, r.info) == text);
    }

    // What the varied shape adds, read back: the fact of the refused map's
    // frame grid, the searched interval widened by the margins, a payload of
    // every kind.
    const Built b = varied();
    const Read  r = read(document(b));
    REQUIRE(r.ok);
    REQUIRE(r.timeline.axis.runs.size() == 1);
    CHECK_FALSE(r.timeline.axis.runs[0].warning);
    CHECK(r.timeline.axis.runs[0].first == 125);
    CHECK(r.timeline.axis.runs[0].last == 199);
    CHECK(r.timeline.axis.runs[0].largest == 3.0);
    CHECK(r.timeline.axis.warnings.empty());
    REQUIRE(r.attributions.size() == 1);
    CHECK(r.attributions[0].searched.ref_index == 74);
    CHECK(r.attributions[0].searched.ref_end == 127);
    const auto corrupt = std::find_if(r.timeline.events.begin(), r.timeline.events.end(),
                                      [](const Event& e) { return e.type == "corrupt_packet"; });
    REQUIRE(corrupt != r.timeline.events.end());
    // "whole", the double 2.0, comes back as the integer 2, as from_json()
    // says; number_of() reads both forms, and nothing else as a number.
    const Value& whole = corrupt->payload[4].second;
    CHECK(std::holds_alternative<std::int64_t>(whole.v));
    CHECK(timeline::number_of(whole) == 2.0);
    CHECK(timeline::number_of(corrupt->payload[3].second) == 0.1);    // "ratio"
    CHECK(std::get<double>(corrupt->payload[5].second.v) == 1e16);    // "big"
    CHECK_FALSE(timeline::number_of(corrupt->payload[0].second));     // "none"
    CHECK_FALSE(timeline::number_of(corrupt->payload[1].second));     // "flag"
    CHECK(std::get<std::string>(corrupt->payload[6].second.v) == "a \"quoted\" line\n, caf\xc3\xa9");
    CHECK_FALSE(timeline::number_of(corrupt->payload[6].second));
    // So does a cue_out's declared duration of 2 s, which WP04 holds as a
    // double.
    const auto cue_of = [](const timeline::Timeline& t) {
        const auto it = std::find_if(t.events.begin(), t.events.end(),
                                     [](const Event& e) { return e.id == "scte35/cue_out/2/0"; });
        REQUIRE(it != t.events.end());
        REQUIRE(it->payload[3].first == "duration");
        return it->payload[3].second;
    };
    CHECK(std::get<double>(cue_of(b.timeline).v) == 2.0);
    const Value duration = cue_of(r.timeline);
    CHECK(std::holds_alternative<std::int64_t>(duration.v));
    CHECK(timeline::number_of(duration) == 2.0);
}

TEST_CASE("timeline::from_json: an unknown schema_version, or another schema, is refused",
          "[timeline][timeline_doc]")
{
    const std::string text = document(ad_ladder());
    const auto refused = [&text](const std::function<void(json::Value&)>& change) {
        json::Value doc = parsed(text);
        change(doc);
        const Read r = read(text_of(doc));
        CHECK_FALSE(r.ok);
        CHECK(r.timeline.events.empty());
        CHECK(r.timeline.variants.empty());
        CHECK(r.attributions.empty());
        CHECK(r.info.report.path.empty());
        return r.err;
    };
    CHECK(refused([](json::Value& d) { at(d, "schema_version") = integer_value(2); }) ==
          "schema_version is 2, which this reader doesn't read: it reads schema version 1 only (D-072)");
    CHECK(refused([](json::Value& d) { at(d, "schema_version") = integer_value(0); }) ==
          "schema_version is 0, which this reader doesn't read: it reads schema version 1 only (D-072)");
    CHECK(refused([](json::Value& d) { at(d, "schema_version") = string_value("1"); }) ==
          "schema_version is a string, not an integer (D-072)");
    // 1.0 is a number, not the integer 1: changed in the text, since a tree
    // written out writes the double 1.0 as 1.
    std::string as_double = text;
    const std::string version = "\"schema_version\": 1,";
    REQUIRE(as_double.find(version) != std::string::npos);
    as_double.replace(as_double.find(version), version.size(), "\"schema_version\": 1.0,");
    const Read r10 = read(as_double);
    CHECK_FALSE(r10.ok);
    CHECK(r10.err == "schema_version is a number, not an integer (D-072)");
    CHECK(refused([](json::Value& d) { erase(d, "", "schema_version"); }) ==
          "no schema_version: a timeline document has one (D-072)");
    CHECK(refused([](json::Value& d) { at(d, "schema") = string_value("mirage-ladder"); }) ==
          R"(not a timeline document: its schema is "mirage-ladder", not "mirage-timeline")");
    CHECK(refused([](json::Value& d) { erase(d, "", "schema"); }) ==
          R"(not a timeline document: its schema is missing, not "mirage-timeline")");

    Read r = read("[]");
    CHECK_FALSE(r.ok);
    CHECK(r.err == "not a timeline document: the text holds an array, not an object");
    r = read("{\"schema\": ");
    CHECK_FALSE(r.ok);
    CHECK(r.err.starts_with("not JSON: line 1, column "));
    // A ladder report isn't one either.
    r = read(R"({"status": "complete", "pairs": []})");
    CHECK(r.err == "not a timeline document: its schema is missing, not \"mirage-timeline\"");
}

TEST_CASE("timeline::from_json: refusals, each naming the key", "[timeline][timeline_doc]")
{
    const std::string text = document(ad_ladder());
    REQUIRE(read(text).ok);
    const auto refused = [&text](const std::function<void(json::Value&)>& change) {
        json::Value doc = parsed(text);
        change(doc);
        const Read r = read(text_of(doc));
        CHECK_FALSE(r.ok);
        CHECK(r.timeline.events.empty());
        return r.err;
    };
    const auto set = [](std::string path, json::Value v) {
        return [path, v](json::Value& d) { at(d, path) = v; };
    };
    // Where v180's and v360's cue-outs are in the timeline, as written.
    json::Value tree = parsed(text);
    const auto index_of = [&tree](std::string_view id) {
        std::size_t i = 0;
        while (i < at(tree, "timeline").array()->size() &&
               *at(tree, std::format("timeline[{}].id", i)).string() != id)
            ++i;
        return i;
    };
    const std::size_t cue = index_of("scte35/cue_out/2/0"), cue0 = index_of("scte35/cue_out/0/0");
    REQUIRE(cue0 < cue);
    const std::string cue_at = std::format("timeline[{}]", cue);

    // Keys: one missing, one unknown deeper in.
    CHECK(refused([](json::Value& d) { erase(d, "", "axis"); }) == "no axis: schema version 1 has it");
    CHECK(refused([&](json::Value& d) {
              std::get<json::Value::Object>(at(d, cue_at).v).emplace_back("extra", json::Value{});
          }) == cue_at + ".extra is no key of schema version 1");
    CHECK(refused([](json::Value& d) { erase(d, "axis.reference", "sequences"); }) ==
          "no axis.reference.sequences: schema version 1 has it");

    // The session part, which a ladder's document holds as null and empty
    // until WP09b fills it, and the sessions an event can name: none.
    CHECK(refused([](json::Value& d) { erase(d, "", "sessions"); }) == "no sessions: schema version 1 has it");
    CHECK(refused([](json::Value& d) { erase(d, "", "session_plan"); }) ==
          "no session_plan: schema version 1 has it");
    CHECK(refused(set("session_plan", json::Value{ json::Value::Object{} })) ==
          "session_plan is an object, not null: a ladder's timeline has no session plan");
    CHECK(refused(set("sessions", json::Value{ json::Value::Array{ json::Value{ json::Value::Object{} } } })) ==
          "sessions has 1 entry, not none: a ladder's timeline has no session");
    CHECK(refused(set("sessions", json::Value{ json::Value::Object{} })) == "sessions is an object, not an array");
    CHECK(refused(set(cue_at + ".session", integer_value(0))) ==
          cue_at + ".session is 0, which names none of the 0 sessions");

    // Kinds of value.
    CHECK(refused(set("ladder_report.model_sha256", integer_value(5))) ==
          "ladder_report.model_sha256 is an integer, not a string or null");
    CHECK(refused(set(cue_at + ".payload", json::Value{ json::Value::Array{} })) ==
          cue_at + ".payload is an array, not an object");
    CHECK(refused(set("axis.frames", string_value("200"))) == "axis.frames is a string, not an integer");
    CHECK(refused(set("options", integer_value(1))) == "options is an integer, not an object");
    CHECK(refused(set("ffmpeg_libraries.libavutil", integer_value(1))) ==
          "ffmpeg_libraries.libavutil is an integer, not a string");

    // Values version 1 fixes.
    CHECK(refused(set("axis.seconds", string_value("pts"))) ==
          R"(axis.seconds is "pts", not "playlist-time" or "frame-grid": schema version 1's)");
    CHECK(refused(set("overlap.scope", string_value("every-variant"))) ==
          R"(overlap.scope is "every-variant", not "curve-inputs": schema version 1's)");
    CHECK(refused(set("overlap.margin_before", string_value("0.5"))) ==
          R"(overlap.margin_before is "0.5", not a rational "num/den")");
    CHECK(refused(set("detection.baseline", string_value("mean-non-low"))) ==
          R"(detection.baseline is "mean-non-low", not "median-non-low", D-077's)");
    CHECK(refused(set("detection.warm_up", integer_value(24))) ==
          "detection.warm_up is 24, not 25, the warm-up of a window of 50 (D-077)");
    CHECK(refused(set("detection.window", integer_value(0))).starts_with("detection: "));
    CHECK(refused(set("detection.session_rules", json::Value{ json::Value::Object{} })) ==
          "detection.session_rules is an object, not null: a ladder's timeline has no session");
    CHECK(refused(set("detection.curves[1].index", integer_value(2))) ==
          "detection.curves[1].index is 2, the pair of none of the variants");

    // Variants.
    CHECK(refused(set("variants[1].id", integer_value(5))) ==
          "variants[1].id is 5, not 1, its place among the variants");
    CHECK(refused(set("variants[1].role", string_value("reference"))) ==
          R"(variants[1].role is "reference", but its pair is 0)");
    CHECK(refused(set("variants[2].pair", integer_value(0))) ==
          "variants[2].pair is 0, the pair of variants[1] too");
    CHECK(refused([](json::Value& d) {
              at(d, "variants[1].role") = string_value("reference");
              at(d, "variants[1].pair") = json::Value{};
          }) == "variants[1] is the reference, and so is variants[0]");
    CHECK(refused(set("variants[0].bandwidth", json::Value{})) ==
          "variants[0].bandwidth is null, not an integer, its entries' highest BANDWIDTH");
    CHECK(refused(set("variants[0].rank_from_top", integer_value(0))) ==
          "variants[0].rank_from_top is 0, not a rank from 1");
    CHECK(refused(set("variants[0].segment_map", string_value("maybe"))) ==
          R"(variants[0].segment_map is "maybe", neither "ok" nor a refusal that starts "refused: " )"
          R"((D-081 (c)))");
    CHECK(refused(set("variants[2].playlist.url", string_value("v180.m3u8"))) ==
          "variants[2].playlist.url disagrees with the variant's url");

    // Events: their vocabulary, their ids, their variants, their seconds.
    CHECK(refused(set(cue_at + ".type", string_value("segment"))) ==
          cue_at + R"(.type is "segment", no type of the scte35 source in schema version 1's vocabulary )"
                   R"((eval/phase3-survey/design.md §5.9))");
    CHECK(refused(set(cue_at + ".source", string_value("ad"))) ==
          cue_at + R"(.source is "ad", none of section 3.1's sources)");
    CHECK(refused(set(cue_at + ".id", string_value("scte35/cue_out/0/0"))) ==
          std::format(R"({}.id is "scte35/cue_out/0/0", the id of timeline[{}] too)", cue_at, cue0));
    CHECK(refused(set(cue_at + ".id", string_value(""))) == cue_at + ".id is empty");
    CHECK(refused(set(cue_at + ".variant", integer_value(3))) ==
          cue_at + ".variant is 3, which names none of the 3 variants");
    CHECK(refused(set(cue_at + ".t", json::Value{ 3.04 })) ==
          cue_at + ".t disagrees with the axis's seconds of its ref_index");
    CHECK(refused(set(cue_at + ".t_end", integer_value(5))) ==
          cue_at + ".t_end disagrees with the axis's seconds of its ref_end");
    CHECK(refused(set(cue_at + ".pts", integer_value(11))) ==
          cue_at + ".pts disagrees with pts_ticks on the axis's time base");
    CHECK(refused(set(cue_at + ".seq", integer_value(std::int64_t{ 1 } << 40))) ==
          cue_at + ".seq is 1099511627776, beyond a sequence's 32 bits");
    CHECK(refused(set(cue_at + ".wallclock", json::Value{ 0.0000005 })) ==
          cue_at + ".wallclock is 5e-07 s, not a whole number of microseconds");

    // Events: their media keys, the axis's at their frames (Event::key); the
    // sources a session has; the wall clock observed events alone have.
    CHECK(refused(set(cue_at + ".seq", integer_value(0))) ==
          cue_at + ".seq disagrees with the axis's media key of its ref_index");
    CHECK(refused(set(cue_at + ".seq", integer_value(-1))) == cue_at + ".seq is -1, not a sequence from 0");
    CHECK(refused([&](json::Value& d) {
              at(d, cue_at + ".pts_ticks") = json::Value{};
              at(d, cue_at + ".pts")       = json::Value{};
          }) == cue_at + ".pts_ticks disagrees with the axis's media key of its ref_index");
    CHECK(refused([&](json::Value& d) {
              at(d, cue_at + ".source") = string_value("network");
              at(d, cue_at + ".type")   = string_value("playlist_request");
          }) == cue_at + " is a network event without a session: the network source's events are a session's "
                         "(D-068)");
    CHECK(refused(set(cue_at + ".wallclock", json::Value{ 1.5 })) ==
          cue_at + ".wallclock is 1.5 s, but a scte35 event is offline and has no wall clock (D-073)");

    // The axis's stretches.
    CHECK(refused(set("axis.ticks[0].first", integer_value(1))) ==
          "axis.ticks[0].first is 1, not 0, the frame the stretch before ends at");
    CHECK(refused(set("axis.ticks[0].frames", integer_value(199))) ==
          "axis.ticks covers 199 of the axis's 200 frames");
    CHECK(refused(set("axis.ticks[0].frames", integer_value(201))) ==
          "axis.ticks[0].frames is 201: the stretches cover the axis's 200 frames, a frame or more each");
    CHECK(refused(set("axis.keys[1].first_pts_ticks", json::Value{})) ==
          "axis.keys[1].step is 3600, not 0, for frames without a PTS");
    CHECK(refused(set("axis.ticks[0].step", integer_value(std::numeric_limits<std::int64_t>::max()))) ==
          "axis.ticks[0]'s values go past 64 bits");
    CHECK(refused(set("axis.ticks", json::Value{ json::Value::Array{} })) ==
          "axis.ticks covers 0 of the axis's 200 frames");
    CHECK(refused(set("axis.keys", json::Value{ json::Value::Array{} })) ==
          "axis.keys covers 0 of the axis's 200 frames");
    CHECK(refused(set("axis.frames", integer_value((std::int64_t{ 1 } << 24) + 1))) ==
          "axis.frames is 16777217, more than the 16777216 frames this reader expands");
    // At that limit the expanded ticks and keys take 2^24 x (8 + 24) bytes,
    // 512 MiB, on this build, as src/TimelineDoc.cpp and
    // docs/TIMELINE_FORMAT.md state it.
    CHECK((std::size_t{ 1 } << 24) * (sizeof(std::int64_t) + sizeof(timeline::MediaKey)) ==
          std::size_t{ 512 } << 20);
    // The axis's clock and its check, paired as build_axis() pairs them.
    CHECK(refused(set("axis.checked", string_value("report-only"))) ==
          R"(axis.checked is "report-only", not "segment-map", the check that goes with axis.seconds )"
          R"("playlist-time" (design §4.2))");
    CHECK(refused(set("axis.seconds", string_value("frame-grid"))) ==
          R"(axis.checked is "segment-map", not "report-only", the check that goes with axis.seconds )"
          R"("frame-grid" (design §4.2))");
    CHECK(refused(set("axis.tick_den", integer_value(0))) ==
          "axis.tick_den is 0, not a positive number of ticks per second");
    CHECK(refused(set("axis.reference.sequences[1].first_pts", integer_value(11))) ==
          "axis.reference.sequences[1].first_pts disagrees with first_pts_ticks on the time base");

    // Attributions: their curve, their groups, the events they name and
    // the offsets they give.
    CHECK(refused(set("attributions[0].curve.index", integer_value(9))) ==
          "attributions[0].curve.index is 9, the pair of none of the variants");
    CHECK(refused(set("attributions[0].curve.kind", string_value("session"))) ==
          R"(attributions[0].curve.kind is "session", not "pair": schema version 1's)");
    CHECK(refused([](json::Value& d) {
              std::get<json::Value::Array>(at(d, "attributions[0].overlapping_events").v).pop_back();
          }) == "attributions[0].overlapping_events has 5 groups, not one per source, 6");
    CHECK(refused([](json::Value& d) {
              auto& g = std::get<json::Value::Array>(at(d, "attributions[0].overlapping_events").v);
              std::swap(g[0], g[1]);
          }) == R"(attributions[0].overlapping_events[0].source is "network", not "vmaf": schema version 1's)");
    const std::string first_cue = "attributions[0].overlapping_events[3].events[0]";
    CHECK(refused(set(first_cue + ".event", string_value("scte35/cue_out/9/0"))) ==
          first_cue + R"(.event is "scte35/cue_out/9/0", the id of no event of the timeline)");
    CHECK(refused(set(first_cue + ".event", string_value("playlist/segment/0/3"))) ==
          first_cue + R"(.event is "playlist/segment/0/3", an event of the playlist source, not of scte35)");
    const std::string ad_break = "attributions[0].overlapping_events[3].events[2]";
    CHECK(refused(set(first_cue + ".offset_frames", integer_value(1))) ==
          first_cue + ".offset_frames disagrees with its event's ref_index");
    CHECK(refused(set(ad_break + ".offset_end_frames", json::Value{})) ==
          ad_break + ".offset_end_frames disagrees with its event's ref_end");
    CHECK(refused(set(ad_break + ".offset_end", integer_value(3))) ==
          ad_break + ".offset_end disagrees with the axis's seconds from the interval's start to its event's end");
    CHECK(refused(set("attributions[0].interval.t", integer_value(4))) ==
          "attributions[0].interval.t disagrees with the axis's seconds of its ref_index");
    CHECK(refused(set("attributions[0].interval.points", integer_value(-1))) ==
          "attributions[0].interval.points is -1, not a count from 0");
    // A detected drop needs the detection that found it (D-077).
    CHECK(refused(set("detection", json::Value{})) ==
          "attributions[0].detected is true, but detection is null: a detected drop needs the detection "
          "that found it (D-077)");

    // What it takes that mirage_vmaf timeline never writes: an interval a
    // user gave, detected false, whether detection ran or not
    // (src/TimelineDoc.h).
    for (const bool with_detection : { true, false }) {
        json::Value doc = parsed(text);
        at(doc, "attributions[0].detected") = json::Value{ false };
        if (!with_detection) at(doc, "detection") = json::Value{};
        const Read r = read(text_of(doc));
        INFO(r.err);
        REQUIRE(r.ok);
        REQUIRE(r.attributions.size() == 1);
        CHECK_FALSE(r.attributions[0].detected);
        CHECK(r.info.detection.has_value() == with_detection);
    }
}

TEST_CASE("timeline::to_json: what breaks its preconditions is written, and from_json() refuses it",
          "[timeline][timeline_doc]")
{
    // to_json() checks nothing (src/TimelineDoc.h): each input below breaks one
    // of its preconditions and is written all the same, and from_json() of the
    // text refuses it, naming the first key that is wrong.
    const Built good = ad_ladder();
    REQUIRE(read(document(good)).ok);
    const auto refusal = [](const Built& b) {
        const Read r = read(document(b));
        CHECK_FALSE(r.ok);
        return r.err;
    };
    const auto position = [](const Built& b, std::string_view id) {
        json::Value doc = parsed(document(b));
        const json::Value::Array& events = *at(doc, "timeline").array();
        std::size_t i = 0;
        while (i < events.size() && *events[i].find("id")->string() != id) ++i;
        return std::format("timeline[{}]", i);
    };
    const std::string cue_id = "scte35/cue_out/2/0";
    const auto cue_index = static_cast<std::size_t>(
        std::find_if(good.timeline.events.begin(), good.timeline.events.end(),
                     [&](const Event& e) { return e.id == cue_id; }) -
        good.timeline.events.begin());
    REQUIRE(cue_index < good.timeline.events.size());
    const std::string cue_at = position(good, cue_id);

    // Events without ids, as before assign_ids(), and one id twice.
    Built b = good;
    for (Event& e : b.timeline.events) e.id.clear();
    CHECK(refusal(b) == "timeline[0].id is empty");
    b = good;
    b.timeline.events[1].id = b.timeline.events[0].id;
    CHECK(refusal(b) ==
          std::format(R"(timeline[1].id is "{}", the id of timeline[0] too)", good.timeline.events[0].id));

    // A payload with one name twice, which no JSON object holds.
    b = good;
    b.timeline.events[cue_index].payload = { { "line", Value{ std::int64_t{ 1 } } },
                                             { "line", Value{ std::int64_t{ 2 } } } };
    const std::string twice = refusal(b);
    CHECK(twice.starts_with("not JSON: "));
    CHECK(twice.find(R"(the key "line" again in one object)") != std::string::npos);

    // A variant whose index isn't its place, two references, two variants of
    // one pair.
    b = good;
    b.timeline.variants[1].index = 5;
    CHECK(refusal(b) == "variants[1].id is 5, not 1, its place among the variants");
    b = good;
    b.timeline.variants[1].pair = -1;
    CHECK(refusal(b) == "variants[1] is the reference, and so is variants[0]");
    b = good;
    b.timeline.variants[2].pair = 0;
    CHECK(refusal(b) == "variants[2].pair is 0, the pair of variants[1] too");

    // Detection's options that check_options() refuses, and a detected
    // attribution without the detection.
    b = good;
    b.info.detection->options.window = 0;
    CHECK(refusal(b) == "detection: the window W wants a whole number of points from 1, not 0");
    b = good;
    b.info.detection.reset();
    CHECK(refusal(b) == "attributions[0].detected is true, but detection is null: a detected drop needs the "
                        "detection that found it (D-077)");

    // An event whose media key isn't the axis's, one with a session, an
    // offline one with a wall clock, a network one without a session.
    b = good;
    b.timeline.events[cue_index].key.sequence = 0;
    CHECK(refusal(b) == cue_at + ".seq disagrees with the axis's media key of its ref_index");
    b = good;
    b.timeline.events[cue_index].session = 0;
    CHECK(refusal(b) == cue_at + ".session is 0, which names none of the 0 sessions");
    b = good;
    b.timeline.events[cue_index].wallclock_us = 1500000;
    CHECK(refusal(b) ==
          cue_at + ".wallclock is 1.5 s, but a scte35 event is offline and has no wall clock (D-073)");
    b = good;
    b.timeline.events[cue_index].source = Source::Network;
    b.timeline.events[cue_index].type   = "playlist_request";
    CHECK(refusal(b) == position(b, cue_id) + " is a network event without a session: the network source's "
                                              "events are a session's (D-068)");

    // A Found past the events, written as null, is the byte-identical test
    // case's.
}

// ── The report's provenance and the reference ────────────────────────────────

TEST_CASE("timeline::report_info and reference_info: what a document copies from its report",
          "[timeline][timeline_doc]")
{
    timeline::LadderScores s = ad_report();
    const timeline::ReportInfo r = timeline::report_info(s);
    CHECK(r.path.empty());
    CHECK(r.sha256.empty());
    CHECK(r.scores_sha256.empty());
    CHECK(r.status == "complete");
    CHECK(r.reference_mode == "top-rung");
    CHECK(r.model == "vmaf_v0.6.1");
    CHECK(r.model_source == "built-in");
    CHECK(r.model_sha256.empty());
    CHECK(r.scaler == "bicubic");
    CHECK(r.libvmaf_version == "1.2.3");
    CHECK(r.libvmaf_package_version == "1.2.4");
    CHECK(r.master_url == s.master_url);
    CHECK(r.master_location == s.master_location);
    CHECK(r.master_sha256 == s.master_sha256);

    // The reference's sequences and frame rate are those of the pair whose
    // reference build_axis() takes: the one that read the most reference
    // frames, the first of equal ones. reference_info() takes the axis made
    // of the same scores, whose time base is that pair's.
    timeline::ReferenceInfo ref;
    std::string             err;
    const auto of = [&](const timeline::LadderScores& scores) {
        const timeline::Axis axis = timeline::build_axis(scores, nullptr, nullptr);
        const bool           ok   = timeline::reference_info(scores, axis, ref, err);
        CHECK(ok == err.empty());
        return ok;
    };
    REQUIRE(of(s));
    CHECK(ref.url == url_of(0));
    CHECK(same(ref.frame_rate, AVRational{ 25, 1 }));
    REQUIRE(ref.sequences.size() == 2);
    CHECK(ref.sequences[1].first_index == 75);

    s.pairs[1].ref_time_base = { 1, 1000 };
    s.pairs[1].ref_rate      = { 50, 1 };
    s.pairs[1].ref_sequences = { { .first_index = 0, .first_pts = 10000, .frames = 201 } };
    REQUIRE(of(s));
    CHECK(same(ref.frame_rate, AVRational{ 50, 1 }));
    REQUIRE(ref.sequences.size() == 1);
    CHECK(ref.sequences[0].frames == 201);
    CHECK(same(timeline::build_axis(s, nullptr, nullptr).time_base, AVRational{ 1, 1000 }));

    // An axis that wasn't built on the pair reference_info() takes, here
    // one made of the plain report, on 1/90000, is refused: the sequences'
    // first_pts_ticks would be on 1/1000.
    const timeline::Axis other = timeline::build_axis(ad_report(), nullptr, nullptr);
    CHECK_FALSE(timeline::reference_info(s, other, ref, err));
    CHECK(err == "pair 1, whose reference the axis takes by build_axis()'s rule, reads it with time base "
                 "1/1000, but the axis has 1/90000: the axis wasn't built from these scores, or its rule has "
                 "changed");
    CHECK(ref.url.empty());
    CHECK(ref.sequences.empty());

    s.pairs[1].ref_sequences[0].frames = 200;   // as many as pair 0's: pair 0's
    REQUIRE(of(s));
    CHECK(same(ref.frame_rate, AVRational{ 25, 1 }));
    CHECK(same(timeline::build_axis(s, nullptr, nullptr).time_base, kTs));
    CHECK(timeline::reference_info(s, other, ref, err));

    s.pairs.clear();
    REQUIRE(of(s));
    CHECK(ref.url == url_of(0));
    CHECK(same(ref.frame_rate, AVRational{ 0, 1 }));
    CHECK(ref.sequences.empty());
}

// ── Writing the file ─────────────────────────────────────────────────────────

TEST_CASE("timeline::write_document: through a temporary file renamed over the path",
          "[timeline][timeline_doc]")
{
    const fs::path dir = testtemp::path("timeline_doc");
    fs::create_directories(dir);
    const fs::path out = dir / "ad.timeline.json";
    const std::string text = document(ad_ladder());
    std::string err;
    REQUIRE(timeline::write_document(out.string(), text, err));
    CHECK(err.empty());
    const auto contents = [](const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        return s.str();
    };
    CHECK(contents(out) == text);
    // Replaces a document there, and leaves no temporary file.
    REQUIRE(timeline::write_document(out.string(), "{}\n", err));
    CHECK(contents(out) == "{}\n");
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir)) files.push_back(e.path().filename());
    CHECK(files == std::vector<fs::path>{ "ad.timeline.json" });

    // A directory that isn't there, and a path that is a directory: an
    // error, and nothing left behind.
    const fs::path missing = dir / "missing" / "t.json";
    CHECK_FALSE(timeline::write_document(missing.string(), text, err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(fs::exists(missing));
    const fs::path taken = dir / "taken";
    fs::create_directories(taken / "child");
    err.clear();
    CHECK_FALSE(timeline::write_document(taken.string(), text, err));
    CHECK_FALSE(err.empty());
    CHECK(fs::is_directory(taken / "child"));
    CHECK_FALSE(fs::exists(dir / "taken.partial"));
}
