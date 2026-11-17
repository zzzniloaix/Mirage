// The timeline panel (eval/phase3-survey/design.md §10; DECISIONS.md D-114): written by WP13,
// D-124 (a) and (c) put in by WP13b.

#include "TimelinePanel.h"

#include "JsonText.h"
#include "Logger.h"
#include "NetInspectorUi.h"   // the network sidebar's chip, glyph button and ×, shared
#include "PlaylistEvents.h"
#include "Scte35.h"
#include "Scte35Events.h"
#include "SessionPlan.h"
#include "TimelineBuild.h"
#include "TimelineDoc.h"
#include "UiFonts.h"
#include "UiTheme.h"
#include "UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>

namespace th = ui::theme;

using timeline::CurveRef;
using timeline::Event;
using timeline::Source;

namespace {

// ── Layout (logical px) ──────────────────────────────────────────────────────

constexpr float kHeaderH  = 30.0f;    // the title, the curves' chips, the buttons
constexpr float kAxisH    = 16.0f;    // the axis's tick labels
constexpr float kLaneH    = 12.0f;    // one event lane per source (design §10)
constexpr float kLabelW   = 62.0f;    // the lanes' names and the scores' scale, left of the plot
constexpr float kPad      = 8.0f;
constexpr float kEdgeH    = 6.0f;     // the top edge, which resizes the panel (ui.md §2.1)
constexpr float kMinH     = 168.0f;
constexpr float kAutoMaxH = 300.0f;
constexpr float kTickPx   = 64.0f;    // ticks at least this far apart, as the Alignment tab's
// Hit testing, which the design leaves to the panel (src/TimelineView.h): the
// Alignment tab's marker hovers, a flag within 4 px and a ring within 5
// (src/NetInspectorAlign.cpp), for a click on a curve and for a lane's
// markers; markers closer than 6 px drawn as one, with their count.
constexpr float kPickRadius    = 4.0f;
constexpr float kMarkerRadius  = 5.0f;
constexpr float kMarkerSpacing = 6.0f;
// Points per AddPolyline() call: a thick line takes up to four vertices a
// point, and one draw call's 16-bit indices address 2^16 (ui.md §4.2).
constexpr int kPolyChunk = 16000;
// Lines of events a tooltip lists before it counts the rest.
constexpr std::size_t kTipEvents = 12;

constexpr ImU32 kCurveCols[] = { th::kAccent, th::rgba(167, 139, 250, 1.0f), th::kWarn,
                                 th::rgba(52, 211, 153, 1.0f), th::rgba(244, 114, 182, 1.0f),
                                 th::rgba(148, 163, 184, 1.0f) };
// The lanes' colours, in §3.1's order of sources (src/TimelineModel.h).
constexpr ImU32 kSourceCols[timeline::kSourceCount] = {
    th::rgba(167, 139, 250, 1.0f),   // vmaf
    th::rgba(56, 189, 248, 1.0f),    // network
    th::rgba(52, 211, 153, 1.0f),    // playlist
    th::kWarn,                       // scte35
    th::rgba(244, 114, 182, 1.0f),   // player
    th::rgba(148, 163, 184, 1.0f),   // decoder
};

ImU32 C(ImU32 c, float mul = 1.0f) { return ImGui::GetColorU32(c, mul); }
ImU32 curve_col(std::size_t line) { return kCurveCols[line % std::size(kCurveCols)]; }

// The most a user's height can be: 70% of what the top bar and the control
// stack leave of the display.
float max_height(float win_h)
{
    return std::max(kMinH, (win_h - th::kTopBarH - th::kControlStackH) * 0.7f);
}

// A tick step in frames, on an axis without seconds, where the panel labels
// frame numbers (D-121 (e), Q90 (a); src/TimelineView.h): 1, 2, 5, 10, 20,
// 50 ..., the first whose ticks lie at least `min_px` apart.
double frame_step(double span, float width, float min_px)
{
    for (double s = 1.0; s < 1e15; s *= 10.0)
        for (const double m : { 1.0, 2.0, 5.0 })
            if (span / (s * m) * min_px <= width) return s * m;
    return 1e15;
}

// ── Text, in mirage_vmaf attribute's words ───────────────────────────────────
// The CLI's answer text (src/TimelineCli.cpp: answer_block() and the helpers
// it calls) isn't compiled into the player (CMakeLists.txt's mirage target,
// P-C), so the card writes its lines here as answer_block() writes them, so
// that the panel and `mirage_vmaf attribute --format text` read alike: the
// interval's head, its seconds as Axis::seconds() gives them, and a line per
// event in event_detail()'s words for every type that names, a session's
// requests and rungs, a segment's byte range, an ad break's declared
// duration, a corrupt packet's next boundary, and a decoded SCTE-35
// message's carrier, joined cue and CRC_32 (D-106 (c), D-109 (e)) among them;
// any other type's payload, as the CLI writes a type it doesn't know. The
// copies below are the CLI's functions of the same names; a change to one is
// a change to the other, and the QA record compares the two
// (eval/timeline/ui-qa-2026-11-16/check_ui.py).

const timeline::Value* member(const timeline::Payload& p, std::string_view name)
{
    for (const auto& [n, v] : p)
        if (n == name) return &v;
    return nullptr;
}

std::string value_text(const timeline::Value& v);

std::string payload_text(const timeline::Payload& p)
{
    std::string out = "{";
    for (std::size_t i = 0; i < p.size(); ++i)
        out += std::format("{}{}: {}", i == 0 ? "" : ", ", json::quote(p[i].first),
                           std::holds_alternative<std::string>(p[i].second.v)
                               ? json::quote(std::get<std::string>(p[i].second.v))
                               : value_text(p[i].second));
    return out + "}";
}

std::string value_text(const timeline::Value& v)
{
    if (const bool* b = std::get_if<bool>(&v.v)) return *b ? "true" : "false";
    if (const auto* i = std::get_if<std::int64_t>(&v.v)) return std::format("{}", *i);
    if (const auto* d = std::get_if<double>(&v.v)) return json::number(*d);
    if (const auto* s = std::get_if<std::string>(&v.v)) return *s;
    if (const auto* l = std::get_if<timeline::Value::List>(&v.v)) {
        std::string out = "[";
        for (std::size_t i = 0; i < l->size(); ++i) out += (i == 0 ? "" : ", ") + value_text((*l)[i]);
        return out + "]";
    }
    if (const auto* o = std::get_if<timeline::Value::Object>(&v.v)) return payload_text(*o);
    return "null";
}

// A member's text, "?" when the payload lacks it.
std::string member_text(const timeline::Payload& p, std::string_view name)
{
    const timeline::Value* v = member(p, name);
    return v ? value_text(*v) : std::string("?");
}

// Whether the payload has `name` and it isn't null.
bool present(const timeline::Payload& p, std::string_view name)
{
    const timeline::Value* v = member(p, name);
    return v && !std::holds_alternative<std::monostate>(v->v);
}

// The object member `name`; an empty one when the payload lacks it.
const timeline::Payload& object_member(const timeline::Payload& p, std::string_view name)
{
    static const timeline::Payload kNone;
    const timeline::Value* v = member(p, name);
    return v && std::holds_alternative<timeline::Value::Object>(v->v) ? std::get<timeline::Value::Object>(v->v)
                                                                      : kNone;
}

// Whether the payload's `name` is the boolean false, not null or absent.
bool is_false(const timeline::Payload& p, std::string_view name)
{
    const timeline::Value* v = member(p, name);
    return v && std::holds_alternative<bool>(v->v) && !std::get<bool>(v->v);
}

// A signed offset of seconds, three decimals, never "-0.000".
std::string offset_text(double s)
{
    const double r = std::round(s * 1000.0) / 1000.0;
    return std::format("{:+.3f} s", r == 0.0 ? 0.0 : r);
}

// A number of seconds of a payload, three decimals; "? s" for none.
std::string seconds_text(const timeline::Payload& p, std::string_view name)
{
    const timeline::Value* v = member(p, name);
    const std::optional<double> s = v ? timeline::number_of(*v) : std::nullopt;
    return s ? std::format("{:.3f} s", *s) : std::string("? s");
}

// A rung as a session's player events name it (D-075; src/SessionPlan.h):
// its entry, URI, RESOLUTION, BANDWIDTH and both ranks, what it lacks left out.
std::string rung_text(const timeline::Payload& r)
{
    std::string out = std::format("entry {} ({}", member_text(r, "entry"), member_text(r, "uri"));
    if (present(r, "resolution")) out += ", " + member_text(r, "resolution");
    if (present(r, "bandwidth")) out += ", BANDWIDTH " + member_text(r, "bandwidth");
    if (present(r, "rank_from_top"))
        out += std::format("; rank {} from the top, {} from the bottom", member_text(r, "rank_from_top"),
                           member_text(r, "rank_from_bottom"));
    return out + ")";
}

// A session fetch's request (src/SessionPlan.h's payload): its URL, its byte
// range, when it was made on the session's clock, how long it took to its
// last byte and to its response's head, its bytes, its status, and whether it
// failed and why.
std::string request_text(const timeline::Payload& p)
{
    std::string out = member_text(p, "url");
    const timeline::Payload& range = object_member(p, "range");
    if (!range.empty())
        out += std::format(" (bytes {} at {})", member_text(range, "length"), member_text(range, "offset"));
    out += std::format(", requested at {}: {} from request to last byte", seconds_text(p, "start"),
                       seconds_text(p, "duration"));
    const timeline::Value* start = member(p, "start");
    const timeline::Value* head  = member(p, "first_byte");
    const std::optional<double> s = start ? timeline::number_of(*start) : std::nullopt;
    const std::optional<double> h = head ? timeline::number_of(*head) : std::nullopt;
    if (s && h) out += std::format(" ({:.3f} s to the response head)", *h - *s);
    if (present(p, "bytes")) out += std::format(", {} bytes", member_text(p, "bytes"));
    if (present(p, "status")) out += std::format(", {} {}", member_text(p, "status"), member_text(p, "status_text"));
    const timeline::Value* failed = member(p, "failed");
    if (failed && std::holds_alternative<bool>(failed->v) && std::get<bool>(failed->v))
        out += present(p, "error") ? std::format(", failed: {}", member_text(p, "error")) : ", failed";
    return out;
}

// The line of the tag that carried a decoded SCTE-35 message: its position
// anchor's, which a cue_in that joins a cue_out keeps where its `line` takes
// that cue's (D-109 (e)); for an event without anchors, its `line`; nullopt
// for a carrier other than a tag.
std::optional<std::string> carrier_line(const timeline::Payload& p)
{
    const timeline::Value* anchors = member(p, "anchors");
    if (!anchors) return present(p, "line") ? std::optional(member_text(p, "line")) : std::nullopt;
    if (const auto* list = std::get_if<timeline::Value::List>(&anchors->v))
        for (const timeline::Value& a : *list)
            if (const auto* o = std::get_if<timeline::Value::Object>(&a.v);
                o && member_text(*o, "anchor") == "position" && present(*o, "line"))
                return member_text(*o, "line");
    return std::nullopt;
}

// A decoded SCTE-35 message's event as its line starts: the line of the tag
// that carried it and its form, or the form alone for another carrier; a
// cue_in that joins a cue_out names that cue (D-109 (e); WP09c's wording,
// which the owner kept, D-113 (c)).
std::string message_head(const timeline::Payload& p)
{
    const std::string                form = member_text(p, "form");
    const std::optional<std::string> own  = carrier_line(p);
    std::string out = own ? std::format("line {}, {}", *own, form) : form;
    if (present(p, "line") && (!own || member_text(p, "line") != *own))
        out += std::format(", joining the cue_out at line {}", member_text(p, "line"));
    return out;
}

// What an event adds where a decoded message's CRC_32 fails, the message
// kept, marked (D-106 (c)): its own crc_valid, and that of the messages a cue
// holds (D-106 (d), D-109 (a), (b)).
std::string crc_text(const timeline::Payload& p)
{
    std::string out = is_false(p, "crc_valid") ? ", its CRC_32 fails: kept, crc_valid false (D-106 (c))" : "";
    std::size_t held = 0;
    const timeline::Value* records = member(p, "messages");
    if (const auto* list = records ? std::get_if<timeline::Value::List>(&records->v) : nullptr)
        for (const timeline::Value& r : *list)
            if (const auto* o = std::get_if<timeline::Value::Object>(&r.v); o && is_false(*o, "crc_valid")) ++held;
    if (held == 1) out += ", a message it holds fails its CRC_32: kept, crc_valid false (D-106 (c))";
    if (held > 1)
        out += std::format(", {} messages it holds fail their CRC_32: kept, crc_valid false (D-106 (c))", held);
    return out;
}

// A segmentation_type_id with its name in Table 23: "0x10 (Program Start)".
std::string segmentation_type_text(const timeline::Value& v)
{
    const auto* t = std::get_if<std::int64_t>(&v.v);
    if (!t || *t < 0 || *t > 0xFF) return value_text(v);
    return std::format("0x{:02X} ({})", *t, scte35::segmentation_type_name(static_cast<std::uint8_t>(*t)));
}

// Items in words: "a", "a and b", "a, b and c".
std::string listed_text(const std::vector<std::string>& items)
{
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i)
        out += (i == 0 ? "" : i + 1 == items.size() ? " and " : ", ") + items[i];
    return out;
}

