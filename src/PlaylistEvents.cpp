#include "PlaylistEvents.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <climits>
#include <cmath>
#include <format>
#include <iterator>
#include <limits>
#include <tuple>
#include <utility>

namespace timeline {

namespace {

constexpr auto npos = std::string_view::npos;

// ── Text ─────────────────────────────────────────────────────────────────────

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

bool is_blank(char c) { return c == ' ' || c == '\t'; }

// Two ASCII names, compared without regard to case.
bool same_name(std::string_view a, std::string_view b)
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x))
                   == std::tolower(static_cast<unsigned char>(y));
           });
}

// A decimal-floating-point (RFC 8216 §4.2): digits and at most one '.', at
// least one digit, no sign.
bool is_decimal(std::string_view s)
{
    bool digit = false, dot = false;
    for (char c : s) {
        if (c >= '0' && c <= '9') digit = true;
        else if (c == '.' && !dot) dot = true;
        else return false;
    }
    return digit;
}

// A hexadecimal-sequence (RFC 8216 §4.2): 0x or 0X, then hex digits.
bool is_hex(std::string_view s)
{
    return s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')
        && std::all_of(s.begin() + 2, s.end(),
                       [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

// The value of a decimal-floating-point (is_decimal(s)), the double nearest it.
double decimal_value(std::string_view s)
{
    double v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

// `seconds` as the rational its shortest decimal text names, the text
// json::number writes: 29.988 as 7497/250, not as the binary fraction the
// double holds, so that a duration written in decimal ends where it says
// (exact time, design §13, P-A). nullopt for a value that isn't finite and
// non-negative, and for one that no AVRational holds exactly, its numerator
// or its denominator above INT_MAX once reduced, which av_reduce() would
// clamp to the nearest fraction it can hold: FrameAfter is given exact
// durations only (src/PlaylistEvents.h).
std::optional<AVRational> exact_seconds(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0) return std::nullopt;
    const std::string s = std::format("{}", seconds);   // digits[.digits][e[+|-]digits]
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    std::int64_t num = 0;
    int          scale = 0;   // the value is num * 10^scale
    bool         fits = true, fraction = false;
    std::size_t  i = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '.') {
            fraction = true;
        } else if (c >= '0' && c <= '9') {
            if (num > (kMax - 9) / 10) {
                fits = false;
                break;
            }
            num = num * 10 + (c - '0');
            if (fraction) --scale;
        } else {
            break;
        }
    }
    if (fits && i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        std::size_t from = i + 1;
        if (from < s.size() && s[from] == '+') ++from;   // std::from_chars takes no '+'
        int e = 0;
        std::from_chars(s.data() + from, s.data() + s.size(), e);
        scale += e;
    }
    std::int64_t den = 1;
    for (; fits && scale > 0; --scale) {
        if (num > kMax / 10) fits = false;
        else num *= 10;
    }
    // Each power of ten of the denominator less the factor of it that num
    // shares (10, 5, 2 or none), so that den is the reduced fraction's and
    // passes an int64's range only when the reduced fraction's does.
    for (; fits && scale < 0; ++scale) {
        const std::int64_t shared = num % 10 == 0 ? 10 : num % 5 == 0 ? 5 : num % 2 == 0 ? 2 : 1;
        num /= shared;
        if (den > kMax / (10 / shared)) fits = false;
        else den *= 10 / shared;
    }
    // A term that no int64 holds is above INT_MAX too; av_reduce() reports
    // a fraction that it had to clamp to INT_MAX as inexact.
    AVRational q{ 0, 1 };
    if (!fits || !av_reduce(&q.num, &q.den, num, den, INT_MAX)) return std::nullopt;
    return q;
}

// ── Payloads ─────────────────────────────────────────────────────────────────

Value int_value(std::int64_t v) { return Value{ v }; }
Value text_value(std::string_view s) { return Value{ std::string(s) }; }

const Value* member(const Payload& p, std::string_view name)
{
    for (const auto& [n, v] : p)
        if (n == name) return &v;
    return nullptr;
}

std::optional<std::int64_t> int_member(const Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    if (const auto* i = v ? std::get_if<std::int64_t>(&v->v) : nullptr) return *i;
    return std::nullopt;
}

// A member that is a number, integer or not.
std::optional<double> number_member(const Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    if (!v) return std::nullopt;
    if (const auto* d = std::get_if<double>(&v->v)) return *d;
    if (const auto* i = std::get_if<std::int64_t>(&v->v)) return static_cast<double>(*i);
    return std::nullopt;
}

// ── Tags ─────────────────────────────────────────────────────────────────────

// Tags whose content the playlist's header (hls::MediaTimeline) or a
// segment's event (segment_event()) holds: they make no event of their own.
bool held_elsewhere(std::string_view name)
{
    static constexpr std::string_view kHeld[] = {
        "EXTM3U", "EXT-X-VERSION", "EXT-X-TARGETDURATION", "EXT-X-MEDIA-SEQUENCE",
        "EXT-X-DISCONTINUITY-SEQUENCE", "EXT-X-PLAYLIST-TYPE", "EXT-X-ENDLIST",
        "EXT-X-INDEPENDENT-SEGMENTS", "EXTINF", "EXT-X-BYTERANGE", "EXT-X-GAP",
    };
    return std::find(std::begin(kHeld), std::end(kHeld), name) != std::end(kHeld);
}

// The value of `tag`, trimmed, viewing the tag's own text; empty without one.
std::string_view value_of(const PlaylistTag& tag)
{
    return tag.value ? trim(*tag.value) : std::string_view();
}

// The line of `tag` as written, trimmed.
std::string raw_line(const PlaylistTag& tag)
{
    return tag.value ? std::format("#{}:{}", tag.name, *tag.value) : "#" + tag.name;
}

// The media sequence number of segment `segment_index`; nullopt for -1, none.
std::optional<std::int64_t> segment_seq(const PlaylistEvents& p, int segment_index)
{
    if (segment_index < 0) return std::nullopt;
    return p.timeline.segments[static_cast<std::size_t>(segment_index)].seq;
}

// An event of `tag`, not placed, its payload begun with the tag's line.
Event tag_event(const PlaylistEvents& p, const PlaylistTag& tag, Source source,
                std::string_view type)
{
    Event e;
    e.source = source;
    e.type.assign(type);
    e.variant   = p.variant;
    e.segment   = segment_seq(p, tag.segment_index);
    e.ref_index = kUnplaced;
    e.payload.emplace_back("line", int_value(tag.line));
    return e;
}

// A message `tag` embeds, handed on for the decoder as written (design §5.5),
// its anchor's segment -1 when no segment follows the tag.
void add_message(PlaylistEvents& p, const PlaylistTag& tag, std::string form, std::string_view text)
{
    CueInput c;
    c.carrier = CueCarrier::PlaylistTag;
    c.anchor  = PlaylistAnchor{ .variant = p.variant,
                                .segment = segment_seq(p, tag.segment_index).value_or(-1),
                                .line    = tag.line };
    c.form = std::move(form);
    c.bytes.assign(text.begin(), text.end());
    p.messages.push_back(std::move(c));
}

// The attribute named `name`, without regard to case; null when none is.
const Attribute* find_attribute(const std::vector<Attribute>& attrs, std::string_view name)
{
    for (const Attribute& a : attrs)
        if (same_name(a.name, name)) return &a;
    return nullptr;
}

// A cue tag's number: `text` as a decimal, or null, with `err` naming the
// first that isn't one.
Value cue_number(std::string_view text, std::string_view what, std::string& err)
{
    if (is_decimal(text)) return Value{ decimal_value(text) };
    if (err.empty()) err = std::format("its {} '{}' isn't a decimal number", what, text);
    return Value{};
}

// The attributes of a cue tag, each as written: no specification gives
// their types (scte35.md §2).
Value cue_attributes(const std::vector<Attribute>& attrs)
{
    Value::Object o;
    for (const Attribute& a : attrs) o.emplace_back(a.name, text_value(a.value));
    return Value{ std::move(o) };
}

// The attribute list `list` of a cue tag, or none with `err` set.
std::vector<Attribute> cue_attribute_list(std::string_view list, std::string& err)
{
    std::string why;
    std::vector<Attribute> attrs = parse_attributes(list, why);
    if (!why.empty() && err.empty()) err = "its attributes don't parse: " + why;
    return attrs;
}

// A cue tag's SCTE35= attribute, handed on as a message: "every message a
// tag embeds" (design §5.5).
void add_cue_message(PlaylistEvents& p, const PlaylistTag& tag, std::string_view form,
                     const std::vector<Attribute>& attrs)
{
    if (const Attribute* m = find_attribute(attrs, "SCTE35"))
        add_message(p, tag, std::format("{} SCTE35", form), m->value);
}

// A cue event's payload after its line: form and raw line.
void begin_cue(Event& e, const PlaylistTag& tag, std::string_view form)
{
    e.payload.emplace_back("form", text_value(form));
    e.payload.emplace_back("raw", text_value(raw_line(tag)));
}

// EXT-X-CUE-OUT: bare; ":30.000"; ":DURATION=120"; ":10.50,K=V";
// ":ID=\"…\",X-TYPE=\"EABN\",DURATION=…" (scte35.md §2.1).
void add_cue_out(PlaylistEvents& p, const PlaylistTag& tag)
{
    Event e = tag_event(p, tag, Source::Scte35, kTypeCueOut);
    begin_cue(e, tag, "CUE-OUT");
    std::string err;
    Value duration;
    std::vector<Attribute> attrs;
    const std::string_view v = value_of(tag);
    if (!v.empty()) {
        // A leading number is the duration; attributes may follow it.
        const std::size_t comma = v.find(',');
        std::string_view rest = v;
        if (const std::string_view lead = trim(v.substr(0, comma)); is_decimal(lead)) {
            duration = Value{ decimal_value(lead) };
            rest = comma == npos ? std::string_view() : v.substr(comma + 1);
        }
        attrs = cue_attribute_list(rest, err);
        if (const Attribute* d = find_attribute(attrs, "DURATION");
            d && std::holds_alternative<std::monostate>(duration.v))
            duration = cue_number(d->value, "DURATION", err);
    }
    e.payload.emplace_back("duration", std::move(duration));
    e.payload.emplace_back("attributes", cue_attributes(attrs));
    if (!err.empty()) e.payload.emplace_back("error", text_value(err));
    p.cues.push_back(std::move(e));
    add_cue_message(p, tag, "CUE-OUT", attrs);
}

// EXT-X-CUE-OUT-CONT: elapsed/duration ("10/30"), or the ElapsedTime=,
// Duration= and SCTE35= attributes (scte35.md §2.2).
void add_cue_out_cont(PlaylistEvents& p, const PlaylistTag& tag)
{
    Event e = tag_event(p, tag, Source::Scte35, kTypeCueOutCont);
    begin_cue(e, tag, "CUE-OUT-CONT");
    std::string err;
    Value elapsed, duration;
    std::vector<Attribute> attrs;
    const std::string_view v = value_of(tag);
    if (!v.empty()) {
        if (const std::size_t slash = v.find('/'); slash != npos && v.find('=') == npos) {
            elapsed  = cue_number(trim(v.substr(0, slash)), "elapsed time", err);
            duration = cue_number(trim(v.substr(slash + 1)), "duration", err);
        } else {
            attrs = cue_attribute_list(v, err);
            if (const Attribute* a = find_attribute(attrs, "ElapsedTime"))
                elapsed = cue_number(a->value, "ElapsedTime", err);
            if (const Attribute* a = find_attribute(attrs, "Duration"))
                duration = cue_number(a->value, "Duration", err);
        }
    }
    e.payload.emplace_back("elapsed", std::move(elapsed));
    e.payload.emplace_back("duration", std::move(duration));
    e.payload.emplace_back("attributes", cue_attributes(attrs));
    if (!err.empty()) e.payload.emplace_back("error", text_value(err));
    p.cues.push_back(std::move(e));
    add_cue_message(p, tag, "CUE-OUT-CONT", attrs);
}

// EXT-X-CUE-IN: bare, or ":ID=\"…\"" (scte35.md §2.3).
void add_cue_in(PlaylistEvents& p, const PlaylistTag& tag)
{
    Event e = tag_event(p, tag, Source::Scte35, kTypeCueIn);
    begin_cue(e, tag, "CUE-IN");
    std::string err;
    std::vector<Attribute> attrs;
    if (const std::string_view v = value_of(tag); !v.empty())
        attrs = cue_attribute_list(v, err);
    e.payload.emplace_back("attributes", cue_attributes(attrs));
    if (!err.empty()) e.payload.emplace_back("error", text_value(err));
    p.cues.push_back(std::move(e));
    add_cue_message(p, tag, "CUE-IN", attrs);
}

bool is_scte35_attribute(std::string_view name)
{
    return name == "SCTE35-CMD" || name == "SCTE35-OUT" || name == "SCTE35-IN";
}

// Why `a` isn't of the type RFC 8216 §4.3.2.7 gives an EXT-X-DATERANGE
// attribute of its name; empty when it is, or when the RFC gives the name
// no type. These are the type errors that make a DATERANGE malformed,
// which the owner's answer to Q47 (D-106 (e)) leaves as they are
// (add_daterange()): ID, CLASS,
// START-DATE or END-DATE not a quoted-string (the dates in START-DATE and
// END-DATE aren't read); DURATION or PLANNED-DURATION not a
// decimal-floating-point; SCTE35-CMD, SCTE35-OUT or SCTE35-IN not a
// hexadecimal-sequence, whose digits are taken here in either case where
// RFC 8216 §4.2 gives [A..F] only; END-ON-NEXT not the enumerated-string
// YES; an X- attribute neither a quoted-string, a hexadecimal-sequence nor
// a decimal-floating-point. An attribute of any other name is taken with
// whatever value it has.
std::string daterange_type_error(const Attribute& a)
{
    const std::string_view n = a.name;
    if (n == "ID" || n == "CLASS" || n == "START-DATE" || n == "END-DATE") {
        if (!a.quoted) return std::format("{} isn't a quoted-string (RFC 8216 §4.3.2.7)", n);
    } else if (n == "DURATION" || n == "PLANNED-DURATION") {
        if (a.quoted || !is_decimal(a.value))
            return std::format("{} isn't a decimal-floating-point (RFC 8216 §4.3.2.7)", n);
    } else if (is_scte35_attribute(n)) {
        if (a.quoted || !is_hex(a.value))
            return std::format("{} isn't a hexadecimal-sequence (RFC 8216 §4.3.2.7)", n);
    } else if (n == "END-ON-NEXT") {
        if (a.quoted || a.value != "YES") return "END-ON-NEXT isn't YES (RFC 8216 §4.3.2.7)";
    } else if (n.starts_with("X-")) {
        if (!a.quoted && !is_hex(a.value) && !is_decimal(a.value))
            return std::format("{} is neither a quoted-string, a hexadecimal-sequence nor a "
                               "decimal-floating-point (RFC 8216 §4.3.2.7)", n);
    }
    return {};
}

// The pieces of an attribute list that parse_attributes() refuses, for the
// SCTE35-* attributes a malformed DATERANGE still hands on (D-106 (e)): the
// list split at each ',' outside a quoted-string, each piece without the
// blanks around it, its name up to its first '=' and its value after it,
// quoted when it starts and ends with '"'; a piece without '=' is a name
// with an empty value. A quote that never closes holds the rest of the
// list, where no attribute can be told from another.
std::vector<Attribute> split_attributes(std::string_view list)
{
    std::vector<Attribute> out;
    const auto piece = [&out](std::string_view s) {
        while (!s.empty() && is_blank(s.front())) s.remove_prefix(1);
        while (!s.empty() && is_blank(s.back())) s.remove_suffix(1);
        Attribute a;
        const std::size_t eq = s.find('=');
        a.name.assign(s.substr(0, eq));
        if (eq != npos) {
            std::string_view v = s.substr(eq + 1);
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                a.quoted = true;
                v        = v.substr(1, v.size() - 2);
            }
            a.value.assign(v);
        }
        out.push_back(std::move(a));
    };
    bool        quoted = false;
    std::size_t from   = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i] == '"') quoted = !quoted;
        else if (list[i] == ',' && !quoted) {
            piece(list.substr(from, i - from));
            from = i + 1;
        }
    }
    piece(list.substr(from));
    return out;
}

