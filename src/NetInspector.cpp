#include "NetInspector.h"

#include "ManifestParser.h"
#include "NetworkLogger.h"
#include "UiFonts.h"
#include "UiTheme.h"
#include "UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <ctime>
#include <format>

namespace th = ui::theme;

namespace {

// ── Palette (DevTools-inspired, tuned to the player theme) ───────────────────
constexpr ImU32 kSideBg      = th::rgba( 15,  16,  20, 0.985f);
constexpr ImU32 kSideBar     = th::rgba(255, 255, 255, 0.035f);   // toolbar / header strips
constexpr ImU32 kColVideo    = th::kAccent;
constexpr ImU32 kColAudio    = th::rgba(167, 139, 250, 1.00f);    // violet-400
constexpr ImU32 kColSubs     = th::kWarn;
constexpr ImU32 kColManifest = th::rgba( 52, 211, 153, 1.00f);    // emerald-400
constexpr ImU32 kColOther    = th::rgba(148, 163, 184, 1.00f);    // slate-400
constexpr ImU32 kWfConnect   = th::rgba(251, 146,  60, 1.00f);    // orange — DNS/TCP/TLS + queue
constexpr ImU32 kWfWait      = th::rgba( 52, 211, 153, 1.00f);    // green — waiting (TTFB)
constexpr ImU32 kWfDownload  = th::rgba( 56, 189, 248, 1.00f);    // blue — content download
constexpr ImU32 kSelRow      = th::rgba( 56, 189, 248, 0.22f);
constexpr ImU32 kPlayRow     = th::rgba( 56, 189, 248, 0.14f);

constexpr float kHeaderH  = 40.0f;
constexpr float kToolbarH = 36.0f;
constexpr float kChipsH   = 32.0f;
constexpr float kFooterH  = 26.0f;
constexpr float kSplitH   = 6.0f;
constexpr float kPad      = 10.0f;
constexpr float kMinW     = 300.0f;

enum Col { ColName, ColStatus, ColType, ColInit, ColSize, ColTime, ColWaterfall, ColCount };

ImU32 C(ImU32 c, float mul = 1.0f) { return ImGui::GetColorU32(c, mul); }

ImU32 media_color(const NetDesc& d)
{
    if (d.kind == NetKind::Manifest || d.kind == NetKind::Playlist) return kColManifest;
    switch (d.media) {
        case NetMedia::Video: return kColVideo;
        case NetMedia::Audio: return kColAudio;
        case NetMedia::Subs:  return kColSubs;
        case NetMedia::None:  break;
    }
    return kColOther;
}

const char* media_name(NetMedia m)
{
    switch (m) {
        case NetMedia::Video: return "video";
        case NetMedia::Audio: return "audio";
        case NetMedia::Subs:  return "subtitles";
        case NetMedia::None:  break;
    }
    return "";
}

bool is_error(const NetRequest& r) { return r.failed || r.status >= 400; }

bool is_helper(const NetRequest& r)
{
    return r.initiator == NetInitiator::Thumbnails || r.initiator == NetInitiator::Scrub;
}

std::string lower(std::string s)
{
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string clock_text(std::int64_t t_us)
{
    const std::int64_t ms = NetworkLogger::wall_ms(t_us);
    const std::time_t  t  = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    localtime_r(&t, &tm);
    return std::format("{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms % 1000));
}

std::string partial_note(const NetRequest& r)
{
    const std::string of = r.content_length > 0 ? std::format(" of {}", net_format_bytes(r.content_length)) : "";
    return std::format("Partial: {}{} read. The player stopped reading this response before its end "
                       "(a variant probed at startup or no longer selected, or a full playback buffer). "
                       "Time runs to the last byte; the row continues if reading resumes.",
                       net_format_bytes(std::max<std::int64_t>(r.bytes, 0)), of);
}

std::string status_text(const NetRequest& r)
{
    if (r.status > 0) return std::to_string(r.status);
    if (r.failed)     return "failed";
    if (r.protocol == "file") return "—";
    return r.done() ? "—" : "…";
}

// Pill-shaped toggle chip; returns true when clicked.
bool chip_button(const char* id, const char* label, bool active, ImU32 dot = 0)
{
    ImFont* f = ui_fonts().ui_small;
    const float fs  = 12.0f;
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
    const float dot_w = dot ? 12.0f : 0.0f;
    const ImVec2 sz(std::round(ts.x + 20.0f + dot_w), 22.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, sz);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + sz.x, p.y + sz.y);
    dl->AddRectFilled(p, q, C(active ? th::alpha(th::kAccent, 0.20f) : hov ? th::kHoverBg : th::rgba(255, 255, 255, 0.05f)),
                      sz.y * 0.5f);
    if (active) dl->AddRect(p, q, C(th::alpha(th::kAccent, 0.55f)), sz.y * 0.5f, 0, 1.0f);
    float x = p.x + 10.0f;
    if (dot) {
        dl->AddCircleFilled(ImVec2(x + 3.5f, p.y + sz.y * 0.5f), 3.5f, C(dot), 12);
        x += dot_w;
    }
    dl->AddText(f, fs, ImVec2(x, std::round(p.y + (sz.y - ts.y) * 0.5f)),
                C(active ? th::kAccentHover : hov ? th::kText : th::kTextDim), label);
    return clicked;
}

// Small square glyph button (× close, clear, record); returns true when clicked.
bool glyph_button(const char* id, const char* tip, float sz, bool active = false)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(sz, sz));
    const bool hov = ImGui::IsItemHovered();
    if (hov || active)
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz),
            C(ImGui::IsItemActive() ? th::kPressBg : hov ? th::kHoverBg : th::rgba(255, 255, 255, 0.06f)),
            th::kRadiusSmall);
    if (hov && tip) ui::tooltip(tip);
    return clicked;
}

void draw_x(ImDrawList* dl, ImVec2 c, float r, ImU32 col)
{
    dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 1.6f);
    dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, 1.6f);
}

// Uppercase section caption.
void section_caption(const char* text)
{
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 20.0f));
    ImGui::GetWindowDrawList()->AddText(ui_fonts().ui_small, 11.5f, ImVec2(p.x, p.y + 4.0f), C(th::kTextDim), text);
}

// Key / value row inside a 2-column table.
void kv_row(const char* key, const std::string& value, ImU32 col = 0)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
    ImGui::TextUnformatted(key);
    ImGui::PopStyleColor();
    ImGui::TableSetColumnIndex(1);
    if (col) ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(col));
    ImGui::TextWrapped("%s", value.c_str());
    if (col) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::SetClipboardText(value.c_str());
}

bool begin_kv_table(const char* id, float key_w)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) return false;
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, key_w);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

// Right-aligned text in the current table cell.
void text_right(const std::string& s, ImU32 col)
{
    const float w = ImGui::CalcTextSize(s.c_str()).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);
    ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(col));
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopStyleColor();
}