// Discontinuity sequences in words: "0 and 1".
std::string sequences_text(const std::vector<int>& sequences)
{
    std::vector<std::string> items;
    for (const int s : sequences) items.push_back(std::to_string(s));
    return listed_text(items);
}

// What an event's payload says, in a few words (the design's P-AD); the
// payload itself for a type this doesn't know.
std::string event_detail(const Event& e)
{
    const timeline::Payload& p = e.payload;
    const std::string line = std::format("line {}", member_text(p, "line"));
    if (e.type == timeline::kTypeVariantSpan) {
        const timeline::Value* scored = member(p, "scored");
        const bool none = scored && std::holds_alternative<bool>(scored->v) && !std::get<bool>(scored->v);
        const std::string first = member_text(p, "first_seq"), last = member_text(p, "last_seq");
        return std::format("{}, {}{}", rung_text(p),
                           first == last ? "segment " + first : std::format("segments {}-{}", first, last),
                           none ? ", not scored: the reference in top-rung mode (D-105 (b))" : "");
    }
    if (e.type == timeline::kTypeVariantSwitch)
        return std::format("{} -> {}", rung_text(object_member(p, "from")), rung_text(object_member(p, "to")));
    if (e.type == timeline::kTypeSegmentRequest || e.type == timeline::kTypePlaylistRequest) return request_text(p);
    if (e.type == timeline::kTypeSegment) {
        std::string out = std::format("{}, EXTINF {} s", member_text(p, "uri"), member_text(p, "extinf"));
        const timeline::Value* r = member(p, "byte_range");
        if (r && std::holds_alternative<timeline::Value::Object>(r->v)) {
            const auto& o = std::get<timeline::Value::Object>(r->v);
            out += std::format(", bytes {} at {}", member_text(o, "length"), member_text(o, "offset"));
        }
        return out;
    }
    if (e.type == timeline::kTypeDiscontinuity)
        return std::format("{}, discontinuity sequence {}", line, member_text(p, "discontinuity_sequence"));
    if (e.type == timeline::kTypeProgramDateTime) return std::format("{}, {}", line, member_text(p, "date"));
    // A daterange's and a plain cue's tag as written; a cue of a decoded
    // message's own by the line and form of the tag that carried it.
    if (e.type == timeline::kTypeDaterange || e.type == timeline::kTypeCueOut ||
        e.type == timeline::kTypeCueOutCont || e.type == timeline::kTypeCueIn)
        return (member(p, "classes") ? message_head(p) : std::format("{}, {}", line, member_text(p, "raw"))) +
               crc_text(p);
    // A decoded message's splice time (D-106 (a)).
    if (e.type == timeline::kTypeSplicePoint) {
        std::string out = std::format("{}, {}: splice time {} (90 kHz)", message_head(p), member_text(p, "command"),
                                      member_text(p, "pts_adjusted"));
        const timeline::Value* adjustment = member(p, "pts_adjustment");
        const auto* ticks = adjustment ? std::get_if<std::int64_t>(&adjustment->v) : nullptr;
        if (ticks && *ticks != 0)
            out += std::format(", pts_time {} + pts_adjustment {}", member_text(p, "pts_time"), *ticks);
        if (present(p, "component_tag")) out += ", component " + member_text(p, "component_tag");
        out += std::format(", frame {} of its input", member_text(p, "index"));
        if (is_false(p, "in_segment")) out += ", outside its tag's segment (D-106 (a))";
        return out + crc_text(p);
    }
    // A decoded message that is neither a cue-out nor a cue-in (D-106 (b)).
    if (e.type == timeline::kTypeMessage) {
        std::string out = std::format("{}, {}", message_head(p), member_text(p, "command"));
        const timeline::Value* types = member(p, "segmentation_types");
        const auto* list = types ? std::get_if<timeline::Value::List>(&types->v) : nullptr;
        if (list && !list->empty()) {
            std::vector<std::string> names;
            for (const timeline::Value& t : *list) names.push_back(segmentation_type_text(t));
            out += std::format(", segmentation type{} {}", names.size() == 1 ? "" : "s", listed_text(names));
        }
        return out + crc_text(p);
    }
    // A cancellation (D-106 (b)): the events it cancels.
    if (e.type == timeline::kTypeCancel) {
        std::vector<std::string> what;
        if (present(p, "splice_event_id")) what.push_back("splice_event_id " + member_text(p, "splice_event_id"));
        const timeline::Value* ids = member(p, "segmentation_event_ids");
        const auto* list = ids ? std::get_if<timeline::Value::List>(&ids->v) : nullptr;
        if (list)
            for (const timeline::Value& id : *list) what.push_back("segmentation_event_id " + value_text(id));
        return std::format("{}, {} cancelling {}{}", message_head(p), member_text(p, "command"),
                           what.empty() ? std::string("no event it names") : listed_text(what), crc_text(p));
    }
    if (e.type == timeline::kTypeTag) {
        const timeline::Value* value = member(p, "value");
        const bool none = !value || std::holds_alternative<std::monostate>(value->v);
        return std::format("{}, #{}{}", line, member_text(p, "name"), none ? "" : ":" + value_text(*value));
    }
    if (e.type == timeline::kTypeAdBreak) {
        const std::string end = member_text(p, "end");
        if (end == "cue_in")
            return std::format("from the cue_out at line {} to the cue_in at line {}", member_text(p, "cue_out_line"),
                               member_text(p, "cue_in_line"));
        std::string declared = "?";
        if (const timeline::Value* d = member(p, "durations"); d && std::holds_alternative<timeline::Value::Object>(d->v))
            declared = member_text(std::get<timeline::Value::Object>(d->v), end);
        return std::format("from the cue_out at line {}, ended by its {} of {} s", member_text(p, "cue_out_line"),
                           end, declared);
    }
    if (e.type == timeline::kTypeContextStart)
        return std::format("libvmaf context {}, from frame pair {}", member_text(p, "context"),
                           member_text(p, "first_index"));
    if (e.type == timeline::kTypeUnpairedFrame)
        return std::format("{} frame {}, {}", member_text(p, "input"), member_text(p, "index"),
                           member_text(p, "reason"));
    if (e.type == timeline::kTypeKeyframe) return std::format("frame {} of its input", member_text(p, "index"));
    if (e.type == timeline::kTypeCorruptPacket) {
        std::string out = std::format("frame {} of its input", member_text(p, "index"));
        const timeline::Value* b = member(p, "next_boundary");
        if (!b || !std::holds_alternative<timeline::Value::Object>(b->v)) return out + ", in its last segment";
        const auto& o = std::get<timeline::Value::Object>(b->v);
        const timeline::Value* disc = member(o, "discontinuity");
        const bool after = disc && std::holds_alternative<bool>(disc->v) && std::get<bool>(disc->v);
        return out + std::format("; the next segment, {}, starts at reference frame {}{}",
                                 member_text(o, "media_sequence"), member_text(o, "ref_index"),
                                 after ? ", after an EXT-X-DISCONTINUITY" : "");
    }
    return payload_text(p);
}

// A variant by its identity (D-075): its master entries, or for a variant
// without any, the reference's role or its index; as the CLI names one.
std::string variant_name(const timeline::Timeline& t, int v)
{
    if (v < 0 || static_cast<std::size_t>(v) >= t.variants.size()) return "no variant";
    const timeline::Variant& var = t.variants[static_cast<std::size_t>(v)];
    if (var.entries.empty()) return var.pair < 0 ? "the reference" : std::format("variant {}", v);
    std::string out = var.entries.size() == 1 ? "entry" : "entries";
    for (std::size_t i = 0; i < var.entries.size(); ++i) out += std::format("{}{}", i == 0 ? " " : ", ", var.entries[i]);
    return out;
}

std::string frames_text(timeline::Interval i)
{
    if (i.ref_end <= i.ref_index) return std::format("no reference frame, an empty interval at {}", i.ref_index);
    const std::int64_t n = i.ref_end - i.ref_index;
    return std::format("reference frames {}-{} ({} frame{})", i.ref_index, i.ref_end - 1, n, n == 1 ? "" : "s");
}

std::string score_text(const std::optional<double>& v)
{
    return v ? std::format("{:.3f}", *v) : std::string("none");
}

// A position on the axis as the panel labels it: seconds, or a frame on an
// axis without seconds (Q90 (a)).
std::string position_text(const timeline::AxisScale& s, double p)
{
    return s.seconds ? std::format("{:.3f} s", p) : std::format("frame {:.0f}", p);
}

// Two positions as the panel labels a span: "3.000-5.000 s", or "frames
// 75-125" on an axis without seconds.
std::string range_text(const timeline::AxisScale& s, double a, double b)
{
    return s.seconds ? std::format("{:.3f}-{:.3f} s", a, b) : std::format("frames {:.0f}-{:.0f}", a, b);
}

// An event's line in an attribution, as answer_block() writes it.
std::string found_line(const timeline::Timeline& t, Source source, const timeline::Found& f)
{
    const Event& e = t.events[f.event];
    std::string where = e.variant < 0 && e.type == timeline::kTypePlaylistRequest ? std::string("the master")
                                                                                   : variant_name(t, e.variant);
    if (e.segment) where += std::format(", segment {}", *e.segment);
    std::string seconds = offset_text(f.offset_s);
    if (f.offset_end_s) seconds += " to " + offset_text(*f.offset_end_s);
    const std::string frames = f.offset_end_frames
                                   ? std::format("{:+} to {:+} frames", f.offset_frames, *f.offset_end_frames)
                                   : std::format("{:+} frames", f.offset_frames);
    return std::format("{:<9}{:<16} {} at {} ({}): {}", timeline::source_name(source), e.type, where, seconds,
                       frames, event_detail(e));
}

// An event as a tooltip lists it: where it sits and what it says.
std::string event_line(const timeline::Timeline& t, const Event& e)
{
    std::string where = variant_name(t, e.variant);
    if (e.segment) where += std::format(", segment {}", *e.segment);
    std::string frames = e.ref_end ? std::format("frames {}-{}", e.ref_index, *e.ref_end - 1)
                                   : std::format("frame {}", e.ref_index);
    return std::format("{} {} ({}; {}): {}", timeline::source_name(e.source), e.type, where, frames,
                       event_detail(e));
}

bool same_curve(CurveRef a, CurveRef b) { return a.kind == b.kind && a.index == b.index; }

} // namespace

// ── The document ─────────────────────────────────────────────────────────────

void TimelinePanel::set_document(const std::string& path)
{
    if (path.empty()) return;
    document_     = path;
    load_pending_ = true;
}

float TimelinePanel::height(float win_h) const
{
    // Automatic: a third of the display, from kMinH to kAutoMaxH; the user's,
    // dragged by its top edge, up to max_height().
    const float want = height_ > 0.0f ? height_ : std::clamp(win_h * 0.34f, kMinH, kAutoMaxH);
    return std::round(std::clamp(want, kMinH, max_height(win_h)));
}

