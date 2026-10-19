// Network inspector — the Response tab of the details pane: the body the
// player read for the selected request (NetworkLogger keeps it). Playlists
// show as text with line numbers and tag colouring, or as a table of their
// segments with playlist times; segment heads as their start timestamps,
// box outline and a hex dump.

#include "NetInspector.h"

#include "NetInspectorUi.h"

#include <cstring>

using namespace netui;

namespace {

constexpr float kToolH   = 32.0f;
constexpr float kLineH   = 17.0f;
constexpr float kMonoSz  = 12.5f;

bool looks_like_text(const std::string& d)
{
    const std::size_t n = std::min<std::size_t>(d.size(), 4096);
    if (n == 0) return false;
    std::size_t bad = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto c = static_cast<unsigned char>(d[i]);
        if (c < 0x09 || (c > 0x0d && c < 0x20)) ++bad;
    }
    return bad * 50 < n;
}

// One playlist line, coloured: tag name, attribute names / values, URIs, comments.
void draw_text_line(ImDrawList* dl, ImFont* f, ImVec2 p, std::string_view line, bool playlist)
{
    auto put = [&](std::string_view s, ImU32 col) {
        if (s.empty()) return;
        dl->AddText(f, kMonoSz, p, C(col), s.data(), s.data() + s.size());
        p.x += f->CalcTextSizeA(kMonoSz, FLT_MAX, 0.0f, s.data(), s.data() + s.size()).x;
    };
    if (!playlist) { put(line, th::kText); return; }
    if (line.starts_with("#EXT")) {
        const auto colon = line.find(':');
        put(line.substr(0, colon == std::string_view::npos ? line.size() : colon + 1), kColManifest);
        if (colon == std::string_view::npos) return;
        std::string_view rest = line.substr(colon + 1);
        // NAME=value,NAME="quoted, value"
        while (!rest.empty()) {
            const auto eq = rest.find('=');
            const auto comma = rest.find(',');
            if (eq == std::string_view::npos || (comma != std::string_view::npos && comma < eq)) {
                const std::size_t n = comma == std::string_view::npos ? rest.size() : comma + 1;
                put(rest.substr(0, n), th::kText);
                rest.remove_prefix(n);
                continue;
            }
            put(rest.substr(0, eq + 1), th::kTextDim);
            rest.remove_prefix(eq + 1);
            std::size_t n;
            if (!rest.empty() && rest[0] == '"') {
                const auto q = rest.find('"', 1);
                n = q == std::string_view::npos ? rest.size() : q + 1;
            } else {
                const auto c = rest.find(',');
                n = c == std::string_view::npos ? rest.size() : c;
            }
            put(rest.substr(0, n), th::kWarn);
            rest.remove_prefix(n);
            if (!rest.empty() && rest[0] == ',') { put(",", th::kTextDim); rest.remove_prefix(1); }
        }
    } else if (line.starts_with("#")) {
        put(line, th::kTextFaint);
    } else {
        put(line, th::kAccentHover);
    }
}

std::string hex_line(const std::string& d, std::size_t off)
{
    std::string s = std::format("{:08x}  ", off);
    for (std::size_t i = 0; i < 16; ++i) {
        if (off + i < d.size()) s += std::format("{:02x} ", static_cast<unsigned char>(d[off + i]));
        else s += "   ";
        if (i == 7) s += ' ';
    }
    s += ' ';
    for (std::size_t i = 0; i < 16 && off + i < d.size(); ++i) {
        const auto c = static_cast<unsigned char>(d[off + i]);
        s += (c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '.';
    }
    return s;
}

std::string no_body_reason(const NetRequest& r, NetKind kind)
{
    if (r.body_dropped) return "Dropped to keep response bodies under 96 MB in all; newer ones are kept.";
    if (r.failed)       return "The request failed: nothing to show.";
    if (kind == NetKind::Key) return "Decryption keys are never recorded.";
    if (r.protocol == "file" && kind == NetKind::File) return "A local file: there is no HTTP response.";
    if (!r.done())      return "Waiting for the response body…";
    if (kind == NetKind::Playlist && (r.initiator == NetInitiator::Thumbnails || r.initiator == NetInitiator::Scrub))
        return "Not recorded for the thumbnail and scrub decoders' own playlist opens (FFmpeg probes that input "
               "before reading it). The same playlist as playback fetched it is in the list with its body.";
    return "No body was recorded. Bodies are kept for what the HLS demuxer reads (playlists, segments, "
           "init sections) and for manifest fetches — not for a progressive file's own input.";
}

} // namespace