void draw_empty_state(const char* title, const char* body, float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImFont* f  = ui_fonts().ui;
    ImFont* fs = ui_fonts().ui_small;
    const float cy = p.y + std::max(40.0f, h * 0.35f);
    const ImVec2 t1 = f->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, title);
    dl->AddText(f, 14.0f, ImVec2(std::round(p.x + (w - t1.x) * 0.5f), cy), C(th::kText), title);
    const float wrap = std::min(w - 40.0f, 320.0f);
    const ImVec2 t2 = fs->CalcTextSizeA(12.0f, FLT_MAX, wrap, body);
    dl->AddText(fs, 12.0f, ImVec2(std::round(p.x + (w - wrap) * 0.5f + (wrap - t2.x) * 0.5f), cy + 24.0f),
                C(th::kTextDim), body, nullptr, wrap);
    ImGui::Dummy(ImVec2(w, h));
}

} // namespace

// ── Public ───────────────────────────────────────────────────────────────────

float NetInspector::dock_width(float win_w) const
{
    const float auto_w = std::clamp(win_w * 0.44f, 360.0f, 640.0f);
    const float want   = width_ > 0.0f ? width_ : auto_w;
    const float max_w  = std::max(kMinW, win_w * 0.65f);
    return std::round(std::clamp(want, std::min(kMinW, win_w * 0.5f), max_w));
}

void NetInspector::reset()
{
    selected_ = 0;
    cat_key_.clear();
    cat_ = {};
    view_dirty_ = true;
}

