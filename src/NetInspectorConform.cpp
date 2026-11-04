// Network inspector — the Conformance tab: does the HLS master declare what
// its media holds? "Check" runs ConformProbe (HlsConformProbe.h) over the
// master playback opened: every variant and audio playlist, the size of
// every segment, a few segments of each read in full — inspector requests,
// listed in Network. hls::analyze_conformance (HlsConform.h) compares; this
// draws a row per EXT-X-STREAM-INF with a cell per check, and the findings
// (only a row's own once it is clicked).

#include "NetInspector.h"

#include "Logger.h"
#include "ManifestParser.h"
#include "NetInspectorUi.h"

#include <fstream>

using namespace netui;

namespace {

// The table's columns after "#" and BANDWIDTH.
constexpr hls::Check kCols[] = {
    hls::Check::AverageBandwidth, hls::Check::Codecs, hls::Check::Resolution, hls::Check::FrameRate,
    hls::Check::VideoRange, hls::Check::SegmentStart, hls::Check::Duration,
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

std::string cell_text(hls::Check k, const hls::Cell& c)
{
    std::string t = std::format("{} — {}", hls::check_name(k), c.checked ? hls::severity_name(c.sev) : "not checked");
    if (!c.declared.empty()) t += "\nDeclared: " + c.declared;
    if (!c.measured.empty()) t += "\nMeasured: " + c.measured;
    if (!c.note.empty()) t += "\n\n" + c.note;
    return t;
}

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

void NetInspector::conform_tick(const Inputs& in)
{
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

    // ── Toolbar: segments read · sizes · check / stop ────────────────────────
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + kToolbarH), C(kSideBar));
    dl->AddLine(ImVec2(p0.x, p0.y + kToolbarH - 0.5f), ImVec2(p0.x + w, p0.y + kToolbarH - 0.5f), C(th::kPanelBorder));
    {
        float cx = p0.x + kPad;
        const float cy = p0.y + (kToolbarH - 22.0f) * 0.5f;
        dl->AddText(fs, 12.0f, ImVec2(cx, cy + 4.0f), C(th::kTextDim), "Read");
        cx += fs->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, "Read").x + 6.0f;
        const struct { int n; const char* label; } segs[] = { { 1, "1" }, { 3, "3" }, { 10, "10" }, { 0, "All" } };
        ImGui::BeginDisabled(running);
        for (const auto& s : segs) {
            ImGui::SetCursorScreenPos(ImVec2(cx, cy));
            ImGui::PushID(s.n + 500);
            if (chip_button("##segs", s.label, conform_segments_ == s.n)) conform_segments_ = s.n;
            ImGui::PopID();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ui::tooltip(s.n == 0 ? "Read every segment of every playlist in full"
                                     : "Segments read in full per variant playlist: the first, the last, evenly between");
            cx = ImGui::GetItemRectMax().x + 4.0f;
        }
        ImGui::SetCursorScreenPos(ImVec2(cx + 6.0f, cy));
        if (chip_button("##sizes", "Size all", conform_sizes_)) conform_sizes_ = !conform_sizes_;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ui::tooltip("Size every segment for BANDWIDTH's peak: EXT-X-BYTERANGE lengths, else a request for its first\n"
                        "byte (the size comes back in Content-Range). Off: only the segments read are sized, and the\n"
                        "peak is a lower bound.");
        ImGui::EndDisabled();