void TimelinePanel::reset()
{
    match_open_ = nullptr;
    match_.reset();
    override_     = false;
    play_variant_ = -1;
    play_state_   = {};
    playhead_.reset();
    playhead_unknown_.clear();
    seen_any_ = false;
    own_seek_.reset();
    pick_.reset();
    pending_click_.reset();
    qa_picks_.clear();
    last_entry_  = -1;
    lanes_dirty_ = true;
    drawn_valid_ = false;
}

void TimelinePanel::take_load()
{
    std::optional<timeline::LoadResult> r = loader_.take();
    if (!r) return;
    loading_ = false;
    if (!r->snapshot) {
        // A refused document shows as refused, not as the one before it.
        snap_.reset();
        lines_.clear();
        load_error_ = r->error;
        logger::warn("timeline: {} not loaded: {}", document_, load_error_);
        return;
    }
    load_error_.clear();
    snap_ = std::move(r->snapshot);
    on_snapshot();
    logger::info("timeline: loaded {}: {} events on {} reference frames, {} curves, {} attributions; "
                 "the ladder report {}: {}", snap_->document_path, snap_->timeline.events.size(),
                 snap_->timeline.axis.frames, snap_->curves.size(), snap_->attributions.size(), snap_->report_path,
                 snap_->report_check);
}

void TimelinePanel::on_snapshot()
{
    const timeline::Timeline& t = snap_->timeline;
    lines_.clear();
    for (std::size_t c = 0; c < snap_->curves.size(); ++c) {
        const timeline::PanelCurve& pc = snap_->curves[c];
        PanelLine l;
        l.curve = c;
        if (pc.ref.kind == CurveRef::Kind::Pair) {
            l.variant = pc.points.variant;
            const std::optional<int> n = timeline::pair_number(t, pc.ref);
            l.name  = n ? std::format("pair:{}", *n) : std::string("pair:?");
            l.label = variant_name(t, l.variant);
            if (l.variant >= 0 && static_cast<std::size_t>(l.variant) < t.variants.size()) {
                const timeline::Variant& v = t.variants[static_cast<std::size_t>(l.variant)];
                if (v.resolution) l.label += " · " + *v.resolution;
            }
        } else {
            const std::string& id = t.sessions[static_cast<std::size_t>(pc.ref.index)].id;
            l.name  = std::format("session:{}", id);
            l.label = l.name;
        }
        lines_.push_back(std::move(l));
    }
    apply_qa_shown();
    env_.assign(lines_.size(), {});
    drawn_.assign(lines_.size(), {});
    drawn_valid_ = false;
    clocks_.assign(t.variants.size(), std::nullopt);
    match_open_ = nullptr;
    match_.reset();
    override_     = false;
    play_variant_ = -1;
    play_state_   = {};
    playhead_.reset();
    playhead_unknown_.clear();
    seen_any_ = false;
    pick_.reset();
    pending_click_.reset();
    lanes_dirty_ = true;
    timeline::fit_view(view_, snap_->scale);
    qa_view_due_ = qa_fit_ || qa_zoom_.has_value();
}

// QA (--ui-timeline): the curves curves= names alone, else those of the
// variants variants= names; every curve where neither is given.
void TimelinePanel::apply_qa_shown()
{
    if (!snap_ || (!qa_curves_ && !qa_entries_)) return;
    const timeline::Timeline& t = snap_->timeline;
    for (PanelLine& l : lines_) {
        if (qa_curves_) {
            l.shown = std::find(qa_curves_->begin(), qa_curves_->end(), l.name) != qa_curves_->end();
            continue;
        }
        l.shown = false;
        if (l.variant < 0) continue;
        for (const std::size_t e : t.variants[static_cast<std::size_t>(l.variant)].entries)
            if (std::find(qa_entries_->begin(), qa_entries_->end(), e) != qa_entries_->end()) l.shown = true;
    }
    if (qa_curves_)
        for (const std::string& c : *qa_curves_)
            if (std::none_of(lines_.begin(), lines_.end(), [&](const PanelLine& l) { return l.name == c; }))
                logger::warn("--ui-timeline: curves= names {}, which the document has no curve of", c);
    lanes_dirty_ = true;
}

// ── The open stream and the variant on screen ────────────────────────────────

void TimelinePanel::ensure_match(const Inputs& in)
{
    if (!snap_ || !in.open) return;
    if (match_ && match_open_ == in.open) return;
    match_open_ = in.open;
    match_      = timeline::match_stream(*snap_, *in.open);
    play_variant_ = -1;
    lanes_dirty_  = true;
    logger::info("timeline: matched to {} by {}{}", in.open->path, timeline::match_name(match_->by),
                 match_->warnings.empty() ? std::string()
                                          : std::format(", {} warning(s): {}", match_->warnings.size(),
                                                        match_->warnings.front()));
}

int TimelinePanel::screen_variant(const Inputs& in) const
{
    if (!match_) return -1;
    switch (match_->by) {
    case timeline::StreamMatch::By::MasterBytes:
    case timeline::StreamMatch::By::Address:
        return in.entry >= 0 && static_cast<std::size_t>(in.entry) < match_->variant_of.size()
                   ? match_->variant_of[static_cast<std::size_t>(in.entry)] : -1;
    case timeline::StreamMatch::By::Variant: return match_->opened_variant;
    case timeline::StreamMatch::By::None:    break;
    }
    return -1;
}

const timeline::VariantClock& TimelinePanel::clock_of(int variant)
{
    static const timeline::VariantClock kNone;
    if (!snap_ || variant < 0 || static_cast<std::size_t>(variant) >= clocks_.size()) return kNone;
    std::optional<timeline::VariantClock>& c = clocks_[static_cast<std::size_t>(variant)];
    if (!c) c = timeline::variant_clock(*snap_, variant);   // once per variant: O(its frames and events)
    return *c;
}

// What became of the seek the pick asked for (Pick::sent). main.cpp makes a
// seek from s_seek_to at the start of its next iteration, before it draws
// the panel again, and a seek it makes moves the seek serial: so the next
// draw tells. A serial moved to the target asked for is the seek made; one
// moved elsewhere, another seek that took its place in that iteration, a
// seek-bar release or a keyframe step; one that didn't move, no seek made,
// main.cpp seeking by the difference from the PTS on screen, which was the
// target already: none needed where the playhead, as last drawn, is in the
// sequence of the frame asked for, so that the frame on screen is that one.
// Where the panel wasn't drawn in between, closed and opened again, a serial
// moved elsewhere can't tell which seek was whose.
void TimelinePanel::settle_seek(const Inputs& in, bool drawn_last_frame)
{
    if (!pick_ || pick_->sent != Sent::Asked) return;
    Pick& p = *pick_;
    const bool moved = in.seek_serial != p.asked_serial;
    if (moved && std::fabs(in.seek_target - p.asked_target) < 1e-6) {
        p.sent = Sent::Made;
        logger::info("timeline: the seek to PTS {:.3f} made", p.asked_target);
        return;
    }
    p.sent = Sent::Refused;
    // Where no seek was made, the frame on screen, at the PTS asked for, may
    // be another than the one asked for, the picked frame or the offered
    // segment start: where the playhead isn't known, that PTS lies in more
    // than one sequence (D-124 (c)); where it is known in another sequence
    // than the frame asked for, the frame on screen is that sequence's, as
    // inside an ad whose timestamps restart at the main content's origin.
    // The note says so, the panel offering nothing more, as WP13 built it:
    // whether it should then offer a segment start, as for a frame that no
    // seek reaches (D-114 (b)), is pending the owner.
    const bool offer = p.decision.kind == timeline::SeekDecision::Kind::Offer;
    if (!moved && !playhead_unknown_.empty())
        p.note = std::format("no seek made: the player was at that PTS already, which lies in sequences {}: "
                             "the panel doesn't know whether the frame on screen is {} (D-124 (c))",
                             sequences_text(playhead_unknown_),
                             offer ? "the offered segment start" : "the one picked");
    else if (!moved && playhead_ && p.asked_sequence >= 0 && playhead_->sequence != p.asked_sequence)
        p.note = std::format("not made: the player was at that PTS already, in sequence {}, not {} sequence {}",
                             playhead_->sequence, offer ? "the offered segment start's" : "the picked frame's",
                             p.asked_sequence);
    else if (!moved)
        p.note = "no seek needed: the player was at that PTS already";
    else if (drawn_last_frame)
        p.note = std::format("not made: another seek, to PTS {:.3f} s, took its place", in.seek_target);
    else
        p.note = std::format("not known: the panel was closed before the player took it, and its latest seek "
                             "went to PTS {:.3f} s", in.seek_target);
    logger::info("timeline: the seek to PTS {:.3f}: {}", p.asked_target, p.note);
}

// The playhead (eval/phase3-survey/ui.md §2.4): the frame on screen tracked
// frame by frame through track_playhead(), which follows the discontinuity
// sequences as the PTS jump; after a seek the sequence of the frame the
// seek shows, the panel's own seek's frame, else where simulate_seek() lands
// the target, where that is one sequence for every lead of the first
// packet's DTS that the seek decision allows, and otherwise none (WP12's
// notes; playhead_sought()), so that a jump back caused by a seek isn't
// taken for the next sequence; after a switch, where simulate_seek() lands
// the first new frame's PTS (below; playhead_switched()); after a frame step
// the sequence it was in. Where the sequence isn't known and the PTS on
// screen lies in more than one, no playhead, and a line that says why, until
// a seek, a switch or a jump of the PTS tells it, or the PTS lies in one
// sequence only (D-124 (c), Q93 (c)): the tracker decides it.
void TimelinePanel::follow_playhead(const Inputs& in)
{
    last_entry_ = in.entry;
    bool track = false;
    bool first_sight = false;
    if (!seen_any_) {
        // First sight of the player's counters, at a new session, a new
        // document, a new match or the panel opened again: the sequence isn't
        // known, and track_playhead() is told it by where the PTS on screen
        // lies, as it is after a seek whose landing isn't known. Inside a
        // sequence whose PTS repeat an earlier one's, an ad's restarting at
        // the main content's origin, as D-069's does, the PTS lies in both:
        // no playhead then, and the line that says why, until a seek, a
        // switch or a jump of the PTS tells the panel which, or the PTS lies
        // in one sequence only, on D-069's ladder where the ad ends, the
        // main content's PTS after it being in no other (D-124 (c), Q93 (c)).
        // Closed, the panel isn't fed, so that the render loop does nothing
        // for it then.
        seen_any_   = true;
        seen_seek_  = in.seek_serial;
        seen_step_  = in.step_mark;
        seen_frame_ = in.frame_serial;
        play_state_ = {};
        playhead_.reset();
        playhead_unknown_.clear();
        own_seek_.reset();
        track       = true;
        first_sight = true;
    }
    const int v = snap_ ? screen_variant(in) : -1;
    if (v != play_variant_) {
        // Another variant on screen: a switch, or the match just made. Its
        // frames carry PTS of their own. The Demuxer repositions a switch
        // with a seek by PTS (av_seek_frame on the position playing,
        // src/Demuxer.cpp), so the new variant's frames go on from where that
        // seek lands, the first new frame being where simulate_seek() lands
        // its PTS: its sequence, which tells the panel the sequence, else,
        // where the simulation shows no frame, the one being played
        // (playhead_switched()). Inside an ad whose timestamps restart at the
        // main content's origin that seek lands in the main content, which
        // the player then plays again: the Demuxer's doing, reported in
        // Phase 3's PR, the Demuxer unchanged, which the owner chose
        // (D-124 (e), Q95 (a)); the playhead follows the main content's frame
        // on screen. On first sight there is no switch to go on from.
        const bool switched = !first_sight && play_variant_ >= 0 && v >= 0;
        play_variant_ = v;
        if (switched && in.has_frame && !in.scrubbing)
            timeline::playhead_switched(play_state_, clock_of(v), in.seek, in.frame_pts);
        else
            timeline::playhead_seeked(play_state_, play_state_.sequence);
        lanes_dirty_ = true;
        track = true;
        // A decision whose seek wasn't made was the old variant's: made
        // again for the one on screen, its seek left to a click (one seek
        // per click).
        if (pick_ && pick_->variant != v && (pick_->sent == Sent::No || pick_->sent == Sent::Refused)) {
            decide(*pick_, in);
            pick_->redecided = true;
            pick_->sent      = Sent::No;
            pick_->note.clear();
        }
    }
    if (v < 0) {
        playhead_.reset();
        playhead_unknown_.clear();
        seen_seek_  = in.seek_serial;
        seen_step_  = in.step_mark;
        seen_frame_ = in.frame_serial;
        return;
    }
    const timeline::VariantClock& clock = clock_of(v);
    if (in.seek_serial != seen_seek_) {
        // A seek tells the panel the sequence where it can tell where the
        // seek lands (D-124 (c)): the panel's own, by its frame's, which a
        // seek to its PTS shows for every lead that decide_seek() allows;
        // another, by where simulate_seek() lands its target, where that is
        // one sequence for every such lead (playhead_sought()); otherwise it
        // tells nothing, and the frame on screen tells it where its PTS lies
        // in one sequence only.
        seen_seek_ = in.seek_serial;
        seen_step_ = in.step_mark;
        if (own_seek_ && std::fabs(own_seek_->target - in.seek_target) < 1e-6)
            timeline::playhead_seeked(play_state_, own_seek_->sequence);
        else if (in.seek_target >= 0.0)
            timeline::playhead_sought(play_state_, clock, in.seek, in.seek_target);
        else
            timeline::playhead_seeked(play_state_, -1);
        own_seek_.reset();
    } else if (in.step_mark != seen_step_) {
        // A step that moves step_mark alone, a step back (main.cpp's, by the
        // scrub decoder's seek by PTS) or a keyframe step that found no
        // keyframe: the sequence it was in, its last PTS forgotten, so that
        // the step isn't taken for a jump, as WP13 built it. A step whose
        // frame is another sequence's, as one across a sequence's start,
        // isn't told apart, and the playhead stays in the sequence it was
        // in, which can mark another frame than the one on screen: left to a
        // follow-up, the inputs not telling the steps apart.
        seen_step_ = in.step_mark;
        timeline::playhead_seeked(play_state_, play_state_.sequence);
    }
    if (in.scrubbing || !in.has_frame) return;   // a scrub's frames are the scrub decoder's, anywhere
    if (in.frame_serial == seen_frame_ && !track) return;
    seen_frame_ = in.frame_serial;
    const bool was_unknown = !playhead_unknown_.empty();
    playhead_         = timeline::track_playhead(clock, play_state_, in.frame_pts);
    playhead_unknown_ = play_state_.unknown;
    playhead_pts_     = in.frame_pts;
    // The playhead's sequence lost or told, once each time (D-124 (c)).
    if (!playhead_unknown_.empty() && !was_unknown)
        logger::info("timeline: no playhead: {}", unknown_line());
    else if (playhead_ && was_unknown)
        logger::info("timeline: the playhead known again: sequence {}, reference frame {}, at PTS {:.3f}",
                     playhead_->sequence, playhead_->ref_index, playhead_pts_);
}