void NetInspector::qa_apply(const std::string& spec)
{
    std::size_t pos = 0;
    while (pos < spec.size()) {
        const auto comma = spec.find(',', pos);
        const std::string item = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        const auto eq = item.find('=');
        const std::string k = item.substr(0, eq), v = eq == std::string::npos ? "" : item.substr(eq + 1);
        if (k == "tab")      tab_ = (v == "manifest") ? Tab::Manifest : Tab::Network;
        if (k == "select")   qa_select_segment_ = true;
        if (k == "detail")   detail_tab_ = (v == "timing") ? 1 : 0;
        if (k == "filter")   { std::snprintf(filter_, sizeof(filter_), "%s", v.c_str()); filter_lc_ = lower(v); }
        if (k == "width")    width_ = std::strtof(v.c_str(), nullptr);
        if (k == "chip") {
            chip_ = v == "manifest" ? Chip::Manifest : v == "video" ? Chip::Video : v == "audio" ? Chip::Audio
                  : v == "subs" ? Chip::Subs : v == "errors" ? Chip::Errors : Chip::All;
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    view_dirty_ = true;
}

// ── Data sync ────────────────────────────────────────────────────────────────

void NetInspector::sync(const Inputs& in)
{
    // Manifest catalog (rebuilt when the parsed manifest changes).
    if (in.manifest) {
        const std::string key = std::format("{}|{}|{}", in.media_url, in.manifest->variants().size(),
                                            in.manifest->renditions().size());
        if (key != cat_key_) {
            cat_key_ = key;
            cat_ = NetCatalog::build(ManifestParser::is_manifest(in.media_url) ? in.media_url : std::string(),
                                     in.manifest->variants(), in.manifest->renditions());
            for (auto& row : rows_) row.d = net_describe(row.r, cat_);
            view_dirty_ = true;
        }
    }

    NetworkLogger* log = in.log;
    if (!log) return;
    if (log->generation() != gen_) {
        gen_ = log->generation();
        rows_.clear();
        seq_ = 0;
        base_t_ = -1;
        view_dirty_ = true;
    }
    const std::uint64_t target = log->change_seq();
    if (target == seq_) return;

    std::vector<NetRequest> changed;
    std::uint64_t oldest = 0;
    log->copy_changed_since(seq_, changed, oldest);
    seq_ = target;

    // Evicted from the logger's ring buffer → evict here too.
    if (!rows_.empty() && rows_.front().r.id < oldest) {
        auto it = std::lower_bound(rows_.begin(), rows_.end(), oldest,
                                   [](const Row& r, std::uint64_t id) { return r.r.id < id; });
        rows_.erase(rows_.begin(), it);
    }
    for (auto& r : changed) {
        auto it = std::lower_bound(rows_.begin(), rows_.end(), r.id,
                                   [](const Row& row, std::uint64_t id) { return row.r.id < id; });
        if (it != rows_.end() && it->r.id == r.id) {
            it->r = std::move(r);
            it->d = net_describe(it->r, cat_);
        } else {
            Row row;
            row.name = net_request_name(r.url);
            row.r    = std::move(r);
            row.d    = net_describe(row.r, cat_);
            rows_.insert(it, std::move(row));
        }
    }
    if (!rows_.empty() && (base_t_ < 0 || rows_.front().r.t_start < base_t_)) base_t_ = rows_.front().r.t_start;
    view_dirty_ = true;
}

bool NetInspector::row_visible(const Row& row) const
{
    const NetRequest& r = row.r;
    if (hide_helpers_ && is_helper(r)) return false;
    switch (chip_) {
        case Chip::All: break;
        case Chip::Manifest: if (row.d.kind != NetKind::Manifest && row.d.kind != NetKind::Playlist) return false; break;
        case Chip::Video:    if (row.d.media != NetMedia::Video || row.d.kind == NetKind::Playlist) return false; break;
        case Chip::Audio:    if (row.d.media != NetMedia::Audio || row.d.kind == NetKind::Playlist) return false; break;
        case Chip::Subs:     if (row.d.media != NetMedia::Subs && row.d.kind != NetKind::Subtitle) return false; break;
        case Chip::Errors:   if (!is_error(r) && !r.retry_of_failed && r.reconnects == 0) return false; break;
    }
    if (!filter_lc_.empty()) {
        const std::string hay = lower(r.url) + " " + net_kind_name(row.d.kind) + " " + lower(row.d.rendition)
                              + " " + net_initiator_name(r.initiator) + " " + std::to_string(r.status);
        if (filter_lc_.starts_with('-')) {                   // DevTools-style negative filter
            if (filter_lc_.size() > 1 && hay.find(filter_lc_.substr(1)) != std::string::npos) return false;
        } else if (hay.find(filter_lc_) == std::string::npos) {
            return false;
        }
    }
    return true;
}

void NetInspector::rebuild_view()
{
    view_.clear();
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        if (row_visible(rows_[static_cast<std::size_t>(i)])) view_.push_back(i);

    if (sort_col_ >= 0) {
        const std::int64_t now = NetworkLogger::now_us();
        auto key_dur = [now](const NetRequest& r) { return (r.done() ? r.t_end : now) - r.t_start; };
        auto less = [&](int a, int b) {
            const Row& A = rows_[static_cast<std::size_t>(a)];
            const Row& B = rows_[static_cast<std::size_t>(b)];
            switch (sort_col_) {
                case ColName:   if (A.name != B.name) return A.name < B.name; break;
                case ColStatus: if (A.r.status != B.r.status) return A.r.status < B.r.status; break;
                case ColType:   if (A.d.kind != B.d.kind) return A.d.kind < B.d.kind;
                                if (A.d.rendition != B.d.rendition) return A.d.rendition < B.d.rendition; break;
                case ColInit:   if (A.r.initiator != B.r.initiator) return A.r.initiator < B.r.initiator; break;
                case ColSize:   if (A.r.effective_bytes() != B.r.effective_bytes())
                                    return A.r.effective_bytes() < B.r.effective_bytes(); break;
                case ColTime:   if (key_dur(A.r) != key_dur(B.r)) return key_dur(A.r) < key_dur(B.r); break;
                default: break;
            }
            return A.r.id < B.r.id;
        };
        std::stable_sort(view_.begin(), view_.end(), less);
        if (sort_desc_) std::reverse(view_.begin(), view_.end());
    } else if (sort_desc_) {
        std::reverse(view_.begin(), view_.end());
    }
    view_dirty_ = false;
}

const NetInspector::Row* NetInspector::find_row(std::uint64_t id) const
{
    auto it = std::lower_bound(rows_.begin(), rows_.end(), id,
                               [](const Row& r, std::uint64_t v) { return r.r.id < v; });
    return (it != rows_.end() && it->r.id == id) ? &*it : nullptr;
}

// ── Draw ─────────────────────────────────────────────────────────────────────

NetInspector::Out NetInspector::draw(const Inputs& in, float win_w, float win_h)
{
    Out out;
    sync(in);

    const float w = dock_width(win_w);
    const float x = win_w - w;
    narrow_ = w < 620.0f;          // hides Initiator
    tiny_   = w < 430.0f;          // hides Type + Waterfall (Name shows the rendition)

    ImGui::SetNextWindowPos(ImVec2(x, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(w, win_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, th::vec4(kSideBg));
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##net_inspector", nullptr, kFlags);
    ImGui::PushFont(ui_fonts().ui_small);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ── Left-edge resize handle ──────────────────────────────────────────────
    {
        ImGui::SetCursorScreenPos(ImVec2(x - 1.0f, 0.0f));
        ImGui::InvisibleButton("##net_resize", ImVec2(6.0f, win_h));
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        if (hov || act) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (act) width_ = std::clamp(w - ImGui::GetIO().MouseDelta.x, kMinW, std::max(kMinW, win_w * 0.65f));
        dl->AddLine(ImVec2(x + 0.5f, 0.0f), ImVec2(x + 0.5f, win_h),
                    C(act ? th::kAccent : hov ? th::alpha(th::kAccent, 0.6f) : th::kPanelBorder), act || hov ? 2.0f : 1.0f);
    }

    // ── Header: tabs + close ─────────────────────────────────────────────────
    ImGui::SetCursorScreenPos(ImVec2(x, 0.0f));
    {
        const ImVec2 p(x, 0.0f);
        dl->AddRectFilled(p, ImVec2(x + w, kHeaderH), C(kSideBar));
        dl->AddLine(ImVec2(x, kHeaderH - 0.5f), ImVec2(x + w, kHeaderH - 0.5f), C(th::kPanelBorder));
        ImFont* f = ui_fonts().ui;
        float tx = x + kPad + 2.0f;
        const char* labels[] = { "Network", "Manifest" };
        for (int i = 0; i < 2; ++i) {
            const ImVec2 ts = f->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, labels[i]);
            ImGui::SetCursorScreenPos(ImVec2(tx - 8.0f, 0.0f));
            ImGui::PushID(i);
            const bool clicked = ImGui::InvisibleButton("##tab", ImVec2(ts.x + 16.0f, kHeaderH));
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            const bool active = static_cast<int>(tab_) == i;
            if (clicked) tab_ = static_cast<Tab>(i);
            dl->AddText(f, 14.0f, ImVec2(tx, std::round((kHeaderH - ts.y) * 0.5f)),
                        C(active ? th::kText : hov ? th::kText : th::kTextDim), labels[i]);
            if (active)
                dl->AddRectFilled(ImVec2(tx - 2.0f, kHeaderH - 2.5f), ImVec2(tx + ts.x + 2.0f, kHeaderH - 0.5f),
                                  C(th::kAccent), 1.0f);
            tx += ts.x + 22.0f;
        }
        // Keyboard hint + close
        const float bsz = 26.0f;
        ImGui::SetCursorScreenPos(ImVec2(x + w - bsz - 8.0f, (kHeaderH - bsz) * 0.5f));
        if (glyph_button("##net_close", "Close (N)", bsz)) out.close = true;
        draw_x(dl, ImVec2(x + w - 8.0f - bsz * 0.5f, kHeaderH * 0.5f), 4.5f,
               C(ImGui::IsItemHovered() ? th::kText : th::kTextDim));
    }

    const float body_y = kHeaderH;
    const float body_h = win_h - body_y;
    ImGui::SetCursorScreenPos(ImVec2(x, body_y));
    if (tab_ == Tab::Network) draw_network(in, w, body_h);
    else                      draw_manifest(in, out, w, body_h);

    // Keep the player's shortcuts alive. PlayerUI enables keyboard nav, and a
    // click on a row, chip or button leaves this window (or one of its child
    // windows — tables, details pane — which do not inherit NoNav) as
    // ImGui's nav window, so io.WantCaptureKeyboard would stay true and
    // Space would re-press the last button instead of pausing. Drop focus as
    // soon as no widget is held; typing in the filter keeps it (active item).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive())
        ImGui::SetWindowFocus(nullptr);

    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    return out;
}

void NetInspector::draw_toolbar(const Inputs& in, float w)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    NetworkLogger* log = in.log;
    const float bsz = 26.0f;
    const float cy  = p.y + kToolbarH * 0.5f;

    // Record toggle (red dot = recording, like DevTools).
    const bool rec = log && log->recording();
    ImGui::SetCursorScreenPos(ImVec2(p.x + kPad - 4.0f, cy - bsz * 0.5f));
    if (glyph_button("##rec", rec ? "Stop recording network log" : "Record network log", bsz) && log)
        log->set_recording(!rec);
    {
        const ImVec2 c(p.x + kPad - 4.0f + bsz * 0.5f, cy);
        if (rec) dl->AddCircleFilled(c, 5.5f, C(th::kError), 20);
        else     dl->AddCircle(c, 5.5f, C(th::kTextDim), 20, 1.6f);
    }
    // Clear (circle with a slash, DevTools' ⊘).
    ImGui::SetCursorScreenPos(ImVec2(p.x + kPad - 4.0f + bsz + 2.0f, cy - bsz * 0.5f));
    if (glyph_button("##clear", "Clear", bsz) && log) {
        log->clear();
        selected_ = 0;
    }
    {
        const ImVec2 c(p.x + kPad - 4.0f + bsz * 1.5f + 2.0f, cy);
        const ImU32 col = C(ImGui::IsItemHovered() ? th::kText : th::kTextDim);
        dl->AddCircle(c, 6.0f, col, 20, 1.5f);
        dl->AddLine(ImVec2(c.x - 4.2f, c.y + 4.2f), ImVec2(c.x + 4.2f, c.y - 4.2f), col, 1.5f);
    }

    // Filter box (stretches).
    const float fx = p.x + kPad - 4.0f + bsz * 2.0f + 10.0f;
    const float fw = std::max(80.0f, p.x + w - kPad - fx);
    ImGui::SetCursorScreenPos(ImVec2(fx, cy - 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, th::kRadiusSmall);
    ImGui::SetNextItemWidth(fw);
    if (ImGui::InputTextWithHint("##net_filter", "Filter  (URL, type, rendition, status; -text excludes)",
                                 filter_, sizeof(filter_))) {
        filter_lc_  = lower(filter_);
        view_dirty_ = true;
    }
    ImGui::PopStyleVar(2);
    if (filter_[0]) {   // clear-filter ×
        const ImVec2 c(fx + fw - 12.0f, cy);
        ImGui::SetCursorScreenPos(ImVec2(c.x - 9.0f, c.y - 9.0f));
        if (ImGui::InvisibleButton("##filter_x", ImVec2(18.0f, 18.0f))) { filter_[0] = 0; filter_lc_.clear(); view_dirty_ = true; }
        draw_x(dl, c, 3.5f, C(ImGui::IsItemHovered() ? th::kText : th::kTextDim));
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + kToolbarH));

    // ── Chips ────────────────────────────────────────────────────────────────
    const ImVec2 cp = ImGui::GetCursorScreenPos();
    int n_err = 0;
    for (const auto& row : rows_) if (is_error(row.r)) ++n_err;
    struct ChipDef { Chip c; const char* label; ImU32 dot; };
    const std::string err_label = n_err ? std::format("Errors {}", n_err) : std::string("Errors");
    const ChipDef chips[] = {
        { Chip::All,      "All",      0 },
        { Chip::Manifest, "Manifest", kColManifest },
        { Chip::Video,    "Video",    kColVideo },
        { Chip::Audio,    "Audio",    kColAudio },
        { Chip::Subs,     "Subs",     kColSubs },
        { Chip::Errors,   err_label.c_str(), n_err ? th::kError : 0u },
    };
    float cx = cp.x + kPad;
    const float chip_y = cp.y + (kChipsH - 22.0f) * 0.5f - 2.0f;
    for (const auto& c : chips) {
        ImGui::SetCursorScreenPos(ImVec2(cx, chip_y));
        ImGui::PushID(static_cast<int>(c.c));
        if (chip_button("##chip", c.label, chip_ == c.c, c.dot)) { chip_ = c.c; view_dirty_ = true; }
        ImGui::PopID();
        cx = ImGui::GetItemRectMax().x + 5.0f;
    }
    // Helper-traffic toggle (thumbnail / scrub decoders open their own inputs).
    const char* hl = hide_helpers_ ? "Helpers hidden" : "Helpers";
    const float hw = ui_fonts().ui_small->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, hl).x + 20.0f;
    if (cx + hw < cp.x + w - kPad) {
        ImGui::SetCursorScreenPos(ImVec2(cp.x + w - kPad - hw, chip_y));
        if (chip_button("##helpers", hl, hide_helpers_)) { hide_helpers_ = !hide_helpers_; view_dirty_ = true; }
        if (ImGui::IsItemHovered())
            ui::tooltip("Hide requests made by the thumbnail and scrub decoders");
    }
    ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + kChipsH));
}

