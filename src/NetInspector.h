#pragma once

// Network inspector: a docked right-hand sidebar (N key) modelled on Chrome
// DevTools' Network panel — sortable request table with a waterfall, filter
// box + type chips, a details pane (headers / response / timing) for the
// selected row, a "Manifest" tab listing the parsed variants, renditions and
// timeline tags, and an "Alignment" tab checking that the renditions played
// together line up (HlsAlign.h): playlist timelines, discontinuities and the
// start timestamps inside the segments, on a timeline per rendition. Data
// comes from NetworkLogger (requests and the response bodies it keeps);
// layout is Dear ImGui (main thread). NetInspectorResponse.cpp and
// NetInspectorAlign.cpp hold the Response and Alignment views.

#include "HlsAlign.h"
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
    };
    struct Out {
        double seek_to   = -1.0;   // tag row clicked in the Manifest tab
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
    // "finding=<n>" (expand the n-th finding).
    void qa_apply(const std::string& spec);

    // QA: the alignment report as text (findings, then one line per rendition).
    std::string alignment_text() const;

private:
    enum class Tab { Network, Manifest, Alignment };
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
