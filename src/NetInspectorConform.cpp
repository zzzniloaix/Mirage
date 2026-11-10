// Network inspector — the Conformance tab: does the HLS master declare what
// its media holds? "Check" runs ConformProbe (HlsConformProbe.h) over the
// master playback opened: every variant and audio playlist, the size of
// every segment, a few segments of each read in full — inspector requests,
// listed in Network. hls::analyze_conformance (HlsConform.h) compares; this
// draws a row per EXT-X-STREAM-INF with a cell per check, and the findings
// (only a row's own once it is clicked). Its DASH mode does the same for an
// MPD given by URL or path, with DashProbe (DashConformProbe.h) and
// dash::analyze_conformance (DashConform.h), a row per Representation: the
// player cannot open MPDs itself (FFmpeg as built here has no DASH
// demuxer), so that check needs nothing playing. An HLS master can be given
// by URL too, for one the player cannot open (DRM): its playlists and
// segments are read all the same. Below the variants, the break markers
// (HlsMarkers.h): each splice point, where every rendition marks it, a seek
// to it; and Export HTML writes the report mirage_hlscheck --html does.

#include "NetInspector.h"

#include "HlsReportHtml.h"
#include "Logger.h"
#include "ManifestParser.h"
#include "NetInspectorUi.h"
#include "UiFormat.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

#include <spawn.h>

extern char** environ;

using namespace netui;

namespace {

// The table's columns after "#" and BANDWIDTH.
constexpr hls::Check kCols[] = {
    hls::Check::AverageBandwidth, hls::Check::Codecs, hls::Check::Resolution, hls::Check::FrameRate,
    hls::Check::VideoRange, hls::Check::SegmentStart, hls::Check::Duration,
};

// The DASH table's columns after "#", @bandwidth, needs and safe.
constexpr dash::Check kDashCols[] = {
    dash::Check::Codecs, dash::Check::Resolution, dash::Check::FrameRate, dash::Check::Audio,
    dash::Check::Colour, dash::Check::Sap,        dash::Check::Timeline,  dash::Check::Alignment,
};

const char* column_label(hls::Check c)
{
    switch (c) {
        case hls::Check::Bandwidth:        return "Bandwidth · peak";
        case hls::Check::AverageBandwidth: return "Avg";
        case hls::Check::Codecs:           return "Codecs";
        case hls::Check::Resolution:       return "Res";
        case hls::Check::FrameRate:        return "FPS";
        case hls::Check::VideoRange:       return "Range";
        case hls::Check::SegmentStart:     return "Starts";
        case hls::Check::Duration:         return "Dur";
        case hls::Check::Count:            break;
    }
    return "?";
}

const char* column_label(dash::Check c)
{
    switch (c) {
        case dash::Check::Bandwidth:  return "@bandwidth";
        case dash::Check::Codecs:     return "Codecs";
        case dash::Check::Resolution: return "Size";
        case dash::Check::FrameRate:  return "FPS";
        case dash::Check::Audio:      return "Audio";
        case dash::Check::Colour:     return "Colour";
        case dash::Check::Sap:        return "SAP";
        case dash::Check::Timeline:   return "Time";
        case dash::Check::Alignment:  return "Align";
        case dash::Check::Count:      break;
    }
    return "?";
}

// A tooltip that wraps: the notes run to a few sentences.
void wrapped_tooltip(const std::string& text)
{
    if (text.empty() || !ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 5.0f));
    if (ImGui::BeginTooltip()) {
        ImGui::PushFont(ui_fonts().ui_small);
        ImGui::PushTextWrapPos(420.0f);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
}

std::string cell_text(const char* name, const hls::Cell& c)
{
    std::string t = std::format("{} — {}", name, c.checked ? hls::severity_name(c.sev) : "not checked");
    if (!c.declared.empty()) t += "\nDeclared: " + c.declared;
    if (!c.measured.empty()) t += "\nMeasured: " + c.measured;
    if (!c.note.empty()) t += "\n\n" + c.note;
    return t;
}

std::string cell_text(hls::Check k, const hls::Cell& c) { return cell_text(hls::check_name(k), c); }

std::string short_uri(const std::string& uri)
{
    const std::string p = uri.substr(0, uri.find_first_of("?#"));
    const auto last = p.rfind('/');
    if (last == std::string::npos || last == 0) return p;
    const auto prev = p.rfind('/', last - 1);
    return prev == std::string::npos ? p : p.substr(prev + 1);
}

bool names(const hls::Finding& f, int variant)
{
    return f.renditions.empty() || std::find(f.renditions.begin(), f.renditions.end(), variant) != f.renditions.end();
}

// The line over a check's result: why it failed, else its errors, else its
// warnings, else that the declarations match.
struct Verdict {
    std::string   text;
    hls::Severity sev = hls::Severity::Ok;
};

Verdict verdict_of(ConformProbe::State state, const std::string& error, int errors, int warnings,
                   const std::vector<hls::Finding>& findings)
{
    Verdict v;
    int notes = 0;
    for (const auto& f : findings) notes += f.sev == hls::Severity::Info ? 1 : 0;
    if (state == ConformProbe::State::Failed) {
        v.text = "Could not check: " + error;
        v.sev = hls::Severity::Error;
    } else if (errors > 0) {
        v.text = std::format("{} error{}{}", errors, errors == 1 ? "" : "s",
                             warnings ? std::format(", {} warning{}", warnings, warnings == 1 ? "" : "s") : std::string());
        v.sev = hls::Severity::Error;
    } else if (warnings > 0) {
        v.text = std::format("{} warning{}", warnings, warnings == 1 ? "" : "s");
        v.sev = hls::Severity::Warning;
    } else {
        v.text = notes ? std::format("Declarations match · {} note{}", notes, notes == 1 ? "" : "s")
                       : std::string("Declarations match");
    }
    if (state == ConformProbe::State::Stopped) v.text += " (stopped: over what was read)";
    return v;
}

// The findings, each with its detail under it once clicked: `sel` ≥ 0 only
// those naming row `sel`, `open` the one expanded; `one` / `many` say what a
// row is.
void draw_findings(const std::vector<hls::Finding>& findings, int sel, int& open, const char* one, const char* many,
                   float cw, int id_base = 700)
{
    ImDrawList* cdl = ImGui::GetWindowDrawList();
    ImFont*     fs  = ui_fonts().ui_small;
    const float wrap = cw - 26.0f;
    int shown = 0;
    for (int i = 0; i < static_cast<int>(findings.size()); ++i) {
        const auto& f = findings[static_cast<std::size_t>(i)];
        if (sel >= 0 && !names(f, sel)) continue;
        ++shown;
        const bool is_open = open == i;
        const std::string title = f.renditions.empty() || !one ? f.title
            : std::format("{}   · {} {}", f.title, f.renditions.size() == 1 ? one : many, hls::format_list(f.renditions));
        const ImVec2 fp = ImGui::GetCursorScreenPos();
        const ImVec2 ts = fs->CalcTextSizeA(13.0f, FLT_MAX, wrap, title.c_str());
        const ImVec2 ds = is_open && !f.detail.empty() ? fs->CalcTextSizeA(12.0f, FLT_MAX, wrap, f.detail.c_str()) : ImVec2(0, 0);
        const float fh = 8.0f + ts.y + (is_open && !f.detail.empty() ? ds.y + 6.0f : 0.0f) + 6.0f;
        ImGui::PushID(i + id_base);
        const bool clicked = ImGui::InvisibleButton("##cf", ImVec2(cw, fh));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (clicked) open = is_open ? -1 : i;
        cdl->AddRectFilled(fp, ImVec2(fp.x + cw, fp.y + fh),
                           C(is_open ? th::rgba(255, 255, 255, 0.06f) : hov ? th::rgba(255, 255, 255, 0.04f)
                                                                            : th::rgba(255, 255, 255, 0.02f)), 4.0f);
        cdl->AddRectFilled(fp, ImVec2(fp.x + 2.5f, fp.y + fh), C(th::alpha(sev_color(f.sev), 0.8f)), 1.0f);
        sev_icon(cdl, ImVec2(fp.x + 13.0f, fp.y + 4.0f + 8.5f), f.sev);
        cdl->AddText(fs, 13.0f, ImVec2(fp.x + 24.0f, fp.y + 5.0f), C(th::kText), title.c_str(), nullptr, wrap);
        if (is_open && !f.detail.empty())
            cdl->AddText(fs, 12.0f, ImVec2(fp.x + 24.0f, fp.y + 9.0f + ts.y), C(th::kTextDim), f.detail.c_str(), nullptr,
                         wrap);
        if (hov && !is_open && !f.detail.empty()) ui::tooltip("Click for details");
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
    }
    if (shown == 0) ImGui::TextDisabled("Nothing to report.");
}

// A typed or pasted URL without the blanks around it.
std::string trimmed(const char* s)
{
    std::string_view v(s);
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front()))) v.remove_prefix(1);
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.remove_suffix(1);
    return std::string(v);
}