void NetInspector::draw_network(const Inputs& in, float w, float h)
{
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + kToolbarH + kChipsH), C(kSideBar));
    draw_toolbar(in, w);
    dl->AddLine(ImVec2(p0.x, p0.y + kToolbarH + kChipsH - 0.5f), ImVec2(p0.x + w, p0.y + kToolbarH + kChipsH - 0.5f),
                C(th::kPanelBorder));

    if (view_dirty_) rebuild_view();

    // QA: select the newest playback segment.
    if (qa_select_segment_) {
        for (auto it = rows_.rbegin(); it != rows_.rend(); ++it)
            if (it->d.kind == NetKind::Segment && it->r.initiator == NetInitiator::Playback && it->r.done()) {
                selected_ = it->r.id;
                break;
            }
        if (selected_) qa_select_segment_ = false;
    }
    if (selected_ && !find_row(selected_)) selected_ = 0;

    const float top    = kToolbarH + kChipsH;
    const float avail  = h - top - kFooterH;
    const float det_h  = selected_ ? std::round(std::clamp(avail * details_frac_, 120.0f, avail - 110.0f)) : 0.0f;
    const float table_h = avail - (selected_ ? det_h + kSplitH : 0.0f);

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + top));
    if (rows_.empty()) {
        ImGui::BeginChild("##net_empty", ImVec2(w, table_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        const bool rec = in.log && in.log->recording();
        draw_empty_state(rec ? "Recording network activity…" : "Recording paused",
                         in.local_media ? "Local file — requests appear here when FFmpeg opens a network URL."
                                        : "Requests FFmpeg makes for playback, thumbnails and scrubbing appear here.",
                         w, table_h);
        ImGui::EndChild();
    } else {
        draw_table(w, table_h);
    }

    if (selected_) {
        // Splitter
        const ImVec2 sp(p0.x, p0.y + top + table_h);
        ImGui::SetCursorScreenPos(sp);
        ImGui::InvisibleButton("##net_split", ImVec2(w, kSplitH));
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        if (hov || act) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (act && avail > 0.0f)
            details_frac_ = std::clamp(details_frac_ - ImGui::GetIO().MouseDelta.y / avail, 0.2f, 0.8f);
        dl->AddRectFilled(sp, ImVec2(sp.x + w, sp.y + kSplitH), C(kSideBar));
        dl->AddLine(ImVec2(sp.x, sp.y + 0.5f), ImVec2(sp.x + w, sp.y + 0.5f),
                    C(act || hov ? th::alpha(th::kAccent, 0.7f) : th::kPanelBorder));
        ImGui::SetCursorScreenPos(ImVec2(p0.x, sp.y + kSplitH));
        draw_details(w, det_h);
    }

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h - kFooterH));
    draw_footer(w);
}