void NetInspector::draw_response(const Row& row, float w, float h)
{
    const NetRequest& r = row.r;
    ImFont* mono = ui_fonts().mono ? ui_fonts().mono : ui_fonts().ui_small;

    // ── Refresh the parsed view when the row or its body changes ────────────
    if (body_.id != r.id || body_.bytes != r.body_bytes) {
        const bool same = body_.id == r.id;
        body_ = {};
        body_.id = r.id;
        body_.bytes = r.body_bytes;
        if (!same) find_cursor_ = -1;
        if (r.body_bytes > 0 && sync_log_) sync_log_->copy_body(r.id, body_.data);
        const NetKind k = row.d.kind;
        body_.text = k == NetKind::Manifest || k == NetKind::Playlist || k == NetKind::Subtitle
                  || (k != NetKind::Segment && k != NetKind::Init && looks_like_text(body_.data));
        if (body_.text) {
            body_.lines.push_back(0);
            for (std::size_t i = 0; i < body_.data.size(); ++i)
                if (body_.data[i] == '\n' && i + 1 < body_.data.size()) body_.lines.push_back(static_cast<std::uint32_t>(i + 1));
            if (k == NetKind::Playlist) body_.tl = hls::parse_media_timeline(body_.data, r.url);
        } else if (!body_.data.empty()) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(body_.data.data());
            const std::size_t n = body_.data.size();
            if (k == NetKind::Init) {
                const hls::InitInfo info = hls::parse_init_segment(p, n);
                for (const auto& t : info.tracks)
                    body_.media_lines.push_back(std::format(
                        "track {}: {} {} · timescale {}{}", t.id, hls::track_name(t.kind), t.codec, t.timescale,
                        t.edit_shift && t.timescale
                            ? std::format(" · edit list shifts it {}", hls::format_seconds(-static_cast<double>(t.edit_shift) / t.timescale))
                            : std::string()));
                if (!info.ok) body_.media_lines.push_back("Not an fMP4 init segment (no moov with tracks).");
            } else {
                const hls::SegmentStart st = hls::parse_segment_start(p, n, init_for(row));
                for (const auto& t : st.tracks) {
                    std::string line = std::format("{}{}{}", hls::track_name(t.kind),
                                                   t.codec.empty() ? "" : " " + t.codec,
                                                   st.format == hls::SegmentStart::Format::Ts
                                                       ? std::format(" (PID 0x{:x})", t.id)
                                                       : st.format == hls::SegmentStart::Format::Fmp4
                                                           ? std::format(" (track {})", t.id) : std::string());
                    if (!std::isnan(t.local_zero))
                        line += std::format(": cue time 0 = media {}", hls::format_seconds(t.local_zero));
                    if (!std::isnan(t.start))
                        line += std::format("{} starts at {}", std::isnan(t.local_zero) ? ":" : ";",
                                            hls::format_clock(t.start));
                    if (!std::isnan(t.decode_start) && std::fabs(t.decode_start - t.start) > 1e-6)
                        line += std::format(" (decode {})", hls::format_clock(t.decode_start));
                    if (!t.note.empty()) line += " — " + t.note;
                    body_.media_lines.push_back(std::move(line));
                }
                if (!st.error.empty() && !st.ok()) body_.media_lines.push_back(st.error);
                body_.media = std::format("{}{}", hls::format_name(st.format),
                                          st.fragment_seq ? std::format(" · fragment #{}", st.fragment_seq) : std::string());
            }
            if (const std::string boxes = hls::describe_boxes(p, n); !boxes.empty())
                body_.media_lines.push_back("boxes: " + boxes);
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();

    // ── Toolbar: what was kept · mode · find · copy ──────────────────────────
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + kToolH), C(kSideBar));
    dl->AddLine(ImVec2(p0.x, p0.y + kToolH - 0.5f), ImVec2(p0.x + w, p0.y + kToolH - 0.5f), C(th::kPanelBorder));
    const bool have = !body_.data.empty();
    float right = p0.x + w - kPad;
    if (have) {
        ImGui::SetCursorScreenPos(ImVec2(right - 46.0f, p0.y + 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        if (ImGui::Button("Copy##body", ImVec2(46.0f, 0.0f)))
            ImGui::SetClipboardText(body_.data.c_str());
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) ui::tooltip(body_.text ? "Copy the response text" : "Copy the kept bytes (binary)");
        right -= 52.0f;
    }
    const bool can_parse = body_.tl.ok;
    if (!can_parse && resp_mode_ == 1) resp_mode_ = 0;
    if (can_parse) {
        const char* labels[] = { "Raw", "Segments" };
        float bx = right;
        for (int i = 1; i >= 0; --i) {
            const float bw = ui_fonts().ui_small->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, labels[i]).x + 20.0f;
            bx -= bw + 4.0f;
        }
        right = bx - 4.0f;
        for (int i = 0; i < 2; ++i) {
            ImGui::SetCursorScreenPos(ImVec2(bx, p0.y + 5.0f));
            ImGui::PushID(200 + i);
            if (chip_button("##mode", labels[i], resp_mode_ == i)) resp_mode_ = i;
            ImGui::PopID();
            bx = ImGui::GetItemRectMax().x + 4.0f;
        }
    }
    // Find (raw text only).
    if (have && body_.text && resp_mode_ == 0 && right - (p0.x + kPad) > 260.0f) {
        const float fw = std::min(170.0f, right - (p0.x + kPad) - 150.0f);
        ImGui::SetCursorScreenPos(ImVec2(right - fw, p0.y + 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        ImGui::SetNextItemWidth(fw);
        if (ImGui::InputTextWithHint("##find", "Find", find_, sizeof(find_), ImGuiInputTextFlags_EnterReturnsTrue)) {
            find_scroll_ = true;
            ++find_cursor_;   // Enter: next match
        }
        if (ImGui::IsItemEdited()) { find_cursor_ = 0; find_scroll_ = true; }
        ImGui::PopStyleVar();
        right -= fw + 6.0f;
    }
    // Size line.
    {
        std::string info;
        if (!have) {
            info = r.body_bytes == 0 && !r.done() && !r.failed ? "Waiting…" : "No body";
        } else {
            const std::string kept = net_format_bytes(r.body_bytes);
            const std::int64_t total = r.content_length > 0 ? r.content_length : r.bytes;
            if (!r.body_truncated && body_.text)
                info = std::format("{} · {} lines", kept, body_.lines.size());
            else if (!r.body_truncated)
                info = r.partial || (total > 0 && r.body_bytes < total)
                     ? std::format("{} · all the player read{}", kept, total > 0 ? " of " + net_format_bytes(total) : "")
                     : std::format("{} · the whole response", kept);
            else if (body_.text)
                info = std::format("first {} (cap reached)", kept);
            else
                info = std::format("first {}{} · head kept for timestamps", kept,
                                   total > 0 ? " of " + net_format_bytes(total) : std::string());
        }
        ImFont* fs = ui_fonts().ui_small;
        std::string shown = info;
        while (shown.size() > 6 && fs->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, shown.c_str()).x > right - (p0.x + kPad) - 6.0f)
            shown = shown.substr(0, shown.size() - 4) + "…";
        dl->AddText(fs, 12.0f, ImVec2(p0.x + kPad, std::round(p0.y + (kToolH - 14.0f) * 0.5f)), C(th::kTextDim), shown.c_str());
        if (shown != info) {
            ImGui::SetCursorScreenPos(ImVec2(p0.x + kPad, p0.y));
            ImGui::InvisibleButton("##info", ImVec2(std::max(1.0f, right - (p0.x + kPad) - 6.0f), kToolH));
            if (ImGui::IsItemHovered()) ui::tooltip(info.c_str());
        }
    }

    // ── Body ─────────────────────────────────────────────────────────────────
    const float body_h = std::max(10.0f, h - kToolH);
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + kToolH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));
    ImGui::BeginChild("##net_response", ImVec2(w, body_h), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* cdl = ImGui::GetWindowDrawList();

    if (!have) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", no_body_reason(r, row.d.kind).c_str());
        ImGui::PopTextWrapPos();
    } else if (body_.text && resp_mode_ == 0) {
        // Raw text: line numbers + colouring, matches highlighted.
        const int n_lines = static_cast<int>(body_.lines.size());
        const std::string_view all(body_.data);
        auto line_at = [&](int i) {
            const std::size_t a = body_.lines[static_cast<std::size_t>(i)];
            std::size_t b = i + 1 < n_lines ? body_.lines[static_cast<std::size_t>(i) + 1] : all.size();
            while (b > a && (all[b - 1] == '\n' || all[b - 1] == '\r')) --b;
            return all.substr(a, b - a);
        };
        const std::string needle = lower(find_);
        std::vector<int> hits;
        if (!needle.empty())
            for (int i = 0; i < n_lines; ++i)
                if (lower(std::string(line_at(i))).find(needle) != std::string::npos) hits.push_back(i);
        int cur = -1;
        if (!hits.empty()) {
            if (find_cursor_ < 0) find_cursor_ = 0;
            find_cursor_ %= static_cast<int>(hits.size());
            cur = hits[static_cast<std::size_t>(find_cursor_)];
        }
        const float gutter = mono->CalcTextSizeA(kMonoSz, FLT_MAX, 0.0f, std::to_string(n_lines).c_str()).x + 14.0f;
        std::size_t longest = 0;
        for (int i = 0; i < n_lines; ++i) longest = std::max(longest, line_at(i).size());
        const float content_w = gutter + mono->CalcTextSizeA(kMonoSz, FLT_MAX, 0.0f, "M").x * static_cast<float>(longest) + 20.0f;
        const bool playlist = row.d.kind == NetKind::Playlist || row.d.kind == NetKind::Manifest
                           || body_.data.starts_with("#EXTM3U");
        if (find_scroll_ && cur >= 0) {
            ImGui::SetScrollY(std::max(0.0f, cur * kLineH - body_h * 0.4f));
            find_scroll_ = false;
        }
        ImGuiListClipper clip;
        clip.Begin(n_lines, kLineH);
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const ImVec2 lp = ImGui::GetCursorScreenPos();
                ImGui::PushID(i);
                ImGui::InvisibleButton("##ln", ImVec2(std::max(content_w, ImGui::GetContentRegionAvail().x), kLineH));
                const bool hov = ImGui::IsItemHovered();
                ImGui::PopID();
                const bool hit = !hits.empty() && std::binary_search(hits.begin(), hits.end(), i);
                if (i == cur || hit || hov)
                    cdl->AddRectFilled(lp, ImVec2(lp.x + std::max(content_w, w), lp.y + kLineH),
                                       C(i == cur ? th::alpha(th::kWarn, 0.28f) : hit ? th::alpha(th::kWarn, 0.12f)
                                                                                     : th::rgba(255, 255, 255, 0.04f)));
                const std::string num = std::to_string(i + 1);
                const float nw = mono->CalcTextSizeA(kMonoSz, FLT_MAX, 0.0f, num.c_str()).x;
                cdl->AddText(mono, kMonoSz, ImVec2(lp.x + gutter - 10.0f - nw, lp.y + 1.0f), C(th::kTextFaint), num.c_str());
                const std::string_view ln = line_at(i);
                draw_text_line(cdl, mono, ImVec2(lp.x + gutter, lp.y + 1.0f), ln, playlist);
                if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    ImGui::SetClipboardText(std::string(ln).c_str());
            }
        }
        if (!needle.empty()) {
            // Match count, pinned bottom-right of the pane.
            const std::string m = hits.empty() ? "no match" : std::format("{} / {}  (Enter: next)", find_cursor_ + 1, hits.size());
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 ws = ImGui::GetWindowSize();
            ImFont* fs = ui_fonts().ui_small;
            const ImVec2 ts = fs->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, m.c_str());
            const ImVec2 a(wp.x + ws.x - ts.x - 22.0f, wp.y + ws.y - ts.y - 16.0f);
            cdl->AddRectFilled(ImVec2(a.x - 6.0f, a.y - 3.0f), ImVec2(a.x + ts.x + 6.0f, a.y + ts.y + 3.0f),
                               C(th::rgba(30, 32, 38, 0.95f)), 4.0f);
            cdl->AddText(fs, 12.0f, a, C(hits.empty() ? th::kError : th::kText), m.c_str());
        }
    } else if (body_.text) {
        // Parsed media playlist: one row per segment.
        const auto& tl = body_.tl;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 2.0f));
        ImGui::TextDisabled("%s", std::format("{} segments · {} · target {:g} s · media sequence {}{}{}{}",
                                              tl.segments.size(), hls::format_clock(tl.total), tl.target, tl.media_seq,
                                              tl.discontinuities ? std::format(" · {} discontinuities", tl.discontinuities) : "",
                                              tl.endlist ? " · ENDLIST" : " · live",
                                              tl.has_pdt() ? " · PROGRAM-DATE-TIME" : "").c_str());
        for (const auto& pr : tl.problems) {
            ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kWarn));
            ImGui::TextWrapped("%s", pr.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        constexpr ImGuiTableFlags kT = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY
            | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Resizable;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(5.0f, 2.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, th::vec4(th::rgba(255, 255, 255, 0.025f)));
        if (ImGui::BeginTable("##segs", 5, kT, ImVec2(0.0f, std::max(60.0f, ImGui::GetContentRegionAvail().y)))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#",        ImGuiTableColumnFlags_WidthFixed, 46.0f);
            ImGui::TableSetupColumn("Start",    ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("EXTINF",   ImGuiTableColumnFlags_WidthFixed, 58.0f);
            ImGui::TableSetupColumn("Segment",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Got",      ImGuiTableColumnFlags_WidthFixed, 38.0f);
            ImGui::TableHeadersRow();
            ImGuiListClipper clip;
            clip.Begin(static_cast<int>(tl.segments.size()), 19.0f);
            while (clip.Step()) {
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                    const auto& sg = tl.segments[static_cast<std::size_t>(i)];
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 19.0f);
                    if (sg.discontinuity) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, C(th::alpha(th::kWarn, 0.12f)));
                    ImGui::TableSetColumnIndex(0);
                    const Row* got = find_body_row(sg.uri, sg.range.offset);
                    const Row* any = got;
                    if (!any)
                        for (auto it = rows_.rbegin(); it != rows_.rend(); ++it)
                            if (it->r.url == sg.uri && (sg.range.whole() ? it->r.range_start <= 0
                                                                         : it->r.range_start == sg.range.offset)) {
                                any = &*it;
                                break;
                            }
                    ImGui::PushID(i);
                    if (ImGui::Selectable(std::to_string(sg.seq).c_str(), false,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap) && any) {
                        selected_ = any->r.id;    // jump to that request
                        detail_tab_ = 0;
                    }
                    const bool hov = ImGui::IsItemHovered();
                    ImGui::PopID();
                    if (hov)
                        ui::tooltip(any ? "Click to show this segment's request"
                                        : "Not requested yet — the player has not fetched this segment");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(hls::format_clock(sg.start).c_str());
                    ImGui::TableSetColumnIndex(2);
                    text_right(std::format("{:.3f}", sg.duration), th::kText);
                    ImGui::TableSetColumnIndex(3);
                    std::string name = net_request_name(sg.uri);
                    if (!sg.range.whole()) name += std::format(" @{}", sg.range.offset);
                    ImGui::TextUnformatted(name.c_str());
                    std::string flags;
                    if (sg.discontinuity) flags += " DISC";
                    if (sg.gap) flags += " GAP";
                    if (sg.pdt_explicit) flags += " PDT";
                    if (!sg.key.empty()) flags += " " + sg.key;
                    if (!flags.empty()) {
                        ImGui::SameLine(0.0f, 6.0f);
                        ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(sg.discontinuity ? th::kWarn : th::kTextDim));
                        ImGui::TextUnformatted(flags.c_str() + 1);
                        ImGui::PopStyleColor();
                    }
                    ImGui::TableSetColumnIndex(4);
                    if (any) {
                        const bool err = is_error(any->r);
                        ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(err ? th::kError : th::kTextDim));
                        ImGui::TextUnformatted(status_text(any->r).c_str());
                        ImGui::PopStyleColor();
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    } else {
        // Binary: what the head says, then a hex dump.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 3.0f));
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        if (!body_.media.empty()) section_caption(std::format("MEDIA · {}", body_.media).c_str());
        else if (!body_.media_lines.empty()) section_caption("MEDIA");
        ImGui::PushTextWrapPos(0.0f);
        for (const auto& ln : body_.media_lines) ImGui::TextUnformatted(ln.c_str());
        ImGui::PopTextWrapPos();
        section_caption("BYTES");
        ImGui::PopStyleVar();
        const std::size_t n = body_.data.size();
        const int rows = static_cast<int>((n + 15) / 16);
        ImGuiListClipper clip;
        clip.Begin(rows, kLineH);
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const ImVec2 lp = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(mono->CalcTextSizeA(kMonoSz, FLT_MAX, 0.0f, "M").x * 78.0f, kLineH));
                const std::string s = hex_line(body_.data, static_cast<std::size_t>(i) * 16);
                cdl->AddText(mono, kMonoSz, ImVec2(lp.x, lp.y + 1.0f), C(th::kTextFaint), s.c_str(), s.c_str() + 10);
                cdl->AddText(mono, kMonoSz, ImVec2(lp.x + mono->CalcTextSizeA(kMonoSz, FLT_MAX, 0.0f, s.c_str(), s.c_str() + 10).x,
                                                   lp.y + 1.0f),
                             C(th::kText), s.c_str() + 10);
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}