// "754.360 s (12:34.4)".
std::string event_time(double t)
{
    if (!std::isfinite(t)) return "—";
    const int s = static_cast<int>(std::floor(t));
    return t >= 60.0 ? std::format("{:.3f} s ({}:{:02}.{})", t, s / 60, s % 60, static_cast<int>((t - s) * 10.0))
                     : std::format("{:.3f} s", t);
}

// "+16 ms", "−2.005 s".
std::string offset_text(double off)
{
    if (!std::isfinite(off)) return "—";
    const char* sign = off > 0.0 ? "+" : off < 0.0 ? "\u2212" : "";
    const double a = std::fabs(off);
    return a >= 1.0 ? std::format("{}{:.3f} s", sign, a) : std::format("{}{:.0f} ms", sign, a * 1000.0);
}

// Where a marker sits against the boundary nearest the event: the slack
// band, the boundary, and a dot `off` away, `scale` at either end.
void offset_gauge(ImDrawList* dl, ImVec2 p, float w, float h, double off, double scale, double slack, hls::Severity s)
{
    const float cx = p.x + w * 0.5f, cy = p.y + h * 0.5f, half = w * 0.5f - 5.0f;
    auto x_of = [&](double v) { return cx + static_cast<float>(std::clamp(v / scale, -1.0, 1.0)) * half; };
    dl->AddRectFilled(ImVec2(x_of(-slack), p.y + 2.0f), ImVec2(x_of(slack), p.y + h - 2.0f),
                      C(th::alpha(sev_color(hls::Severity::Ok), 0.22f)), 2.0f);
    dl->AddLine(ImVec2(p.x + 3.0f, cy), ImVec2(p.x + w - 3.0f, cy), C(th::rgba(255, 255, 255, 0.18f)));
    dl->AddLine(ImVec2(cx, p.y + 1.0f), ImVec2(cx, p.y + h - 1.0f), C(th::kTextDim), 1.5f);
    if (std::isfinite(off)) dl->AddCircleFilled(ImVec2(x_of(off), cy), 4.0f, C(sev_color(s)));
}

// Error first: the enum's order is not the severity's.
int rank(hls::Severity s)
{
    switch (s) {
        case hls::Severity::Error:   return 3;
        case hls::Severity::Warning: return 2;
        case hls::Severity::Info:    return 1;
        case hls::Severity::Ok:      return 0;
    }
    return 0;
}

hls::Severity worse(hls::Severity a, hls::Severity b) { return rank(a) >= rank(b) ? a : b; }

} // namespace

std::vector<std::string> NetInspector::marker_tag_list() const
{
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        std::string_view v(cur);
        while (v.starts_with('#')) v.remove_prefix(1);
        v = v.substr(0, v.find(':'));
        if (!v.empty() && std::find(out.begin(), out.end(), v) == out.end()) out.emplace_back(v);
        cur.clear();
    };
    for (const char* c = marker_tags_; *c; ++c) {
        if (std::isspace(static_cast<unsigned char>(*c)) || *c == ',') flush();
        else cur += *c;
    }
    flush();
    return out;
}

std::string NetInspector::report_json() const
{
    const auto& r = conform_res_;
    return hls::with_markers(hls::conformance_json(r.master, r.playlists, r.report, r.source), r.marker_in, r.markers);
}

bool NetInspector::export_html(const std::string& path)
{
    std::ofstream f(path, std::ios::binary);
    f << hls::report_html(report_json(), hls::run_meta_json(conform_res_.seconds, conform_run_markers_only_,
                                                            marker_tag_list()));
    if (!f) {
        export_note_ = "Could not write " + path;
        logger::warn("conformance: {}", export_note_);
        return false;
    }
    export_note_ = "Saved " + path;
    logger::info("conformance: wrote {}", path);
    return true;
}