void NetInspector::draw_table(float w, float h)
{
    constexpr ImGuiTableFlags kTbl = ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable
        | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_HighlightHoveredColumn;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, th::vec4(th::rgba(255, 255, 255, 0.025f)));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, th::vec4(th::rgba(255, 255, 255, 0.04f)));
    if (!ImGui::BeginTable("##net_table", ColCount, kTbl, ImVec2(w, h))) {
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name",      ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 1.5f, ColName);
    ImGui::TableSetupColumn("Status",    ImGuiTableColumnFlags_WidthFixed, 44.0f, ColStatus);
    ImGui::TableSetupColumn("Type",      ImGuiTableColumnFlags_WidthFixed, 116.0f, ColType);
    ImGui::TableSetupColumn("Initiator", ImGuiTableColumnFlags_WidthFixed, 66.0f, ColInit);
    ImGui::TableSetupColumn("Size",      ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 58.0f, ColSize);
    ImGui::TableSetupColumn("Time",      ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 54.0f, ColTime);
    ImGui::TableSetupColumn("Waterfall", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 1.0f, ColWaterfall);
    // Auto-hide columns as the sidebar narrows (re-enable from the header
    // context menu); only when the width class changes. Narrow: Initiator.
    // Tiny: also Type and Waterfall — the Name cell then carries the
    // rendition, so rows of different variants and offsets stay distinct.
    const int width_class = tiny_ ? 2 : narrow_ ? 1 : 0;
    if (!narrow_applied_ || width_class != last_width_class_) {
        ImGui::TableSetColumnEnabled(ColInit, width_class == 0);
        ImGui::TableSetColumnEnabled(ColType, width_class < 2);
        ImGui::TableSetColumnEnabled(ColWaterfall, width_class < 2);
        narrow_applied_    = true;
        last_width_class_  = width_class;
    }
    const bool type_shown = (ImGui::TableGetColumnFlags(ColType) & ImGuiTableColumnFlags_IsEnabled) != 0;
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
        if (specs->SpecsCount == 0 || specs->Specs[0].ColumnUserID == ColWaterfall) {
            sort_col_  = -1;
            sort_desc_ = specs->SpecsCount > 0 && specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
        } else {
            sort_col_  = static_cast<int>(specs->Specs[0].ColumnUserID);
            sort_desc_ = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
        }
        specs->SpecsDirty = false;
        rebuild_view();
    }

    // Shared waterfall time range over the rows on screen (last frame's
    // clipper range), so bars stay readable deep into a long session.
    const std::int64_t now = NetworkLogger::now_us();
    std::int64_t t0 = INT64_MAX, t1 = 0;
    const int vis_a = std::clamp(vis_start_, 0, static_cast<int>(view_.size()));
    const int vis_b = std::clamp(vis_end_ > vis_start_ ? vis_end_ : static_cast<int>(view_.size()), vis_a,
                                 static_cast<int>(view_.size()));
    for (int k = vis_a; k < vis_b; ++k) {
        const int i = view_[static_cast<std::size_t>(k)];
        const NetRequest& r = rows_[static_cast<std::size_t>(i)].r;
        t0 = std::min(t0, r.t_start);
        t1 = std::max(t1, r.done() ? r.t_end : now);
    }
    if (t0 == INT64_MAX) { t0 = now - 1000; t1 = now; }
    if (t1 <= t0) t1 = t0 + 1000;
    ImGui::TableSetColumnIndex(ColWaterfall);
    const double span = static_cast<double>(t1 - t0);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(view_.size()));
    int seen_a = INT32_MAX, seen_b = 0;
    while (clipper.Step()) {
        for (int vi = clipper.DisplayStart; vi < clipper.DisplayEnd; ++vi) {
            const Row& row = rows_[static_cast<std::size_t>(view_[static_cast<std::size_t>(vi)])];
            const NetRequest& r = row.r;
            const bool err  = is_error(r);
            const bool sel  = r.id == selected_;
            // Helper traffic (thumbnail / scrub decoders) is dimmed.
            const ImU32 txt = err ? th::kError : is_helper(r) ? th::kTextDim : th::kText;

            ImGui::TableNextRow(ImGuiTableRowFlags_None, 20.0f);
            if (sel) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, C(kSelRow));

            // Name (+ row selectable spanning all columns)
            ImGui::TableSetColumnIndex(ColName);
            ImGui::PushID(static_cast<int>(r.id));
            if (ImGui::Selectable("##row", sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0.0f, 20.0f - 6.0f)))
                selected_ = sel ? 0 : r.id;
            const bool row_hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemVisible()) { seen_a = std::min(seen_a, vi); seen_b = std::max(seen_b, vi + 1); }
            ImGui::PopID();
            ImGui::SameLine(0.0f, 0.0f);
            {
                const ImVec2 cp = ImGui::GetCursorScreenPos();
                dl->AddRectFilled(ImVec2(cp.x, cp.y + 3.0f), ImVec2(cp.x + 3.0f, cp.y + 11.0f), C(media_color(row.d)), 1.0f);
                ImGui::SetCursorScreenPos(ImVec2(cp.x + 8.0f, cp.y));
                ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(txt));
                ImGui::TextUnformatted(row.name.c_str());
                ImGui::PopStyleColor();
                if (r.range_start >= 0) {
                    ImGui::SameLine(0.0f, 5.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
                    ImGui::TextUnformatted(r.range_end >= 0 ? std::format("@{}", r.range_start).c_str()
                                                            : std::format("@{}-", r.range_start).c_str());
                    ImGui::PopStyleColor();
                }
                if (!type_shown) {   // Type column hidden: rendition (or kind) after the name
                    ImGui::SameLine(0.0f, 6.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextFaint));
                    ImGui::TextUnformatted(row.d.rendition.empty() ? net_kind_name(row.d.kind) : row.d.rendition.c_str());
                    ImGui::PopStyleColor();
                }
            }
            if (row_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::SetClipboardText(r.url.c_str());
            if (row_hovered && ImGui::TableGetHoveredColumn() == ColName) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
                ImGui::TextUnformatted(r.url.c_str());
                ImGui::PopTextWrapPos();
                ImGui::TextDisabled("Initiator: %s  \u00B7  right-click to copy URL", net_initiator_name(r.initiator));
                ImGui::EndTooltip();
            }

            // Status
            if (ImGui::TableSetColumnIndex(ColStatus)) {
                std::string s = status_text(r);
                if (r.retry_of_failed) s = "↻" + s;   // ↻ retry
                ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(err ? th::kError : r.status >= 300 ? th::kWarn
                                                       : r.status > 0 ? th::kText : th::kTextFaint));
                ImGui::TextUnformatted(s.c_str());
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered() && !r.error.empty()) ui::tooltip(r.error.c_str());
            }

            // Type: kind + rendition
            if (ImGui::TableSetColumnIndex(ColType)) {
                ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(txt));
                ImGui::TextUnformatted(net_kind_name(row.d.kind));
                ImGui::PopStyleColor();
                if (!row.d.rendition.empty()) {
                    ImGui::SameLine(0.0f, 5.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
                    ImGui::TextUnformatted(row.d.rendition.c_str());
                    ImGui::PopStyleColor();
                }
                if (row_hovered && ImGui::TableGetHoveredColumn() == ColType && !row.d.rendition.empty())
                    ui::tooltip(row.d.rendition.c_str());
            }

            if (ImGui::TableSetColumnIndex(ColInit)) {
                ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(r.initiator == NetInitiator::Playback ? th::kText : th::kTextDim));
                ImGui::TextUnformatted(net_initiator_name(r.initiator));
                ImGui::PopStyleColor();
            }

            // Partial: the player stopped reading this body (variant no longer
            // selected, or its buffer is full) — amber, with what arrived.
            const bool partial = r.partial && !err;
            const ImU32 warn   = is_helper(r) ? th::alpha(th::kWarn, 0.6f) : th::kWarn;
            if (ImGui::TableSetColumnIndex(ColSize)) {
                // In flight: bytes so far; before the first byte the expected
                // size (Content-Length / file size), dimmed.
                const std::int64_t b = r.effective_bytes() >= 0 ? r.effective_bytes() : r.content_length;
                text_right(b >= 0 ? net_format_bytes(b) : r.done() ? "—" : "…",
                           partial ? warn : (r.done() && b >= 0) ? txt : th::kTextDim);
                if (partial && row_hovered && ImGui::TableGetHoveredColumn() == ColSize)
                    ui::tooltip(partial_note(r).c_str());
            }

            if (ImGui::TableSetColumnIndex(ColTime)) {
                if (r.done()) text_right(net_format_duration_us(r.t_end - r.t_start), partial ? warn : txt);
                else          text_right(net_format_duration_us(now - r.t_start), th::kTextFaint);   // elapsed, live
                if (row_hovered && ImGui::TableGetHoveredColumn() == ColTime)
                    ui::tooltip(partial ? partial_note(r).c_str()
                                        : r.done() ? "Request start → body finished"
                                                   : "In flight — elapsed since the request started");
            }

            // Waterfall bar: connect/queue → waiting (TTFB) → download.
            if (ImGui::TableSetColumnIndex(ColWaterfall)) {
                const ImVec2 cp = ImGui::GetCursorScreenPos();
                const float cw  = ImGui::GetContentRegionAvail().x;
                ImGui::Dummy(ImVec2(cw, 14.0f));
                auto X = [&](std::int64_t t) {
                    return cp.x + static_cast<float>(static_cast<double>(t - t0) / span) * cw;
                };
                const float y0 = cp.y + 3.0f, y1 = cp.y + 11.0f;
                const std::int64_t te = r.done() ? r.t_end : now;
                const std::int64_t ts = r.t_sent >= 0 ? r.t_sent : r.t_start;
                const std::int64_t tf = r.t_first_byte >= 0 ? r.t_first_byte : te;
                const float a = (r.done() && !r.partial) ? 1.0f : 0.55f;
                if (err) {
                    dl->AddRectFilled(ImVec2(X(r.t_start), y0), ImVec2(std::max(X(te), X(r.t_start) + 2.0f), y1),
                                      C(th::alpha(th::kError, a)), 1.0f);
                } else {
                    auto seg = [&](std::int64_t a_t, std::int64_t b_t, ImU32 col) {
                        if (b_t <= a_t) return;
                        dl->AddRectFilled(ImVec2(X(a_t), y0), ImVec2(std::max(X(b_t), X(a_t) + 1.0f), y1),
                                          C(th::alpha(col, a)));
                    };
                    seg(r.t_start, ts, kWfConnect);
                    seg(ts, tf, kWfWait);
                    seg(tf, te, kWfDownload);
                    if (X(te) - X(r.t_start) < 2.0f)
                        dl->AddRectFilled(ImVec2(X(r.t_start), y0), ImVec2(X(r.t_start) + 2.0f, y1),
                                          C(th::alpha(kWfDownload, a)));
                }
            }
        }
    }
    if (seen_b > seen_a) { vis_start_ = seen_a; vis_end_ = seen_b; }
    // Follow the newest request unless the user scrolled up.
    if (sort_col_ < 0 && !sort_desc_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f)
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndTable();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