// EXT-X-DATERANGE: its SCTE35-* messages handed on, or a daterange event
// with every attribute; a malformed one is kept raw, with the reason, and
// hands on its SCTE35-* attributes that are well-formed hexadecimal-sequences
// (design §5.4, §5.5; D-106 (e)).
void add_daterange(PlaylistEvents& p, const PlaylistTag& tag)
{
    const std::string value = tag.value.value_or("");
    std::string err;
    const std::vector<Attribute> attrs = parse_attributes(value, err);
    const bool parsed = err.empty();
    for (const Attribute& a : attrs) {
        if (!err.empty()) break;
        err = daterange_type_error(a);
    }
    // Here a DATERANGE is malformed when parse_attributes() refuses its
    // attribute list, or one of its attributes isn't of its type
    // (daterange_type_error()), which the owner's answer to Q47 (D-106 (e))
    // leaves as it is. Not refused: a DATERANGE without ID or without
    // START-DATE, although RFC 8216 §4.3.2.7 requires both, an empty one
    // included; one that breaks that section's rules between attributes or
    // between tags (an END-DATE before its START-DATE, or other than
    // START-DATE plus DURATION where both are given; END-ON-NEXT without
    // CLASS, or with DURATION or END-DATE; two tags of one ID giving an
    // attribute different values; a playlist without
    // EXT-X-PROGRAM-DATE-TIME); and the departures from RFC 8216 §4.2's
    // grammar that parse_attributes() accepts.
    if (err.empty()
        && std::any_of(attrs.begin(), attrs.end(),
                       [](const Attribute& a) { return is_scte35_attribute(a.name); })) {
        for (const Attribute& a : attrs)
            if (is_scte35_attribute(a.name)) add_message(p, tag, "DATERANGE " + a.name, a.value);
        return;
    }
    // A malformed one hands on each SCTE35-* attribute that is itself a
    // well-formed hexadecimal-sequence, whatever else is malformed (D-106
    // (e)): of the attributes parse_attributes() read, or of the pieces of a
    // list it refused. The decoder warns of each (src/Scte35Events.h).
    if (!err.empty())
        for (const Attribute& a : parsed ? attrs : split_attributes(value))
            if (is_scte35_attribute(a.name) && !a.quoted && is_hex(a.value))
                add_message(p, tag, "DATERANGE " + a.name, a.value);
    Event e = tag_event(p, tag, Source::Playlist, kTypeDaterange);
    e.payload.emplace_back("raw", text_value(value));
    Value::Object o;
    if (err.empty()) {
        // A decimal-floating-point as a number; a quoted-string's characters;
        // a hexadecimal-sequence, or an enumerated-string, as written.
        for (const Attribute& a : attrs)
            o.emplace_back(a.name, !a.quoted && is_decimal(a.value) ? Value{ decimal_value(a.value) }
                                                                    : text_value(a.value));
    }
    e.payload.emplace_back("attributes", Value{ std::move(o) });
    if (!err.empty()) e.payload.emplace_back("error", text_value(err));
    p.events.push_back(std::move(e));
}