        const std::string label = running ? std::string("Stop")
                                : state == ConformProbe::State::Idle ? std::string("Check") : std::string("Check again");
        const float bw = fs->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, label.c_str()).x + 24.0f;
        ImGui::SetCursorScreenPos(ImVec2(p0.x + w - kPad - bw, cy));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
        ImGui::BeginDisabled(!ready && !running);
        if (ImGui::Button(label.c_str(), ImVec2(bw, 22.0f))) {
            if (running) conform_.stop();
            else start_conform(in);
        }
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ui::tooltip(running ? "Stop: what was read so far is compared"
                                : "Read every variant and audio playlist, size their segments, read some in full and\n"
                                  "compare with the master's declarations. Listed in Network as initiator \"inspector\".");
    }

    const float body_top = p0.y + kToolbarH;
    ImGui::SetCursorScreenPos(ImVec2(p0.x, body_top));
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
                         "CODECS, RESOLUTION, FRAME-RATE, VIDEO-RANGE — with what its segments hold.",
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
            std::string verdict;
            hls::Severity vs = hls::Severity::Ok;
            int notes = 0;
            for (const auto& f : rep.findings) notes += f.sev == hls::Severity::Info ? 1 : 0;
            if (state == ConformProbe::State::Failed) {
                verdict = "Could not check: " + res.error;
                vs = hls::Severity::Error;
            } else if (rep.errors > 0) {
                verdict = std::format("{} error{}{}", rep.errors, rep.errors == 1 ? "" : "s",
                                      rep.warnings ? std::format(", {} warning{}", rep.warnings, rep.warnings == 1 ? "" : "s")
                                                   : std::string());
                vs = hls::Severity::Error;
            } else if (rep.warnings > 0) {
                verdict = std::format("{} warning{}", rep.warnings, rep.warnings == 1 ? "" : "s");
                vs = hls::Severity::Warning;
            } else {
                verdict = notes ? std::format("Declarations match · {} note{}", notes, notes == 1 ? "" : "s")
                                : std::string("Declarations match");
            }
            if (state == ConformProbe::State::Stopped) verdict += " (stopped: over what was read)";
            sev_icon(cdl, ImVec2(vp.x + 7.0f, vp.y + 10.0f), vs);
            cdl->AddText(ui_fonts().ui, 15.0f, ImVec2(vp.x + 20.0f, vp.y + 1.0f), C(sev_color(vs)), verdict.c_str(),
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
        const float wrap = cw - 26.0f;
        int shown = 0;
        for (int i = 0; i < static_cast<int>(rep.findings.size()); ++i) {
            const auto& f = rep.findings[static_cast<std::size_t>(i)];
            if (conform_sel_ >= 0 && !names(f, conform_sel_)) continue;
            ++shown;
            const bool open = conform_open_ == i;
            const std::string title = f.renditions.empty() ? f.title
                : std::format("{}   · {} {}", f.title, f.renditions.size() == 1 ? "variant" : "variants",
                              hls::format_list(f.renditions));
            const ImVec2 fp = ImGui::GetCursorScreenPos();
            const ImVec2 ts = fs->CalcTextSizeA(13.0f, FLT_MAX, wrap, title.c_str());
            const ImVec2 ds = open && !f.detail.empty() ? fs->CalcTextSizeA(12.0f, FLT_MAX, wrap, f.detail.c_str()) : ImVec2(0, 0);
            const float fh = 8.0f + ts.y + (open && !f.detail.empty() ? ds.y + 6.0f : 0.0f) + 6.0f;
            ImGui::PushID(i + 700);
            const bool clicked = ImGui::InvisibleButton("##cf", ImVec2(cw, fh));
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            if (clicked) conform_open_ = open ? -1 : i;
            cdl->AddRectFilled(fp, ImVec2(fp.x + cw, fp.y + fh),
                               C(open ? th::rgba(255, 255, 255, 0.06f) : hov ? th::rgba(255, 255, 255, 0.04f)
                                                                            : th::rgba(255, 255, 255, 0.02f)), 4.0f);
            cdl->AddRectFilled(fp, ImVec2(fp.x + 2.5f, fp.y + fh), C(th::alpha(sev_color(f.sev), 0.8f)), 1.0f);
            sev_icon(cdl, ImVec2(fp.x + 13.0f, fp.y + 4.0f + 8.5f), f.sev);
            cdl->AddText(fs, 13.0f, ImVec2(fp.x + 24.0f, fp.y + 5.0f), C(th::kText), title.c_str(), nullptr, wrap);
            if (open && !f.detail.empty())
                cdl->AddText(fs, 12.0f, ImVec2(fp.x + 24.0f, fp.y + 9.0f + ts.y), C(th::kTextDim), f.detail.c_str(),
                             nullptr, wrap);
            if (hov && !open && !f.detail.empty()) ui::tooltip("Click for details");
            ImGui::Dummy(ImVec2(0.0f, 1.0f));
        }
        if (shown == 0) ImGui::TextDisabled("Nothing to report.");

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (ImGui::SmallButton("Copy the report as JSON"))
            ImGui::SetClipboardText(hls::conformance_json(res.master, res.playlists, rep, res.source).c_str());
        if (ImGui::IsItemHovered())
            ui::tooltip("Declarations, measurements and findings per variant, and every playlist read — what\n"
                        "mirage_hlscheck --json writes");
    }
    end();
}