void NetInspector::draw_details(float w, float h)
{
    const Row* row = find_row(selected_);
    if (!row) return;
    const NetRequest& r = row->r;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();

    // Sub-header: name · Headers | Timing · ×
    constexpr float kSubH = 30.0f;
    dl->AddLine(ImVec2(p.x, p.y + kSubH - 0.5f), ImVec2(p.x + w, p.y + kSubH - 0.5f), C(th::kPanelBorder));
    float tx = p.x + kPad;
    const char* tabs[] = { "Headers", "Timing" };
    ImFont* f = ui_fonts().ui_small;
    for (int i = 0; i < 2; ++i) {
        const ImVec2 ts = f->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, tabs[i]);
        ImGui::SetCursorScreenPos(ImVec2(tx - 6.0f, p.y));
        ImGui::PushID(100 + i);
        if (ImGui::InvisibleButton("##dtab", ImVec2(ts.x + 12.0f, kSubH))) detail_tab_ = i;
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool act = detail_tab_ == i;
        dl->AddText(f, 12.0f, ImVec2(tx, std::round(p.y + (kSubH - ts.y) * 0.5f)),
                    C(act || hov ? th::kText : th::kTextDim), tabs[i]);
        if (act) dl->AddRectFilled(ImVec2(tx - 1.0f, p.y + kSubH - 2.5f), ImVec2(tx + ts.x + 1.0f, p.y + kSubH - 0.5f),
                                   C(th::kAccent), 1.0f);
        tx += ts.x + 18.0f;
    }
    // Name (elided) between the tabs and the close button.
    {
        const float name_x = tx + 6.0f, name_r = p.x + w - 34.0f;
        if (name_r - name_x > 40.0f) {
            std::string nm = row->name;
            while (nm.size() > 4 && f->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, nm.c_str()).x > name_r - name_x)
                nm = nm.substr(0, nm.size() - 4) + "…";
            const float nw = f->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, nm.c_str()).x;
            dl->AddText(f, 12.0f, ImVec2(name_r - nw, std::round(p.y + (kSubH - 14.0f) * 0.5f)),
                        C(th::kTextFaint), nm.c_str());
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - 30.0f, p.y + 3.0f));
    if (glyph_button("##det_close", "Close details", 24.0f)) selected_ = 0;
    draw_x(dl, ImVec2(p.x + w - 18.0f, p.y + 15.0f), 4.0f, C(ImGui::IsItemHovered() ? th::kText : th::kTextDim));

    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + kSubH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad + 2.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 2.0f));
    ImGui::BeginChild("##net_details", ImVec2(w, h - kSubH), ImGuiChildFlags_AlwaysUseWindowPadding);

    const float key_w = 112.0f;
    if (detail_tab_ == 0) {
        // URL: read-only field (selectable / copyable) + Copy.
        section_caption("GENERAL");
        {
            const float bw = 48.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
            std::string url = r.url;
            ImGui::InputText("##url", url.data(), url.size() + 1, ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            if (ImGui::Button("Copy", ImVec2(bw, 0.0f))) ImGui::SetClipboardText(r.url.c_str());
            ImGui::PopStyleVar();
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (begin_kv_table("##gen", key_w)) {
            kv_row("Method", r.method.empty() ? "GET" : r.method);
            if (r.status > 0)
                kv_row("Status", std::format("{} {}", r.status, r.status_text), is_error(r) ? th::kError : th::kText);
            else
                kv_row("Status", r.failed ? "failed" : r.protocol == "file" ? "local file (no HTTP)" : r.done() ? "—" : "pending",
                       r.failed ? th::kError : th::kTextDim);
            if (!r.error.empty()) kv_row(r.failed ? "Error" : "Note", r.error, r.failed ? th::kError : th::kWarn);
            if (r.partial && !r.failed) kv_row("Partial", partial_note(r), th::kWarn);
            if (r.retry_of_failed) kv_row("Retry", "Same URL failed earlier — this request retries it", th::kWarn);
            if (r.reconnects > 0) kv_row("Reconnects", std::to_string(r.reconnects), th::kWarn);
            std::string type = net_kind_name(row->d.kind);
            if (row->d.media != NetMedia::None) type += std::format(" · {}", media_name(row->d.media));
            kv_row("Type", type);
            if (!row->d.rendition.empty()) kv_row("Stream", row->d.rendition);
            kv_row("Initiator", net_initiator_name(r.initiator));
            if (!r.mime.empty()) kv_row("Content-Type", r.mime);
            if (r.range_start >= 0)
                kv_row("Range", r.range_end >= 0 ? std::format("bytes {}–{} ({})", r.range_start, r.range_end,
                                                               net_format_bytes(r.range_end - r.range_start + 1))
                                                 : std::format("bytes {}–", r.range_start));
            kv_row("Transferred", r.bytes >= 0 ? std::format("{} ({} bytes)", net_format_bytes(r.bytes), r.bytes)
                                               : r.content_length >= 0 ? std::format("{} (Content-Length)", net_format_bytes(r.content_length))
                                                                       : std::string("—"));
            if (!r.http_version.empty()) kv_row("Protocol", std::format("HTTP/{}", r.http_version));
            if (r.conn_id)
                kv_row("Connection", std::format("#{}{}", r.conn_id, r.reused_connection ? " (reused, keep-alive)" : ""));
            ImGui::EndTable();
        }
        if (!r.response_headers.empty()) {
            section_caption(std::format("RESPONSE HEADERS ({})", r.response_headers.size()).c_str());
            if (begin_kv_table("##resp", key_w)) {
                for (const auto& [k, v] : r.response_headers) kv_row(k.c_str(), v);
                ImGui::EndTable();
            }
        }
        if (!r.request_headers.empty()) {
            section_caption(std::format("REQUEST HEADERS ({})", r.request_headers.size()).c_str());
            if (begin_kv_table("##req", key_w)) {
                for (const auto& [k, v] : r.request_headers) kv_row(k.c_str(), v);
                ImGui::EndTable();
            }
        }
        if (r.protocol != "file" && r.response_headers.empty() && r.status == 0 && r.done())
            ImGui::TextDisabled("No HTTP response headers were captured for this request.");
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextDisabled("Right-click a value to copy it.");
    } else {
        const std::int64_t now = NetworkLogger::now_us();
        const std::int64_t te  = r.done() ? r.t_end : now;
        const std::int64_t ts  = r.t_sent >= 0 ? r.t_sent : r.t_start;
        const std::int64_t tf  = r.t_first_byte >= 0 ? r.t_first_byte : -1;
        struct Phase { const char* name; std::int64_t a, b; ImU32 col; const char* tip; };
        std::vector<Phase> phases;
        if (ts > r.t_start) phases.push_back({ "Connect / queue", r.t_start, ts, kWfConnect,
                                               "From io_open() to the request being sent: DNS, TCP and TLS on a new connection" });
        if (tf >= 0) {
            phases.push_back({ "Waiting (TTFB)", ts, tf, kWfWait, "Request sent → first response byte (status line)" });
            phases.push_back({ "Content download", tf, te, kWfDownload,
                               "First byte → body finished. FFmpeg reads on demand, so for media segments this is "
                               "paced by the demuxer, not only by the network" });
        } else {
            phases.push_back({ r.protocol == "file" ? "Open → close" : "Request", ts, te, kWfDownload, nullptr });
        }

        section_caption("TIMING");
        if (begin_kv_table("##tstart", key_w)) {
            kv_row("Started", std::format("{}  (+{} after first request)", clock_text(r.t_start),
                                          net_format_duration_us(base_t_ >= 0 ? r.t_start - base_t_ : 0)));
            ImGui::EndTable();
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        const std::int64_t total = std::max<std::int64_t>(1, te - r.t_start);
        if (ImGui::BeginTable("##phases", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthFixed, key_w);
            ImGui::TableSetupColumn("bar", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("d", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            for (const auto& ph : phases) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 22.0f);
                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
                ImGui::TextUnformatted(ph.name);
                ImGui::PopStyleColor();
                if (ph.tip && ImGui::IsItemHovered()) ui::tooltip(ph.tip);
                ImGui::TableSetColumnIndex(1);
                const ImVec2 cp = ImGui::GetCursorScreenPos();
                const float cw = ImGui::GetContentRegionAvail().x;
                ImGui::Dummy(ImVec2(cw, 14.0f));
                const float xa = cp.x + cw * static_cast<float>(static_cast<double>(ph.a - r.t_start) / static_cast<double>(total));
                const float xb = cp.x + cw * static_cast<float>(static_cast<double>(ph.b - r.t_start) / static_cast<double>(total));
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(cp.x, cp.y + 3.0f), ImVec2(cp.x + cw, cp.y + 11.0f),
                                                          C(th::rgba(255, 255, 255, 0.05f)), 2.0f);
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(xa, cp.y + 3.0f), ImVec2(std::max(xb, xa + 2.0f), cp.y + 11.0f),
                                                          C(is_error(r) ? th::kError : ph.col), 2.0f);
                ImGui::TableSetColumnIndex(2);
                text_right(net_format_duration_us(ph.b - ph.a), th::kText);
            }
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(r.done() ? "Total" : "Elapsed");
            ImGui::TableSetColumnIndex(2);
            text_right(net_format_duration_us(te - r.t_start), th::kText);
            ImGui::EndTable();
        }
        if (tf >= 0 && (row->d.kind == NetKind::Segment || row->d.kind == NetKind::Media)) {
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Download time follows demuxer reads, not only the network.");
            ImGui::PopTextWrapPos();
        }
        const std::int64_t b = r.effective_bytes();
        if (r.done() && b > 0 && tf >= 0 && te > tf) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            if (begin_kv_table("##tput", key_w)) {
                kv_row("Throughput", std::format("{:.2f} Mb/s  (body bytes ÷ download time)",
                                                 static_cast<double>(b) * 8.0 / static_cast<double>(te - tf)));
                ImGui::EndTable();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
}

void NetInspector::draw_footer(float w)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + kFooterH), C(kSideBar));
    dl->AddLine(ImVec2(p.x, p.y + 0.5f), ImVec2(p.x + w, p.y + 0.5f), C(th::kPanelBorder));

    std::int64_t bytes = 0, recent = 0;
    const std::int64_t now = NetworkLogger::now_us();
    constexpr std::int64_t kWin = 10'000'000;   // throughput window: 10 s
    for (const auto& row : rows_) {
        const std::int64_t b = row.r.effective_bytes();
        if (b > 0) {
            bytes += b;
            if (row.r.done() && now - row.r.t_end <= kWin) recent += b;
        }
    }
    std::string s = view_.size() != rows_.size()
        ? std::format("{} / {} requests", view_.size(), rows_.size())
        : std::format("{} request{}", rows_.size(), rows_.size() == 1 ? "" : "s");
    s += std::format("  ·  {} transferred", net_format_bytes(bytes));
    const std::string rate = std::format("↓ {:.2f} Mb/s", static_cast<double>(recent) * 8.0 / (kWin / 1e6) / 1e6);
    ImFont* f = ui_fonts().ui_small;
    const float ty = std::round(p.y + (kFooterH - 14.0f) * 0.5f);
    dl->AddText(f, 12.0f, ImVec2(p.x + kPad, ty), C(th::kTextDim), s.c_str());
    const float rw = f->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, rate.c_str()).x;
    if (p.x + kPad + f->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, s.c_str()).x + 16.0f < p.x + w - kPad - rw) {
        dl->AddText(f, 12.0f, ImVec2(p.x + w - kPad - rw, ty), C(th::kTextDim), rate.c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - kPad - rw, p.y));
        ImGui::InvisibleButton("##rate", ImVec2(rw, kFooterH));
        if (ImGui::IsItemHovered()) ui::tooltip("Bytes of requests finished in the last 10 s");
    }
}