bool NetInspector::conform_ready(const Inputs& in) const
{
    return in.manifest && !in.manifest->text().empty() && !in.manifest->variants().empty()
        && !ManifestParser::is_dash(in.media_url);
}

void NetInspector::start_conform(const Inputs& in)
{
    const std::string url = trimmed(hls_url_);
    if (url.empty() && !conform_ready(in)) return;
    ConformProbe::Options o;
    o.segments = conform_segments_;
    o.sizes = conform_sizes_;
    o.marker_tags = marker_tag_list();
    o.markers_only = markers_only_;
    conform_from_url_ = !url.empty();
    conform_run_markers_only_ = markers_only_;
    if (conform_from_url_) conform_.start(url, o);
    else conform_.start(in.media_url, in.manifest->text(), in.manifest->final_url(), o);
    conform_sel_ = conform_open_ = marker_ev_open_ = marker_open_ = -1;
    export_note_.clear();
}

void NetInspector::start_dash()
{
    const std::string url = trimmed(dash_url_);
    if (url.empty()) return;
    DashProbe::Options o;
    o.segments = conform_segments_;
    o.sizes = conform_sizes_;
    dash_.start(url, o);
    conform_sel_ = conform_open_ = -1;
}

void NetInspector::conform_tick(const Inputs& in)
{
    if (qa_dash_) {
        qa_dash_ = false;
        start_dash();
    }
    if (dash_.version() != dash_ver_) {
        dash_ver_ = dash_.version();
        dash_res_ = dash_.result();
        if (!qa_conform_out_.empty() && conform_dash_ && dash_.state() == DashProbe::State::Done) {
            std::ofstream f(qa_conform_out_);
            f << dash::conformance_json(dash_res_.mpd, dash_res_.reads, dash_res_.report, dash_res_.source);
            logger::info("conform_out: wrote {}", qa_conform_out_);
            qa_conform_out_.clear();
        }
    }
    if (qa_hls_) {
        qa_hls_ = false;
        qa_conform_started_ = true;
        start_conform(in);
    }
    if (qa_conform_ && !qa_conform_started_ && conform_ready(in)) {
        qa_conform_started_ = true;
        start_conform(in);
    }
    if (conform_.version() == conform_ver_) return;
    conform_ver_ = conform_.version();
    conform_res_ = conform_.result();
    if (!qa_conform_out_.empty() && conform_.state() == ConformProbe::State::Done) {
        std::ofstream f(qa_conform_out_);
        f << report_json();
        logger::info("conform_out: wrote {}", qa_conform_out_);
        qa_conform_out_.clear();
    }
    if (qa_marker_open_ >= 0 && conform_.state() == ConformProbe::State::Done) {
        marker_ev_open_ = qa_marker_open_;
        qa_marker_open_ = -1;
    }
    if (!qa_conform_html_.empty() && conform_.state() == ConformProbe::State::Done) {
        export_html(qa_conform_html_);
        qa_conform_html_.clear();
    }
}

