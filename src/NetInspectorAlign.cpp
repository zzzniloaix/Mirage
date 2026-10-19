// Network inspector — the Alignment tab: are the renditions played together
// (video variant, audio, subtitles — or every rendition of the master)
// aligned? Inputs are the playlists and segment heads NetworkLogger kept;
// "Probe" fetches what is missing (first segment, the playhead's, both
// sides of each discontinuity) as inspector requests. The checks are
// hls::analyze_alignment (HlsAlign.h); this draws its findings, a timeline
// with one row per rendition, and a table of their numbers.

#include "NetInspector.h"

#include "NetInspectorUi.h"

#include <map>
#include <set>

using namespace netui;

namespace {

constexpr float kRowH    = 24.0f;
constexpr float kLabelW  = 118.0f;
constexpr float kAxisH   = 20.0f;
constexpr double kRebuildEvery = 0.35;   // s, while requests keep changing

hls::Track track_of(NetMedia m)
{
    switch (m) {
        case NetMedia::Video: return hls::Track::Video;
        case NetMedia::Audio: return hls::Track::Audio;
        case NetMedia::Subs:  return hls::Track::Text;
        case NetMedia::None:  break;
    }
    return hls::Track::Unknown;
}

ImU32 sev_color(hls::Severity s)
{
    switch (s) {
        case hls::Severity::Error:   return th::kError;
        case hls::Severity::Warning: return th::kWarn;
        case hls::Severity::Info:    return th::kAccent;
        case hls::Severity::Ok:      return kColManifest;
    }
    return th::kTextDim;
}

void sev_icon(ImDrawList* dl, ImVec2 c, hls::Severity s)
{
    const ImU32 col = C(sev_color(s));
    switch (s) {
        case hls::Severity::Error:
            dl->AddCircleFilled(c, 5.5f, col, 16);
            dl->AddLine(ImVec2(c.x - 2.2f, c.y - 2.2f), ImVec2(c.x + 2.2f, c.y + 2.2f), C(th::rgba(15, 16, 20, 1.0f)), 1.5f);
            dl->AddLine(ImVec2(c.x - 2.2f, c.y + 2.2f), ImVec2(c.x + 2.2f, c.y - 2.2f), C(th::rgba(15, 16, 20, 1.0f)), 1.5f);
            break;
        case hls::Severity::Warning:
            dl->AddTriangleFilled(ImVec2(c.x, c.y - 5.5f), ImVec2(c.x + 6.0f, c.y + 4.8f), ImVec2(c.x - 6.0f, c.y + 4.8f), col);
            break;
        case hls::Severity::Info:
            dl->AddCircle(c, 5.0f, col, 16, 1.5f);
            break;
        case hls::Severity::Ok:
            dl->AddLine(ImVec2(c.x - 4.5f, c.y), ImVec2(c.x - 1.2f, c.y + 3.5f), col, 2.0f);
            dl->AddLine(ImVec2(c.x - 1.2f, c.y + 3.5f), ImVec2(c.x + 5.0f, c.y - 4.0f), col, 2.0f);
            break;
    }
}

// Axis step (s) giving ticks at least `min_px` apart.
double nice_step(double span, float width, float min_px)
{
    static constexpr double kSteps[] = { 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200 };
    for (double s : kSteps)
        if (span / s * min_px <= width) return s;
    return 14400;
}

// Unix seconds as "2024-03-01 12:00:00.000 UTC".
std::string utc_text(double unix_s)
{
    const auto whole = static_cast<std::time_t>(std::floor(unix_s));
    std::tm tm{};
    gmtime_r(&whole, &tm);
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} UTC", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                       tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>((unix_s - std::floor(unix_s)) * 1000.0));
}

// Unix seconds as the UTC time of day: "20:41:30", "20:41" (minute steps),
// "20:41:30.16" (sub-second steps), "20:41:30.160" (step 0.001).
std::string wall_label(double unix_s, double step)
{
    const double day = std::fmod(unix_s, 86400.0);
    const auto ms = static_cast<std::int64_t>(std::llround(day * 1000.0));
    const std::int64_t h = ms / 3'600'000 % 24, m = ms / 60'000 % 60, sec = ms / 1000 % 60;
    if (step <= 0.001) return std::format("{:02}:{:02}:{:02}.{:03}", h, m, sec, ms % 1000);
    if (step < 1.0)    return std::format("{:02}:{:02}:{:02}.{:02}", h, m, sec, ms % 1000 / 10);
    if (step >= 60.0)  return std::format("{:02}:{:02}", h, m);
    return std::format("{:02}:{:02}:{:02}", h, m, sec);
}

std::string axis_label(double t, double step)
{
    if (step < 1.0) return std::format("{:.2f}", t);
    const auto s = static_cast<std::int64_t>(std::llround(t));
    if (s >= 3600) return std::format("{}:{:02}:{:02}", s / 3600, s / 60 % 60, s % 60);
    return std::format("{}:{:02}", s / 60, s % 60);
}

} // namespace

// ── Model ────────────────────────────────────────────────────────────────────

double NetInspector::playlist_time_now(const Inputs& in) const
{
    const int ref = align_.rep.reference;
    if (ref < 0 || ref >= static_cast<int>(align_.rep.rows.size())) return hls::kNaN;
    const double off = align_.rep.rows[static_cast<std::size_t>(ref)].offset;
    if (std::isnan(off) || std::isnan(in.cur_pts)) return hls::kNaN;
    return in.cur_pts - off;
}