// ── Manifest tab ─────────────────────────────────────────────────────────────

void NetInspector::draw_manifest(const Inputs& in, Out& out, float w, float h)
{
    const ManifestParser* m = in.manifest;
    const bool has = m && (!m->variants().empty() || !m->renditions().empty() || !m->tags().empty());

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad + 2.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::BeginChild("##manifest", ImVec2(w, h), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (!has) {
        draw_empty_state(in.local_media || !ManifestParser::is_manifest(in.media_url) ? "Not an adaptive stream"
                                                                                       : "No manifest data",
                         in.local_media || !ManifestParser::is_manifest(in.media_url)
                             ? "Variants, renditions and timeline tags appear here for HLS (.m3u8) and DASH (.mpd) sources."
                             : "The manifest could not be fetched or parsed.",
                         w - 2.0f * (kPad + 2.0f), h - 20.0f);
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        return;
    }

    // Source URL
    section_caption("SOURCE");
    {
        const float bw = 48.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
        std::string url = in.media_url;
        ImGui::InputText("##murl", url.data(), url.size() + 1, ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        if (ImGui::Button("Copy", ImVec2(bw, 0.0f))) ImGui::SetClipboardText(in.media_url.c_str());
        ImGui::PopStyleVar();
    }

    constexpr ImGuiTableFlags kTbl = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
        | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings
        | ImGuiTableFlags_ScrollX;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, th::vec4(th::rgba(255, 255, 255, 0.025f)));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, th::vec4(th::rgba(255, 255, 255, 0.04f)));

    // ── Variants ─────────────────────────────────────────────────────────────
    const auto& vars = m->variants();
    if (!vars.empty()) {
        section_caption(std::format("VARIANTS ({})", vars.size()).c_str());
        const float rows_h = std::min(static_cast<float>(vars.size()) * 21.0f + 26.0f, std::max(160.0f, h * 0.42f));
        if (ImGui::BeginTable("##variants", 7, kTbl | ImGuiTableFlags_ScrollY, ImVec2(0.0f, rows_h))) {
            ImGui::TableSetupScrollFreeze(1, 1);
            ImGui::TableSetupColumn("#",          ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableSetupColumn("Bandwidth",  ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableSetupColumn("Avg",        ImGuiTableColumnFlags_WidthFixed, 66.0f);
            ImGui::TableSetupColumn("Resolution", ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableSetupColumn("FPS",        ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableSetupColumn("Codecs",     ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Playlist",   ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < static_cast<int>(vars.size()); ++i) {
                const auto& v = vars[static_cast<std::size_t>(i)];
                const bool playing = i == in.playing_variant;
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 20.0f);
                if (playing) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, C(kPlayRow));
                const ImU32 tc = playing ? th::kAccentHover : th::kText;
                ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(tc));
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(playing ? std::format("▶ {}", i).c_str() : std::to_string(i).c_str());
                ImGui::TableSetColumnIndex(1);
                text_right(v.bandwidth > 0 ? std::format("{:.2f} M", static_cast<double>(v.bandwidth) / 1e6) : "—", tc);
                ImGui::TableSetColumnIndex(2);
                text_right(v.avg_bandwidth > 0 ? std::format("{:.2f} M", static_cast<double>(v.avg_bandwidth) / 1e6) : "—",
                           playing ? tc : th::kTextDim);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(v.width > 0 ? std::format("{}×{}", v.width, v.height).c_str() : "—");
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(v.frame_rate > 0.0 ? std::format("{:g}", std::round(v.frame_rate * 100.0) / 100.0).c_str() : "—");
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(v.codecs.empty() ? "—" : v.codecs.c_str());
                if (ImGui::IsItemHovered() && !v.audio_group.empty())
                    ui::tooltip(std::format("audio group {}{}", v.audio_group,
                                            v.subtitle_group.empty() ? "" : " · subtitles " + v.subtitle_group).c_str());
                ImGui::TableSetColumnIndex(6);
                const NetRendition* rend = cat_.match(v.url);
                const std::string pl = v.url.empty() ? std::string("—")
                                     : rend ? rend->label.substr(0, rend->label.find(' ')) : net_request_name(v.url);
                ImGui::TextUnformatted(pl.c_str());
                if (ImGui::IsItemHovered() && !v.url.empty()) ui::tooltip(v.url.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
        if (in.playing_variant < 0)
            ImGui::TextDisabled("Playing variant unknown (not an HLS program).");
    }

    // ── Renditions (EXT-X-MEDIA) ────────────────────────────────────────────
    const auto& rends = m->renditions();
    if (!rends.empty()) {
        section_caption(std::format("RENDITIONS ({})", rends.size()).c_str());
        if (ImGui::BeginTable("##renditions", 6, kTbl & ~ImGuiTableFlags_ScrollX)) {
            ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableSetupColumn("Group",    ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Lang",     ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("Ch",       ImGuiTableColumnFlags_WidthFixed, 24.0f);
            ImGui::TableSetupColumn("Playlist", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableHeadersRow();
            for (const auto& mr : rends) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 20.0f);
                ImGui::TableSetColumnIndex(0);
                const ImU32 dot = mr.type == "AUDIO" ? kColAudio : mr.type == "SUBTITLES" ? kColSubs
                                : mr.type == "CLOSED-CAPTIONS" ? kColSubs : kColVideo;
                const ImVec2 cp = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cp.x + 3.5f, cp.y + 7.5f), 3.0f, C(dot), 10);
                ImGui::SetCursorScreenPos(ImVec2(cp.x + 11.0f, cp.y));
                ImGui::TextUnformatted(mr.type == "CLOSED-CAPTIONS" ? "CC" : lower(mr.type).c_str());
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(mr.group_id.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(mr.is_default ? (mr.name + " *").c_str() : mr.name.c_str());
                if (mr.is_default && ImGui::IsItemHovered()) ui::tooltip("DEFAULT=YES");
                ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(mr.language.empty() ? "—" : mr.language.c_str());
                ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(mr.channels.empty() ? "—" : mr.channels.c_str());
                ImGui::TableSetColumnIndex(5);
                if (mr.url.empty()) {
                    ImGui::TextDisabled("in-band");
                } else {
                    const NetRendition* rend = cat_.match(mr.url);
                    const std::string pl = net_request_name(mr.url);
                    ImGui::TextUnformatted(rend ? rend->label.substr(0, rend->label.find(' ')).append("/").append(pl).c_str()
                                                : pl.c_str());
                    if (ImGui::IsItemHovered()) ui::tooltip(mr.url.c_str());
                }
            }
            ImGui::EndTable();
        }
    }

    // ── Timeline tags ────────────────────────────────────────────────────────
    const auto& tags = m->tags();
    section_caption(std::format("TIMELINE TAGS ({})", tags.size()).c_str());
    if (tags.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("No timeline tags. Discontinuity / cue / date-time tags live in media playlists; "
                            "a master playlist carries none.");
        ImGui::PopTextWrapPos();
    } else if (ImGui::BeginTable("##tags", 3, kTbl | ImGuiTableFlags_ScrollY,
                                 ImVec2(0.0f, std::max(120.0f, ImGui::GetContentRegionAvail().y - 4.0f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time",  ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Tag",   ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        int current = -1;
        for (int i = 0; i < static_cast<int>(tags.size()); ++i)
            if (tags[static_cast<std::size_t>(i)].pts <= in.cur_pts + 0.05) current = i;
        for (int i = 0; i < static_cast<int>(tags.size()); ++i) {
            const auto& t = tags[static_cast<std::size_t>(i)];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 20.0f);
            if (i == current) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, C(kPlayRow));
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            const int s = static_cast<int>(t.pts);
            if (ImGui::Selectable(std::format("{}:{:02}.{}", s / 60, s % 60, static_cast<int>((t.pts - s) * 10.0)).c_str(),
                                  false, ImGuiSelectableFlags_SpanAllColumns))
                out.seek_to = t.pts;
            if (ImGui::IsItemHovered()) ui::tooltip("Click to seek");
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            std::string tag = t.raw.substr(0, t.raw.find(':'));
            if (tag.starts_with("#EXT-X-")) tag = tag.substr(7);
            ImGui::TextUnformatted(tag.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(t.value.c_str());
        }
        ImGui::EndTable();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}