void NetInspector::draw_conformance(const Inputs& in, Out& out, float w, float h)
{
    (void)out;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool ready = conform_ready(in) || !trimmed(hls_url_).empty();
    const ConformProbe::State state = conform_.state();
    const bool running = state == ConformProbe::State::Running;
    const ConformProbe::Result& res = conform_res_;
    const hls::ConformReport& rep = res.report;
    ImFont* fs = ui_fonts().ui_small;
    const bool dash = conform_dash_;
    const ConformProbe::State shown_state = dash ? dash_.state() : state;
    const bool busy = shown_state == ConformProbe::State::Running;
    const float bar_h = 2.0f * kToolbarH;   // the URL on a row of its own (HLS: and the marker tags)

    // ── Toolbar: HLS / DASH · segments read · sizes · check / stop ───────────
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + bar_h), C(kSideBar));
    dl->AddLine(ImVec2(p0.x, p0.y + bar_h - 0.5f), ImVec2(p0.x + w, p0.y + bar_h - 0.5f), C(th::kPanelBorder));
    {
        float cx = p0.x + kPad;
        const float cy = p0.y + (kToolbarH - 22.0f) * 0.5f;
        const struct { bool dash; const char* label; } modes[] = { { false, "HLS master" }, { true, "DASH MPD" } };
        for (const auto& md : modes) {
            ImGui::SetCursorScreenPos(ImVec2(cx, cy));
            ImGui::PushID(md.dash ? 491 : 490);
            if (chip_button("##mode", md.label, dash == md.dash) && dash != md.dash) {
                conform_dash_ = md.dash;
                conform_sel_ = conform_open_ = -1;
            }
            ImGui::PopID();
            if (ImGui::IsItemHovered())
                ui::tooltip(md.dash ? "Check a DASH MPD given by its URL or path: the player cannot open MPDs itself"
                                    : "Check an HLS master: the one playing, or one given by its URL or path\n"
                                      "(one the player cannot open, a DRM stream, is checked all the same)");
            cx = ImGui::GetItemRectMax().x + 4.0f;
        }
        cx += 10.0f;
        dl->AddText(fs, 12.0f, ImVec2(cx, cy + 4.0f), C(th::kTextDim), "Read");
        cx += fs->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, "Read").x + 6.0f;
        const struct { int n; const char* label; } segs[] = { { 1, "1" }, { 3, "3" }, { 10, "10" }, { 0, "All" } };
        ImGui::BeginDisabled(busy || (!dash && markers_only_));
        for (const auto& s : segs) {
            ImGui::SetCursorScreenPos(ImVec2(cx, cy));
            ImGui::PushID(s.n + 500);
            if (chip_button("##segs", s.label, conform_segments_ == s.n)) conform_segments_ = s.n;
            ImGui::PopID();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ui::tooltip(s.n == 0 ? (dash ? "Read every segment of every Representation in full"
                                             : "Read every segment of every playlist in full")
                                     : (dash ? "Segments read in full per video Representation (one per audio one): the "
                                               "first, the last, evenly between"
                                             : "Segments read in full per variant playlist: the first, the last, evenly between"));
            cx = ImGui::GetItemRectMax().x + 4.0f;
        }
        ImGui::SetCursorScreenPos(ImVec2(cx + 6.0f, cy));
        if (chip_button("##sizes", "Size all", conform_sizes_)) conform_sizes_ = !conform_sizes_;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ui::tooltip(dash ? "Size every segment for @bandwidth's bucket model: the Segment Index's or @mediaRange's size\n"
                               "where there is one, else a request for its first byte. Off: only the segments read are\n"
                               "sized, and the rates needed are lower bounds."
                             : "Size every segment for BANDWIDTH's peak: EXT-X-BYTERANGE lengths, else a request for its first\n"
                               "byte (the size comes back in Content-Range). Off: only the segments read are sized, and the\n"
                               "peak is a lower bound.");
        ImGui::EndDisabled();

        const std::string label = busy ? std::string("Stop")
                                : shown_state == ConformProbe::State::Idle ? std::string("Check") : std::string("Check again");
        const float bw = fs->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, label.c_str()).x + 24.0f;
        ImGui::SetCursorScreenPos(ImVec2(p0.x + w - kPad - bw, cy));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
        ImGui::BeginDisabled(!(dash ? !trimmed(dash_url_).empty() : ready) && !busy);
        if (ImGui::Button(label.c_str(), ImVec2(bw, 22.0f))) {
            if (dash && busy) dash_.stop();
            else if (dash) start_dash();
            else if (running) conform_.stop();
            else start_conform(in);
        }
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ui::tooltip(busy ? "Stop: what was read so far is compared"
                        : dash ? "Fetch the MPD, size every segment, read some of each Representation in full and compare\n"
                                 "with what the MPD declares. Listed in Network as initiator \"inspector\"."
                               : "Read every variant and audio playlist, size their segments, read some in full and\n"
                                 "compare with the master's declarations. Listed in Network as initiator \"inspector\".");

        const float row2 = p0.y + kToolbarH + (kToolbarH - 22.0f) * 0.5f;
        if (dash) {
            ImGui::SetCursorScreenPos(ImVec2(p0.x + kPad, row2));
            ImGui::SetNextItemWidth(w - 2.0f * kPad);
            ImGui::BeginDisabled(busy);
            if (ImGui::InputTextWithHint("##dash_url", "MPD URL or path (Enter checks it)", dash_url_, sizeof(dash_url_),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
                start_dash();
            ImGui::EndDisabled();
        } else {
            // URL · Markers only · marker tags
            const float tags_w = std::clamp(w * 0.28f, 120.0f, 220.0f);
            const float mo_w = fs->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, "Markers only").x + 30.0f;
            const float url_w = std::max(80.0f, w - 2.0f * kPad - tags_w - mo_w - 12.0f);
            ImGui::BeginDisabled(busy);
            ImGui::SetCursorScreenPos(ImVec2(p0.x + kPad, row2));
            ImGui::SetNextItemWidth(url_w);
            if (ImGui::InputTextWithHint("##hls_url", "Master URL or path · empty: the one playing (Enter checks it)",
                                         hls_url_, sizeof(hls_url_), ImGuiInputTextFlags_EnterReturnsTrue))
                start_conform(in);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ui::tooltip("A master playlist to check instead of the one playing, e.g. one the player cannot\n"
                            "open (DRM): SAMPLE-AES segments are read without their key.");
            ImGui::SetCursorScreenPos(ImVec2(p0.x + kPad + url_w + 6.0f, row2));
            if (chip_button("##markers_only", "Markers only", markers_only_)) markers_only_ = !markers_only_;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ui::tooltip("The break-marker check only: every playlist is read, no segment requested");
            ImGui::SetCursorScreenPos(ImVec2(p0.x + w - kPad - tags_w, row2));
            ImGui::SetNextItemWidth(tags_w);
            if (ImGui::InputTextWithHint("##marker_tags", "Marker tags, e.g. EXT-X-CUE-OUT", marker_tags_,
                                         sizeof(marker_tags_), ImGuiInputTextFlags_EnterReturnsTrue) && ready)
                start_conform(in);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ui::tooltip("A packager's own break tags to match beside EXT-X-DATERANGE, by their ID attribute\n"
                            "(else their value); names without '#', blanks or commas between");
            ImGui::EndDisabled();
        }
    }

    const float body_top = p0.y + bar_h;
    ImGui::SetCursorScreenPos(ImVec2(p0.x, body_top));
    if (dash) return draw_dash(w, h - bar_h);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad + 2.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::BeginChild("##conformance", ImVec2(w, h - bar_h), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImDrawList* cdl = ImGui::GetWindowDrawList();
    const float cw = ImGui::GetContentRegionAvail().x;
    auto end = [&] {
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    };

    if (!ready && state == ConformProbe::State::Idle) {
        draw_empty_state("Not an HLS master playlist",
                         "Conformance compares what an HLS master playlist declares for each variant — BANDWIDTH, "
                         "CODECS, RESOLUTION, FRAME-RATE, VIDEO-RANGE — with what its segments hold, and where each "
                         "rendition marks its breaks. Type a master's URL above to check one that is not playing; for "
                         "a DASH MPD, choose DASH MPD.",
                         cw, h - bar_h - 30.0f);
        return end();
    }
    if (state == ConformProbe::State::Idle) {
        draw_empty_state("Declared vs measured",
                         "Check reads every variant and audio playlist, sizes their segments and reads a few of each "
                         "in full, then compares each EXT-X-STREAM-INF with what the media holds, and the break "
                         "markers of every rendition with each other.",
                         cw, h - bar_h - 30.0f);
        return end();
    }

    // ── Progress, or the verdict ─────────────────────────────────────────────
    {
        const ConformProbe::Progress p = conform_.progress();
        if (running) {
            const int done = p.playlists + p.reads + p.sizes;
            const int total = std::max(1, p.playlists_total + p.reads_total + p.sizes_total);
            const std::string t = std::format("playlists {}/{} · read {}/{} · sized {}/{} · {:.1f} MB", p.playlists,
                                              p.playlists_total, p.reads, p.reads_total, p.sizes, p.sizes_total,
                                              static_cast<double>(p.bytes) / 1e6);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, th::vec4(th::kAccent));
            ImGui::ProgressBar(static_cast<float>(done) / static_cast<float>(total), ImVec2(cw, 18.0f), t.c_str());
            ImGui::PopStyleColor();
        } else {
            const ImVec2 vp = ImGui::GetCursorScreenPos();
            const bool mo = conform_run_markers_only_;
            std::vector<hls::Finding> all = mo ? std::vector<hls::Finding>{} : rep.findings;
            all.insert(all.end(), res.markers.findings.begin(), res.markers.findings.end());
            Verdict v = verdict_of(state, res.error, (mo ? 0 : rep.errors) + res.markers.errors,
                                   (mo ? 0 : rep.warnings) + res.markers.warnings, all);
            if (mo && v.text.starts_with("Declarations match"))
                v.text.replace(0, std::string_view("Declarations match").size(),
                               res.markers.any() ? "Breaks marked alike" : "No break markers");
            sev_icon(cdl, ImVec2(vp.x + 7.0f, vp.y + 10.0f), v.sev);
            cdl->AddText(ui_fonts().ui, 15.0f, ImVec2(vp.x + 20.0f, vp.y + 1.0f), C(sev_color(v.sev)), v.text.c_str(),
                         nullptr, cw - 20.0f);
            ImGui::Dummy(ImVec2(cw, 22.0f));
            int reads = 0, sized = 0, segs = 0;
            for (const auto& pl : res.playlists) {
                reads += static_cast<int>(pl.reads.size());
                segs += static_cast<int>(pl.tl.segments.size());
                for (const auto b : pl.sizes) sized += b >= 0 ? 1 : 0;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
            ImGui::TextWrapped("%s", std::format("{} variants · {} playlists · {} segments read · {} of {} sized · "
                                                 "{:.1f} MB in {:.1f} s", res.master.variants.size(), res.playlists.size(),
                                                 reads, sized, segs, static_cast<double>(p.bytes) / 1e6, res.seconds)
                                         .c_str());
            ImGui::PopStyleColor();
        }
    }

    // ── Variants ─────────────────────────────────────────────────────────────
    const auto& vars = res.master.variants;
    if (!vars.empty() && !conform_run_markers_only_) {
        section_caption(std::format("VARIANTS ({})", vars.size()).c_str());
        constexpr ImGuiTableFlags kTbl = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollX
            | ImGuiTableFlags_ScrollY;
        constexpr int kNCols = 3 + static_cast<int>(std::size(kCols));
        const float rows_h = std::min(static_cast<float>(vars.size()) * 21.0f + 26.0f, std::max(170.0f, h * 0.42f));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 3.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, th::vec4(th::rgba(255, 255, 255, 0.025f)));
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, th::vec4(th::rgba(255, 255, 255, 0.04f)));
        if (ImGui::BeginTable("##conform_variants", kNCols, kTbl, ImVec2(0.0f, rows_h))) {
            ImGui::TableSetupScrollFreeze(1, 1);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableSetupColumn(column_label(hls::Check::Bandwidth), ImGuiTableColumnFlags_WidthFixed, 132.0f);
            for (const hls::Check k : kCols)
                ImGui::TableSetupColumn(column_label(k), ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Playlist", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < static_cast<int>(vars.size()); ++i) {
                const bool have_row = i < static_cast<int>(rep.rows.size());
                const hls::VariantRow* row = have_row ? &rep.rows[static_cast<std::size_t>(i)] : nullptr;
                const bool sel = conform_sel_ == i, playing = !conform_from_url_ && i == in.playing_variant;
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 20.0f);
                if (sel) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, C(kSelRow));
                else if (playing) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, C(kPlayRow));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i + 600);
                if (ImGui::Selectable("##row", sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                      ImVec2(0.0f, 18.0f)))
                    conform_sel_ = sel ? -1 : i;
                ImGui::PopID();
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextUnformatted(playing ? std::format("▶ {}", i).c_str() : std::to_string(i).c_str());

                ImGui::TableSetColumnIndex(1);
                if (row) {
                    const hls::Cell& c = row->cell(hls::Check::Bandwidth);
                    const ImU32 col = c.checked && c.sev != hls::Severity::Ok ? sev_color(c.sev) : th::kText;
                    ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(col));
                    ImGui::TextUnformatted(std::format("{} · {}", c.declared, c.measured.empty() ? "—" : c.measured).c_str());
                    ImGui::PopStyleColor();
                    wrapped_tooltip(cell_text(hls::Check::Bandwidth, c));
                } else {
                    ImGui::TextDisabled("%s", vars[static_cast<std::size_t>(i)].bandwidth >= 0
                                                  ? hls::format_bitrate(static_cast<double>(vars[static_cast<std::size_t>(i)].bandwidth)).c_str()
                                                  : "—");
                }
                for (std::size_t k = 0; k < std::size(kCols); ++k) {
                    ImGui::TableSetColumnIndex(static_cast<int>(k) + 2);
                    if (!row) {
                        ImGui::TextDisabled("·");
                        continue;
                    }
                    const hls::Cell& c = row->cell(kCols[k]);
                    const ImVec2 cp = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(28.0f, 16.0f));
                    if (c.checked) sev_icon(ImGui::GetWindowDrawList(), ImVec2(cp.x + 14.0f, cp.y + 8.0f), c.sev);
                    else ImGui::GetWindowDrawList()->AddText(fs, 12.0f, ImVec2(cp.x + 10.0f, cp.y + 1.0f), C(th::kTextDim), "—");
                    wrapped_tooltip(cell_text(kCols[k], c));
                }
                ImGui::TableSetColumnIndex(kNCols - 1);
                ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
                ImGui::TextUnformatted(short_uri(vars[static_cast<std::size_t>(i)].uri).c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    }

    // ── Findings ─────────────────────────────────────────────────────────────
    if (!running && state != ConformProbe::State::Failed) {
        // The breaks before the findings: a few cards, where the findings run
        // long. Seeking to a break means something only in the master playing.
        const std::string url = trimmed(hls_url_);
        draw_markers(in, out, cw, !conform_from_url_ || url == in.media_url);
        if (!conform_run_markers_only_) {
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            section_caption(conform_sel_ >= 0
                                ? std::format("FINDINGS · VARIANT #{} (CLICK IT AGAIN FOR ALL)", conform_sel_).c_str()
                                : "FINDINGS");
            draw_findings(rep.findings, conform_sel_, conform_open_, "variant", "variants", cw);
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (ImGui::SmallButton("Copy the report as JSON")) ImGui::SetClipboardText(report_json().c_str());
        if (ImGui::IsItemHovered())
            ui::tooltip("Declarations, measurements and findings per variant, the break markers, and every\n"
                        "playlist read — what mirage_hlscheck --json writes");
        ImGui::SameLine();
        if (ImGui::SmallButton("Export HTML")) {
            const char* home = std::getenv("HOME");
            namespace fs = std::filesystem;
            fs::path dir = home ? fs::path(home) / "Downloads" : fs::temp_directory_path();
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) dir = home ? fs::path(home) : fs::temp_directory_path();
            char stamp[32] = "";
            const std::time_t now = std::time(nullptr);
            std::tm lt{};
            if (localtime_r(&now, &lt)) std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &lt);
            const std::string path = (dir / std::format("hlscheck-{}.html", stamp)).string();
            if (export_html(path)) {
                // Into the browser: `open` hands it to the default one.
                const char* argv[] = { "open", path.c_str(), nullptr };
                pid_t pid = 0;
                posix_spawnp(&pid, "open", nullptr, nullptr, const_cast<char* const*>(argv), environ);
            }
        }
        if (ImGui::IsItemHovered())
            ui::tooltip("The report as a page to read in a browser, self-contained (what mirage_hlscheck --html\n"
                        "writes): saved in Downloads and opened");
        if (!export_note_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
            ImGui::TextWrapped("%s", export_note_.c_str());
            ImGui::PopStyleColor();
        }
    }
    end();
}