// ── A pick: the card and the seek ────────────────────────────────────────────

// The pick's seek decision, on the variant on screen: seek only where a seek
// shows the frame (D-114 (b)); otherwise the card says why and offers the
// decision's segment start. The decision doesn't take the playhead: the
// player seeks to a PTS from wherever it is, and the decision follows the
// seek from the stream's start (decide_seek()), so that it is made alike
// where the playhead isn't known (D-124 (c)); the seek, once the player
// makes it, tells the panel the sequence, its frame's (follow_playhead()).
void TimelinePanel::decide(Pick& p, const Inputs& in)
{
    p.variant  = screen_variant(in);
    p.decision = {};
    if (!match_ || match_->by == timeline::StreamMatch::By::None) {
        p.decision.why = "the document isn't the open stream's: no seek";
    } else if (p.variant < 0) {
        p.decision.why = "the frame on screen isn't one of the document's variants', so where a seek lands "
                         "can't be told";
    } else {
        p.decision = timeline::decide_seek(clock_of(p.variant), in.seek, p.ref_index);
    }
}

// Ask the player for the pick's seek, to its decision's target, whose frame
// is of sequence `sequence`: once, through Out::seek_to, where main.cpp makes
// it now (Inputs::can_seek); settle_seek() then tells whether it did.
void TimelinePanel::ask_seek(Pick& p, int sequence, const Inputs& in, Out& out)
{
    if (!in.can_seek) {
        p.sent = Sent::Refused;
        p.note = "not sent: the seek bar was being dragged, and its release seeks";
        logger::info("timeline: the seek to PTS {:.3f} {}", p.decision.target, p.note);
        return;
    }
    out.seek_to      = p.decision.target;
    own_seek_        = OwnSeek{ .target = p.decision.target, .sequence = sequence };
    p.sent           = Sent::Asked;
    p.note.clear();
    p.asked_target   = p.decision.target;
    p.asked_serial   = in.seek_serial;
    p.asked_sequence = sequence;
}

void TimelinePanel::make_pick(std::size_t line, std::size_t point, std::string how, const Inputs& in, Out& out)
{
    const timeline::PanelCurve& pc = snap_->curves[lines_[line].curve];
    Pick p;
    p.line      = line;
    p.point     = point;
    p.ref_index = pc.points.ref_index[point];
    p.vmaf      = pc.points.vmaf[point];
    p.how       = std::move(how);
    // Inside a detected drop the document's attribution, elsewhere the query
    // of the clicked frame's segment of the curve's variant (D-114 (d)).
    p.answer = timeline::click_answer(*snap_, pc.ref, p.ref_index);
    decide(p, in);
    if (p.decision.kind == timeline::SeekDecision::Kind::Seek) {
        const timeline::VariantClock& c = clock_of(p.variant);
        const bool known = p.decision.frame && *p.decision.frame < c.frames.size();
        ask_seek(p, known ? c.frames[*p.decision.frame].sequence : -1, in, out);
    }
    static constexpr const char* kKind[] = { "seek", "offer", "none" };
    logger::info("timeline: {} picked {} at reference frame {} (VMAF {:.3f}), {}; seek decision: {}{}{}", p.how,
                 lines_[line].name, p.ref_index, p.vmaf,
                 p.answer.drop ? std::format("in the document's attribution {}", *p.answer.drop)
                               : std::string("outside the document's drops"),
                 kKind[static_cast<int>(p.decision.kind)],
                 p.decision.kind == timeline::SeekDecision::Kind::None
                     ? std::string() : std::format(" to PTS {:.3f}", p.decision.target),
                 p.decision.why.empty() ? std::string() : ": " + p.decision.why);
    pick_ = std::move(p);
}

bool TimelinePanel::pick_attribution(std::size_t n, std::string how, const Inputs& in, Out& out)
{
    if (!snap_ || n >= snap_->attributions.size()) return false;
    const timeline::Attribution& a = snap_->attributions[n];
    for (std::size_t l = 0; l < lines_.size(); ++l) {
        const timeline::PanelCurve& pc = snap_->curves[lines_[l].curve];
        if (!same_curve(pc.ref, a.curve)) continue;
        // Its lowest point, the earliest of equal ones (src/TimelineView.h).
        const timeline::PointRange r = timeline::points_in(pc.points, a.interval.ref_index, a.interval.ref_end);
        const timeline::MinMax     m = timeline::range_min_max(pc.envelope, r.first, r.last);
        if (!m.any()) return false;
        make_pick(l, m.min_at, std::move(how), in, out);
        return true;
    }
    return false;
}

bool TimelinePanel::pick_at_x(float x, std::optional<float> y, float x0, float width, std::string how,
                              const Inputs& in, Out& out)
{
    // Each shown curve's lowest point near x (pick_lowest()); with a cursor,
    // the curve whose point lies nearest it, else the lowest of them, the
    // earliest curve of equal ones.
    const float cy0 = curves_y0_, cy1 = curves_y1_;
    auto y_of = [&](double v) { return cy1 - static_cast<float>(std::clamp(v, 0.0, 100.0) / 100.0) * (cy1 - cy0); };
    std::optional<std::size_t> best;
    timeline::PickedPoint best_p;
    double best_d = 0.0;
    for (std::size_t l = 0; l < lines_.size(); ++l) {
        if (!lines_[l].shown) continue;
        // No point picked in a session's span that isn't scored (D-124 (a),
        // Q91 (b)): `unscored` is empty for a pair's curve.
        const timeline::PanelCurve& pc = snap_->curves[lines_[l].curve];
        const std::optional<timeline::PickedPoint> pp = timeline::pick_lowest(
            pc.points, pc.envelope, pc.unscored, snap_->scale, view_, x, x0, width, kPickRadius);
        if (!pp) continue;
        const double d = y ? std::fabs(static_cast<double>(*y - y_of(pp->vmaf))) : pp->vmaf;
        if (!best || d < best_d) {
            best   = l;
            best_p = *pp;
            best_d = d;
        }
    }
    if (!best) return false;
    make_pick(*best, best_p.point, std::move(how), in, out);
    return true;
}

void TimelinePanel::run_qa_picks(const Inputs& in, float x0, float width, Out& out)
{
    for (QaPick& q : qa_picks_) {
        if (q.done || out.seek_to >= 0.0) continue;   // one seek a frame
        // What a click needs: a frame on screen, its time come, the document
        // matched to the stream.
        if (!in.has_frame || in.qa_t < 0.0 || (q.at >= 0.0 && in.qa_t < q.at) || !match_) continue;
        q.done = true;
        const std::string how = std::format("--ui-timeline {} at {:.2f} s after the first frame", q.text, in.qa_t);
        const bool ok = q.attribution
                            ? pick_attribution(q.n, how, in, out)
                            : pick_at_x(timeline::x_at(view_, q.position, x0, width), std::nullopt, x0, width, how,
                                        in, out);
        if (!ok) logger::warn("timeline: {}: nothing to pick there", how);
    }
}

// ── What the view shows ──────────────────────────────────────────────────────

const TimelinePanel::Envelope& TimelinePanel::envelope_of(std::size_t line, int columns)
{
    Envelope& e = env_[line];
    if (e.columns != columns || e.t0 != view_.t0 || e.t1 != view_.t1) {
        // No score in a column over a session's span that isn't scored
        // (D-124 (a)): `unscored` is empty for a pair's curve.
        const timeline::PanelCurve& pc = snap_->curves[lines_[line].curve];
        e.mm      = timeline::envelope(pc.points, pc.envelope, pc.unscored, snap_->scale, view_, columns);
        e.t0      = view_.t0;
        e.t1      = view_.t1;
        e.columns = columns;
    }
    return e;
}

bool TimelinePanel::lane_shows(const Event& e) const
{
    // A lane shows the events of the shown curves' inputs: the reference's,
    // those of no variant, a shown curve's variant's, the variant on
    // screen's, and a shown session's own.
    const timeline::Timeline& t = snap_->timeline;
    if (e.session >= 0) {
        for (const PanelLine& l : lines_) {
            const timeline::PanelCurve& pc = snap_->curves[l.curve];
            if (pc.ref.kind == CurveRef::Kind::Session && pc.ref.index == e.session) return l.shown;
        }
        return false;
    }
    if (e.variant < 0 || static_cast<std::size_t>(e.variant) >= t.variants.size()) return true;
    if (t.variants[static_cast<std::size_t>(e.variant)].pair < 0) return true;
    if (e.variant == play_variant_) return true;
    for (const PanelLine& l : lines_)
        if (l.shown && l.variant == e.variant) return true;
    return false;
}