// The events and messages of one tag.
void add_tag(PlaylistEvents& p, const PlaylistTag& tag)
{
    const std::string& name = tag.name;
    if (held_elsewhere(name)) return;
    if (name == "EXT-X-DISCONTINUITY") {
        Event e = tag_event(p, tag, Source::Playlist, kTypeDiscontinuity);
        // The sequence it starts: hls::parse_media_timeline counts the
        // discontinuity tags before a segment once.
        e.payload.emplace_back(
            "discontinuity_sequence",
            tag.segment_index < 0
                ? Value{}
                : int_value(p.timeline.segments[static_cast<std::size_t>(tag.segment_index)].disc_seq));
        p.events.push_back(std::move(e));
    } else if (name == "EXT-X-PROGRAM-DATE-TIME") {
        Event e = tag_event(p, tag, Source::Playlist, kTypeProgramDateTime);
        const double unix = tag.value ? hls::parse_iso8601(*tag.value) : hls::kNaN;
        e.payload.emplace_back("date", tag.value ? text_value(*tag.value) : Value{});
        e.payload.emplace_back("unix", std::isnan(unix) ? Value{} : Value{ unix });
        p.events.push_back(std::move(e));
    } else if (name == "EXT-X-DATERANGE") {
        add_daterange(p, tag);
    } else if (name == "EXT-X-CUE-OUT") {
        add_cue_out(p, tag);
    } else if (name == "EXT-X-CUE-OUT-CONT") {
        add_cue_out_cont(p, tag);
    } else if (name == "EXT-X-CUE-IN") {
        add_cue_in(p, tag);
    } else if (name == "EXT-OATCLS-SCTE35") {
        add_message(p, tag, "OATCLS", value_of(tag));
    } else {
        // Vendor tags, and the signalings outside D-070's list (D-076).
        Event e = tag_event(p, tag, Source::Playlist, kTypeTag);
        e.payload.emplace_back("name", text_value(name));
        e.payload.emplace_back("value", tag.value ? text_value(*tag.value) : Value{});
        p.events.push_back(std::move(e));
    }
}