void NetInspector::update_alignment(const Inputs& in)
{
    const double now = ImGui::GetTime();
    const std::uint64_t log_seq = in.log ? in.log->change_seq() : 0;
    const bool same_inputs = align_.log_seq == log_seq && align_.scope == scope_ && align_.cat_key == cat_key_
                          && align_.cat_learned == cat_.learned;
    if (same_inputs) return;
    if (align_.built_at >= 0.0 && now - align_.built_at < kRebuildEvery && align_.scope == scope_
        && align_.cat_key == cat_key_)
        return;

    AlignModel m;
    m.log_seq = log_seq;
    m.scope = scope_;
    m.cat_key = cat_key_;
    m.cat_learned = cat_.learned;
    m.built_at = now;
    // A request matched to no rendition may be listed by a playlist since.
    if (align_.cat_key != cat_key_ || align_.cat_learned != cat_.learned) row_cat_.clear();
    const int sel_cat = sel_rend_ >= 0 && sel_rend_ < static_cast<int>(align_.rend.size())
                      ? align_.rend[static_cast<std::size_t>(sel_rend_)].cat : -1;
    // Rendition (cat_ index) of a request, worked out once per request.
    auto cat_of = [&](const Row& row) {
        auto [it, fresh] = row_cat_.try_emplace(row.r.id, -1);
        if (fresh)
            if (const NetRendition* r = cat_.match(row.r.url)) it->second = static_cast<int>(r - cat_.renditions.data());
        return it->second;
    };

    // Which renditions: what is played now, or all of them.
    const int n_cat = static_cast<int>(cat_.renditions.size());
    std::vector<int> pick;
    std::vector<bool> in_use(static_cast<std::size_t>(n_cat), false);
    auto index_of = [&](const NetRendition* r) {
        return r ? static_cast<int>(r - cat_.renditions.data()) : -1;
    };
    {
        int video = -1;
        if (in.manifest && in.playing_variant >= 0
            && in.playing_variant < static_cast<int>(in.manifest->variants().size()))
            video = index_of(cat_.match(in.manifest->variants()[static_cast<std::size_t>(in.playing_variant)].url));
        // Audio and subtitles: the rendition whose segments the player is
        // requesting. FFmpeg's hls demuxer reads a segment of every playlist
        // while it opens — audio-only variants, renditions it then discards —
        // so one counts once it had more than that, while its requests keep
        // up with the newest one.
        static constexpr std::int64_t kRecentAudio = 20'000'000, kRecentSubs = 90'000'000;   // µs
        struct Seen { int count = 0; std::int64_t last = 0; };
        std::unordered_map<int, Seen> seen;
        std::int64_t newest = 0;
        int newest_video = -1;
        for (const Row& row : rows_) {
            if (row.r.initiator != NetInitiator::Playback) continue;
            if (row.d.kind != NetKind::Segment && row.d.kind != NetKind::Subtitle) continue;
            const int ci = cat_of(row);
            if (ci < 0) continue;
            Seen& sn = seen[ci];
            ++sn.count;
            sn.last = std::max(sn.last, row.r.t_start);
            if (row.r.t_start >= newest) {
                newest = row.r.t_start;
                if (cat_.renditions[static_cast<std::size_t>(ci)].media == NetMedia::Video) newest_video = ci;
            }
        }
        if (video < 0) video = newest_video;
        int audio = -1, subs = -1;
        std::int64_t audio_t = 0, subs_t = 0;
        for (const auto& [ci, sn] : seen) {
            if (sn.count < 2) continue;
            const NetMedia med = cat_.renditions[static_cast<std::size_t>(ci)].media;
            if (med == NetMedia::Audio && newest - sn.last <= kRecentAudio && sn.last >= audio_t) {
                audio = ci;
                audio_t = sn.last;
            }
            if (med == NetMedia::Subs && newest - sn.last <= kRecentSubs && sn.last >= subs_t) {
                subs = ci;
                subs_t = sn.last;
            }
        }
        for (const int ci : { video, audio, subs })
            if (ci >= 0) in_use[static_cast<std::size_t>(ci)] = true;
        if (scope_ == Scope::All) {
            for (int i = 0; i < n_cat; ++i) pick.push_back(i);
        } else {
            for (const int ci : { video, audio, subs })
                if (ci >= 0) pick.push_back(ci);
        }
    }

    // Each one's newest playlist body → timeline (cached per request). A
    // rendition whose playlist request did not change since the last build
    // is carried over as it was — timeline, keys, start times read — so a
    // long playlist is not copied again each time a request moves.
    std::unordered_map<int, std::size_t> prev;
    for (std::size_t j = 0; j < align_.rend.size(); ++j) prev[align_.rend[j].cat] = j;
    for (const int ci : pick) {
        const NetRendition& nr = cat_.renditions[static_cast<std::size_t>(ci)];
        std::uint64_t req = 0;
        for (auto it = rows_.rbegin(); it != rows_.rend(); ++it) {
            if (it->d.kind != NetKind::Playlist || it->r.body_bytes <= 0 || it->r.failed || !it->r.done()) continue;
            if (cat_of(*it) != ci) continue;
            req = it->r.id;
            break;
        }
        if (auto pj = prev.find(ci); pj != prev.end() && req && align_.rend[pj->second].playlist_req == req) {
            AlignRend ar = std::move(align_.rend[pj->second]);
            hls::RenditionIn ri = std::move(align_.in[pj->second]);
            ri.in_use = in_use[static_cast<std::size_t>(ci)];
            ar.played.clear();
            ar.probed.clear();
            m.rend.push_back(std::move(ar));
            m.in.push_back(std::move(ri));
            continue;
        }
        AlignRend ar;
        ar.cat = ci;
        ar.playlist_req = req;
        hls::RenditionIn ri;
        ri.label  = nr.label;
        ri.kind   = track_of(nr.media);
        ri.in_use = in_use[static_cast<std::size_t>(ci)];
        if (ar.playlist_req) {
            auto c = tl_cache_.find(ar.playlist_req);
            if (c == tl_cache_.end()) {
                std::string text;
                if (sync_log_ && sync_log_->copy_body(ar.playlist_req, text)) {
                    const Row* pr = find_row(ar.playlist_req);
                    c = tl_cache_.emplace(ar.playlist_req, std::make_shared<const hls::MediaTimeline>(
                                              hls::parse_media_timeline(text, pr ? pr->r.url : nr.url))).first;
                }
            }
            if (c != tl_cache_.end()) {
                ri.tl = *c->second;
                for (const auto& sg : ri.tl.segments)
                    ar.by_key[std::format("{}@{}", sg.uri, sg.range.offset < 0 ? 0 : sg.range.offset)] = sg.seq;
            }
        } else {
            ++m.missing_playlists;
            ri.tl.problems.push_back("its playlist has not been fetched");
        }
        m.rend.push_back(std::move(ar));
        m.in.push_back(std::move(ri));
    }

    // Segment heads the logger kept → start timestamps, by sequence number.
    std::unordered_map<int, std::size_t> slot_of_cat;
    for (std::size_t k = 0; k < m.in.size(); ++k) slot_of_cat[m.rend[k].cat] = k;
    for (const Row& row : rows_) {
        if (row.d.kind != NetKind::Segment && row.d.kind != NetKind::Subtitle && row.d.kind != NetKind::Init) continue;
        auto slot = slot_of_cat.find(cat_of(row));
        if (slot == slot_of_cat.end()) continue;
        const std::size_t k = slot->second;
        auto hit = m.rend[k].by_key.find(std::format("{}@{}", row.r.url, row.r.range_start < 0 ? 0 : row.r.range_start));
        if (hit == m.rend[k].by_key.end()) continue;
        const std::int64_t seq = hit->second;
        auto& ar = m.rend[k];
        auto& list = row.r.initiator == NetInitiator::Playback ? ar.played : ar.probed;
        if (list.empty() || list.back() != seq) list.push_back(seq);
        if (row.r.body_bytes <= 0 || !(row.r.done() || row.r.body_truncated) || row.r.failed) continue;
        if (m.in[k].starts.contains(seq)) continue;

        // Its init segment (fMP4), from the timeline's EXT-X-MAP.
        const hls::MediaTimeline& tl = m.in[k].tl;
        const int si = tl.index_of(seq);
        const hls::InitInfo* init = nullptr;
        std::uint64_t init_id = 0;
        if (si >= 0) {
            const int mi = tl.segments[static_cast<std::size_t>(si)].map;
            if (mi >= 0) {
                const auto& mp = tl.maps[static_cast<std::size_t>(mi)];
                if (const Row* ir = find_body_row(mp.uri, mp.range.offset)) {
                    init_id = ir->r.id;
                    auto c = init_cache_.find(init_id);
                    if (c == init_cache_.end()) {
                        std::string data;
                        if (sync_log_ && sync_log_->copy_body(init_id, data))
                            c = init_cache_.emplace(init_id, hls::parse_init_segment(
                                reinterpret_cast<const std::uint8_t*>(data.data()), data.size())).first;
                    }
                    if (c != init_cache_.end() && c->second.ok) init = &c->second;
                }
            }
        }
        auto c = start_cache_.find(row.r.id);
        if (c == start_cache_.end() || (init && !c->second.ok())) {
            std::string data;
            if (!sync_log_ || !sync_log_->copy_body(row.r.id, data)) continue;
            c = start_cache_.insert_or_assign(row.r.id, hls::parse_segment_start(
                    reinterpret_cast<const std::uint8_t*>(data.data()), data.size(), init)).first;
        }
        if (c->second.ok()) {
            m.in[k].starts[seq] = c->second;
            ar.seg_req[seq] = row.r.id;
        }
    }

    m.rep = hls::analyze_alignment(m.in);
    // Keep the selection on the same rendition if it is still listed.
    if (sel_cat >= 0) {
        sel_rend_ = -1;
        for (std::size_t k = 0; k < m.rend.size(); ++k)
            if (m.rend[k].cat == sel_cat) sel_rend_ = static_cast<int>(k);
        if (sel_rend_ < 0) sel_seq_ = -1;
    }
    // Caches: timelines of the playlists in use; start times of requests still listed.
    for (auto it = tl_cache_.begin(); it != tl_cache_.end(); ) {
        const bool used = std::any_of(m.rend.begin(), m.rend.end(),
                                      [&](const AlignRend& a) { return a.playlist_req == it->first; });
        it = used ? std::next(it) : tl_cache_.erase(it);
    }
    if (!rows_.empty() && start_cache_.size() > rows_.size()) {
        const std::uint64_t oldest = rows_.front().r.id;
        std::erase_if(start_cache_, [&](const auto& e) { return e.first < oldest; });
        std::erase_if(row_cat_, [&](const auto& e) { return e.first < oldest; });
    }
    if (open_finding_ >= static_cast<int>(m.rep.findings.size())) open_finding_ = -1;
    if (qa_finding_ >= 0 && qa_finding_ < static_cast<int>(m.rep.findings.size())) open_finding_ = qa_finding_;
    follow_live_window(align_, m);
    align_ = std::move(m);

    if (qa_probe_ && !align_.in.empty()) {
        qa_probe_ = false;
        plan_probes(in);
    }
}

