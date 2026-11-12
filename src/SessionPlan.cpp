#include "SessionPlan.h"

#include "JsonRead.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <initializer_list>
#include <limits>
#include <map>
#include <utility>

namespace timeline {

namespace {

Value int_value(std::int64_t v) { return Value{ v }; }
Value text_value(std::string s) { return Value{ std::move(s) }; }

// Microseconds from the session's start in seconds, the unit of the
// document's wall clock (D-073); null for a time not reached (-1).
Value seconds_value(std::int64_t us)
{
    return us < 0 ? Value{} : Value{ static_cast<double>(us) / 1e6 };
}

std::string plural(std::size_t n, std::string_view one, std::string_view many)
{
    return std::format("{} {}", n, n == 1 ? one : many);
}

// ── The plan's reader ────────────────────────────────────────────────────────

// "the session plan's spans[2]" for a span's member, "the session plan" for
// the top's.
std::string where_text(std::optional<std::size_t> span)
{
    return span ? std::format("the session plan's spans[{}]", *span) : std::string("the session plan");
}

// "schema, schema_version, name and spans"
std::string names_text(std::initializer_list<std::string_view> names)
{
    std::string out;
    std::size_t i = 0;
    for (const std::string_view n : names) {
        out += std::format("{}{}", i == 0 ? "" : i + 1 == names.size() ? " and " : ", ", n);
        ++i;
    }
    return out;
}

const json::Value* member(const json::Value::Object& object, std::string_view name)
{
    for (const auto& [key, value] : object)
        if (key == name) return &value;
    return nullptr;
}

// Each of `names` a member of `object`, and no other: version 1's layout
// read strictly, so that a misspelt member is refused rather than missed.
bool check_members(const json::Value::Object& object, std::initializer_list<std::string_view> names,
                   std::optional<std::size_t> span, std::string& err)
{
    for (const auto& [key, value] : object)
        if (std::find(names.begin(), names.end(), key) == names.end()) {
            err = std::format("{} has a member \"{}\", which isn't one of version {}'s: {}", where_text(span),
                              key, kPlanVersion, names_text(names));
            return false;
        }
    for (const std::string_view n : names)
        if (!member(object, n)) {
            err = std::format("{} has no member \"{}\"", where_text(span), n);
            return false;
        }
    return true;
}

// A member that must be an integer from 0.
bool whole_number(const json::Value::Object& object, std::string_view name, std::optional<std::size_t> span,
                  std::int64_t& out, std::string& err)
{
    const json::Value* v = member(object, name);
    if (!v) {
        err = std::format("{} has no member \"{}\"", where_text(span), name);
        return false;
    }
    const std::int64_t* i = v->integer();
    if (!i) {
        err = std::format("{}'s {} is {}, not an integer", where_text(span), name, json::kind_name(*v));
        return false;
    }
    if (*i < 0) {
        err = std::format("{}'s {} is {}: it is a whole number from 0", where_text(span), name, *i);
        return false;
    }
    out = *i;
    return true;
}

// ── The session ──────────────────────────────────────────────────────────────

// The `segment` events of a timeline by their variant and media sequence
// number: where build() placed each segment (design §5.1).
using SegmentSpans = std::map<std::pair<int, std::int64_t>, const Event*>;

SegmentSpans segment_spans(const Timeline& t)
{
    SegmentSpans out;
    for (const Event& e : t.events)
        if (e.source == Source::Playlist && e.type == "segment" && e.segment && e.ref_end)
            out.emplace(std::pair{ e.variant, *e.segment }, &e);
    return out;
}

// The first of variant `v`'s segments `first` to `last` that isn't on the
// timeline; nullopt when each is.
std::optional<std::int64_t> missing_segment(const SegmentSpans& segments, int v, std::int64_t first,
                                            std::int64_t last)
{
    std::int64_t expect = first;
    for (auto it = segments.lower_bound({ v, first }); it != segments.end() && it->first.first == v; ++it) {
        if (it->first.second != expect) return expect;
        if (expect == last) return std::nullopt;
        ++expect;
    }
    return expect;
}

const MasterEntry* entry_of(const std::vector<MasterEntry>& entries, std::size_t index)
{
    for (const MasterEntry& e : entries)
        if (e.index == index) return &e;
    return nullptr;
}

// The place in Timeline::variants of the variant whose playlist master
// entry `entry` names, the reference or a complete pair's distorted
// playlist; -1: none.
int variant_of_entry(const Timeline& t, std::size_t entry)
{
    for (std::size_t v = 0; v < t.variants.size(); ++v) {
        const std::vector<std::size_t>& named = t.variants[v].entries;
        if (std::find(named.begin(), named.end(), entry) != named.end()) return static_cast<int>(v);
    }
    return -1;
}

const Variant* variant_at(const Timeline& t, int v)
{
    return v >= 0 && static_cast<std::size_t>(v) < t.variants.size() ? &t.variants[static_cast<std::size_t>(v)]
                                                                      : nullptr;
}

const Session* session_at(const Timeline& t, int session)
{
    return session >= 0 && static_cast<std::size_t>(session) < t.sessions.size()
               ? &t.sessions[static_cast<std::size_t>(session)]
               : nullptr;
}

std::string no_session_text(const Timeline& t, int session)
{
    return std::format("the timeline has no session {}: it has {}", session,
                       plural(t.sessions.size(), "session", "sessions"));
}

// "spans[1] (entry 2, segments 3 to 4)"
std::string span_text(std::size_t k, const PlanSpan& p)
{
    return p.first_seq == p.last_seq
               ? std::format("spans[{}] (entry {}, segment {})", k, p.entry, p.first_seq)
               : std::format("spans[{}] (entry {}, segments {} to {})", k, p.entry, p.first_seq, p.last_seq);
}

// "frames 75 to 99", or "frame 75", of a half-open [a, b) with a < b.
std::string frames_text(std::int64_t a, std::int64_t b)
{
    return b - a == 1 ? std::format("frame {}", a) : std::format("frames {} to {}", a, b - 1);
}

// The media key of reference frame `ref_index` (Event::key); a default
// one off the axis.
MediaKey key_at(const Timeline& t, std::int64_t ref_index)
{
    if (ref_index < 0 || static_cast<std::size_t>(ref_index) >= t.axis.keys.size()) return {};
    return t.axis.keys[static_cast<std::size_t>(ref_index)];
}

// ── Its events ───────────────────────────────────────────────────────────────

// A rung as the player events name it (D-075): its master entry, the URI
// the master gives it, its BANDWIDTH and RESOLUTION, and its variant's two
// ranks.
Payload rung_members(const Timeline& t, const std::vector<MasterEntry>& entries, const Span& span)
{
    const MasterEntry* e = entry_of(entries, span.entry);
    const Variant*     v = variant_at(t, span.variant);
    const auto rank = [](int r) { return r > 0 ? int_value(r) : Value{}; };
    Payload out;
    out.emplace_back("entry", int_value(static_cast<std::int64_t>(span.entry)));
    out.emplace_back("uri", e ? text_value(e->uri) : Value{});
    out.emplace_back("bandwidth", e ? int_value(e->bandwidth) : Value{});
    out.emplace_back("resolution", e && e->resolution ? text_value(*e->resolution) : Value{});
    out.emplace_back("rank_from_top", v ? rank(v->rank_from_top) : Value{});
    out.emplace_back("rank_from_bottom", v ? rank(v->rank_from_bottom) : Value{});
    return out;
}

Event player_event(const Timeline& t, std::string_view type, int session, int variant, std::int64_t at,
                   std::optional<std::int64_t> end)
{
    Event e;
    e.source    = Source::Player;
    e.type      = std::string(type);
    e.variant   = variant;
    e.session   = session;
    e.ref_index = at;
    e.ref_end   = end;
    e.key       = key_at(t, at);
    return e;
}

// ── Its fetch ────────────────────────────────────────────────────────────────

const Value* payload_member(const Payload& p, std::string_view name)
{
    for (const auto& [n, v] : p)
        if (n == name) return &v;
    return nullptr;
}

std::optional<std::int64_t> int_member(const Payload& p, std::string_view name)
{
    const Value* v = payload_member(p, name);
    if (const auto* i = v ? std::get_if<std::int64_t>(&v->v) : nullptr) return *i;
    return std::nullopt;
}

// `path` with its dot segments resolved, as RFC 3986 §5.2.4 removes them:
// each "." segment dropped and each ".." one with the segment before it, a
// path that ends in one of them then ending with '/'. A ".." at the start of
// a relative path, which leaves the directory the path starts from, is
// kept, since it names a directory above that one; at the root of an
// absolute path it is dropped, as the RFC drops it.
std::string resolved_path(std::string_view path)
{
    const bool absolute = path.starts_with('/');
    if (absolute) path.remove_prefix(1);
    std::vector<std::string_view> kept;
    for (;;) {
        const std::size_t      slash = path.find('/');
        const std::string_view seg   = path.substr(0, slash);
        if (seg == "..") {
            if (!kept.empty() && kept.back() != "..") kept.pop_back();
            else if (!absolute) kept.push_back(seg);
        } else if (seg != ".") {
            kept.push_back(seg);
        }
        if (slash == std::string_view::npos) {
            if (seg == "." || seg == "..") kept.emplace_back();   // a directory: its '/'
            break;
        }
        path.remove_prefix(slash + 1);
    }
    std::string out = absolute ? "/" : "";
    for (std::size_t i = 0; i < kept.size(); ++i) {
        if (i > 0) out += '/';
        out += kept[i];
    }
    return out;
}

// `url` with the dot segments of its path resolved (resolved_path()), what
// comes before the path, a scheme and an authority ("http://host:port"), and
// a query or fragment, from the first '?' or '#' on, kept as they are, as
// fetch_url() splits a URL; a URL without "://" is a path.
std::string without_dot_segments(std::string_view url)
{
    const std::size_t      end  = std::min(url.find_first_of("?#"), url.size());
    const std::string_view head = url.substr(0, end);
    std::size_t            at   = 0;   // where its path starts
    if (const std::size_t scheme = head.find("://"); scheme != std::string_view::npos)
        at = std::min(head.find('/', scheme + 3), head.size());
    return std::string(head.substr(0, at)) + resolved_path(head.substr(at)) + std::string(url.substr(end));
}

// "the master", "variant 2's media playlist", "segment 3 of variant 2"
std::string file_text(const Request& r)
{
    switch (r.kind) {
    case FetchedFile::Master:        return "the master";
    case FetchedFile::MediaPlaylist: return std::format("variant {}'s media playlist", r.variant);
    case FetchedFile::Segment:
        return r.segment ? std::format("segment {} of variant {}", *r.segment, r.variant)
                         : std::format("a segment of variant {}", r.variant);
    }
    return "a file";
}

// A row of a network event's chain, with the members request_events()
// lists (src/SessionPlan.h): a form of WP07's, which the owner kept (D-108
// (c)) and the schema file types.
Value row_value(const RequestRow& row)
{
    const bool http = row.status > 0;
    return Value{ Value::Object{
        { "url", text_value(row.url) },
        { "start", seconds_value(row.t_start) },
        { "sent", seconds_value(row.t_sent) },
        { "first_byte", seconds_value(row.t_first_byte) },
        { "end", seconds_value(row.t_end) },
        { "status", http ? int_value(row.status) : Value{} },
        { "status_text", http ? text_value(row.status_text) : Value{} },
        { "bytes", row.bytes >= 0 ? int_value(row.bytes) : Value{} },
        { "content_length", row.content_length >= 0 ? int_value(row.content_length) : Value{} },
        { "failed", Value{ row.failed } },
        { "error", row.error.empty() ? Value{} : text_value(row.error) },
    } };
}

// ── Its drops ────────────────────────────────────────────────────────────────

// The pair's curve whose distorted variant is `variant`; nullptr: none.
const Curve* curve_of_variant(const Timeline& t, int variant)
{
    for (const Curve& c : t.curves)
        if (c.variant == variant) return &c;
    return nullptr;
}

// The position of the first of `c`'s points at or after reference frame
// `frame`, its points ascending as a curve's do.
std::size_t first_at_or_after(const Curve& c, std::int64_t frame)
{
    const auto end = c.ref_index.begin() + static_cast<std::ptrdiff_t>(std::min(c.ref_index.size(), c.vmaf.size()));
    return static_cast<std::size_t>(std::lower_bound(c.ref_index.begin(), end, frame) - c.ref_index.begin());
}

} // namespace

// ── The plan ─────────────────────────────────────────────────────────────────

SessionPlan parse_session_plan(std::string_view text, std::string& err)
{
    err.clear();
    json::Error parse_err;
    const std::optional<json::Value> doc = json::parse(text, parse_err);
    if (!doc) {
        err = std::format("the session plan isn't JSON: {}", json::error_text(parse_err));
        return {};
    }
    const json::Value::Object* top = doc->object();
    if (!top) {
        err = std::format("the session plan is {}, not an object", json::kind_name(*doc));
        return {};
    }
    // What it says it is, before anything else is read: another file's
    // members are no plan's.
    const json::Value* schema = member(*top, "schema");
    if (!schema) {
        err = "the session plan has no member \"schema\"";
        return {};
    }
    if (!schema->string()) {
        err = std::format("the session plan's schema is {}, not a string", json::kind_name(*schema));
        return {};
    }
    if (*schema->string() != kPlanSchema) {
        err = std::format("the session plan's schema is \"{}\", not \"{}\"", *schema->string(), kPlanSchema);
        return {};
    }
    std::int64_t version = 0;
    if (!whole_number(*top, "schema_version", std::nullopt, version, err)) return {};
    if (version != kPlanVersion) {
        err = std::format("the session plan's schema_version is {}: this reader reads version {}", version,
                          kPlanVersion);
        return {};
    }
    if (!check_members(*top, { "schema", "schema_version", "name", "spans" }, std::nullopt, err)) return {};

    SessionPlan plan;
    const json::Value* name = member(*top, "name");
    if (!name->string()) {
        err = std::format("the session plan's name is {}, not a string", json::kind_name(*name));
        return {};
    }
    plan.name = *name->string();

    const json::Value* spans = member(*top, "spans");
    if (!spans->array()) {
        err = std::format("the session plan's spans is {}, not an array", json::kind_name(*spans));
        return {};
    }
    if (spans->array()->empty()) {
        err = "the session plan's spans is empty: a session plays one span at least";
        return {};
    }
    for (std::size_t k = 0; k < spans->array()->size(); ++k) {
        const json::Value&         s = (*spans->array())[k];
        const json::Value::Object* o = s.object();
        if (!o) {
            err = std::format("the session plan's spans[{}] is {}, not an object", k, json::kind_name(s));
            return {};
        }
        if (!check_members(*o, { "entry", "first_seq", "last_seq" }, k, err)) return {};
        std::int64_t entry = 0;
        PlanSpan     p;
        if (!whole_number(*o, "entry", k, entry, err) || !whole_number(*o, "first_seq", k, p.first_seq, err) ||
            !whole_number(*o, "last_seq", k, p.last_seq, err))
            return {};
        if (p.last_seq < p.first_seq) {
            err = std::format("the session plan's spans[{}]'s last_seq is {}, before its first_seq {}", k,
                              p.last_seq, p.first_seq);
            return {};
        }
        p.entry = static_cast<std::size_t>(entry);
        plan.spans.push_back(p);
    }
    return plan;
}

// ── The session ──────────────────────────────────────────────────────────────

Session make_session(const Timeline& timeline, const std::vector<MasterEntry>& entries,
                     const SessionPlan& plan, std::string& err)
{
    err.clear();
    if (plan.spans.empty()) {
        err = "the session plan has no span: a session plays one span at least";
        return {};
    }
    const SegmentSpans segments = segment_spans(timeline);
    Session s;
    s.name = plan.name;
    for (std::size_t k = 0; k < plan.spans.size(); ++k) {
        const PlanSpan&    p     = plan.spans[k];
        const MasterEntry* entry = entry_of(entries, p.entry);
        if (!entry) {
            err = std::format("the session plan's {} plays entry {}, which isn't one of the master's {}",
                              span_text(k, p), p.entry, plural(entries.size(), "entry", "entries"));
            return {};
        }
        const int v = variant_of_entry(timeline, p.entry);
        if (v < 0) {
            err = std::format("the session plan's {} plays entry {} ({}), whose playlist the timeline has no "
                              "variant of: it is neither the reference nor a complete pair's distorted "
                              "playlist, so it has no score and no segment on the reference's frames",
                              span_text(k, p), p.entry, entry->uri);
            return {};
        }
        const Variant& var = timeline.variants[static_cast<std::size_t>(v)];
        if (var.segment_map != "ok") {
            err = std::format("the session plan's {} plays variant {} ({}), whose segment map was {}: its "
                              "segments aren't on the reference's frames (D-081 (c))",
                              span_text(k, p), v, var.url, var.segment_map);
            return {};
        }
        if (p.last_seq < p.first_seq) {
            err = std::format("the session plan's {} ends before it starts", span_text(k, p));
            return {};
        }
        // Each of its segments on the timeline: one its playlist doesn't
        // list, or that the build couldn't place, would leave the span with
        // frames it can't name, and the fetch without the segment's URL
        // (fetch_files()). So the whole plan is refused for one such
        // segment, also for one none of whose frames paired, and a session
        // can't play across it. This check is WP07's, beside design §14's
        // unknown entries and gaps or overlaps, and the owner kept it
        // (D-108 (c)).
        if (const std::optional<std::int64_t> seq = missing_segment(segments, v, p.first_seq, p.last_seq)) {
            err = std::format("the session plan's {} plays segment {} of variant {} ({}), which isn't on the "
                              "timeline: its playlist lists no segment {}, or the build couldn't place it on "
                              "the reference's frames, which one of the timeline's warnings then says "
                              "(design §5.1)",
                              span_text(k, p), *seq, v, var.url, *seq);
            return {};
        }
        const Event& first = *segments.at({ v, p.first_seq });
        const Event& last  = *segments.at({ v, p.last_seq });
        Span span;
        span.variant   = v;
        span.entry     = p.entry;
        span.first_seq = p.first_seq;
        span.last_seq  = p.last_seq;
        span.ref_index = first.ref_index;
        span.ref_end   = *last.ref_end;
        // The reference, the top rung in top-rung mode, has no score
        // (D-105 (b)); every other variant is a complete pair's.
        span.scored = var.pair != -1;
        if (k > 0) {
            const Span& before = s.spans.back();
            if (span.ref_index != before.ref_end) {
                const std::int64_t a = std::max(before.ref_index, span.ref_index);
                const std::int64_t b = std::min(before.ref_end, span.ref_end);
                const std::string how =
                    span.ref_index > before.ref_end
                        ? std::format("they leave reference {} in no span",
                                      frames_text(before.ref_end, span.ref_index))
                    : a < b ? std::format("they overlap on reference {}", frames_text(a, b))
                            : std::string("it starts before the span before it does");
                err = std::format("the session plan's {} ends at reference frame {} and its {} starts at frame "
                                  "{}: {}, where each span starts where the one before it ended (D-105 (a))",
                                  span_text(k - 1, plan.spans[k - 1]), before.ref_end, span_text(k, p),
                                  span.ref_index, how);
                return {};
            }
        }
        s.spans.push_back(span);
    }
    return s;
}

// ── Its events ───────────────────────────────────────────────────────────────

std::vector<Event> session_events(const Timeline& timeline, const std::vector<MasterEntry>& entries,
                                  int session)
{
    std::vector<Event> out;
    const Session* s = session_at(timeline, session);
    if (!s) return out;
    for (std::size_t k = 0; k < s->spans.size(); ++k) {
        const Span& span = s->spans[k];
        // A switch at each span boundary after the first (design §5.6)
        // where the entry played changes: none between two spans of one
        // entry, where nothing changes, while two entries that name one
        // playlist still switch, the master's entry being what changes
        // (D-108 (b)).
        if (k > 0 && s->spans[k - 1].entry != span.entry) {
            Event e = player_event(timeline, kTypeVariantSwitch, session, span.variant, span.ref_index,
                                   std::nullopt);
            e.payload.emplace_back("from", Value{ rung_members(timeline, entries, s->spans[k - 1]) });
            e.payload.emplace_back("to", Value{ rung_members(timeline, entries, span) });
            e.payload.emplace_back("origin", text_value("plan"));
            out.push_back(std::move(e));
        }
        Event e = player_event(timeline, kTypeVariantSpan, session, span.variant, span.ref_index, span.ref_end);
        e.payload = rung_members(timeline, entries, span);
        e.payload.emplace_back("first_seq", int_value(span.first_seq));
        e.payload.emplace_back("last_seq", int_value(span.last_seq));
        e.payload.emplace_back("scored", Value{ span.scored });
        out.push_back(std::move(e));
    }
    return out;
}

// ── Its fetch ────────────────────────────────────────────────────────────────

std::string fetch_url(const std::string& url, const std::string& master_url, const std::string& fetch_base,
                      std::string& err)
{
    err.clear();
    if (fetch_base.empty()) return url;
    // The rule is WP07's reading of design §5.7's paths rebased onto
    // --fetch-base, which the owner kept (D-108 (c); src/SessionPlan.h): a
    // URL's part after the master's directory, added to the base; a URL
    // outside that directory, on another host or beside it, is refused, and
    // with it the whole of fetch_files().
    // Both URLs are compared with their dot segments resolved (RFC 3986
    // §5.2.4; without_dot_segments()), the paths they name: a URL that
    // leaves the directory through "../" is outside it, whatever its text
    // starts with, and refuses the whole fetch (D-108 (c)). The report's URLs
    // can hold dot segments: mirage_vmaf --ladder joins a local master's
    // directory and its entries' URIs as text (vmafladder::resolve(),
    // src/VMAFLadder.h); and a playlist's segments come with their dot
    // segments resolved where the playlist's URL is an absolute path or an
    // http or https URL, but keep them where it is a relative local path
    // (hls::resolve_uri(), src/HlsAlign.cpp). So where the master is given
    // by a relative path, a URL's text can start with its directory while
    // the URL leaves it.
    // A URL under the directory is fetched at its resolved path from it, so
    // that one that leaves it and comes back is fetched without its dot
    // segments, and a refusal's message gives the URL resolved beside it
    // where the two differ: a choice pending the owner, Q78, the others
    // being its text after the directory, for the server to resolve, which
    // names the same file only where its dot segments stay inside the
    // directory, or a refusal of any URL that has them.
    const std::string resolved = without_dot_segments(url);
    const std::string master   = without_dot_segments(master_url);
    // The master's directory: its URL up to its last '/' before any query
    // or fragment; empty for a master in the current directory, under which
    // only a relative path is.
    const std::string_view head  = std::string_view(master).substr(0, master.find_first_of("?#"));
    const std::size_t      slash = head.rfind('/');
    const std::string      dir(slash == std::string_view::npos ? std::string_view() : head.substr(0, slash + 1));
    const bool relative = resolved.find("://") == std::string::npos && !resolved.starts_with('/');
    // The URL's path from the directory, which a ".." left at its start, as
    // only a relative path keeps one, would climb out of.
    const std::string_view rest =
        resolved.starts_with(dir) ? std::string_view(resolved).substr(dir.size()) : std::string_view();
    if (!resolved.starts_with(dir) || (dir.empty() && !relative) || rest.empty() || rest.starts_with("../")) {
        // The URL, then, where resolving its dot segments changes it, the
        // URL resolved, or "the current directory" where that leaves
        // nothing, as of "a/..": a form pending the owner with the rebasing
        // (Q78).
        err = std::format("{}{} isn't under the master's directory {}, so it has no path to fetch from {} "
                          "(design §5.7)",
                          url,
                          resolved == url ? std::string()
                                          : std::format(" ({} with its dot segments resolved)",
                                                        resolved.empty() ? std::string("the current directory")
                                                                         : resolved),
                          dir.empty() ? std::string("(the current directory)") : dir, fetch_base);
        return {};
    }
    // At its path from the directory, its dot segments resolved: the choice
    // pending the owner above (Q78).
    std::string out = fetch_base;
    if (!out.ends_with('/')) out += '/';
    return out + std::string(rest);
}

std::vector<Request> fetch_files(const Timeline& timeline, int session, const std::string& master_url,
                                 const std::string& fetch_base, std::string& err)
{
    err.clear();
    const Session* s = session_at(timeline, session);
    if (!s) {
        err = no_session_text(timeline, session);
        return {};
    }
    const SegmentSpans   segments = segment_spans(timeline);
    std::vector<Request> out;
    const auto add = [&](Request r, const std::string& url) {
        r.url = fetch_url(url, master_url, fetch_base, err);
        if (!err.empty()) return false;
        out.push_back(std::move(r));
        return true;
    };
    if (!add(Request{ .kind = FetchedFile::Master }, master_url)) return {};
    std::vector<int> fetched;   // the variants whose media playlist the fetch has
    for (const Span& span : s->spans) {
        const Variant* var = variant_at(timeline, span.variant);
        if (!var) {
            err = std::format("the session's span at reference frame {} plays variant {}, which the timeline "
                              "doesn't have", span.ref_index, span.variant);
            return {};
        }
        // A variant's media playlist once, the first time a span plays it
        // (D-104 (a)): a VOD playlist doesn't change during a session.
        if (std::find(fetched.begin(), fetched.end(), span.variant) == fetched.end()) {
            fetched.push_back(span.variant);
            if (!add(Request{ .kind = FetchedFile::MediaPlaylist, .variant = span.variant }, var->url)) return {};
        }
        for (std::int64_t seq = span.first_seq; seq <= span.last_seq; ++seq) {
            const auto   it   = segments.find({ span.variant, seq });
            const Value* url  = it == segments.end() ? nullptr : payload_member(it->second->payload, "url");
            const auto*  text = url ? std::get_if<std::string>(&url->v) : nullptr;
            if (!text) {
                err = std::format("segment {} of variant {}, which the session plays, isn't on the timeline "
                                  "with its URL", seq, span.variant);
                return {};
            }
            Request r{ .kind = FetchedFile::Segment, .variant = span.variant, .segment = seq };
            // Its EXT-X-BYTERANGE, as the playlist's segment event gives it
            // (src/PlaylistEvents.h): the bytes of its file it is.
            if (const Value* range = payload_member(it->second->payload, "byte_range")) {
                if (const auto* o = std::get_if<Value::Object>(&range->v)) {
                    r.range_offset = int_member(*o, "offset").value_or(-1);
                    r.range_length = int_member(*o, "length").value_or(-1);
                }
            }
            if (!add(std::move(r), *text)) return {};
            if (seq == std::numeric_limits<std::int64_t>::max()) break;
        }
    }
    return out;
}

RequestEvents request_events(const Timeline& timeline, int session, const std::vector<Request>& files)
{
    RequestEvents  out;
    const Session* s = session_at(timeline, session);
    if (!s) {
        if (!files.empty())
            out.warnings.push_back(std::format("{}: none of the session fetch's {} makes an event",
                                               no_session_text(timeline, session),
                                               plural(files.size(), "file", "files")));
        return out;
    }
    const SegmentSpans segments = segment_spans(timeline);
    for (const Request& r : files) {
        const auto lost = [&](const std::string& why) {
            out.warnings.push_back(std::format("the session fetch's request for {} ({}) {}: it makes no event "
                                               "(design §5.1)", file_text(r), r.url, why));
        };
        if (r.chain.empty()) {
            lost("has no row of NetworkLogger's");
            continue;
        }
        // Where it sits on the media axis (D-105 (c)).
        std::int64_t                at = 0;
        std::optional<std::int64_t> end;
        if (r.kind == FetchedFile::Master) {
            if (s->spans.empty()) {
                lost("has no frame to sit at: the session has no span");
                continue;
            }
            at = s->spans.front().ref_index;
        } else if (r.kind == FetchedFile::MediaPlaylist) {
            const auto span = std::find_if(s->spans.begin(), s->spans.end(),
                                           [&r](const Span& sp) { return sp.variant == r.variant; });
            if (span == s->spans.end()) {
                lost(std::format("is of variant {}, which no span of the session plays", r.variant));
                continue;
            }
            at = span->ref_index;
        } else {
            const auto it = r.segment ? segments.find({ r.variant, *r.segment }) : segments.end();
            if (it == segments.end()) {
                lost("is of a segment that isn't on the timeline");
                continue;
            }
            at  = it->second->ref_index;
            end = it->second->ref_end;
        }
        const RequestRow& first = r.chain.front();
        const RequestRow& last  = r.chain.back();
        Event e;
        e.source       = Source::Network;
        e.type         = std::string(r.kind == FetchedFile::Segment ? kTypeSegmentRequest : kTypePlaylistRequest);
        e.variant      = r.kind == FetchedFile::Master ? -1 : r.variant;
        e.session      = session;
        e.segment      = r.kind == FetchedFile::Segment ? r.segment : std::nullopt;
        e.ref_index    = at;
        e.ref_end      = end;
        e.key          = key_at(timeline, at);
        e.wallclock_us = first.t_start;
        // The payload's members and units are WP07's, which the owner kept
        // (D-108 (c)) and the schema file types (src/SessionPlan.h).
        const bool http = last.status > 0;
        Payload&   p    = e.payload;
        p.emplace_back("url", text_value(r.url));
        p.emplace_back("range", r.range_offset >= 0
                                    ? Value{ Value::Object{ { "offset", int_value(r.range_offset) },
                                                            { "length", int_value(r.range_length) } } }
                                    : Value{});
        p.emplace_back("status", http ? int_value(last.status) : Value{});
        p.emplace_back("status_text", http ? text_value(last.status_text) : Value{});
        p.emplace_back("bytes", last.bytes >= 0 ? int_value(last.bytes) : Value{});
        p.emplace_back("start", seconds_value(first.t_start));
        p.emplace_back("first_byte", seconds_value(last.t_first_byte));
        p.emplace_back("end", seconds_value(last.t_end));
        p.emplace_back("duration", last.t_end >= 0 && first.t_start >= 0 && last.t_end >= first.t_start
                                       ? seconds_value(last.t_end - first.t_start)
                                       : Value{});
        p.emplace_back("failed", Value{ !r.error.empty() || last.failed });
        p.emplace_back("error", r.error.empty() ? Value{} : text_value(r.error));
        p.emplace_back("origin", text_value("fetch"));
        Value::List chain;
        for (const RequestRow& row : r.chain) chain.push_back(row_value(row));
        p.emplace_back("chain", Value{ std::move(chain) });
        out.events.push_back(std::move(e));
    }
    return out;
}

// ── Its drops ────────────────────────────────────────────────────────────────

std::vector<Drop> session_drops(const Timeline& timeline, int session, const DetectOptions& options,
                                std::string& err)
{
    err.clear();
    std::vector<Drop> out;
    // No detection without W and N (D-077), and then nothing else checked,
    // the session included, as detect() checks nothing then.
    if (!options.given()) return out;
    const Session* s = session_at(timeline, session);
    if (!s) {
        err = no_session_text(timeline, session);
        return out;
    }
    if (std::string why = check_options(options); !why.empty()) {
        err = std::move(why);
        return out;
    }
    const std::int64_t n = *options.min_points;

    // Each pair's drops, found once on its whole curve.
    std::map<const Curve*, std::vector<Drop>> found;
    // The session's curve is the scored spans' points in their order
    // (curve_points()): `base` counts those of the spans walked.
    std::size_t base = 0;
    for (std::size_t k = 0; k < s->spans.size();) {
        const Span& span = s->spans[k];
        if (!span.scored) {
            ++k;
            continue;
        }
        const Curve* c = curve_of_variant(timeline, span.variant);
        if (!c) {
            err = std::format("the session's span at reference frames {} to {} is scored, but no pair's curve "
                              "is of its variant {}", span.ref_index, span.ref_end - 1, span.variant);
            return {};
        }
        // The stretch of frames over which the session plays this curve:
        // this span and the scored spans of its variant that follow it with
        // no frame between.
        const std::int64_t from = span.ref_index;
        std::int64_t       to   = span.ref_end;
        std::size_t        next = k + 1;
        while (next < s->spans.size() && s->spans[next].scored && s->spans[next].variant == span.variant &&
               s->spans[next].ref_index == to) {
            to = s->spans[next].ref_end;
            ++next;
        }
        auto [it, added] = found.try_emplace(c);
        if (added) {
            std::string why;
            it->second = detect(*c, options, why);
            if (!why.empty()) {
                const Variant* v = variant_at(timeline, c->variant);
                err = v && v->pair >= 0 ? std::format("pair {}'s curve: {}", v->pair, why)
                                        : std::format("the curve of variant {}: {}", c->variant, why);
                return {};
            }
        }
        // The curve's points in the stretch, positions lo to hi - 1: the
        // session curve's from `base` on.
        const std::size_t lo = first_at_or_after(*c, from);
        const std::size_t hi = first_at_or_after(*c, to);
        for (const Drop& d : it->second) {
            const std::int64_t a = std::max(d.interval.ref_index, from);
            const std::int64_t b = std::min(d.interval.ref_end, to);
            if (a >= b) continue;
            // The drop's low points in [a, b): every point with a score from
            // its first low point to its last is low (detect()).
            Drop piece;
            piece.interval = { .ref_index = a, .ref_end = b };
            piece.baseline = d.baseline;   // rule (iii)
            double lowest = std::numeric_limits<double>::infinity(), sum = 0.0;
            for (std::size_t i = std::max(d.first_point, lo); i <= d.last_point && i < hi; ++i) {
                const std::int64_t f = c->ref_index[i];
                if (f < a || f >= b || !std::isfinite(c->vmaf[i])) continue;
                if (piece.points == 0) piece.first_point = base + (i - lo);
                piece.last_point = base + (i - lo);
                ++piece.points;
                lowest = std::min(lowest, c->vmaf[i]);
                sum += c->vmaf[i];
            }
            if (piece.points <= n) continue;   // rule (i)
            piece.min            = lowest;
            piece.mean           = sum / static_cast<double>(piece.points);
            piece.drop_magnitude = piece.baseline - piece.min;
            piece.mean_drop      = piece.baseline - piece.mean;
            out.push_back(piece);
        }
        base += hi - lo;
        k = next;
    }
    return out;
}

} // namespace timeline
