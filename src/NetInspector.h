#pragma once

// Network inspector: a docked right-hand sidebar (N key) modelled on Chrome
// DevTools' Network panel — sortable request table with a waterfall, filter
// box + type chips, a details pane (headers / response / timing) for the
// selected row, a "Manifest" tab listing the parsed variants, renditions and
// timeline tags, an "Alignment" tab checking that the renditions played
// together line up (HlsAlign.h): playlist timelines, discontinuities and the
// start timestamps inside the segments, on a timeline per rendition — and a
// "Conformance" tab checking that the master declares what its media holds
// (HlsConform.h): BANDWIDTH, CODECS, RESOLUTION, FRAME-RATE, VIDEO-RANGE and
// the segments, read on request by its own probe (HlsConformProbe.h) — or,
// in its DASH mode, an MPD given by URL (DashConform.h, DashConformProbe.h),
// which the player itself cannot open. Data
// comes from NetworkLogger (requests and the response bodies it keeps);
// layout is Dear ImGui (main thread). NetInspectorResponse.cpp,
// NetInspectorAlign.cpp and NetInspectorConform.cpp hold the Response,
// Alignment and Conformance views.

#include "DashConformProbe.h"
#include "HlsAlign.h"
#include "HlsConformProbe.h"
#include "NetDescribe.h"
#include "NetProbe.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class NetworkLogger;
class ManifestParser;

class NetInspector {
public:
    struct Inputs {
        NetworkLogger*        log      = nullptr;
        const ManifestParser* manifest = nullptr;
        std::string           media_url;            // what is playing (manifest URL for HLS/DASH)
        bool                  local_media     = false;
        int                   playing_variant = -1; // index into manifest->variants(), −1 unknown
        double                cur_pts  = 0.0;
        double                duration = 0.0;
        // Where positions count from (UiFormat.h): the Manifest tab's tag
        // times are positions; cur_pts and seek_to are absolute PTS.
        double                start    = 0.0;
    };
    struct Out {
        double seek_to   = -1.0;   // PTS of the tag row clicked in the Manifest tab
        bool   close     = false;  // × clicked
        bool   width_set = false;  // the user resized it (or reset the width): user_width() to keep
    };

    // The width the user dragged the sidebar to (logical px), < 0 =
    // automatic; set_user_width restores one kept from an earlier run.
    float user_width() const { return width_; }
    void  set_user_width(float w) { width_ = w > 0.0f ? w : -1.0f; }

    // Docked width (logical px) the sidebar takes in a window `win_w` wide.
    float dock_width(float win_w) const;

    // Draw the sidebar flush right, full height. Call between ImGui frames.
    Out draw(const Inputs& in, float win_w, float win_h);

    // New media session: drop the selection and the cached manifest catalog.
    void reset();

    // QA (--ui-net): comma-separated "tab=manifest|alignment", "select=segment"
    // (newest segment row) / "select=playlist" (newest media playlist),
    // "detail=response|timing", "mode=segments" (Response: parsed playlist),
    // "filter=<text>", "chip=video|audio|...", "width=<px>",
    // "scope=all" (Alignment: every rendition), "probe=1" (probe once ready),
    // "finding=<n>" (expand the n-th finding, 0-based), "tab=conformance",
    // "conform=1" (run the conformance check once the manifest is known),
    // "conform_segments=<n>" (0 = every segment), "conform_out=<path>" (write
    // its JSON report there when it is done), "conform_html=<path>" (and its
    // HTML report), "marker_tags=<A+B>" (tags for the marker check, '+'
    // between), "markers_only=1", "marker_open=<n>" (expand the n-th splice
    // point's card, 0-based as finding=), "dash=<MPD URL or path>" (the tab's
    // DASH mode, checking that MPD at once) or "hls=<master URL or path>" (the
    // HLS check of that master, whatever is playing, or with nothing playing);
    // dash= and hls= last, as the URL may hold commas. With no media given,
    // the player applies the spec on its launcher (conform=1 then has no
    // master to check: use hls=).
    void qa_apply(const std::string& spec);

    // The Conformance tab on `url`, an HLS master (or an MPD when `dash`),
    // checked at once: what hls= and dash= do, and the launcher's "Check it"
    // after an open that failed.
    void check_url(const std::string& url, bool dash);

    // The Conformance tab with its URL field focused: the launcher's link.
    void open_conformance();

    // QA: nothing left to wait for before a capture. No check pending or
    // running, and the last result taken in. conform=1 is not waited for: it
    // needs a playing master.
    bool qa_idle() const;