void NetInspector::follow_live_window(const AlignModel& was, const AlignModel& now)
{
    // The view is on the reference's playlist time, which a live reload
    // moves: segments dropped off the window's start take their durations
    // with them. Move the view by as much (a segment both windows list), so
    // what is on screen stays put — and stay on the live edge if the view
    // reached it.
    if (view_fit_ || view_t1_ <= view_t0_ || was.rep.reference < 0 || now.rep.reference < 0) return;
    const auto wr = static_cast<std::size_t>(was.rep.reference), nr = static_cast<std::size_t>(now.rep.reference);
    if (was.rend[wr].cat != now.rend[nr].cat || was.rend[wr].playlist_req == now.rend[nr].playlist_req) return;
    const hls::MediaTimeline& a = was.in[wr].tl;
    const hls::MediaTimeline& b = now.in[nr].tl;
    if (!a.ok || !b.ok || !b.live()) return;
    double delta = hls::kNaN;
    if (const int k = a.index_of(b.segments.front().seq); k >= 0)
        delta = b.segments.front().start - a.segments[static_cast<std::size_t>(k)].start;
    else if (const int k2 = b.index_of(a.segments.front().seq); k2 >= 0)
        delta = b.segments[static_cast<std::size_t>(k2)].start - a.segments.front().start;
    if (std::isnan(delta)) return;
    const double span = view_t1_ - view_t0_;
    if (view_t1_ >= a.total - 0.5 * std::max(a.target, 1.0)) {
        view_t1_ = std::max(view_t1_ + delta, b.total);
        view_t0_ = view_t1_ - span;
    } else {
        view_t0_ += delta;
        view_t1_ += delta;
    }
}

void NetInspector::plan_probes(const Inputs& in)
{
    std::vector<NetProbe::Job> jobs;
    const double t_play = playlist_time_now(in);
    for (std::size_t k = 0; k < align_.in.size(); ++k) {
        const auto& ri = align_.in[k];
        const auto& ar = align_.rend[k];
        if (!ar.playlist_req) {
            jobs.push_back({ cat_.renditions[static_cast<std::size_t>(ar.cat)].url, -1, -1 });
            continue;
        }
        const auto& tl = ri.tl;
        if (!tl.ok) continue;
        std::set<int> want;
        const int last = static_cast<int>(tl.segments.size()) - 1;
        want.insert(0);
        if (!tl.live()) want.insert(last);
        else want.insert(std::max(0, last - 1));
        const double shift = k < align_.rep.rows.size() ? align_.rep.rows[k].shift : 0.0;
        if (!std::isnan(t_play))
            if (const int at = tl.segment_at(t_play - shift); at >= 0) want.insert(at);
        for (int i = 0; i <= last; ++i)
            if (tl.segments[static_cast<std::size_t>(i)].discontinuity) {
                if (i > 0) want.insert(i - 1);
                want.insert(i);
            }
        int added = 0;
        for (const int i : want) {
            if (added >= 10) break;
            const auto& sg = tl.segments[static_cast<std::size_t>(i)];
            if (ri.starts.contains(sg.seq) || sg.gap) continue;
            // fMP4: its init segment first.
            if (sg.map >= 0) {
                const auto& mp = tl.maps[static_cast<std::size_t>(sg.map)];
                if (!find_body_row(mp.uri, mp.range.offset))
                    jobs.push_back({ mp.uri, mp.range.offset, mp.range.length });
            }
            const auto head = static_cast<std::int64_t>(NetworkLogger::head_bytes(sg.uri));
            const std::int64_t len = sg.range.length > 0 ? std::min(sg.range.length, head) : head;
            jobs.push_back({ sg.uri, sg.range.whole() ? 0 : sg.range.offset,
                             ri.kind == hls::Track::Text ? -1 : len });
            ++added;
        }
    }
    probe_.request(jobs);
}