void TimelinePanel::build_lanes(float width)
{
    if (!lanes_dirty_ && lanes_t0_ == view_.t0 && lanes_t1_ == view_.t1 && lanes_w_ == width) return;
    lanes_dirty_ = false;
    lanes_t0_    = view_.t0;
    lanes_t1_    = view_.t1;
    lanes_w_     = width;
    for (Lane& l : lanes_) l = {};
    const timeline::Timeline&  t = snap_->timeline;
    const timeline::AxisScale& s = snap_->scale;
    // The events meeting the frames in view, by the overlap index (D-078).
    const std::int64_t f0 = s.frame_ending_after(view_.t0);
    const std::int64_t f1 = std::max(f0 + 1, s.frame_after(view_.t1));
    std::array<std::vector<std::pair<float, std::size_t>>, timeline::kSourceCount> points;
    std::array<std::map<std::pair<long, long>, std::size_t>, timeline::kSourceCount> bar_at;
    for (const std::size_t k : t.overlapping(f0, f1)) {
        const Event& e = t.events[k];
        if (!lane_shows(e)) continue;
        const std::size_t src = static_cast<std::size_t>(e.source);
        const float xa = timeline::x_at(view_, s.position(e.ref_index), 0.0f, width);
        if (!e.ref_end) {
            points[src].emplace_back(xa, k);
            continue;
        }
        // One bar per span of px: the rungs' copies of a segment share one.
        const float xb = timeline::x_at(view_, s.position(*e.ref_end), 0.0f, width);
        const std::pair<long, long> key{ std::lround(xa * 4.0f), std::lround(xb * 4.0f) };
        auto [it, fresh] = bar_at[src].try_emplace(key, lanes_[src].bars.size());
        if (fresh) lanes_[src].bars.push_back({ .x0 = xa, .x1 = xb, .events = {} });
        lanes_[src].bars[it->second].events.push_back(k);
    }
    for (std::size_t src = 0; src < timeline::kSourceCount; ++src) {
        std::stable_sort(points[src].begin(), points[src].end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        Lane& lane = lanes_[src];
        for (const auto& [x, k] : points[src]) {
            lane.xs.push_back(x);
            lane.points.push_back(k);
        }
        lane.clusters = timeline::cluster_markers(lane.xs, kMarkerSpacing);
    }
}

std::string TimelinePanel::variant_text(int variant) const
{
    if (!snap_) return "no variant";
    const timeline::Timeline& t = snap_->timeline;
    if (variant < 0 || static_cast<std::size_t>(variant) >= t.variants.size()) return "no variant of the document";
    const timeline::Variant& v = t.variants[static_cast<std::size_t>(variant)];
    std::string role = v.pair < 0
        ? std::string(snap_->scores.reference_mode == "top-rung" ? "the reference, not scored in top-rung mode"
                                                                 : "the reference")
        : std::format("pair {}", v.pair);
    return std::format("{} ({}; variant {}, {})", variant_name(t, variant), v.url, variant, role);
}

// Why there is no playhead where its sequence isn't known (D-124 (c)): the
// line the panel draws in its place and logs, after "No playhead: ".
std::string TimelinePanel::unknown_line() const
{
    return std::format("the frame's PTS, {:.3f} s, lies in sequences {}: the panel doesn't know which until a "
                       "seek, a switch or a jump of the PTS tells it (D-124 (c))",
                       playhead_pts_, sequences_text(playhead_unknown_));
}

// ── The card ─────────────────────────────────────────────────────────────────

// The card's lines: the frame picked and its attribution. The card doesn't
// take the playhead, its frame being the pick's, so that it reads alike
// where the playhead isn't known (D-124 (c)); only its seek line's note
// says so where the player made no seek (settle_seek()).
std::vector<std::string> TimelinePanel::card_lines(const Pick& p) const
{
    std::vector<std::string> out;
    const timeline::Timeline&  t = snap_->timeline;
    const timeline::Axis&      axis = t.axis;
    const timeline::PanelCurve& pc = snap_->curves[lines_[p.line].curve];
    // The frame picked.
    std::string key;
    if (p.ref_index >= 0 && static_cast<std::size_t>(p.ref_index) < axis.keys.size()) {
        const timeline::MediaKey& k = axis.keys[static_cast<std::size_t>(p.ref_index)];
        key = std::format(", sequence {}", k.sequence);
        if (k.pts && axis.time_base.num > 0)
            key += std::format(", PTS {:.3f} s", static_cast<double>(*k.pts) * av_q2d(axis.time_base));
    }
    out.push_back(std::format("Curve {} ({}): reference frame {} at {}{}; VMAF {:.3f}", lines_[p.line].name,
                              pc.ref.kind == CurveRef::Kind::Pair ? variant_name(t, lines_[p.line].variant)
                                                                  : std::string("a session"),
                              p.ref_index, position_text(snap_->scale, snap_->scale.position(p.ref_index)), key,
                              p.vmaf));
    const timeline::Attribution& a = p.answer.attribution;
    if (p.answer.drop) {
        out.push_back(std::format("In the document's attribution {}, a detected drop:", *p.answer.drop));
    } else {
        // The clicked frame's segment of the curve's variant, or the frame
        // alone (D-114 (d)).
        const bool frame = a.interval.ref_end - a.interval.ref_index == 1;
        out.push_back(frame ? std::string("Outside the document's drops: the query of the clicked frame alone, "
                                          "its variant having no segment there (D-114 (d)):")
                            : std::string("Outside the document's drops: the query of the clicked frame's segment "
                                          "(D-114 (d)):"));
    }
    // The head of answer_block()'s block, its seconds as Axis::seconds()
    // gives them, as the CLI writes them on any axis.
    std::string head = std::format("{}, {:.3f}-{:.3f} s; {} point{}", frames_text(a.interval),
                                   axis.seconds(a.interval.ref_index), axis.seconds(a.interval.ref_end), a.points,
                                   a.points == 1 ? "" : "s");
    if (a.detected) head += std::format(", baseline {}", score_text(a.baseline));
    if (a.points > 0) head += std::format(", min {}, mean {}", score_text(a.min), score_text(a.mean));
    if (a.detected)
        head += std::format(", drop_magnitude {}, mean_drop {}", score_text(a.drop_magnitude), score_text(a.mean_drop));
    if (a.searched.ref_index != a.interval.ref_index || a.searched.ref_end != a.interval.ref_end)
        head += std::format("; events looked for in {} with the margins", frames_text(a.searched));
    out.push_back(head);
    bool any = false;
    for (std::size_t s = 0; s < timeline::kSourceCount; ++s)
        for (const timeline::Found& f : a.by_source[s]) {
            if (f.event >= t.events.size()) continue;
            any = true;
            out.push_back("  " + found_line(t, static_cast<Source>(s), f));
        }
    if (!any) out.push_back("  no event");
    return out;
}

std::string TimelinePanel::seek_line(const Pick& p) const
{
    const timeline::SeekDecision& d = p.decision;
    // The variant it was decided on: the one on screen, unless another came
    // on screen since its seek was made.
    const std::string screen = p.variant == play_variant_ ? std::string("the variant on screen")
                                                          : std::string("on screen when it was decided");
    switch (d.kind) {
    case timeline::SeekDecision::Kind::Seek: {
        std::string state;
        switch (p.sent) {
        case Sent::No:
            state = p.redecided ? ": not sent, decided again when this variant came on screen: click the frame "
                                  "to seek it"
                                : ": not sent";
            break;
        case Sent::Asked:   state = ": sent, not made yet"; break;
        case Sent::Made:    state = ": sent and made"; break;
        case Sent::Refused: state = ": " + p.note; break;
        }
        return std::format("Seek: to PTS {:.3f} s, the frame at reference frame {} on {}, {}{}", d.target,
                           p.ref_index, variant_text(p.variant), screen, state);
    }
    case timeline::SeekDecision::Kind::Offer: {
        std::string seg = "a segment's start";
        if (d.segment && p.variant >= 0 && static_cast<std::size_t>(p.variant) < clocks_.size() &&
            clocks_[static_cast<std::size_t>(p.variant)]) {
            const timeline::VariantClock& c = *clocks_[static_cast<std::size_t>(p.variant)];
            if (*d.segment < c.segments.size())
                seg = std::format("the start of segment {}", c.segments[*d.segment].media_sequence);
        }
        std::string state;
        switch (p.sent) {
        case Sent::No:      state = p.redecided ? "not taken, offered again for this variant" : "not taken"; break;
        case Sent::Asked:   state = "taken, not made yet"; break;
        case Sent::Made:    state = "taken and made"; break;
        case Sent::Refused: state = "taken, " + p.note; break;
        }
        return std::format("Seek: the frame isn't reachable by a seek: {}. Offered: PTS {:.3f} s, {} of {}, {}, "
                           "from which playing reaches the frame (D-114 (b)); {}",
                           d.why, d.target, seg, variant_text(p.variant), screen, state);
    }
    case timeline::SeekDecision::Kind::None: break;
    }
    return std::format("Seek: none: {}", d.why.empty() ? std::string("no decision") : d.why);
}

void TimelinePanel::draw_card(const Inputs& in, Out& out, float w, float h)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, th::vec4(th::rgba(255, 255, 255, 0.035f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, th::kRadiusSmall);
    // NoNav: a child doesn't inherit its parent's (src/NetInspector.cpp).
    if (ImGui::BeginChild("##tl_card", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoNav)) {
        ImGui::PushTextWrapPos(0.0f);
        if (!pick_) {
            ImGui::TextColored(th::vec4(th::kTextDim), "ATTRIBUTION");
            ImGui::TextWrapped("Click a curve: the lowest frame near the cursor, what the timeline attributes it to, "
                               "and a seek there where one reaches it.");
            if (!snap_->attributions.empty()) {
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                ImGui::TextColored(th::vec4(th::kTextDim), "THE DOCUMENT'S ATTRIBUTIONS");
            }
            for (std::size_t n = 0; n < snap_->attributions.size(); ++n) {
                const timeline::Attribution& a = snap_->attributions[n];
                std::string curve = "?";
                for (const PanelLine& l : lines_)
                    if (same_curve(snap_->curves[l.curve].ref, a.curve)) curve = l.name;
                const std::string row = std::format("{} · {} · {} · min {}##tl_drop{}", n, curve,
                                                    frames_text(a.interval), score_text(a.min), n);
                if (ImGui::Selectable(row.c_str()))
                    pick_attribution(n, std::format("a click on the card's attribution {}", n), in, out);
            }
        } else {
            Pick& p = *pick_;
            ImGui::TextColored(th::vec4(th::kTextDim), "ATTRIBUTION");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 46.0f);
            if (ImGui::SmallButton("Clear##tl_clear")) {
                pick_.reset();
            } else {
                const std::vector<std::string> lines = card_lines(p);
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    if (i < 3) ImGui::TextUnformatted(lines[i].c_str());
                    else       ImGui::TextColored(th::vec4(th::kTextDim), "%s", lines[i].c_str());
                }
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                const std::string seek = seek_line(p);
                const ImU32 col = p.decision.kind == timeline::SeekDecision::Kind::Seek ? th::kText : th::kWarn;
                ImGui::TextColored(th::vec4(col), "%s", seek.c_str());
                // The button stays until the offer's seek is made: one not
                // made can be asked for again.
                if (p.decision.kind == timeline::SeekDecision::Kind::Offer &&
                    (p.sent == Sent::No || p.sent == Sent::Refused) &&
                    ImGui::Button("Seek to the offered segment start##tl_offer")) {
                    // One seek per click: the user takes the offer (D-114 (b)).
                    const timeline::VariantClock& c = clock_of(p.variant);
                    int sequence = -1;
                    if (p.decision.segment && *p.decision.segment < c.segments.size()) {
                        const std::size_t first = c.segments[*p.decision.segment].first;
                        if (first < c.frames.size()) sequence = c.frames[first].sequence;
                    }
                    logger::info("timeline: the offered segment start taken: seek to PTS {:.3f}", p.decision.target);
                    ask_seek(p, sequence, in, out);
                }
            }
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── Draw ─────────────────────────────────────────────────────────────────────

TimelinePanel::Out TimelinePanel::draw(const Inputs& in)
{
    Out out;
    // Not drawn last frame: closed meanwhile, and the player's counters moved
    // on without the panel; a click that was waiting out the double-click
    // time when it closed picks nothing.
    const int  frame      = ImGui::GetFrameCount();
    const bool drawn_last = frame == drawn_frame_ + 1;
    if (!drawn_last) {
        seen_any_ = false;
        pending_click_.reset();
    }
    drawn_frame_ = frame;

    if (load_pending_) {
        load_pending_ = false;
        loading_      = true;
        load_error_.clear();
        loader_.start({ .document = document_ });   // off the main thread (design §10)
        logger::info("timeline: loading {}", document_);
    }
    take_load();
    ensure_match(in);
    settle_seek(in, drawn_last);
    follow_playhead(in);

    const ImGuiIO& io = ImGui::GetIO();
    const float w  = std::max(1.0f, std::round(in.win_w - in.right_inset));
    const float h  = height(in.win_h);
    const float y0 = std::round(in.win_h - in.bottom - h);
    ImGui::SetNextWindowPos(ImVec2(0.0f, y0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, th::vec4(netui::kSideBg));
    // NoNav and the focus drop below keep the player's keys alive after a
    // click (eval/phase3-survey/ui.md §4.1; src/NetInspector.cpp).
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##timeline", nullptr, kFlags);
    ImGui::PushFont(ui_fonts().ui_small);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    out.hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    // ── Top edge: drag to resize, double-click for the automatic height ──
    {
        ImGui::SetCursorScreenPos(ImVec2(0.0f, y0));
        ImGui::InvisibleButton("##tl_resize", ImVec2(w, kEdgeH));
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        if (hov || act) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (act && io.MouseDelta.y != 0.0f) height_ = std::clamp(h - io.MouseDelta.y, kMinH, max_height(in.win_h));
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) height_ = -1.0f;
        if (hov && !act) ui::tooltip("Drag to resize · double-click: automatic height");
        dl->AddLine(ImVec2(0.0f, y0 + 0.5f), ImVec2(w, y0 + 0.5f),
                    C(act ? th::kAccent : hov ? th::alpha(th::kAccent, 0.6f) : th::kPanelBorder), act || hov ? 2.0f : 1.0f);
    }

    // ── Header: title, document, match; the curves' chips; Fit, Open…, × ──
    const float hy = y0 + kEdgeH;
    const timeline::StreamMatch::By by = match_ ? match_->by : timeline::StreamMatch::By::None;
    const bool unmatched = snap_ && match_ && by == timeline::StreamMatch::By::None;
    {
        ImFont* f  = ui_fonts().ui;
        ImFont* fs = ui_fonts().ui_small;
        float hx = kPad + 2.0f;
        dl->AddText(f, 14.0f, ImVec2(hx, hy + 6.0f), C(th::kText), "Timeline");
        hx += f->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, "Timeline").x + 10.0f;
        int texts = 0;
        auto text = [&](const std::string& s, ImU32 col, const std::string& tip) {
            const ImVec2 ts = fs->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, s.c_str());
            dl->AddText(fs, 12.0f, ImVec2(hx, hy + 8.0f), C(col), s.c_str());
            if (!tip.empty()) {
                ImGui::SetCursorScreenPos(ImVec2(hx, hy + 6.0f));
                ImGui::InvisibleButton(std::format("##tl_tip{}", texts).c_str(), ImVec2(std::max(1.0f, ts.x), 18.0f));
                if (ImGui::IsItemHovered()) ui::tooltip(tip.c_str());
            }
            ++texts;
            hx += ts.x + 10.0f;
        };
        const std::string doc = document_.empty() ? std::string("no document")
                                                  : std::filesystem::path(document_).filename().string();
        text(doc, th::kTextDim, document_.empty() ? std::string() : document_);
        if (snap_ && match_) {
            std::string tip;
            for (const std::string& wn : match_->warnings) tip += (tip.empty() ? "" : "\n") + wn;
            const std::string m = by == timeline::StreamMatch::By::None
                                      ? std::string("another stream's")
                                      : std::format("matched: {}", timeline::match_name(by));
            const ImU32 col = by == timeline::StreamMatch::By::None ? th::kError
                            : match_->warnings.empty() ? netui::kColManifest : th::kWarn;
            text(m, col, tip.empty() ? snap_->report_check : tip + "\n" + snap_->report_check);
            text(snap_->scores.reference_mode, th::kTextDim, std::string());
        }

        // Right: ×, Open…, Fit.
        float rx = w - kPad - 24.0f;
        ImGui::SetCursorScreenPos(ImVec2(rx, hy + 3.0f));
        // E toggles the panel (main.cpp's s_show_timeline), L staying the
        // loudness meters' (D-083), as built, which the owner kept
        // (D-124 (b), Q92 (a)).
        if (netui::glyph_button("##tl_close", "Close (E)", 24.0f)) out.close = true;
        netui::draw_x(dl, ImVec2(rx + 12.0f, hy + 15.0f), 4.5f, C(ImGui::IsItemHovered() ? th::kText : th::kTextDim));
        auto chip_w = [&](const char* label) {
            return std::round(fs->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, label).x + 20.0f);
        };
        rx -= chip_w("Open…") + 6.0f;
        ImGui::SetCursorScreenPos(ImVec2(rx, hy + 4.0f));
        if (netui::chip_button("Open…##tl_open", "Open…", false)) out.open_document = true;
        if (ImGui::IsItemHovered()) ui::tooltip("Load another timeline document; kept in prefs.txt (D-114 (d))");
        if (snap_ && !unmatched) {
            rx -= chip_w("Fit") + 6.0f;
            ImGui::SetCursorScreenPos(ImVec2(rx, hy + 4.0f));
            if (netui::chip_button("Fit##tl_fit", "Fit", view_.fit)) timeline::fit_view(view_, snap_->scale);
            if (ImGui::IsItemHovered()) ui::tooltip("Fit the whole axis (or double-click the plot)");
        }

        // The curves' chips between the texts and the buttons; the variant
        // on screen marked; in top-rung mode the reference, not scored.
        if (snap_ && !unmatched) {
            const timeline::Timeline& t = snap_->timeline;
            float cx = hx;
            std::size_t skipped = 0;
            for (std::size_t i = 0; i <= lines_.size(); ++i) {
                std::string label, tip, id;
                bool active = true;
                ImU32 dot = 0;
                bool  reference_chip = false;
                if (i == lines_.size()) {
                    // The reference, which has no curve in top-rung mode (ui.md §2.3).
                    int ref = -1;
                    for (std::size_t v = 0; v < t.variants.size(); ++v)
                        if (t.variants[v].pair < 0 && !t.variants[v].entries.empty()) ref = static_cast<int>(v);
                    if (ref < 0) break;
                    label = (ref == play_variant_ ? "▶ " : "") + variant_name(t, ref) + " · reference (not scored)";
                    tip   = variant_text(ref);
                    id    = "##tl_chip_ref";
                    active = false;
                    reference_chip = true;
                } else {
                    const PanelLine& l = lines_[i];
                    label  = (l.variant >= 0 && l.variant == play_variant_ ? "▶ " : "") + l.label;
                    tip    = std::format("{}: {}\nclick: show or hide", l.name,
                                         l.variant >= 0 ? variant_text(l.variant) : l.label);
                    id     = std::format("##tl_chip{}", i);
                    active = l.shown;
                    dot    = curve_col(i);
                }
                const float cw = chip_w(label.c_str()) + (dot ? 12.0f : 0.0f);
                if (cx + cw > rx - 6.0f) {
                    ++skipped;
                    continue;
                }
                ImGui::SetCursorScreenPos(ImVec2(cx, hy + 4.0f));
                if (netui::chip_button((label + id).c_str(), label.c_str(), active, dot) && !reference_chip) {
                    lines_[i].shown = !lines_[i].shown;
                    lanes_dirty_    = true;
                }
                if (ImGui::IsItemHovered()) ui::tooltip(tip.c_str());
                cx += cw + 4.0f;
            }
            if (skipped > 0) {
                const std::string more = std::format("+{}", skipped);
                dl->AddText(fs, 12.0f, ImVec2(cx, hy + 8.0f), C(th::kTextDim), more.c_str());
            }
        }
    }

    // ── Body ─────────────────────────────────────────────────────────────────
    const float by0 = hy + kHeaderH;
    const float by1 = y0 + h - kPad;
    ImGui::SetCursorScreenPos(ImVec2(kPad + 2.0f, by0 + 4.0f));
    ImGui::PushTextWrapPos(w - kPad);
    if (!snap_) {
        if (document_.empty())
            ImGui::TextColored(th::vec4(th::kTextDim), "No timeline document. Give one with --timeline <document>, "
                                                       "or Open… one: it is kept as prefs.txt's timeline= line.");
        else if (loading_ || load_pending_)
            ImGui::TextColored(th::vec4(th::kTextDim), "Loading %s…", document_.c_str());
        else if (!load_error_.empty())
            ImGui::TextColored(th::vec4(th::kError), "Not loaded: %s", load_error_.c_str());
    } else if (unmatched && !override_) {
        ImGui::TextColored(th::vec4(th::kError), "This document is another stream's: its curves, drops and events "
                                                 "aren't the open stream's.");
        if (!match_->warnings.empty()) ImGui::TextColored(th::vec4(th::kTextDim), "%s", match_->warnings.front().c_str());
        if (ImGui::Button("Show it anyway##tl_override")) override_ = true;
    }
    ImGui::PopTextWrapPos();

    if (snap_ && (!unmatched || override_)) {
        const timeline::AxisScale& scale = snap_->scale;
        const timeline::Timeline&  t     = snap_->timeline;
        const float card_w = std::round(std::clamp(w * 0.32f, 240.0f, 380.0f));
        const float card_x = w - kPad - card_w;
        const float px0 = kPad + kLabelW;
        const float px1 = std::max(px0 + 40.0f, card_x - kPad);
        const float pw  = px1 - px0;
        const float ay0 = by0, ay1 = by0 + kAxisH;
        const float ly1 = by1, ly0 = ly1 - kLaneH * static_cast<float>(timeline::kSourceCount);
        const float cy0 = ay1 + 2.0f, cy1 = std::max(cy0 + 20.0f, ly0 - 4.0f);
        curves_y0_ = cy0;
        curves_y1_ = cy1;
        auto Y = [&](double v) { return cy1 - static_cast<float>(std::clamp(v, 0.0, 100.0) / 100.0) * (cy1 - cy0); };

        if (qa_view_due_) {
            qa_view_due_ = false;
            if (qa_zoom_ && qa_zoom_->second > qa_zoom_->first) {
                view_ = { .t0 = qa_zoom_->first, .t1 = qa_zoom_->second, .fit = false };
            } else if (qa_fit_) {
                timeline::fit_view(view_, scale);
            }
        }
        if (view_.fit) timeline::fit_view(view_, scale);   // the extent, until zoomed or panned

        // Interaction, as the Alignment tab's: the wheel zooms at the cursor,
        // a drag pans, a double-click fits; a click picks once the
        // double-click time has passed without a second click, on the view
        // it was made on: a zoom, a pan or a resize meanwhile drops it.
        ImGui::SetCursorScreenPos(ImVec2(px0, ay0));
        ImGui::InvisibleButton("##tl_plot", ImVec2(pw, std::max(1.0f, by1 - ay0)));
        const bool hov = ImGui::IsItemHovered();
        if (hov && io.MouseWheel != 0.0f)
            timeline::zoom_at(view_, scale, timeline::position_at(view_, io.MousePos.x, px0, pw), io.MouseWheel);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
            timeline::pan_px(view_, io.MouseDelta.x, pw);
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            timeline::fit_view(view_, scale);
            pending_click_.reset();
        } else if (hov && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && io.MouseClickedLastCount[0] == 1 &&
                   timeline::is_click(io.MouseDragMaxDistanceSqr[0]) && io.MousePos.y >= cy0 && io.MousePos.y <= cy1) {
            pending_click_ = PendingClick{ .x = io.MousePos.x, .y = io.MousePos.y, .at = ImGui::GetTime(),
                                           .t0 = view_.t0, .t1 = view_.t1, .px0 = px0, .pw = pw };
        }
        if (pending_click_ && (pending_click_->t0 != view_.t0 || pending_click_->t1 != view_.t1 ||
                               pending_click_->px0 != px0 || pending_click_->pw != pw)) {
            pending_click_.reset();
            logger::info("timeline: a click dropped: the view changed before the double-click time passed");
        }
        if (pending_click_ && ImGui::GetTime() - pending_click_->at >= io.MouseDoubleClickTime) {
            const PendingClick c = *pending_click_;
            pending_click_.reset();
            if (!pick_at_x(c.x, c.y, px0, pw, "a click", in, out))
                logger::info("timeline: a click at x {:.0f}: no point of a shown curve there", c.x - px0);
        }
        run_qa_picks(in, px0, pw, out);

        // The scores' scale and the lanes' names, left of the plot.
        ImFont* fs = ui_fonts().ui_small;
        for (const int v : { 100, 50, 0 }) {
            const std::string s = std::to_string(v);
            const float tw = fs->CalcTextSizeA(10.0f, FLT_MAX, 0.0f, s.c_str()).x;
            dl->AddText(fs, 10.0f, ImVec2(px0 - 6.0f - tw, Y(v) - 6.0f), C(th::kTextFaint), s.c_str());
        }
        dl->AddText(fs, 10.0f, ImVec2(kPad + 2.0f, cy0), C(th::kTextFaint), "VMAF");
        for (std::size_t s = 0; s < timeline::kSourceCount; ++s) {
            const float y = ly0 + kLaneH * static_cast<float>(s);
            dl->AddText(fs, 10.0f, ImVec2(kPad + 2.0f, y), C(kSourceCols[s], 0.85f),
                        std::string(timeline::source_name(static_cast<Source>(s))).c_str());
        }

        dl->PushClipRect(ImVec2(px0, ay0), ImVec2(px1, by1), true);
        dl->AddRectFilled(ImVec2(px0, cy0), ImVec2(px1, cy1), C(th::rgba(255, 255, 255, 0.03f)), 2.0f);
        for (std::size_t s = 0; s < timeline::kSourceCount; ++s)
            if (s % 2 == 0) {
                const float y = ly0 + kLaneH * static_cast<float>(s);
                dl->AddRectFilled(ImVec2(px0, y), ImVec2(px1, y + kLaneH), C(th::rgba(255, 255, 255, 0.025f)));
            }
        for (const int v : { 20, 40, 60, 80 })
            dl->AddLine(ImVec2(px0, Y(v)), ImVec2(px1, Y(v)), C(th::rgba(255, 255, 255, 0.05f)));

        // The axis: seconds as the Alignment tab ticks them (P-AJ), or frame
        // numbers on an axis without seconds (Q90 (a)).
        const double span = view_.span();
        const double step = scale.seconds ? timeline::nice_step(span, pw, kTickPx) : frame_step(span, pw, kTickPx);
        for (double u = std::ceil(view_.t0 / step) * step; u <= view_.t1; u += step) {
            const float x = timeline::x_at(view_, u, px0, pw);
            dl->AddLine(ImVec2(x, ay1 - 4.0f), ImVec2(x, by1), C(th::rgba(255, 255, 255, 0.05f)));
            const std::string lab = scale.seconds ? timeline::axis_label(u, step) : std::format("{:.0f}", u);
            dl->AddText(fs, 11.0f, ImVec2(x + 3.0f, ay0 + 1.0f), C(th::kTextFaint), lab.c_str());
        }

        // The document's detected drops, shaded in their curve's colour.
        for (const timeline::Attribution& a : snap_->attributions) {
            if (!a.detected) continue;
            for (std::size_t l = 0; l < lines_.size(); ++l) {
                if (!lines_[l].shown || !same_curve(snap_->curves[lines_[l].curve].ref, a.curve)) continue;
                const float xa = timeline::x_at(view_, scale.position(a.interval.ref_index), px0, pw);
                const float xb = timeline::x_at(view_, scale.position(a.interval.ref_end), px0, pw);
                dl->AddRectFilled(ImVec2(xa, cy0), ImVec2(std::max(xb, xa + 1.0f), cy1), C(th::alpha(curve_col(l), 0.12f)));
            }
        }

        // The curves. Where the view holds more points than framebuffer
        // columns, per column the min/max envelope and a line through the
        // minima, so that a one-frame dip stays visible (ui.md §1.8, F4;
        // §4.3); where it holds fewer, each point as a step over its span,
        // from its frame to the next point's (D-081 (g); src/TimelineView.h).
        // Either way at most a point or two a column. A session's span that
        // isn't scored is outside the data (D-124 (a), Q91 (b)): its columns
        // hold no score, the point before it steps up to its start only
        // (point_end()), and the line ends there, so that no curve is drawn
        // over it; a pair's curve has no such span. Where each line was
        // drawn is kept for --timeline-report (drawn_).
        const int   columns = std::max(1, static_cast<int>(std::lround(pw * std::max(in.scale, 1.0f))));
        const float colw    = pw / static_cast<float>(columns);
        const double cols_d = static_cast<double>(columns);
        const std::int64_t vf0 = scale.frame_ending_after(view_.t0);
        const std::int64_t vf1 = std::max(vf0 + 1, scale.frame_after(view_.t1));
        std::vector<ImVec2> poly;
        if (drawn_.size() != lines_.size()) drawn_.assign(lines_.size(), {});
        for (std::size_t l = 0; l < lines_.size(); ++l) {
            std::vector<std::pair<double, double>>& drawn = drawn_[l];
            drawn.clear();
            if (!lines_[l].shown) continue;
            const ImU32 col = curve_col(l);
            poly.clear();
            // The run of positions the line being drawn covers, kept within
            // the view when the line ends.
            std::optional<std::pair<double, double>> run;
            auto cover = [&](double a, double b) {
                if (!run) run = std::pair{ a, b };
                else      run->second = b;
            };
            auto flush = [&] {
                for (std::size_t i = 0; i + 1 < poly.size(); i += kPolyChunk - 1) {
                    const int n = static_cast<int>(std::min<std::size_t>(kPolyChunk, poly.size() - i));
                    dl->AddPolyline(poly.data() + i, n, C(col), ImDrawFlags_None, 1.5f);
                }
                poly.clear();
                if (run && std::min(run->second, view_.t1) > std::max(run->first, view_.t0))
                    drawn.emplace_back(std::max(run->first, view_.t0), std::min(run->second, view_.t1));
                run.reset();
            };
            const timeline::PanelCurve& pc  = snap_->curves[lines_[l].curve];
            const timeline::Curve&      cv  = pc.points;
            const timeline::PointRange  vis = timeline::points_meeting(cv, pc.unscored, vf0, vf1);
            if (vis.last - vis.first <= static_cast<std::size_t>(columns)) {
                for (std::size_t i = vis.first; i < vis.last; ++i) {
                    if (!std::isfinite(cv.vmaf[i])) {   // null in the report (src/LadderReportRead.h)
                        flush();
                        continue;
                    }
                    const std::int64_t end = timeline::point_end(cv, pc.unscored, i);
                    const float y = Y(cv.vmaf[i]);
                    poly.emplace_back(timeline::x_at(view_, scale.position(cv.ref_index[i]), px0, pw), y);
                    poly.emplace_back(timeline::x_at(view_, scale.position(end), px0, pw), y);
                    cover(scale.position(cv.ref_index[i]), scale.position(end));
                    // A span outside the data after the point: the line ends
                    // at its start, not across it to the next point.
                    if (i + 1 < cv.ref_index.size() && end < cv.ref_index[i + 1]) flush();
                }
                flush();
                continue;
            }
            const Envelope& env = envelope_of(l, columns);
            for (std::size_t c = 0; c < env.mm.size(); ++c) {
                const timeline::MinMax& m = env.mm[c];
                if (!m.any()) {
                    flush();
                    continue;
                }
                const float x = px0 + colw * static_cast<float>(c);
                dl->AddRectFilled(ImVec2(x, Y(m.max)), ImVec2(x + colw, Y(m.min) + colw), C(th::alpha(col, 0.35f)));
                poly.emplace_back(x + 0.5f * colw, Y(m.min));
                cover(view_.t0 + view_.span() * static_cast<double>(c) / cols_d,
                      view_.t0 + view_.span() * static_cast<double>(c + 1) / cols_d);
            }
            flush();
        }
        drawn_valid_ = true;
        // The pick.
        if (pick_ && pick_->line < lines_.size() && lines_[pick_->line].shown) {
            const float x = timeline::x_at(view_, scale.position(pick_->ref_index), px0, pw);
            for (float yy = cy0; yy < by1; yy += 6.0f)
                dl->AddLine(ImVec2(x, yy), ImVec2(x, std::min(yy + 3.0f, by1)), C(th::alpha(th::kText, 0.6f)));
            dl->AddCircle(ImVec2(x, Y(pick_->vmaf)), 4.5f, C(th::kText), 12, 1.5f);
        }

        // The lanes: a bar per span, a tick per point, markers closer than
        // kMarkerSpacing as one with their count (ui.md §4.2).
        build_lanes(pw);
        for (std::size_t s = 0; s < timeline::kSourceCount; ++s) {
            const Lane&  lane = lanes_[s];
            const float  y = ly0 + kLaneH * static_cast<float>(s);
            const ImU32  col = kSourceCols[s];
            for (const Lane::Bar& b : lane.bars) {
                dl->AddRectFilled(ImVec2(px0 + b.x0, y + 2.0f), ImVec2(px0 + std::max(b.x1, b.x0 + 1.0f), y + kLaneH - 2.0f),
                                  C(th::alpha(col, 0.22f)));
                dl->AddLine(ImVec2(px0 + b.x0, y + 1.0f), ImVec2(px0 + b.x0, y + kLaneH - 1.0f), C(th::alpha(col, 0.6f)));
            }
            for (const timeline::MarkerCluster& c : lane.clusters) {
                const float x = px0 + c.x0;
                dl->AddQuadFilled(ImVec2(x, y + 1.5f), ImVec2(x + 3.5f, y + kLaneH * 0.5f), ImVec2(x, y + kLaneH - 1.5f),
                                  ImVec2(x - 3.5f, y + kLaneH * 0.5f), C(col));
                if (c.count > 1) {
                    const std::string n = std::to_string(c.count);
                    dl->AddText(fs, 9.0f, ImVec2(x + 4.5f, y), C(th::alpha(col, 0.9f)), n.c_str());
                }
            }
        }

        // The playhead: the frame on screen on the reference's axis. Where
        // its sequence isn't known and the frame's PTS lies in more than one
        // (D-124 (c), Q93 (c)), none, and in its place a line of the panel's
        // own that says why, at the foot of the curves, where the scores are
        // lowest, over a plate of the panel's background.
        if (playhead_) {
            const float x = timeline::x_at(view_, scale.position(playhead_->ref_index), px0, pw);
            dl->AddLine(ImVec2(x, ay1 - 3.0f), ImVec2(x, by1), C(th::kText), 1.5f);
            dl->AddTriangleFilled(ImVec2(x - 4.0f, ay1 - 7.0f), ImVec2(x + 4.0f, ay1 - 7.0f), ImVec2(x, ay1 - 2.0f), C(th::kText));
        } else if (!playhead_unknown_.empty()) {
            const std::string why  = "No playhead: " + unknown_line();
            const float       wrap = std::max(40.0f, pw - 16.0f);
            const ImVec2      ts   = fs->CalcTextSizeA(11.0f, FLT_MAX, wrap, why.c_str());
            const ImVec2      at(px0 + 6.0f, std::max(cy0 + 2.0f, cy1 - ts.y - 5.0f));
            dl->AddRectFilled(ImVec2(at.x - 4.0f, at.y - 2.0f), ImVec2(at.x + ts.x + 4.0f, at.y + ts.y + 2.0f),
                              C(netui::kSideBg, 0.92f), th::kRadiusSmall);
            dl->AddText(fs, 11.0f, at, C(th::kWarn), why.c_str(), nullptr, wrap);
        }
        dl->PopClipRect();

        // Hover: the frame under the cursor, the shown curves' scores there,
        // and in a lane the events near it.
        if (hov && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const double p = timeline::position_at(view_, io.MousePos.x, px0, pw);
            const std::int64_t f = scale.frame_ending_after(p);
            std::string tip;
            if (f >= 0 && f < scale.frames) {
                tip = scale.seconds ? std::format("{:.3f} s · reference frame {}", scale.position(f), f)
                                    : std::format("reference frame {}", f);
                if (static_cast<std::size_t>(f) < t.axis.keys.size()) {
                    const timeline::MediaKey& k = t.axis.keys[static_cast<std::size_t>(f)];
                    tip += std::format("\nsequence {}", k.sequence);
                    if (k.pts && t.axis.time_base.num > 0)
                        tip += std::format(", PTS {:.3f} s", static_cast<double>(*k.pts) * av_q2d(t.axis.time_base));
                }
                // Nothing of a session's curve inside its span that isn't
                // scored (D-124 (a)): no point meets its frames.
                for (std::size_t l = 0; l < lines_.size(); ++l) {
                    if (!lines_[l].shown) continue;
                    const timeline::PanelCurve& pc = snap_->curves[lines_[l].curve];
                    const timeline::Curve&      cv = pc.points;
                    const timeline::PointRange  r  = timeline::points_meeting(cv, pc.unscored, f, f + 1);
                    if (r.first < r.last && std::isfinite(cv.vmaf[r.first]))
                        tip += std::format("\n{} ({}): VMAF {:.3f}", lines_[l].name, lines_[l].label, cv.vmaf[r.first]);
                }
            }
            if (io.MousePos.y >= ly0 && io.MousePos.y < ly1) {
                const std::size_t s = std::min<std::size_t>(timeline::kSourceCount - 1,
                                                            static_cast<std::size_t>((io.MousePos.y - ly0) / kLaneH));
                const Lane& lane = lanes_[s];
                std::vector<std::size_t> events;
                if (const auto c = timeline::nearest_cluster(lane.clusters, io.MousePos.x - px0, kMarkerRadius))
                    for (std::size_t i = 0; i < lane.clusters[*c].count; ++i)
                        events.push_back(lane.points[lane.clusters[*c].first + i]);
                else
                    for (const Lane::Bar& b : lane.bars)
                        if (io.MousePos.x - px0 >= b.x0 && io.MousePos.x - px0 < std::max(b.x1, b.x0 + 1.0f))
                            events.insert(events.end(), b.events.begin(), b.events.end());
                for (std::size_t i = 0; i < events.size() && i < kTipEvents; ++i)
                    tip += "\n" + event_line(t, t.events[events[i]]);
                if (events.size() > kTipEvents) tip += std::format("\n… and {} more", events.size() - kTipEvents);
            }
            tip += "\nclick: attribution and seek · wheel: zoom · drag: pan · double-click: fit";
            ImGui::SetTooltip("%s", tip.c_str());
        }

        // The attribution card, right of the plot.
        ImGui::SetCursorScreenPos(ImVec2(card_x, by0));
        draw_card(in, out, card_w, std::max(20.0f, by1 - by0));
    }

    // Keep the player's shortcuts alive, as the network sidebar does: drop
    // focus as soon as no widget is held (src/NetInspector.cpp).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive())
        ImGui::SetWindowFocus(nullptr);

    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    return out;
}