// ── Ad breaks ───────────────────────────────────────────────────────────────

std::optional<std::int64_t> cue_line(const Event& e) { return int_member(e.payload, "line"); }

// A cue as a warning names it: "the cue_out at line 14".
std::string cue_name(const Event& e)
{
    if (const auto line = cue_line(e)) return std::format("the {} at line {}", e.type, *line);
    if (e.segment) return std::format("the {} of segment {}", e.type, *e.segment);
    return std::format("a {}", e.type);
}

// `ticks` of kDurationClock as seconds, exactly; nullopt for a count that no
// AVRational holds, its numerator above INT_MAX once reduced, which
// av_reduce() would clamp to the nearest fraction it can hold (D-109 (d)).
std::optional<AVRational> tick_seconds(std::int64_t ticks)
{
    AVRational q{ 0, 1 };
    if (ticks < 0 || !av_reduce(&q.num, &q.den, ticks, kDurationClock, INT_MAX)) return std::nullopt;
    return q;
}

// The break that cue_out `o` starts, ended by cue_in `in` or, without one,
// by its first declared duration; `next_out` is the cue_out that follows it
// before any cue_in, if one does, since breaks don't nest (the owner's
// answer to Q46 (D-087)). A cue_out that holds its cue_in in its tag
// (kHeldCueInLine) ends at its declared duration even where `in` is given,
// that cue_in ending nothing (D-109 (b)). A break that can't be made is a
// warning.
void make_break(const Event& o, const std::vector<const Event*>& conts, const Event* in,
                const Event* next_out, const FrameAfter& frame_after, Breaks& out)
{
    const auto warn = [&](const std::string& why) {
        out.warnings.push_back(
            std::format("variant {}: no ad_break for {}: {}", o.variant, cue_name(o), why));
    };
    // Every duration the cue_out declares, kept with its ticks beside it;
    // the first one given ends a break without a cue_in (D-076), by its
    // ticks where they are a whole number from 0, before its seconds (D-109
    // (d)).
    struct Declared {
        std::string_view            name;
        std::optional<double>       seconds;
        std::optional<std::int64_t> ticks;

        std::string text() const
        {
            return ticks ? std::format("{} of {} ticks (90 kHz)", name, *ticks)
                         : std::format("{} of {} s", name, *seconds);
        }
    };
    Value::Object durations;
    std::optional<Declared> declared;
    for (std::string_view name : kDurationNames) {
        const std::string ticks_name = std::string(name) + std::string(kTicksSuffix);
        const auto d = number_member(o.payload, name);
        const auto t = int_member(o.payload, ticks_name);
        if (d) durations.emplace_back(std::string(name), *member(o.payload, name));
        if (t) durations.emplace_back(ticks_name, int_value(*t));
        if (declared) continue;
        if (t && *t >= 0) declared = Declared{ name, d, t };
        else if (d && std::isfinite(*d) && *d >= 0) declared = Declared{ name, d, std::nullopt };
    }
    const std::optional<std::int64_t> held = int_member(o.payload, kHeldCueInLine);

    std::int64_t     end = 0;
    std::string_view how;
    if (in && !held) {
        if (o.ref_index < 0) return warn("it has no reference frame (design §5.1)");
        if (in->ref_index < 0)
            return warn(std::format("{}, which ends it, has no reference frame (design §5.1)",
                                    cue_name(*in)));
        end = in->ref_index;
        how = "cue_in";
    } else {
        // A cue_out that holds its cue_in and declares no duration makes no
        // break, with this warning, and `in`, the cue_in after it if one
        // came, ends nothing, where it would end the break of a cue_out that
        // held none. D-109 (b) ends a held break at its declared duration and
        // says nothing of a tag that declares none: as built, pending the
        // owner.
        if (!declared && held)
            return warn(std::format("the EXT-X-DATERANGE at line {} holds its cue_in (D-109 (b)), and it "
                                    "declares no duration (D-076)", *held));
        if (!declared)
            return warn(std::format("{}, and it declares no duration (D-076)",
                                    next_out ? cue_name(*next_out) + " follows it before any cue_in"
                                             : std::string("no cue_in follows it")));
        if (o.ref_index < 0) return warn("it has no reference frame (design §5.1)");
        // FrameAfter is given a positive, exact duration only: a zero one
        // ends where the break starts, and one that no AVRational holds
        // exactly makes no break.
        const std::optional<AVRational> q =
            declared->ticks ? tick_seconds(*declared->ticks) : exact_seconds(*declared->seconds);
        if (!q)
            return warn(std::format("no AVRational holds its {} exactly, so the axis isn't asked for its "
                                    "end", declared->text()));
        const std::optional<std::int64_t> f =
            q->num == 0 ? std::optional<std::int64_t>{ o.ref_index } : frame_after(o.ref_index, *q);
        if (!f)
            return warn(std::format("the axis gives no reference frame for the end of its {}",
                                    declared->text()));
        end = *f;
        how = declared->name;
    }
    if (end < o.ref_index)
        return warn(std::format("it would end at reference frame {}, before its start at {}", end,
                                o.ref_index));

    Event b;
    b.source = Source::Scte35;
    b.type.assign(kTypeAdBreak);
    b.variant   = o.variant;
    b.session   = o.session;
    b.segment   = o.segment;
    b.ref_index = o.ref_index;
    b.ref_end   = end;
    b.key       = o.key;
    const auto line_value = [](const Event* e) {
        const auto line = e ? cue_line(*e) : std::nullopt;
        return line ? int_value(*line) : Value{};
    };
    Value::List cont_durations;
    for (const Event* c : conts) {
        const Value* d = member(c->payload, "duration");
        cont_durations.push_back(d && number_member(c->payload, "duration") ? *d : Value{});
    }
    b.payload.emplace_back("cue_out_line", line_value(&o));
    b.payload.emplace_back("cue_in_line", held ? int_value(*held) : line_value(in));
    b.payload.emplace_back("end", text_value(how));
    b.payload.emplace_back("durations", Value{ std::move(durations) });
    b.payload.emplace_back("cue_out_cont_durations", Value{ std::move(cont_durations) });
    out.events.push_back(std::move(b));
}

} // namespace