std::string NetInspector::alignment_text() const
{
    std::string s;
    const auto& rep = align_.rep;
    s += std::format("reference: {}\n", rep.reference >= 0 ? align_.in[static_cast<std::size_t>(rep.reference)].label : "none");
    for (const auto& f : rep.findings)
        s += std::format("[{}] {}\n    {}\n", hls::severity_name(f.sev), f.title, f.detail);
    for (std::size_t k = 0; k < align_.in.size(); ++k) {
        const auto& ri = align_.in[k];
        const auto& row = rep.rows[k];
        s += std::format("{}: {} segs {} target {:g} mseq {} disc {} probed {} offset {} vs-ref {} boundary {}{}\n",
                         ri.label, ri.tl.segments.size(), hls::format_clock(ri.tl.total), ri.tl.target, ri.tl.media_seq,
                         ri.tl.discontinuities, row.probed, hls::format_seconds(row.offset),
                         hls::format_seconds(row.offset_vs_ref), hls::format_seconds(row.max_boundary_delta),
                         ri.in_use ? " (in use)" : "");
    }
    return s;
}

// ── Draw ─────────────────────────────────────────────────────────────────────

void NetInspector::draw_alignment(const Inputs& in, Out& out, float w, float h)
{
    (void)out;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const auto& rep = align_.rep;
    const int n = static_cast<int>(align_.in.size());

    // ── Toolbar: scope chips · probe ─────────────────────────────────────────
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + kToolbarH), C(kSideBar));
    dl->AddLine(ImVec2(p0.x, p0.y + kToolbarH - 0.5f), ImVec2(p0.x + w, p0.y + kToolbarH - 0.5f), C(th::kPanelBorder));
    {
        float cx = p0.x + kPad;
        const float cy = p0.y + (kToolbarH - 22.0f) * 0.5f;
        const struct { Scope s; const char* label; const char* tip; } chips[] = {
            { Scope::InUse, "In use", "The video variant, audio and subtitles being played" },
            { Scope::All,   "All renditions", "Every playlist the master lists" },
        };
        for (const auto& c : chips) {
            ImGui::SetCursorScreenPos(ImVec2(cx, cy));
            ImGui::PushID(static_cast<int>(c.s) + 300);
            if (chip_button("##scope", c.label, scope_ == c.s)) scope_ = c.s;
            if (ImGui::IsItemHovered()) ui::tooltip(c.tip);
            ImGui::PopID();
            cx = ImGui::GetItemRectMax().x + 5.0f;
        }
        const int pend = probe_.pending();
        const std::string label = pend > 0 ? std::format("Probing {}…", pend) : std::string("Probe media");
        const float bw = ui_fonts().ui_small->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, label.c_str()).x + 22.0f;
        ImGui::SetCursorScreenPos(ImVec2(p0.x + w - kPad - bw, cy));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
        ImGui::BeginDisabled(n == 0 || pend > 0);
        if (ImGui::Button(label.c_str(), ImVec2(bw, 22.0f))) plan_probes(in);
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ui::tooltip("Read start timestamps the player has not downloaded: the first and last segment, the "
                        "playhead's, and both sides of each discontinuity of every rendition here. Byte-range "
                        "requests of the first 32 kB (MPEG-TS: 256 kB), listed in Network as initiator \"inspector\".");
    }

    const float body_top = p0.y + kToolbarH;
    ImGui::SetCursorScreenPos(ImVec2(p0.x, body_top));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad + 2.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::BeginChild("##alignment", ImVec2(w, h - kToolbarH), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImDrawList* cdl = ImGui::GetWindowDrawList();
    const float cw = ImGui::GetContentRegionAvail().x;

    if (n == 0) {
        draw_empty_state(cat_.renditions.empty() ? "Not an HLS stream" : "Nothing to compare yet",
                         cat_.renditions.empty()
                             ? "Alignment compares the playlists of an HLS master's renditions."
                             : "Waiting for the player to request a rendition. Pick \"All renditions\" to compare every playlist.",
                         cw, h - kToolbarH - 30.0f);
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        return;
    }

    // ── Verdict ──────────────────────────────────────────────────────────────
    {
        const ImVec2 vp = ImGui::GetCursorScreenPos();
        ImFont* f = ui_fonts().ui;
        std::string verdict;
        ImU32 vcol;
        if (rep.errors > 0) {
            verdict = std::format("{} problem{}{}", rep.errors, rep.errors == 1 ? "" : "s",
                                  rep.warnings ? std::format(", {} warning{}", rep.warnings, rep.warnings == 1 ? "" : "s") : "");
            vcol = th::kError;
        } else if (rep.warnings > 0) {
            verdict = std::format("{} warning{}", rep.warnings, rep.warnings == 1 ? "" : "s");
            vcol = th::kWarn;
        } else {
            verdict = "Aligned";
            vcol = kColManifest;
        }
        sev_icon(cdl, ImVec2(vp.x + 7.0f, vp.y + 10.0f),
                 rep.errors ? hls::Severity::Error : rep.warnings ? hls::Severity::Warning : hls::Severity::Ok);
        cdl->AddText(f, 15.0f, ImVec2(vp.x + 20.0f, vp.y + 1.0f), C(vcol), verdict.c_str());
        const std::string count = std::format("{} rendition{}", n, n == 1 ? "" : "s");
        const std::string ref = rep.reference >= 0 && n > 1
            ? std::format("{} · compared with {}", count, align_.in[static_cast<std::size_t>(rep.reference)].label)
            : count;
        ImFont* fs = ui_fonts().ui_small;
        const float rw = fs->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, ref.c_str()).x;
        const float vw = f->CalcTextSizeA(15.0f, FLT_MAX, 0.0f, verdict.c_str()).x;
        if (vp.x + 20.0f + vw + 16.0f < vp.x + cw - rw)
            cdl->AddText(fs, 12.0f, ImVec2(vp.x + cw - rw, vp.y + 3.0f), C(th::kTextDim), ref.c_str());
        ImGui::Dummy(ImVec2(cw, 22.0f));
    }

    // ── Findings ─────────────────────────────────────────────────────────────
    hover_finding_ = -1;
    {
        ImFont* fs = ui_fonts().ui_small;
        const float wrap = cw - 26.0f;
        for (int i = 0; i < static_cast<int>(rep.findings.size()); ++i) {
            const auto& f = rep.findings[static_cast<std::size_t>(i)];
            // A clean bill of health has nothing to expand: its detail shows.
            const bool open = open_finding_ == i || f.sev == hls::Severity::Ok;
            const ImVec2 fp = ImGui::GetCursorScreenPos();
            const ImVec2 ts = fs->CalcTextSizeA(13.0f, FLT_MAX, wrap, f.title.c_str());
            const ImVec2 ds = open ? fs->CalcTextSizeA(12.0f, FLT_MAX, wrap, f.detail.c_str()) : ImVec2(0, 0);
            const float fh = 8.0f + ts.y + (open ? ds.y + 6.0f : 0.0f) + 6.0f;
            ImGui::PushID(i + 400);
            const bool clicked = ImGui::InvisibleButton("##f", ImVec2(cw, fh));
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hov) hover_finding_ = i;
            if (clicked) {
                open_finding_ = open ? -1 : i;
                // Bring its time into view.
                if (!open && !std::isnan(f.time) && view_t1_ > view_t0_) {
                    const double span = view_t1_ - view_t0_;
                    view_t0_ = f.time - span * 0.3;
                    view_t1_ = view_t0_ + span;
                    view_fit_ = false;
                }
            }
            cdl->AddRectFilled(fp, ImVec2(fp.x + cw, fp.y + fh),
                               C(open ? th::rgba(255, 255, 255, 0.06f) : hov ? th::rgba(255, 255, 255, 0.04f)
                                                                            : th::rgba(255, 255, 255, 0.02f)), 4.0f);
            cdl->AddRectFilled(fp, ImVec2(fp.x + 2.5f, fp.y + fh), C(th::alpha(sev_color(f.sev), 0.8f)), 1.0f);
            sev_icon(cdl, ImVec2(fp.x + 13.0f, fp.y + 4.0f + 8.5f), f.sev);
            cdl->AddText(fs, 13.0f, ImVec2(fp.x + 24.0f, fp.y + 5.0f), C(th::kText), f.title.c_str(), nullptr, wrap);
            if (open)
                cdl->AddText(fs, 12.0f, ImVec2(fp.x + 24.0f, fp.y + 9.0f + ts.y), C(th::kTextDim), f.detail.c_str(),
                             nullptr, wrap);
            if (hov && !open) ui::tooltip("Click for details");
            if (hov && open && f.sev != hls::Severity::Ok && !std::isnan(f.time))
                ui::tooltip(std::format("At {} — the timeline scrolled there", hls::format_clock(f.time)).c_str());
            ImGui::Dummy(ImVec2(0.0f, 1.0f));
        }
    }

    // ── Timeline ─────────────────────────────────────────────────────────────
    section_caption("TIMELINE");
    {
        // Extent over every rendition, on the reference's playlist time.
        double lo = 0.0, hi = 1.0;
        bool first = true;
        for (int k = 0; k < n; ++k) {
            const auto& tl = align_.in[static_cast<std::size_t>(k)].tl;
            if (!tl.ok) continue;
            const double sh = rep.rows[static_cast<std::size_t>(k)].shift;
            const double a = tl.segments.front().start + sh, b = tl.total + sh;
            lo = first ? a : std::min(lo, a);
            hi = first ? b : std::max(hi, b);
            first = false;
        }
        if (hi <= lo) hi = lo + 1.0;
        // Live: what was played, on to the live edge — the rest of the window
        // is history nobody played.
        const int ref = rep.reference;
        const hls::MediaTimeline* rtl = ref >= 0 ? &align_.in[static_cast<std::size_t>(ref)].tl : nullptr;
        const bool live = rtl && rtl->ok && rtl->live();
        double fit_lo = lo;
        if (live) {
            double from = playlist_time_now(in);
            for (int k = 0; k < n; ++k) {
                const auto& ri = align_.in[static_cast<std::size_t>(k)];
                for (const std::int64_t q : align_.rend[static_cast<std::size_t>(k)].played)
                    if (const int si = ri.tl.index_of(q); si >= 0) {
                        const double t = ri.tl.segments[static_cast<std::size_t>(si)].start
                                       + rep.rows[static_cast<std::size_t>(k)].shift;
                        from = std::isnan(from) ? t : std::min(from, t);
                    }
            }
            const double margin = 2.0 * std::max(rtl->target, 1.0);
            fit_lo = std::isnan(from) ? hi - 120.0 : std::min(from - margin, hi - 120.0);
            fit_lo = std::max(lo, fit_lo);
        }
        // Until the view is zoomed or panned it fits what there is, as it grows.
        if (view_fit_ || view_t1_ <= view_t0_) {
            view_t0_ = fit_lo;
            view_t1_ = hi;
        }
        // Live with dates: the axis in wall-clock time (UTC), the moments the
        // reference's segments are dated with.
        const double wall0 = live && !std::isnan(rtl->segments.front().pdt)
                           ? rtl->segments.front().pdt - rtl->segments.front().start : hls::kNaN;

        const ImVec2 tp = ImGui::GetCursorScreenPos();
        const float track_x0 = tp.x + kLabelW + 6.0f;
        const float track_x1 = tp.x + cw - 4.0f;
        const float track_w = std::max(10.0f, track_x1 - track_x0);
        const float tl_h = kAxisH + n * kRowH + 4.0f;
        const double span = view_t1_ - view_t0_;
        auto X = [&](double t) { return track_x0 + static_cast<float>((t - view_t0_) / span) * track_w; };
        auto T = [&](float x) { return view_t0_ + (x - track_x0) / track_w * span; };

        // Interaction: wheel zooms at the mouse, drag pans, double-click fits.
        ImGui::SetCursorScreenPos(ImVec2(track_x0, tp.y));
        ImGui::InvisibleButton("##tl", ImVec2(track_w, tl_h));
        const bool tl_hov = ImGui::IsItemHovered();
        const ImGuiIO& io = ImGui::GetIO();
        if (tl_hov && io.MouseWheel != 0.0f) {
            const double at = T(io.MousePos.x);
            const double f = io.MouseWheel > 0 ? 0.8 : 1.25;
            double ns = std::clamp(span * f, 0.5, (hi - lo) * 1.5 + 1.0);
            view_t0_ = at - (at - view_t0_) / span * ns;
            view_t1_ = view_t0_ + ns;
            view_fit_ = false;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            const double dt = -io.MouseDelta.x / track_w * span;
            view_t0_ += dt;
            view_t1_ += dt;
            view_fit_ = false;
        }
        if (tl_hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            view_t0_ = fit_lo;
            view_t1_ = hi;
            view_fit_ = true;
        }
        const bool clicked_tl = tl_hov && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
                             && std::fabs(io.MouseDragMaxDistanceSqr[0]) < 9.0f;

        cdl->PushClipRect(ImVec2(tp.x, tp.y), ImVec2(tp.x + cw, tp.y + tl_h), true);
        // Axis.
        const double step = nice_step(span, track_w, 64.0f);
        ImFont* fs = ui_fonts().ui_small;
        const double base = std::isnan(wall0) ? 0.0 : wall0;   // ticks at round times of the axis' clock
        for (double u = std::ceil((view_t0_ + base) / step) * step; u <= view_t1_ + base; u += step) {
            const float x = X(u - base);
            cdl->AddLine(ImVec2(x, tp.y + kAxisH - 5.0f), ImVec2(x, tp.y + tl_h), C(th::rgba(255, 255, 255, 0.05f)));
            const std::string lab = std::isnan(wall0) ? axis_label(u, step) : wall_label(u, step);
            cdl->AddText(fs, 11.0f, ImVec2(x + 3.0f, tp.y + 3.0f), C(th::kTextFaint), lab.c_str());
        }
        // Rows.
        const double ref_off = ref >= 0 ? rep.rows[static_cast<std::size_t>(ref)].offset : hls::kNaN;
        int hover_k = -1;
        std::int64_t hover_seq = -1;
        for (int k = 0; k < n; ++k) {
            const auto& ri = align_.in[static_cast<std::size_t>(k)];
            const auto& ar = align_.rend[static_cast<std::size_t>(k)];
            const auto& rr = rep.rows[static_cast<std::size_t>(k)];
            const float y0 = tp.y + kAxisH + k * kRowH;
            const float y1 = y0 + kRowH - 5.0f;
            const NetRendition& nr = cat_.renditions[static_cast<std::size_t>(ar.cat)];
            const ImU32 mcol = nr.media == NetMedia::Audio ? kColAudio : nr.media == NetMedia::Subs ? kColSubs : kColVideo;
            bool flagged = false;
            if (hover_finding_ >= 0)
                for (const int w2 : rep.findings[static_cast<std::size_t>(hover_finding_)].renditions) flagged |= w2 == k;
            // Label.
            cdl->AddRectFilled(ImVec2(tp.x, y0 + 3.0f), ImVec2(tp.x + 3.0f, y1 - 3.0f), C(mcol), 1.0f);
            std::string lab = ri.label;
            while (lab.size() > 4 && fs->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, lab.c_str()).x > kLabelW - 18.0f)
                lab = lab.substr(0, lab.size() - 4) + "…";
            cdl->AddText(fs, 12.0f, ImVec2(tp.x + 8.0f, y0 + 3.0f),
                         C(flagged ? th::kText : ri.in_use ? th::kText : th::kTextDim), lab.c_str());
            if (k == ref)
                cdl->AddText(fs, 10.0f, ImVec2(tp.x + kLabelW - 22.0f, y0 + 4.5f), C(th::kTextFaint), "ref");
            if (rr.worst == hls::Severity::Error || rr.worst == hls::Severity::Warning)
                cdl->AddCircleFilled(ImVec2(tp.x + kLabelW - 2.0f, y0 + 9.5f), 3.0f, C(sev_color(rr.worst)), 10);
            // Track background.
            cdl->AddRectFilled(ImVec2(track_x0, y0), ImVec2(track_x1, y1),
                               C(flagged ? th::alpha(sev_color(rep.findings[static_cast<std::size_t>(hover_finding_)].sev), 0.10f)
                                         : th::rgba(255, 255, 255, 0.03f)), 2.0f);
            if (!ri.tl.ok) {
                cdl->AddText(fs, 11.0f, ImVec2(track_x0 + 6.0f, y0 + 3.0f), C(th::kTextFaint),
                             ri.tl.problems.empty() ? "no playlist" : ri.tl.problems.front().c_str());
                continue;
            }
            const double sh = rr.shift;
            const std::set<std::int64_t> played(ar.played.begin(), ar.played.end());
            const std::set<std::int64_t> probed(ar.probed.begin(), ar.probed.end());
            const auto& segs = ri.tl.segments;
            const int a = std::max(0, ri.tl.segment_at(view_t0_ - sh) < 0 ? 0 : ri.tl.segment_at(view_t0_ - sh));
            const float px_per_seg = static_cast<float>(ri.tl.total > 0 ? track_w / span * (ri.tl.total / segs.size()) : 0);
            for (int i = a; i < static_cast<int>(segs.size()); ++i) {
                const auto& sg = segs[static_cast<std::size_t>(i)];
                const double t0 = sg.start + sh, t1 = t0 + sg.duration;
                if (t0 > view_t1_) break;
                if (t1 < view_t0_) continue;
                const float x0 = X(t0), x1 = X(t1);
                if (played.contains(sg.seq))
                    cdl->AddRectFilled(ImVec2(x0, y0 + 2.0f), ImVec2(std::max(x1, x0 + 1.0f), y1 - 2.0f), C(th::alpha(mcol, 0.30f)));
                else if (probed.contains(sg.seq))
                    cdl->AddRectFilled(ImVec2(x0, y0 + 2.0f), ImVec2(std::max(x1, x0 + 1.0f), y1 - 2.0f), C(th::alpha(mcol, 0.14f)));
                if (sg.gap)
                    for (float hx = x0; hx < x1; hx += 5.0f)
                        cdl->AddLine(ImVec2(hx, y1 - 2.0f), ImVec2(std::min(hx + 5.0f, x1), y0 + 2.0f), C(th::alpha(th::kWarn, 0.5f)));
                if (px_per_seg >= 3.0f || sg.discontinuity)
                    cdl->AddLine(ImVec2(x0, y0 + (sg.discontinuity ? 0.0f : 5.0f)), ImVec2(x0, y1 - (sg.discontinuity ? 0.0f : 5.0f)),
                                 C(sg.discontinuity ? th::kWarn : th::rgba(255, 255, 255, 0.22f)), sg.discontinuity ? 2.0f : 1.0f);
                // A start timestamp read: a diamond coloured by its offset vs the reference.
                if (auto st = ri.starts.find(sg.seq); st != ri.starts.end()) {
                    double off = hls::kNaN;
                    if (ri.kind == hls::Track::Text) {
                        if (const auto* t = st->second.first(hls::Track::Text)) off = t->local_zero;
                    } else {
                        double v = st->second.start(ri.kind == hls::Track::Audio ? hls::Track::Audio : hls::Track::Video);
                        if (std::isnan(v)) v = st->second.start(hls::Track::Audio);
                        if (!std::isnan(v)) off = v - sg.start;
                    }
                    const double d = std::isnan(off) || std::isnan(ref_off) ? hls::kNaN : off - ref_off - sh;
                    const ImU32 dc = std::isnan(d) ? th::kTextDim
                                   : std::fabs(d) <= hls::kAlignOk ? kColManifest
                                   : std::fabs(d) <= hls::kAlignBad ? th::kWarn : th::kError;
                    const ImVec2 c((x0 + std::min(x1, x0 + 8.0f)) * 0.5f + 1.0f, (y0 + y1) * 0.5f);
                    cdl->AddQuadFilled(ImVec2(c.x, c.y - 4.5f), ImVec2(c.x + 4.5f, c.y), ImVec2(c.x, c.y + 4.5f),
                                       ImVec2(c.x - 4.5f, c.y), C(dc));
                }
                const bool sel = k == sel_rend_ && sg.seq == sel_seq_;
                if (sel)
                    cdl->AddRect(ImVec2(x0, y0), ImVec2(std::max(x1, x0 + 3.0f), y1), C(th::kText), 1.0f, 0, 1.5f);
                if (tl_hov && io.MousePos.y >= y0 && io.MousePos.y < y0 + kRowH && io.MousePos.x >= x0 && io.MousePos.x < x1) {
                    hover_k = k;
                    hover_seq = sg.seq;
                }
            }
        }
        // Playhead.
        const double tnow = playlist_time_now(in);
        if (!std::isnan(tnow) && tnow >= view_t0_ && tnow <= view_t1_) {
            const float x = X(tnow);
            cdl->AddLine(ImVec2(x, tp.y + kAxisH - 4.0f), ImVec2(x, tp.y + tl_h - 2.0f), C(th::kText), 1.5f);
            cdl->AddTriangleFilled(ImVec2(x - 4.0f, tp.y + kAxisH - 8.0f), ImVec2(x + 4.0f, tp.y + kAxisH - 8.0f),
                                   ImVec2(x, tp.y + kAxisH - 3.0f), C(th::kText));
        }
        cdl->PopClipRect();

        if (hover_k >= 0) {
            const auto& ri = align_.in[static_cast<std::size_t>(hover_k)];
            const int si = ri.tl.index_of(hover_seq);
            if (si >= 0) {
                const auto& sg = ri.tl.segments[static_cast<std::size_t>(si)];
                std::string tip = ri.tl.live() && !std::isnan(sg.pdt)
                    ? std::format("{} · segment #{}\n{} – {} UTC ({:.3f} s)", ri.label, sg.seq,
                                  wall_label(sg.pdt, 0.001), wall_label(sg.pdt + sg.duration, 0.001), sg.duration)
                    : std::format("{} · segment #{}\n{} – {} ({:.3f} s)", ri.label, sg.seq,
                                  hls::format_clock(sg.start), hls::format_clock(sg.start + sg.duration),
                                  sg.duration);
                if (sg.discontinuity) tip += "\nEXT-X-DISCONTINUITY before it";
                if (auto st = ri.starts.find(sg.seq); st != ri.starts.end())
                    for (const auto& t : st->second.tracks)
                        if (!std::isnan(t.start))
                            tip += std::format("\n{} starts at {}", hls::track_name(t.kind), hls::format_clock(t.start));
                tip += "\nClick to select";
                ui::tooltip(tip.c_str());
            }
        }
        if (clicked_tl) {
            sel_rend_ = hover_k;
            sel_seq_ = hover_seq;
        }
        ImGui::SetCursorScreenPos(ImVec2(tp.x, tp.y + tl_h + 2.0f));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Wheel zooms, drag pans, double-click fits. Bars: segments the player requested (faint: "
                            "probed). Diamonds: start timestamps read — green aligned with the reference, amber or red "
                            "off. Amber lines: discontinuities.");
        ImGui::PopTextWrapPos();
    }

    // ── Selected segment ────────────────────────────────────────────────────
    if (sel_rend_ >= 0 && sel_rend_ < n) {
        const auto& ri = align_.in[static_cast<std::size_t>(sel_rend_)];
        const auto& ar = align_.rend[static_cast<std::size_t>(sel_rend_)];
        const int si = ri.tl.index_of(sel_seq_);
        if (si >= 0) {
            const auto& sg = ri.tl.segments[static_cast<std::size_t>(si)];
            section_caption(std::format("SEGMENT #{} · {}", sg.seq, ri.label).c_str());
            if (begin_kv_table("##selseg", 112.0f)) {
                kv_row("Playlist time", std::format("{} – {}  (EXTINF {:.3f} s)", hls::format_clock(sg.start),
                                                    hls::format_clock(sg.start + sg.duration), sg.duration));
                std::string res = net_request_name(sg.uri);
                if (!sg.range.whole()) res += std::format("  bytes {}–{}", sg.range.offset, sg.range.offset + sg.range.length - 1);
                kv_row("Resource", res);
                if (sg.discontinuity || sg.disc_seq != ri.tl.disc_seq)
                    kv_row("Period", std::format("discontinuity sequence {}{}", sg.disc_seq,
                                                 sg.discontinuity ? " (starts here)" : ""), th::kWarn);
                if (!std::isnan(sg.pdt))
                    kv_row("Date", std::format("{}{}", utc_text(sg.pdt), sg.pdt_explicit ? "" : " (carried on)"));
                if (auto st = ri.starts.find(sg.seq); st != ri.starts.end()) {
                    for (const auto& t : st->second.tracks) {
                        std::string v = std::isnan(t.start) ? std::string("—") : hls::format_clock(t.start);
                        if (!std::isnan(t.local_zero)) v = std::format("cue time 0 = media {}", hls::format_seconds(t.local_zero));
                        if (!t.note.empty()) v += "  (" + t.note + ")";
                        kv_row(std::format("{} start", hls::track_name(t.kind)).c_str(), v);
                    }
                    kv_row("Format", hls::format_name(st->second.format));
                } else {
                    kv_row("Start", "not read — probe it, or play past it", th::kTextDim);
                }
                ImGui::EndTable();
            }
            const Row* req = nullptr;
            if (auto it = ar.seg_req.find(sg.seq); it != ar.seg_req.end()) req = find_row(it->second);
            if (!req)
                for (auto it = rows_.rbegin(); it != rows_.rend(); ++it)
                    if (it->r.url == sg.uri && (sg.range.whole() ? it->r.range_start <= 0 : it->r.range_start == sg.range.offset)) {
                        req = &*it;
                        break;
                    }
            ImGui::BeginDisabled(!req);
            if (ImGui::Button("Show request")) {
                selected_ = req->r.id;
                tab_ = Tab::Network;
                detail_tab_ = 1;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            const bool have_start = ri.starts.contains(sg.seq);
            ImGui::BeginDisabled(have_start);
            if (ImGui::Button("Probe this segment")) {
                std::vector<NetProbe::Job> jobs;
                if (sg.map >= 0) {
                    const auto& mp = ri.tl.maps[static_cast<std::size_t>(sg.map)];
                    if (!find_body_row(mp.uri, mp.range.offset)) jobs.push_back({ mp.uri, mp.range.offset, mp.range.length });
                }
                const auto head = static_cast<std::int64_t>(NetworkLogger::head_bytes(sg.uri));
                const std::int64_t len = sg.range.length > 0 ? std::min(sg.range.length, head) : head;
                jobs.push_back({ sg.uri, sg.range.whole() ? 0 : sg.range.offset, ri.kind == hls::Track::Text ? -1 : len });
                probe_.request(jobs);
            }
            ImGui::EndDisabled();
        }
    }

    // ── Renditions table ─────────────────────────────────────────────────────
    section_caption("RENDITIONS");
    constexpr ImGuiTableFlags kTbl = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollX;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, th::vec4(th::rgba(255, 255, 255, 0.025f)));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, th::vec4(th::rgba(255, 255, 255, 0.04f)));
    // "Starts at": the media time at the reference's playlist time 0, the
    // same moment for every row (a live window is fetched at its own time).
    auto starts_at = [&](int k) {
        const auto& rr = rep.rows[static_cast<std::size_t>(k)];
        return std::isnan(rr.offset) ? std::string("—") : hls::format_clock(rr.offset - rr.shift);
    };
    float seq_w = 40.0f, starts_w = 66.0f;
    for (int k = 0; k < n; ++k) {
        const auto& ri = align_.in[static_cast<std::size_t>(k)];
        ImFont* f = ImGui::GetFont();
        seq_w = std::max(seq_w, f->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f,
                                                 std::to_string(ri.tl.media_seq).c_str()).x + 2.0f);
        starts_w = std::max(starts_w, f->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f,
                                                       starts_at(k).c_str()).x + 2.0f);
    }
    if (ImGui::BeginTable("##aligntbl", 8, kTbl)) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Rendition", ImGuiTableColumnFlags_WidthFixed, 112.0f);
        ImGui::TableSetupColumn("Segs",      ImGuiTableColumnFlags_WidthFixed, 34.0f);
        ImGui::TableSetupColumn("Duration",  ImGuiTableColumnFlags_WidthFixed, 66.0f);
        ImGui::TableSetupColumn("Seq",       ImGuiTableColumnFlags_WidthFixed, seq_w);
        ImGui::TableSetupColumn("Disc",      ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Starts at", ImGuiTableColumnFlags_WidthFixed, starts_w);
        ImGui::TableSetupColumn("vs ref",    ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("Read",      ImGuiTableColumnFlags_WidthFixed, 34.0f);
        ImGui::TableHeadersRow();
        for (int k = 0; k < n; ++k) {
            const auto& ri = align_.in[static_cast<std::size_t>(k)];
            const auto& rr = rep.rows[static_cast<std::size_t>(k)];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 20.0f);
            if (k == rep.reference) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, C(kPlayRow));
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(k + 500);
            if (ImGui::Selectable(ri.label.c_str(), sel_rend_ == k, ImGuiSelectableFlags_SpanAllColumns)) {
                sel_rend_ = k;
                if (!ri.tl.segments.empty() && ri.tl.index_of(sel_seq_) < 0) sel_seq_ = ri.tl.segments.front().seq;
            }
            if (ImGui::IsItemHovered()) {
                const auto& ar = align_.rend[static_cast<std::size_t>(k)];
                ui::tooltip(std::format("{}{}{}", cat_.renditions[static_cast<std::size_t>(ar.cat)].url,
                                        ri.in_use ? "\nIn use" : "", k == rep.reference ? "\nReference" : "").c_str());
            }
            ImGui::PopID();
            const ImU32 dim = th::kTextDim;
            ImGui::TableSetColumnIndex(1); text_right(ri.tl.ok ? std::to_string(ri.tl.segments.size()) : "—", th::kText);
            ImGui::TableSetColumnIndex(2); text_right(ri.tl.ok ? hls::format_clock(ri.tl.total) : "—", th::kText);
            ImGui::TableSetColumnIndex(3); text_right(ri.tl.ok ? std::to_string(ri.tl.media_seq) : "—", dim);
            ImGui::TableSetColumnIndex(4);
            text_right(ri.tl.ok ? std::to_string(ri.tl.discontinuities) : "—", ri.tl.discontinuities ? th::kWarn : dim);
            ImGui::TableSetColumnIndex(5);
            text_right(starts_at(k), th::kText);
            if (ImGui::IsItemHovered())
                ui::tooltip(ri.tl.live() ? "Media time at the start of the reference's playlist window, from the first "
                                           "segment whose start was read (the window slides with each reload)"
                                         : "Media time at playlist time 0, from the first segment whose start was read");
            ImGui::TableSetColumnIndex(6);
            const double d = rr.offset_vs_ref;
            const ImU32 dc = std::isnan(d) ? dim : std::fabs(d) <= hls::kAlignOk ? kColManifest
                           : std::fabs(d) <= hls::kAlignBad ? th::kWarn : th::kError;
            text_right(k == rep.reference ? "ref" : std::isnan(d) ? "—" : (d > 0 ? "+" : "") + hls::format_seconds(d), dc);
            ImGui::TableSetColumnIndex(7); text_right(std::to_string(rr.probed), dim);
            if (ImGui::IsItemHovered()) ui::tooltip("Segments whose start timestamps were read");
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    if (align_.missing_playlists > 0) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%d playlist%s not fetched yet — Probe media fetches %s.", align_.missing_playlists,
                            align_.missing_playlists == 1 ? "" : "s", align_.missing_playlists == 1 ? "it" : "them");
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}