// ── QA ───────────────────────────────────────────────────────────────────────

void TimelinePanel::qa_apply(const std::string& spec)
{
    bool in_variants = false, in_curves = false;
    std::size_t pos = 0;
    while (pos <= spec.size()) {
        const std::size_t comma = spec.find(',', pos);
        const std::string item  = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        const std::size_t eq    = item.find('=');
        if (eq == std::string::npos) {
            // A bare item continues variants='s list of entries, or curves='s
            // list of curves.
            if (in_variants && !item.empty()) qa_entries_->push_back(std::strtoul(item.c_str(), nullptr, 10));
            else if (in_curves && !item.empty()) qa_curves_->push_back(item);
            else if (!item.empty()) logger::warn("--ui-timeline: '{}' isn't a key=value item", item);
        } else {
            const std::string k = item.substr(0, eq), v = item.substr(eq + 1);
            in_variants = false;
            in_curves   = false;
            if (k == "fit") {
                qa_fit_ = v != "0";
            } else if (k == "zoom") {
                const std::size_t colon = v.find(':');
                if (colon != std::string::npos)
                    qa_zoom_ = std::pair{ std::strtod(v.c_str(), nullptr), std::strtod(v.c_str() + colon + 1, nullptr) };
            } else if (k == "variants") {
                if (v == "all") {
                    qa_entries_.reset();
                } else {
                    qa_entries_ = std::vector<std::size_t>{ std::strtoul(v.c_str(), nullptr, 10) };
                    in_variants = true;
                }
            } else if (k == "curves") {
                // Named curves alone, a session's among them, which
                // variants= can't show alone (WP13b's scripted QA,
                // eval/timeline/ui-qa-followups.sh).
                if (v == "all") {
                    qa_curves_.reset();
                } else {
                    qa_curves_ = std::vector<std::string>{ v };
                    in_curves  = true;
                }
            } else if (k == "pick") {
                QaPick q;
                q.text = item;
                const std::size_t at = v.find('@');
                const std::string what = v.substr(0, at);
                if (at != std::string::npos) q.at = std::strtod(v.c_str() + at + 1, nullptr);
                if (what.starts_with("attribution:")) {
                    q.attribution = true;
                    q.n = std::strtoul(what.c_str() + 12, nullptr, 10);
                } else {
                    q.position = std::strtod(what.c_str(), nullptr);
                }
                qa_picks_.push_back(std::move(q));
            } else {
                logger::warn("--ui-timeline: unknown key '{}'", k);
            }
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    qa_view_due_ = qa_fit_ || qa_zoom_.has_value();
    apply_qa_shown();
}

std::string TimelinePanel::report_text() const
{
    std::string r = "Timeline panel (--timeline-report)\n";
    r += std::format("document: {}\n", document_.empty() ? std::string("none") : document_);
    if (!snap_) {
        r += loading_ || load_pending_ ? "load: loading, or not started while the panel was closed\n"
           : !load_error_.empty()      ? "load: refused: " + load_error_ + "\n"
                                       : "load: none\n";
        return r;
    }
    const timeline::Snapshot& s = *snap_;
    const timeline::Timeline& t = s.timeline;
    r += std::format("load: loaded: {} events on {} reference frames, {} curves, {} attributions\n", t.events.size(),
                     t.axis.frames, s.curves.size(), s.attributions.size());
    r += std::format("ladder report: {}: {}\n", s.report_path, s.report_check);
    r += s.scale.seconds
             ? std::format("axis: {} reference frames, seconds on the reference's {}, checked by {}\n", t.axis.frames,
                           t.axis.clock == "playlist-time" ? "playlist time" : "frame grid", t.axis.checked)
             : std::format("axis: {} reference frames, no seconds: positions are frame numbers (Q90 (a))\n",
                           t.axis.frames);

    // How the document matched the open stream (ui.md §2.3).
    if (!match_) {
        r += "match: not made: no stream open while the panel was drawn\n";
    } else {
        static constexpr const char* kEntries[] = { "by their order", "by URL", "not matched" };
        r += std::format("match: {}, entries {}{}\n", timeline::match_name(match_->by),
                         kEntries[static_cast<int>(match_->entries)], override_ ? ", shown on the user's word" : "");
        for (const std::string& wn : match_->warnings) r += "  warning: " + wn + "\n";
        if (match_open_)
            for (std::size_t i = 0; i < match_->variant_of.size() && i < match_open_->variants.size(); ++i)
                r += std::format("  player entry {} ({}): {}\n", i, match_open_->variants[i].url,
                                 match_->variant_of[i] >= 0 ? variant_text(match_->variant_of[i])
                                                            : std::string("no variant of the document"));
        if (match_->by == timeline::StreamMatch::By::Variant)
            r += std::format("  opened alone: {}\n", variant_text(match_->opened_variant));
    }
    r += play_variant_ >= 0 ? std::format("variant on screen: player entry {}: {}\n", last_entry_, variant_text(play_variant_))
                            : std::format("variant on screen: none of the document's (player entry {})\n", last_entry_);
    if (playhead_) {
        // The PTS the player shows the frame at, against the report's PTS of
        // the frame it was matched to: equal where the two read the same
        // timestamps (the design's R16).
        const timeline::PlayheadFrame& p = *playhead_;
        const double off = p.distance == 0.0 ? 0.0 : -p.distance;
        r += std::format("playhead: reference frame {} at {}, the variant's frame {} (its input's frame {}), "
                         "sequence {}; the frame on screen at PTS {:.6f} s, {:+.6f} s from the report's PTS of "
                         "that frame\n",
                         p.ref_index, position_text(s.scale, s.scale.position(p.ref_index)), p.frame, p.index,
                         p.sequence, playhead_pts_, off);
    } else if (!playhead_unknown_.empty()) {
        // Its sequence not known (D-124 (c), Q93 (c)): from first sight, or
        // from a seek, a switch or a frame step whose landing the tracker
        // can't tell (timeline::track_playhead()).
        r += std::format("playhead: unknown: the frame on screen at PTS {:.6f} s lies in sequences {} of the "
                         "variant on screen, which nothing has told apart since the panel first saw the player's "
                         "frames, or since a seek, a switch or a step whose landing it can't tell: none drawn "
                         "until a seek, a switch or a jump of the PTS tells it which, or the PTS lies in one "
                         "sequence only (D-124 (c))\n",
                         playhead_pts_, sequences_text(playhead_unknown_));
    } else {
        r += "playhead: none\n";
    }
    r += std::format("view: {} to {}{}\n", position_text(s.scale, view_.t0), position_text(s.scale, view_.t1),
                     view_.fit ? ", fitted" : "");
    std::string shown;
    for (const PanelLine& l : lines_)
        if (l.shown) shown += std::format("{}{} ({})", shown.empty() ? "" : ", ", l.name, l.label);
    r += std::format("curves shown: {}\n", shown.empty() ? std::string("none") : shown);
    // A session's spans that aren't scored, over which no curve is drawn
    // and no point picked (D-124 (a), Q91 (b)); then where each shown curve
    // was drawn at the last draw, in runs of positions within the view.
    for (const PanelLine& l : lines_)
        for (const timeline::Interval& u : s.curves[l.curve].unscored)
            r += std::format("  {}: not scored over {}, {}: outside its data, no curve drawn and no point picked "
                             "there (D-124 (a))\n", l.name, frames_text(u),
                             range_text(s.scale, s.scale.position(u.ref_index), s.scale.position(u.ref_end)));
    if (!drawn_valid_) {
        r += "curves drawn: not drawn since the document loaded\n";
    } else {
        r += "curves drawn, at the last draw:\n";
        for (std::size_t l = 0; l < lines_.size(); ++l) {
            if (!lines_[l].shown) continue;
            std::string runs;
            if (l < drawn_.size())
                for (const auto& [a, b] : drawn_[l])
                    runs += (runs.empty() ? "" : ", ") + range_text(s.scale, a, b);
            r += std::format("  {}: {}\n", lines_[l].name, runs.empty() ? std::string("nowhere in the view") : runs);
        }
    }
    r += std::format("attributions: {}\n", s.attributions.size());
    for (std::size_t n = 0; n < s.attributions.size(); ++n) {
        const timeline::Attribution& a = s.attributions[n];
        std::string curve = "?";
        for (const PanelLine& l : lines_)
            if (same_curve(s.curves[l.curve].ref, a.curve)) curve = l.name;
        const std::string span = range_text(s.scale, s.scale.position(a.interval.ref_index),
                                            s.scale.position(a.interval.ref_end));
        r += std::format("  {}: {}{}, {}, {}, min {}\n", n, a.detected ? "a detected drop of " : "an interval of ",
                         curve, frames_text(a.interval), span, score_text(a.min));
    }
    if (!pick_) {
        r += "pick: none\n";
        for (const QaPick& q : qa_picks_)
            if (!q.done) r += std::format("  --ui-timeline {}: not made yet\n", q.text);
        return r;
    }
    const Pick& p = *pick_;
    r += std::format("pick: {}\n", p.how);
    r += "card:\n";
    for (const std::string& line : card_lines(p)) r += "  " + line + "\n";
    r += seek_line(p) + "\n";
    return r;
}