// ── Public ───────────────────────────────────────────────────────────────────

std::vector<Attribute> parse_attributes(std::string_view list, std::string& err)
{
    err.clear();
    std::vector<Attribute> out;
    std::size_t i = 0;
    const auto skip_blanks = [&] {
        while (i < list.size() && is_blank(list[i])) ++i;
    };
    const auto fail = [&](std::string why) {
        err = std::move(why);
        return std::vector<Attribute>{};
    };
    // Blanks (spaces and tabs) are passed over before the first name, before
    // and after each ',' and after the last value: a departure from RFC 8216
    // §4.2, whose attribute-list has no whitespace, which the owner's answer
    // to Q47 (D-106 (e)) leaves as it is. Between a name and its '=', and
    // between the '=' and the value, a blank is refused, as the RFC has it.
    skip_blanks();
    if (i == list.size()) return out;
    for (;;) {
        const std::size_t name_at = i;
        while (i < list.size() && list[i] != '=' && list[i] != ',' && list[i] != '"'
               && !is_blank(list[i]))
            ++i;
        const std::string_view name = list.substr(name_at, i - name_at);
        if (name.empty()) return fail(std::format("attribute {} has no name", out.size() + 1));
        // Lowercase letters and '_' are taken in a name, where RFC 8216 §4.2
        // allows [A..Z], [0..9] and '-' only: a departure that the owner's
        // answer to Q47 (D-106 (e)) leaves as it is. The cue tags' attribute
        // names, which no specification defines, are written with lowercase
        // letters (ElapsedTime=, Duration=; scte35.md §2.2).
        if (!std::all_of(name.begin(), name.end(), [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
            }))
            return fail(std::format("the attribute name '{}' has a character other than a letter, "
                                    "a digit, '-' or '_'", name));
        if (i == list.size() || list[i] != '=')
            return fail(std::format("attribute {} has no '='", name));
        ++i;
        Attribute a;
        a.name.assign(name);
        if (i < list.size() && list[i] == '"') {
            const std::size_t close = list.find('"', i + 1);
            if (close == npos) return fail(std::format("the quoted value of {} has no closing '\"'", name));
            a.value.assign(list.substr(i + 1, close - i - 1));
            a.quoted = true;
            i = close + 1;
        } else {
            const std::size_t value_at = i;
            while (i < list.size() && list[i] != ',') ++i;
            std::string_view v = list.substr(value_at, i - value_at);
            // Blanks before a ',' or the end (D-106 (e), above).
            while (!v.empty() && is_blank(v.back())) v.remove_suffix(1);
            if (v.empty()) return fail(std::format("attribute {} has no value", name));
            if (v.find_first_of("\" \t") != npos)
                return fail(std::format("the unquoted value of {} has a '\"' or a blank in it", name));
            a.value.assign(v);
        }
        if (std::any_of(out.begin(), out.end(), [&](const Attribute& b) { return b.name == a.name; }))
            return fail(std::format("attribute {} is given twice", a.name));
        out.push_back(std::move(a));
        skip_blanks();
        if (i == list.size()) return out;
        if (list[i] != ',')
            return fail(std::format("'{}' follows the value of {}, where a ',' or the end belongs",
                                    list[i], out.back().name));
        ++i;
        skip_blanks();
        if (i == list.size()) return fail("the list ends with a ','");
    }
}