void NetInspector::draw_markers(const Inputs& in, Out& out, float cw, bool seekable)
{
    const ConformProbe::Result& res = conform_res_;
    const hls::MarkerReport& mk = res.markers;
    const std::vector<std::string> tags = marker_tag_list();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* fs = ui_fonts().ui_small;
    auto label = [&](int i) {
        return i >= 0 && i < static_cast<int>(res.marker_in.size()) ? res.marker_in[static_cast<std::size_t>(i)].label
                                                                    : std::format("#{}", i);
    };
    int splices = 0, others = 0;
    for (const auto& ev : mk.events) (ev.splice ? splices : others) += 1;

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    section_caption(mk.any() ? std::format("BREAK MARKERS · {} SPLICE POINT{} · REFERENCE {} · ±{:.0f} MS", splices,
                                           splices == 1 ? "" : "S", label(mk.reference), hls::kMarkerSlack * 1000.0)
                                   .c_str()
                             : "BREAK MARKERS");
    if (!mk.any()) {
        std::string what = "EXT-X-DATERANGE";
        for (const auto& t : tags) what += ", " + t;
        ImGui::TextDisabled("None: no %s in any playlist.", what.c_str());
    }
    // Tags with an ID the playlists carry and the check was not asked for.
    std::vector<std::string> offer;
    for (const auto& c : res.tag_candidates)
        if (std::find(tags.begin(), tags.end(), c) == tags.end()) offer.push_back(c);
    if (!offer.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
        ImGui::TextUnformatted("Tags with an ID, maybe a packager's break markers:");
        ImGui::PopStyleColor();
        for (std::size_t i = 0; i < offer.size(); ++i) {
            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(i) + 950);
            if (ImGui::SmallButton(std::format("+ {}", offer[i]).c_str())) {
                std::string t = marker_tags_;
                t += (t.empty() ? "" : " ") + offer[i];
                std::snprintf(marker_tags_, sizeof(marker_tags_), "%s", t.c_str());
                start_conform(in);
            }
            ImGui::PopID();
            if (ImGui::IsItemHovered()) ui::tooltip("Match this tag too, and check again");
        }
    }

    // ── Splice points: a card each, its renditions once clicked ──────────────
    int n = 0;
    for (int e = 0; e < static_cast<int>(mk.events.size()); ++e) {
        const hls::MarkerEvent& ev = mk.events[static_cast<std::size_t>(e)];
        if (!ev.splice) continue;
        ++n;
        hls::Severity sev = hls::Severity::Ok;
        int off = 0;
        for (const auto& p : ev.places) {
            sev = worse(sev, p.sev);
            off += p.sev == hls::Severity::Error || p.sev == hls::Severity::Warning;
        }
        if (!ev.missing.empty()) sev = worse(sev, hls::Severity::Warning);
        const bool open = marker_ev_open_ == e;
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        const float card_h = 26.0f;
        ImGui::PushID(e + 1200);
        const bool clicked = ImGui::InvisibleButton("##ev", ImVec2(cw, card_h));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (clicked) marker_ev_open_ = open ? -1 : e;
        dl->AddRectFilled(cp, ImVec2(cp.x + cw, cp.y + card_h),
                          C(open ? th::rgba(255, 255, 255, 0.06f) : hov ? th::rgba(255, 255, 255, 0.04f)
                                                                       : th::rgba(255, 255, 255, 0.02f)), 4.0f);
        dl->AddRectFilled(cp, ImVec2(cp.x + 2.5f, cp.y + card_h), C(th::alpha(sev_color(sev), 0.8f)), 1.0f);
        sev_icon(dl, ImVec2(cp.x + 13.0f, cp.y + card_h * 0.5f), sev);
        float x = cp.x + 24.0f;
        dl->AddText(ui_fonts().mono, 13.0f, ImVec2(x, cp.y + 5.0f), C(th::kText), ev.id.c_str());
        x += ui_fonts().mono->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, ev.id.c_str()).x + 12.0f;
        const std::string when = event_time(ev.time);
        dl->AddText(fs, 13.0f, ImVec2(x, cp.y + 5.0f), C(th::kText), when.c_str());
        x += fs->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, when.c_str()).x + 12.0f;
        const std::string sum = std::format("marked in {}{}{}", ev.places.size(),
                                            ev.missing.empty() ? "" : std::format(" · missing in {}", ev.missing.size()),
                                            off ? std::format(" · {} off", off) : "");
        const float sw = fs->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, sum.c_str()).x;
        dl->AddText(fs, 12.0f, ImVec2(cp.x + cw - sw - 10.0f, cp.y + 6.0f), C(sev == hls::Severity::Ok ? th::kTextDim
                                                                                                  : sev_color(sev)),
                    sum.c_str());
        if (!ev.cls.empty() && x + 40.0f < cp.x + cw - sw - 16.0f) {
            dl->PushClipRect(ImVec2(x, cp.y), ImVec2(cp.x + cw - sw - 16.0f, cp.y + card_h), true);
            dl->AddText(fs, 12.0f, ImVec2(x, cp.y + 6.0f), C(th::kTextDim), ev.cls.c_str());
            dl->PopClipRect();
        }
        if (hov && !open)
            ui::tooltip((ev.time_from.empty() ? std::string("Click for the renditions")
                                              : "Click for the renditions · time from " + ev.time_from).c_str());
        if (!open) {
            ImGui::Dummy(ImVec2(0.0f, 1.0f));
            continue;
        }

        double scale = hls::kMarkerSlack * 4.0;
        for (const auto& p : ev.places)
            if (std::isfinite(p.off)) scale = std::max(scale, std::fabs(p.off) * 1.15);
        std::vector<const hls::MarkerPlace*> places;
        for (const auto& p : ev.places) places.push_back(&p);
        std::stable_sort(places.begin(), places.end(), [](const auto* a, const auto* b) {
            return rank(a->sev) > rank(b->sev);
        });
        constexpr ImGuiTableFlags kTbl = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit
            | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollX;
        const int rows = static_cast<int>(places.size() + ev.missing.size());
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 2.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, th::vec4(th::rgba(255, 255, 255, 0.025f)));
        ImGui::PushID(e + 1400);
        if (ImGui::BeginTable("##places", 6, kTbl, ImVec2(cw, std::min(static_cast<float>(rows) * 20.0f + 24.0f, 260.0f)))) {
            ImGui::TableSetupScrollFreeze(1, 1);
            ImGui::TableSetupColumn("Rendition", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Marker", ImGuiTableColumnFlags_WidthFixed, 132.0f);
            ImGui::TableSetupColumn("At", ImGuiTableColumnFlags_WidthFixed, 66.0f);
            ImGui::TableSetupColumn("Boundary", ImGuiTableColumnFlags_WidthFixed, 92.0f);
            ImGui::TableSetupColumn("Off", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("\u2212 boundary +", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableHeadersRow();
            for (const auto* p : places) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 19.0f);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label(p->rendition).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(p->tag.c_str());
                if (ImGui::IsItemHovered()) ui::tooltip(std::format("#{} · playlist line {}", p->tag, p->line).c_str());
                ImGui::TableSetColumnIndex(2);
                text_right(std::isfinite(p->at) ? std::format("{:.3f} s", p->at) : "—", th::kText);
                ImGui::TableSetColumnIndex(3);
                text_right(std::isfinite(p->nearest) ? std::format("{:.3f} s", p->nearest) : "—", th::kTextDim);
                ImGui::TableSetColumnIndex(4);
                text_right(offset_text(p->off), p->sev == hls::Severity::Ok ? th::kText : sev_color(p->sev));
                ImGui::TableSetColumnIndex(5);
                const ImVec2 gp = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(104.0f, 15.0f));
                offset_gauge(ImGui::GetWindowDrawList(), gp, 104.0f, 15.0f, p->off, scale, hls::kMarkerSlack, p->sev);
            }
            for (const int m : ev.missing) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 19.0f);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label(m).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(sev_color(hls::Severity::Warning)));
                ImGui::TextUnformatted("missing");
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ui::tooltip("Carries the tag at other breaks, not at this one");
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (seekable && std::isfinite(ev.time)) {
            ImGui::PushID(e + 1600);
            if (ImGui::SmallButton("Play from 3 s before the break"))
                out.seek_to = ui::pts_at(std::max(0.0, ev.time - 3.0), in.start);   // a position, as the Manifest tab's tags, made a PTS (D-089)
            ImGui::PopID();
            if (ImGui::IsItemHovered()) ui::tooltip("Seek there: hear whether the audio cuts with the picture");
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }
    if (mk.any() && n == 0)
        ImGui::TextDisabled("No splice point: no interstitial, SCTE-35 or marker tag asked for.");
    if (others > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
        ImGui::TextWrapped("%s", std::format("And {} other EXT-X-DATERANGE{}: no interstitial CLASS or SCTE-35, not "
                                             "splice points, not checked.", others, others == 1 ? "" : "s").c_str());
        ImGui::PopStyleColor();
    }
    if (mk.any()) {
        section_caption("MARKER FINDINGS");
        draw_findings(mk.findings, -1, marker_open_, nullptr, nullptr, cw, 1800);
    }
}

