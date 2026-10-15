#pragma once

// Network inspector: a docked right-hand sidebar (N key) modelled on Chrome
// DevTools' Network panel — sortable request table with a waterfall, filter
// box + type chips, a details pane (headers / timing) for the selected row,
// and a "Manifest" tab listing the parsed variants, renditions and timeline
// tags. Data comes from NetworkLogger; layout is Dear ImGui (main thread).

#include "NetDescribe.h"

#include <cstdint>
#include <string>
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
        double seek_to = -1.0;   // tag row clicked in the Manifest tab
        bool   close   = false;  // × clicked
    };

    // Docked width (logical px) the sidebar takes in a window `win_w` wide.
    float dock_width(float win_w) const;

    // Draw the sidebar flush right, full height. Call between ImGui frames.
    Out draw(const Inputs& in, float win_w, float win_h);

    // New media session: drop the selection and the cached manifest catalog.
    void reset();

    // QA (--ui-net): comma-separated "tab=manifest", "select=segment" (newest
    // segment row), "detail=timing", "filter=<text>", "chip=video|audio|...", "width=<px>".
    void qa_apply(const std::string& spec);

private:
    enum class Tab { Network, Manifest };
    enum class Chip { All, Manifest, Video, Audio, Subs, Errors };

    struct Row {
        NetRequest  r;
        NetDesc     d;
        std::string name;
    };

    void sync(const Inputs& in);
    void rebuild_view();
    bool row_visible(const Row& row) const;

    void draw_header(float w);
    void draw_network(const Inputs& in, float w, float h);
    void draw_toolbar(const Inputs& in, float w);
    void draw_table(float w, float h);
    void draw_details(float w, float h);
    void draw_footer(float w);
    void draw_manifest(const Inputs& in, Out& out, float w, float h);

    const Row* find_row(std::uint64_t id) const;

    // Data
    std::vector<Row>   rows_;            // ascending id
    std::uint64_t      seq_ = 0, gen_ = ~0ull;
    NetCatalog         cat_;
    std::string        cat_key_;
    std::int64_t       base_t_ = -1;     // t_start of the first row (relative clock)

    // View
    std::vector<int>   view_;            // indices into rows_, filtered + sorted
    bool               view_dirty_ = true;
    int                sort_col_ = -1;   // -1 = chronological
    bool               sort_desc_ = false;

    // UI state
    Tab           tab_          = Tab::Network;
    Chip          chip_         = Chip::All;
    bool          hide_helpers_ = false;
    char          filter_[256]  = {};
    std::string   filter_lc_;
    std::uint64_t selected_     = 0;
    int           detail_tab_   = 0;     // 0 headers, 1 timing
    float         width_        = -1.0f; // user width; < 0 = automatic
    float         details_frac_ = 0.46f; // share of the body height
    bool          narrow_       = false;
    bool          tiny_         = false;
    bool          narrow_applied_ = false;
    int           last_width_class_ = 0;   // 0 wide, 1 narrow, 2 tiny
    bool          qa_select_segment_ = false;
    int           vis_start_ = 0, vis_end_ = 0;   // table rows on screen last frame
};