PlaylistEvents parse_playlist_events(const std::string& text, const std::string& base_url,
                                     int variant)
{
    PlaylistEvents p;
    p.variant  = variant;
    p.timeline = hls::parse_media_timeline(text, base_url);
    if (!p.timeline.ok) {
        p.error = std::format("it isn't a media playlist with segments: {}",
                              p.timeline.problems.empty() ? std::string("no media segments")
                                                          : p.timeline.problems.front());
        return p;
    }
    if (p.timeline.live()) {
        p.error = "it is a live playlist, without EXT-X-ENDLIST and not of EXT-X-PLAYLIST-TYPE VOD, "
                  "and a live media playlist isn't read (DECISIONS.md D-081 (a))";
        return p;
    }

    // The lines as hls::parse_media_timeline reads them (src/HlsAlign.cpp),
    // so that its Segment::line and these tags' lines count alike.
    const std::vector<hls::Segment>& segments = p.timeline.segments;
    p.uris.resize(segments.size());
    std::size_t next   = 0;       // the segment whose URI line follows the line read
    bool        header = false;
    std::size_t pos    = 0;
    int         line_no = 0;
    while (pos <= text.size()) {
        const auto nl = text.find('\n', pos);
        std::string_view line(text.data() + pos, (nl == std::string::npos ? text.size() : nl) - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        ++line_no;
        if (line_no == 1 && line.starts_with("\xEF\xBB\xBF")) line.remove_prefix(3);
        line = trim(line);
        if (line.empty()) continue;
        if (!header) {   // #EXTM3U, which parse_media_timeline checked
            header = true;
            continue;
        }
        if (line[0] != '#') {
            // A URI: the next segment's, unless no #EXTINF preceded it.
            if (next < segments.size() && segments[next].line == line_no) p.uris[next++].assign(line);
            continue;
        }
        if (!line.starts_with("#EXT")) continue;   // a comment
        const std::size_t colon = line.find(':');
        PlaylistTag tag;
        tag.segment_index = next < segments.size() ? static_cast<int>(next) : -1;
        tag.line          = line_no;
        tag.name.assign(line.substr(1, colon == npos ? npos : colon - 1));
        if (colon != npos) tag.value.emplace(line.substr(colon + 1));
        add_tag(p, tag);
        p.tags.push_back(std::move(tag));
    }
    return p;
}

Event segment_event(const PlaylistEvents& playlist, std::size_t index,
                    std::optional<std::int64_t> bytes)
{
    const hls::Segment& s = playlist.timeline.segments.at(index);
    Event e;
    e.source = Source::Playlist;
    e.type.assign(kTypeSegment);
    e.variant   = playlist.variant;
    e.segment   = s.seq;
    e.ref_index = kUnplaced;
    Payload& out = e.payload;
    out.emplace_back("line", int_value(s.line));
    out.emplace_back("uri", text_value(index < playlist.uris.size() ? playlist.uris[index] : ""));
    out.emplace_back("url", text_value(s.uri));
    out.emplace_back("media_sequence", int_value(s.seq));
    out.emplace_back("extinf", Value{ s.duration });
    out.emplace_back("byte_range", s.range.whole()
                                       ? Value{}
                                       : Value{ Value::Object{ { "length", int_value(s.range.length) },
                                                               { "offset", int_value(s.range.offset) } } });
    out.emplace_back("discontinuity_sequence", int_value(s.disc_seq));
    out.emplace_back("pdt", std::isnan(s.pdt) ? Value{} : Value{ s.pdt });
    out.emplace_back("pdt_explicit", Value{ s.pdt_explicit });
    out.emplace_back("playlist_time", Value{ s.start });
    out.emplace_back("bytes", bytes ? int_value(*bytes) : Value{});
    out.emplace_back("gap", Value{ s.gap });
    out.emplace_back("map", Value{ s.map >= 0 });
    return e;
}

Breaks pair_breaks(const std::vector<Event>& cues, const FrameAfter& frame_after)
{
    // Play order within each variant: by segment, a cue of none last, then
    // by line; the input's order last, so that the order is total.
    constexpr std::int64_t kLast = std::numeric_limits<std::int64_t>::max();
    struct Key {
        int          variant;
        std::int64_t segment, line;
        std::size_t  index;
    };
    std::vector<Key> order;
    for (std::size_t i = 0; i < cues.size(); ++i) {
        const Event& c = cues[i];
        if (c.type != kTypeCueOut && c.type != kTypeCueOutCont && c.type != kTypeCueIn) continue;
        order.push_back({ c.variant, c.segment.value_or(kLast), cue_line(c).value_or(kLast), i });
    }
    std::sort(order.begin(), order.end(), [](const Key& a, const Key& b) {
        return std::tie(a.variant, a.segment, a.line, a.index)
             < std::tie(b.variant, b.segment, b.line, b.index);
    });

    Breaks out;
    const Event*              open = nullptr;   // the cue_out of the break being read
    std::vector<const Event*> conts;            // its cue_out_conts
    const auto close = [&](const Event* in, const Event* next_out) {
        make_break(*open, conts, in, next_out, frame_after, out);
        open = nullptr;
        conts.clear();
    };
    for (const Key& k : order) {
        const Event& c = cues[k.index];
        if (open && open->variant != c.variant) close(nullptr, nullptr);
        if (c.type == kTypeCueOut) {
            // Breaks don't nest, the owner's answer to Q46 (D-087): the open
            // break gets no cue_in, and this cue_out pairs with the next cue_in.
            if (open) close(nullptr, &c);
            open = &c;
        } else if (c.type == kTypeCueOutCont) {
            if (open) conts.push_back(&c);
        } else if (open) {
            close(&c, nullptr);
        }
    }
    if (open) close(nullptr, nullptr);
    return out;
}

} // namespace timeline