    // QA: the alignment report as text (findings, then one line per rendition).
    std::string alignment_text() const;

private:
    enum class Tab { Network, Manifest, Alignment, Conformance };
    enum class Chip { All, Manifest, Video, Audio, Subs, Errors };

    struct Row {
        NetRequest  r;
        NetDesc     d;
        std::string name;
    };

    void sync(const Inputs& in);
    // Teach cat_ the segments a finished playlist response lists (once per
    // request); true if it learned something new.
    bool learn_from(const Row& row);
    void rebuild_view();
    bool row_visible(const Row& row) const;

    void draw_header(float w);
    void draw_network(const Inputs& in, float w, float h);
    void draw_toolbar(const Inputs& in, float w);
    void draw_table(float w, float h);
    void draw_details(float w, float h);
    void draw_footer(float w);
    void draw_manifest(const Inputs& in, Out& out, float w, float h);
    void draw_response(const Row& row, float w, float h);
    void draw_alignment(const Inputs& in, Out& out, float w, float h);
    void draw_conformance(const Inputs& in, Out& out, float w, float h);

    const Row* find_row(std::uint64_t id) const;
    // A playlist name in the Manifest tab: click shows its newest request's response.
    void playlist_link(const std::string& label, const std::string& url, int id);
    // The newest request for `url` (+ byte range start; -1 = whole) that kept a body.
    const Row* find_body_row(const std::string& url, std::int64_t range_start) const;
    // Parsed init segment of the rendition a segment row belongs to (newest
    // init request of that rendition with a body), or nullptr.
    const hls::InitInfo* init_for(const Row& row);

    // ── Alignment model (NetInspectorAlign.cpp) ──────────────────────────────
    void update_alignment(const Inputs& in);
    void plan_probes(const Inputs& in);
    double playlist_time_now(const Inputs& in) const;   // playhead on the reference timeline, NaN if unknown
    struct AlignModel;
    void follow_live_window(const AlignModel& was, const AlignModel& now);

    // Data
    std::vector<Row>   rows_;            // ascending id
    std::uint64_t      seq_ = 0, gen_ = ~0ull;
    NetworkLogger*     sync_log_ = nullptr;   // from the last sync(): bodies are copied from it
    NetCatalog         cat_;
    std::string        cat_key_;
    std::unordered_set<std::uint64_t> learned_ids_;   // playlist requests cat_ has learned from
    std::int64_t       base_t_ = -1;     // t_start of the first row (relative clock)

    // View
    std::vector<int>   view_;            // indices into rows_, filtered + sorted
    bool               view_dirty_ = true;
    int                sort_col_ = -1;   // -1 = chronological
    bool               sort_desc_ = false;

    // Response view: the selected row's body, parsed once per change.
    struct BodyView {
        std::uint64_t id = 0;
        std::int64_t  bytes = -1;
        std::string   data;
        bool          text = false;
        std::vector<std::uint32_t> lines;    // line start offsets (text)
        hls::MediaTimeline tl;               // when the body is a media playlist
        std::string   media;                 // binary: timestamps / boxes summary
        std::vector<std::string> media_lines;
    };
    BodyView      body_;
    int           resp_mode_ = 0;            // 0 raw / hex, 1 parsed segments
    char          find_[128] = {};
    int           find_cursor_ = -1;         // line of the current match
    bool          find_scroll_ = false;

    // Alignment
    enum class Scope { InUse, All };
    struct AlignRend {
        int          cat = -1;               // index into cat_.renditions
        std::uint64_t playlist_req = 0;      // request whose body is its timeline
        std::vector<std::int64_t> played;    // sequence numbers the player requested
        std::vector<std::int64_t> probed;    // … the inspector / helpers requested
        std::unordered_map<std::int64_t, std::uint64_t> seg_req;   // sequence → request with a head
        std::unordered_map<std::string, std::int64_t> by_key;      // "url@offset" → sequence (its timeline's)
    };
    struct AlignModel {
        std::vector<AlignRend>        rend;
        std::vector<hls::RenditionIn> in;
        hls::AlignReport              rep;
        std::uint64_t                 log_seq = ~0ull;
        Scope                         scope = Scope::InUse;
        std::string                   cat_key;
        std::uint64_t                 cat_learned = 0;
        double                        built_at = -1.0;
        int                           missing_playlists = 0;
        // The break markers of the same playlists (HlsMarkers.h), with the
        // Conformance tab's marker tags: drawn on the timeline.
        std::string                   marker_tags;
        hls::MarkerReport             markers;
    };
    AlignModel    align_;
    Scope         scope_ = Scope::InUse;
    NetProbe      probe_;
    bool          qa_probe_ = false;
    int           qa_finding_ = -1;
    std::unordered_map<std::uint64_t, std::shared_ptr<const hls::MediaTimeline>> tl_cache_;
    std::unordered_map<std::uint64_t, hls::InitInfo>     init_cache_;
    std::unordered_map<std::uint64_t, hls::SegmentStart> start_cache_;   // by segment request id
    std::unordered_map<std::uint64_t, int> row_cat_;   // request id → cat_ rendition (-1 none), per cat_key_
    // Timeline view (reference playlist time) and selection.
    double        view_t0_ = 0.0, view_t1_ = -1.0;
    bool          view_fit_ = true;                  // fits what there is until zoomed / panned
    int           sel_rend_ = -1;                    // index into align_.rend
    std::int64_t  sel_seq_ = -1;
    int           open_finding_ = -1;
    int           hover_finding_ = -1;

