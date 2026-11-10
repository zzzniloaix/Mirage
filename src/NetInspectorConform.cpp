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
// demuxer), so that check needs nothing playing.

#include "NetInspector.h"

#include "Logger.h"
#include "ManifestParser.h"
#include "NetInspectorUi.h"

#include <cctype>
#include <cmath>
#include <fstream>

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
                   float cw)
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
        const std::string title = f.renditions.empty() ? f.title
            : std::format("{}   · {} {}", f.title, f.renditions.size() == 1 ? one : many, hls::format_list(f.renditions));
        const ImVec2 fp = ImGui::GetCursorScreenPos();
        const ImVec2 ts = fs->CalcTextSizeA(13.0f, FLT_MAX, wrap, title.c_str());
        const ImVec2 ds = is_open && !f.detail.empty() ? fs->CalcTextSizeA(12.0f, FLT_MAX, wrap, f.detail.c_str()) : ImVec2(0, 0);
        const float fh = 8.0f + ts.y + (is_open && !f.detail.empty() ? ds.y + 6.0f : 0.0f) + 6.0f;
        ImGui::PushID(i + 700);
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

} // namespace

bool NetInspector::conform_ready(const Inputs& in) const
{
    return in.manifest && !in.manifest->text().empty() && !in.manifest->variants().empty()
        && !ManifestParser::is_dash(in.media_url);
}

void NetInspector::start_conform(const Inputs& in)
{
    if (!conform_ready(in)) return;
    ConformProbe::Options o;
    o.segments = conform_segments_;
    o.sizes = conform_sizes_;
    conform_.start(in.media_url, in.manifest->text(), in.manifest->final_url(), o);
    conform_sel_ = conform_open_ = -1;
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
    if (qa_conform_ && !qa_conform_started_ && conform_ready(in)) {
        qa_conform_started_ = true;
        start_conform(in);
    }
    if (conform_.version() == conform_ver_) return;
    conform_ver_ = conform_.version();
    conform_res_ = conform_.result();
    if (!qa_conform_out_.empty() && conform_.state() == ConformProbe::State::Done) {
        std::ofstream f(qa_conform_out_);
        f << hls::conformance_json(conform_res_.master, conform_res_.playlists, conform_res_.report, conform_res_.source);
        logger::info("conform_out: wrote {}", qa_conform_out_);
        qa_conform_out_.clear();
    }
}

void NetInspector::draw_conformance(const Inputs& in, Out& out, float w, float h)
{
    (void)out;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool ready = conform_ready(in);
    const ConformProbe::State state = conform_.state();
    const bool running = state == ConformProbe::State::Running;
    const ConformProbe::Result& res = conform_res_;
    const hls::ConformReport& rep = res.report;
    ImFont* fs = ui_fonts().ui_small;
    const bool dash = conform_dash_;
    const ConformProbe::State shown_state = dash ? dash_.state() : state;
    const bool busy = shown_state == ConformProbe::State::Running;
    const float bar_h = dash ? 2.0f * kToolbarH : kToolbarH;   // DASH: the MPD's URL on a row of its own

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
                                    : "Check the HLS master playback opened");
            cx = ImGui::GetItemRectMax().x + 4.0f;
        }
        cx += 10.0f;
        dl->AddText(fs, 12.0f, ImVec2(cx, cy + 4.0f), C(th::kTextDim), "Read");
        cx += fs->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, "Read").x + 6.0f;
        const struct { int n; const char* label; } segs[] = { { 1, "1" }, { 3, "3" }, { 10, "10" }, { 0, "All" } };
        ImGui::BeginDisabled(busy);
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

        if (dash) {
            ImGui::SetCursorScreenPos(ImVec2(p0.x + kPad, p0.y + kToolbarH + (kToolbarH - 22.0f) * 0.5f));
            ImGui::SetNextItemWidth(w - 2.0f * kPad);
            ImGui::BeginDisabled(busy);
            if (ImGui::InputTextWithHint("##dash_url", "MPD URL or path (Enter checks it)", dash_url_, sizeof(dash_url_),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
                start_dash();
            ImGui::EndDisabled();
        }
    }

    const float body_top = p0.y + bar_h;
    ImGui::SetCursorScreenPos(ImVec2(p0.x, body_top));
    if (dash) return draw_dash(w, h - bar_h);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad + 2.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::BeginChild("##conformance", ImVec2(w, h - kToolbarH), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImDrawList* cdl = ImGui::GetWindowDrawList();
    const float cw = ImGui::GetContentRegionAvail().x;
    auto end = [&] {
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    };

    if (!ready && state == ConformProbe::State::Idle) {
        draw_empty_state("Not an HLS master playlist",
                         "Conformance compares what an HLS master playlist declares for each variant — BANDWIDTH, "
                         "CODECS, RESOLUTION, FRAME-RATE, VIDEO-RANGE — with what its segments hold. For a DASH MPD, "
                         "choose DASH MPD above.",
                         cw, h - kToolbarH - 30.0f);
        return end();
    }
    if (state == ConformProbe::State::Idle) {
        draw_empty_state("Declared vs measured",
                         "Check reads every variant and audio playlist, sizes their segments and reads a few of each "
                         "in full, then compares each EXT-X-STREAM-INF with what the media holds.",
                         cw, h - kToolbarH - 30.0f);
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
            const Verdict v = verdict_of(state, res.error, rep.errors, rep.warnings, rep.findings);
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
    if (!vars.empty()) {
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
                const bool sel = conform_sel_ == i, playing = i == in.playing_variant;
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
        section_caption(conform_sel_ >= 0 ? std::format("FINDINGS · VARIANT #{} (CLICK IT AGAIN FOR ALL)", conform_sel_).c_str()
                                          : "FINDINGS");
        draw_findings(rep.findings, conform_sel_, conform_open_, "variant", "variants", cw);

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (ImGui::SmallButton("Copy the report as JSON"))
            ImGui::SetClipboardText(hls::conformance_json(res.master, res.playlists, rep, res.source).c_str());
        if (ImGui::IsItemHovered())
            ui::tooltip("Declarations, measurements and findings per variant, and every playlist read — what\n"
                        "mirage_hlscheck --json writes");
    }
    end();
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