void NetInspector::draw_dash(float w, float h)
{
    const DashProbe::State state = dash_.state();
    const bool running = state == DashProbe::State::Running;
    const DashProbe::Result& res = dash_res_;
    const dash::Report& rep = res.report;
    const dash::Mpd& m = res.mpd;
    ImFont* fs = ui_fonts().ui_small;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad + 2.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::BeginChild("##conformance_dash", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImDrawList* cdl = ImGui::GetWindowDrawList();
    const float cw = ImGui::GetContentRegionAvail().x;
    auto end = [&] {
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    };

    if (state == DashProbe::State::Idle) {
        draw_empty_state("Check a DASH MPD",
                         "Give an MPD's URL or path above, then Check: each Representation's @bandwidth (with "
                         "@minBufferTime), @codecs, @width/@height, @frameRate, colour, audio, @startWithSAP, timeline "
                         "and @segmentAlignment against what its segments hold. The player cannot open MPDs itself: "
                         "FFmpeg as built here has no DASH demuxer.",
                         cw, h - 30.0f);
        return end();
    }

    // ── Progress, or the verdict ─────────────────────────────────────────────
    const DashProbe::Progress p = dash_.progress();
    if (running) {
        const int done = p.indexes + p.sizes + p.reads;
        const int total = std::max(1, p.indexes_total + p.sizes_total + p.reads_total);
        const std::string t = std::format("indexes {}/{} · sized {}/{} · read {}/{} · {:.1f} MB", p.indexes, p.indexes_total,
                                          p.sizes, p.sizes_total, p.reads, p.reads_total, static_cast<double>(p.bytes) / 1e6);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, th::vec4(th::kAccent));
        ImGui::ProgressBar(static_cast<float>(done) / static_cast<float>(total), ImVec2(cw, 18.0f), t.c_str());
        ImGui::PopStyleColor();
    } else {
        const ImVec2 vp = ImGui::GetCursorScreenPos();
        const Verdict v = verdict_of(state, res.error, rep.errors, rep.warnings, rep.findings);
        sev_icon(cdl, ImVec2(vp.x + 7.0f, vp.y + 10.0f), v.sev);
        cdl->AddText(ui_fonts().ui, 15.0f, ImVec2(vp.x + 20.0f, vp.y + 1.0f), C(sev_color(v.sev)), v.text.c_str(), nullptr,
                     cw - 20.0f);
        ImGui::Dummy(ImVec2(cw, 22.0f));
        if (state != DashProbe::State::Failed) {
            int reads = 0, sized = 0, segs = 0;
            for (std::size_t i = 0; i < res.reads.size() && i < m.reps.size(); ++i) {
                reads += static_cast<int>(res.reads[i].reads.size());
                segs += static_cast<int>(m.reps[i].segments.size());
                for (const auto b : res.reads[i].sizes) sized += b >= 0 ? 1 : 0;
            }
            const std::string buffer = std::isfinite(m.min_buffer_time) ? std::format("{:.3f} s", m.min_buffer_time)
                                                                        : std::string("absent");
            ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
            ImGui::TextWrapped("%s", std::format("{} MPD · {} Representations in {} AdaptationSets · @minBufferTime {} · "
                                                 "{} segments read · {} of {} sized · {:.1f} MB in {:.1f} s",
                                                 m.dynamic ? "dynamic" : "static", m.reps.size(), m.sets.size(), buffer,
                                                 reads, sized, segs, static_cast<double>(p.bytes) / 1e6, res.seconds)
                                         .c_str());
            ImGui::PopStyleColor();
        }
    }

    // ── Representations ──────────────────────────────────────────────────────
    if (!m.reps.empty()) {
        section_caption(std::format("REPRESENTATIONS ({})", m.reps.size()).c_str());
        constexpr ImGuiTableFlags kTbl = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollX
            | ImGuiTableFlags_ScrollY;
        constexpr int kNCols = 5 + static_cast<int>(std::size(kDashCols));   // #, @bandwidth, needs, safe … label
        // + the horizontal scrollbar's height: the table is wider than the sidebar.
        const float rows_h = std::min(static_cast<float>(m.reps.size()) * 21.0f + 26.0f + ImGui::GetStyle().ScrollbarSize,
                                      std::max(170.0f, h * 0.42f));
        const std::string rates = "Needs: below it playout runs dry whatever the order of each segment's bytes (each "
                                  "segment in by the time it ends playing). Safe: at or above it playout never runs dry "
                                  "(each segment in before it starts). Both with @minBufferTime of buffering.";
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 3.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, th::vec4(th::rgba(255, 255, 255, 0.025f)));
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, th::vec4(th::rgba(255, 255, 255, 0.04f)));
        if (ImGui::BeginTable("##conform_reps", kNCols, kTbl, ImVec2(0.0f, rows_h))) {
            ImGui::TableSetupScrollFreeze(1, 1);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableSetupColumn(column_label(dash::Check::Bandwidth), ImGuiTableColumnFlags_WidthFixed, 84.0f);
            ImGui::TableSetupColumn("Needs", ImGuiTableColumnFlags_WidthFixed, 76.0f);
            ImGui::TableSetupColumn("Safe", ImGuiTableColumnFlags_WidthFixed, 76.0f);
            for (const dash::Check k : kDashCols)
                ImGui::TableSetupColumn(column_label(k), ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Representation", ImGuiTableColumnFlags_WidthFixed, 220.0f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < static_cast<int>(m.reps.size()); ++i) {
                const bool have_row = i < static_cast<int>(rep.rows.size());
                const dash::RepRow* row = have_row ? &rep.rows[static_cast<std::size_t>(i)] : nullptr;
                const bool sel = conform_sel_ == i;
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 20.0f);
                if (sel) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, C(kSelRow));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i + 600);
                if (ImGui::Selectable("##row", sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                      ImVec2(0.0f, 18.0f)))
                    conform_sel_ = sel ? -1 : i;
                ImGui::PopID();
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextUnformatted(std::to_string(i).c_str());

                const hls::Cell* bw = row ? &row->cell(dash::Check::Bandwidth) : nullptr;
                const std::string bw_text = cell_text(dash::check_name(dash::Check::Bandwidth), bw ? *bw : hls::Cell{});
                ImGui::TableSetColumnIndex(1);
                {
                    const auto& r = m.reps[static_cast<std::size_t>(i)];
                    const std::string declared = bw && !bw->declared.empty() ? bw->declared
                        : r.bandwidth > 0 ? hls::format_bitrate(static_cast<double>(r.bandwidth)) : std::string("—");
                    const ImU32 col = bw && bw->checked && bw->sev != hls::Severity::Ok ? sev_color(bw->sev) : th::kText;
                    ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(col));
                    ImGui::TextUnformatted(declared.c_str());
                    ImGui::PopStyleColor();
                    if (bw) wrapped_tooltip(bw_text);
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(bw && !bw->measured.empty() ? bw->measured.c_str() : "—");
                wrapped_tooltip(rates);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(bw && bw->checked ? hls::format_bitrate(row->bucket.safe).c_str() : "—");
                wrapped_tooltip(rates);
                for (std::size_t k = 0; k < std::size(kDashCols); ++k) {
                    ImGui::TableSetColumnIndex(static_cast<int>(k) + 4);
                    if (!row) {
                        ImGui::TextDisabled("·");
                        continue;
                    }
                    const hls::Cell& c = row->cell(kDashCols[k]);
                    const ImVec2 cp = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(28.0f, 16.0f));
                    if (c.checked) sev_icon(ImGui::GetWindowDrawList(), ImVec2(cp.x + 14.0f, cp.y + 8.0f), c.sev);
                    else ImGui::GetWindowDrawList()->AddText(fs, 12.0f, ImVec2(cp.x + 10.0f, cp.y + 1.0f), C(th::kTextDim), "—");
                    wrapped_tooltip(cell_text(dash::check_name(kDashCols[k]), c));
                }
                ImGui::TableSetColumnIndex(kNCols - 1);
                ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
                ImGui::TextUnformatted(dash::rep_label(m, i).c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    }

    // ── Findings ─────────────────────────────────────────────────────────────
    if (!running && state != DashProbe::State::Failed) {
        section_caption(conform_sel_ >= 0
                            ? std::format("FINDINGS · REPRESENTATION #{} (CLICK IT AGAIN FOR ALL)", conform_sel_).c_str()
                            : "FINDINGS");
        draw_findings(rep.findings, conform_sel_, conform_open_, "Representation", "Representations", cw);

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (ImGui::SmallButton("Copy the report as JSON"))
            ImGui::SetClipboardText(dash::conformance_json(m, res.reads, rep, res.source).c_str());
        if (ImGui::IsItemHovered())
            ui::tooltip("Declarations, measurements and findings per Representation, and every segment's [start,\n"
                        "duration, bytes] — what mirage_dashcheck --json writes");
    }
    end();
}