    // Conformance (NetInspectorConform.cpp)
    bool conform_ready(const Inputs& in) const;   // an HLS master the check can read
    void start_conform(const Inputs& in);
    void conform_tick(const Inputs& in);           // QA start / report, the cached result
    ConformProbe         conform_;
    ConformProbe::Result conform_res_;
    std::uint64_t        conform_ver_ = ~0ull;
    int                  conform_segments_ = 3;   // read per variant playlist; 0 = all
    bool                 conform_sizes_ = true;
    int                  conform_sel_ = -1;       // variant row clicked: its findings only
    int                  conform_open_ = -1;      // finding expanded
    bool                 qa_conform_ = false, qa_conform_started_ = false;
    std::string          qa_conform_out_, qa_conform_html_;
    // A master given by URL or path instead of the one playing: what cannot
    // be played (DRM) can still be checked. Kept across media sessions.
    char                 hls_url_[2048] = {};
    bool                 conform_from_url_ = false;   // the result is hls_url_'s
    bool                 qa_hls_ = false;             // "hls=<url>", check_url(): check it once
    bool                 focus_url_ = false;          // open_conformance(): focus the URL field once
    // The marker check (HlsMarkers.h): tags beside EXT-X-DATERANGE, typed
    // as names with blanks or commas between; markers only = playlists only.
    char                 marker_tags_[512] = {};
    bool                 markers_only_ = false;
    bool                 conform_run_markers_only_ = false;   // the result shown was one
    int                  marker_ev_open_ = -1;    // splice point expanded
    int                  marker_open_ = -1;       // marker finding expanded
    int                  qa_marker_open_ = -1;    // "marker_open=<n>", once the result is in
    bool                 qa_log_rows_ = false;    // that card's rows go to the log as shown, once
    std::string          export_note_;            // where Export HTML wrote, or why it could not
    std::vector<std::string> marker_tag_list() const;
    std::string          report_json() const;     // conformance + markers, as --json writes
    bool                 export_html(const std::string& path);
    void                 draw_markers(const Inputs& in, Out& out, float cw, bool seekable);
    // Its DASH mode: an MPD by URL, checked by a probe of its own; kept across
    // media sessions (it does not depend on what is playing).
    void start_dash();
    void draw_dash(float w, float h);              // the body below the toolbar
    bool                 conform_dash_ = false;   // the tab shows the MPD check
    char                 dash_url_[2048] = {};
    DashProbe            dash_;
    DashProbe::Result    dash_res_;
    std::uint64_t        dash_ver_ = ~0ull;
    bool                 qa_dash_ = false;        // "dash=<url>", check_url(): check it once

    // UI state
    Tab           tab_          = Tab::Network;
    Chip          chip_         = Chip::All;
    bool          hide_helpers_ = false;
    char          filter_[256]  = {};
    std::string   filter_lc_;
    std::uint64_t selected_     = 0;
    int           detail_tab_   = 0;     // 0 headers, 1 response, 2 timing
    float         width_        = -1.0f; // user width; < 0 = automatic
    float         details_frac_ = 0.46f; // share of the body height
    bool          narrow_       = false;
    bool          tiny_         = false;
    bool          narrow_applied_ = false;
    int           last_width_class_ = 0;   // 0 wide, 1 narrow, 2 tiny
    bool          qa_select_segment_ = false;
    bool          qa_select_playlist_ = false;
    int           vis_start_ = 0, vis_end_ = 0;   // table rows on screen last frame
};
