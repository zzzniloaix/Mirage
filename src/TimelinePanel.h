#pragma once

// The timeline panel, the player's ImGui window over a timeline document
// (eval/phase3-survey/design.md §10; DECISIONS.md D-114): written by WP13,
// the owner's answers to its questions put in by WP13b (D-124).
//
// Docked above the control stack, full width left of the network sidebar,
// lowest in the bottom stack (D-114 (a)): one curve per scored variant and
// per session as min/max envelopes per framebuffer column, so that a
// one-frame dip stays visible (eval/phase3-survey/ui.md §1.8, F4), and none
// over a session's span that isn't scored, where no point is picked either
// (D-124 (a), Q91 (b)); the document's detected drops shaded; one event
// lane per source, in §3.1's order; the playhead, the frame on screen on the
// reference's axis, or, where its PTS lies in more than one discontinuity
// sequence that nothing has told apart since the panel first saw the
// player's frames, or since a seek, a switch or a step whose landing it
// can't tell, none, and a line that says why (D-124 (c), Q93 (c);
// timeline::track_playhead()). Wheel zoom, drag pan and double-click fit as
// the Alignment tab has them (src/NetInspectorAlign.cpp). A click, a release
// that moved less than 3 px and that no second click makes a double-click,
// picks the lowest frame near the cursor on the curve under it, shows its
// attribution card (click_answer(), D-114 (d)) and seeks through the
// player's s_seek_to only where decide_seek() says that a seek shows the
// frame (D-114 (b)); otherwise the card says why, and offers the segment
// start the decision gives, which the user takes with its button. One seek
// per click, never while the view or the seek bar is dragged; the card says
// whether the player made it, and a decision whose seek wasn't made is made
// again when another variant comes on screen, whose seek a click asks for
// anew.
//
// What decides an attribution, a match or a seek is WP12's, ImGui-free and
// tested in mirage_tests (src/TimelineLoad.h, src/TimelineView.h,
// src/TimelineMatch.h); this file draws and routes. The load runs on the
// Loader's worker thread (design §10). Per frame, the curves are bounded by
// the panel's columns: their envelopes, from the pyramid, are cached until
// the view, the width or the scale changes. The lanes are cached the same
// way, but rebuilt on every frame the view changes, every frame of a pan or a
// zoom, each rebuild walking every event that meets the view, O(events in
// view); and a variant's clock is built on the main thread the first time it
// is needed, O(its frames and events). Neither cost has been measured on a
// large document (P-Y): MEASUREMENT PENDING. While the panel is closed it
// does nothing at all: main.cpp neither draws nor feeds it then.
//
// Its one setting is the document (D-114 (d)): --timeline's, else
// prefs.txt's timeline= line, which the panel's Open… sets. Draw between
// PlayerUI's begin_frame() and end_frame().
//
// WP13's choices that neither the design nor the decisions settled are as
// built, which the owner kept (D-124 (f), Q96 1): the hit radii and the
// markers' spacing, prefs.txt's line written by Open… alone, the lanes'
// events, the fixed score axis, --timeline-report's seconds from the launch,
// a click that waits out the double-click time, and the height not kept.

#include "TimelineLoad.h"
#include "TimelineMatch.h"
#include "TimelineView.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class TimelinePanel {
public:
    struct Inputs {
        // What the player has open (TimelineMatch.h); nullptr: nothing yet.
        const timeline::OpenStream* open = nullptr;
        // The EXT-X-STREAM-INF entry, an index into open->variants, of the
        // frame on screen (MediaSession::playing_hls_variant() of its
        // stream); -1: none or unknown (eval/phase3-survey/ui.md §2.3).
        int entry = -1;
        // The frame on screen: its PTS in seconds, as the player's clock
        // has it, and the serial of its upload, bumped by every frame shown.
        bool          has_frame    = false;
        double        frame_pts    = 0.0;
        std::uint64_t frame_serial = 0;
        // The latest seek the player requested (Demuxer::request_seek's
        // serial) and its target in PTS seconds: the seek bar's, the keys',
        // a keyframe step's or the panel's own. The playhead follows from
        // there (timeline::playhead_seeked()).
        std::uint64_t seek_serial = 0;
        double        seek_target = -1.0;
        // Changes with every seek and every frame step, whose frame need not
        // follow the one before it (main.cpp's last_any_seek_at).
        std::int64_t  step_mark = 0;
        bool          scrubbing = false;   // the frames shown are the scrub decoder's
        // Whether main.cpp makes the seek the panel asks for in this frame:
        // not while the seek bar is dragged, nor in the frame its drag ends
        // (the guard against the buzz on one note, eval/phase3-survey/ui.md
        // §4.1). The panel asks for none when it doesn't, and says so.
        bool          can_seek  = true;
        timeline::SeekClock seek;          // the Demuxer's start, duration and reorder delay
        // QA: wall seconds since the first frame, the clock of --speed-seq's
        // events, which --ui-timeline's picks follow; < 0 before it.
        double qa_t = -1.0;
        float  win_w = 0.0f, win_h = 0.0f;   // logical display size
        float  bottom      = 0.0f;   // logical px under the panel: the control stack's eased height
        float  right_inset = 0.0f;   // logical px of the network sidebar
        float  scale       = 1.0f;   // framebuffer px per logical px
    };
    struct Out {
        double seek_to       = -1.0;    // >= 0: a PTS to seek to, through s_seek_to, once
        bool   close         = false;   // × clicked
        bool   hovered       = false;   // the cursor is over the panel: the chrome stays up
        bool   open_document = false;   // Open… clicked: the player asks for a file
    };

    // The document to load (D-114 (d)): its load starts the next time the
    // panel is drawn, on the Loader's thread, and stops one still running;
    // the same path again loads it again. An empty path changes nothing.
    void set_document(const std::string& path);
    const std::string& document() const { return document_; }

    // Draw it, docked above the control stack. Only while it is open.
    Out draw(const Inputs& in);

    // Logical px the panel takes above the control stack in a display
    // `win_h` tall: what main.cpp adds to the bottom stack for it.
    float height(float win_h) const;

    // A new media session: the document is matched again; the playhead,
    // the pick and the QA picks are forgotten; the snapshot and the view stay.
    void reset();

    // QA (--ui-timeline): comma-separated "fit=1", "zoom=<t0>:<t1>",
    // "variants=all|<entry>,<entry>..." (the curves of the variants those
    // master entries name; the bare items after it continue its list),
    // "curves=all|<curve>,<curve>..." (those curves alone, named as --curve
    // names them, "pair:1" or "session:0", whatever variants= says; the bare
    // items after it continue its list),
    // "pick=<position>" (the lowest point of the shown curves near that
    // position on the axis, as a click there picks it, the curve under the
    // cursor aside) or "pick=attribution:<n>" (the lowest point of the
    // document's attribution n, 0-based, as finding= counts in --ui-net), a
    // pick taking an optional "@<t>", the wall seconds after the first
    // frame, as --speed-seq's events do (eval/phase3-survey/ui.md §5.3).
    // Opens nothing: the player opens the panel.
    void qa_apply(const std::string& spec);

    // QA (--timeline-report): the panel's state as text: the document and
    // its load, how it matched the open stream, the variant on screen, the
    // playhead, or why there is none, the view, where each shown curve was
    // drawn at the last draw and a session's spans that aren't scored, the
    // drops, the pick with its attribution card and its seek, or why there
    // is none (eval/phase3-survey/ui.md §5.3).
    std::string report_text() const;

private:
    // A curve the panel draws, and whether its chip shows it.
    struct PanelLine {
        std::size_t curve   = 0;    // into Snapshot::curves
        int         variant = -1;   // a pair's distorted variant; -1: a session's
        bool        shown   = true;
        std::string name;           // as --curve names it: "pair:1", "session:0"
        std::string label;          // its chip's text
    };
    // What became of the seek a pick asked for: the decision's (Seek), or the
    // offered segment start once its button is clicked (Offer).
    enum class Sent : std::uint8_t {
        No,        // none asked for
        Asked,     // Out::seek_to given; main.cpp makes it at the start of its next iteration
        Made,      // the player's seek serial moved, to the target asked for
        Refused,   // not made: Pick::note says why
    };
    // What a pick shows: its frame, its card and its seek.
    struct Pick {
        std::size_t  line = 0;      // into lines_
        std::size_t  point = 0;     // into the curve's points
        std::int64_t ref_index = 0;
        double       vmaf = 0.0;
        std::string  how;           // "a click", "pick=attribution:0 at 3.02 s", ...
        timeline::ClickAnswer  answer;
        timeline::SeekDecision decision;
        int          variant = -1;  // the variant on screen it was decided on; -1: none
        bool         redecided = false;   // decided again, for another variant on screen
        Sent         sent = Sent::No;
        std::string  note;                // Refused: why
        double        asked_target = 0.0;  // Asked: the PTS asked for
        std::uint64_t asked_serial = 0;    // Asked: the player's seek serial then
        int           asked_sequence = -1; // Asked: the sequence of the frame asked for; -1: not known
    };
    // A scripted pick (--ui-timeline).
    struct QaPick {
        bool        attribution = false;
        std::size_t n = 0;           // attribution:<n>
        double      position = 0.0;  // pick=<position>
        double      at = -1.0;       // @<t>; < 0: as soon as it can
        std::string text;            // as given
        bool        done = false;
    };
    // The seek the panel asked for: the playhead takes its frame's sequence
    // once the player makes it (timeline::playhead_seeked()).
    struct OwnSeek {
        double target   = 0.0;
        int    sequence = -1;
    };
    // A curve's envelope for one view and one number of columns.
    struct Envelope {
        double t0 = 0.0, t1 = 0.0;
        int    columns = 0;
        std::vector<timeline::MinMax> mm;
    };
    // One lane's markers for one view, in px from the plot's left edge: its
    // point events' clusters and its spans' bars, each with its events.
    struct Lane {
        std::vector<float>                   xs;       // the points' x, ascending
        std::vector<std::size_t>             points;   // their events, parallel to xs
        std::vector<timeline::MarkerCluster> clusters;
        struct Bar {
            float x0 = 0.0f, x1 = 0.0f;
            std::vector<std::size_t> events;
        };
        std::vector<Bar> bars;
    };
    // A click waiting out the double-click time, so that a double-click
    // fits the view without seeking; with the view and the plot it was made
    // on, so that a zoom, a pan or a resize meanwhile drops it.
    struct PendingClick {
        float  x = 0.0f, y = 0.0f;
        double at = 0.0;   // ImGui::GetTime() of its release
        double t0 = 0.0, t1 = 0.0;
        float  px0 = 0.0f, pw = 0.0f;
    };

    void take_load();
    void on_snapshot();
    void apply_qa_shown();
    void ensure_match(const Inputs& in);
    int  screen_variant(const Inputs& in) const;
    const timeline::VariantClock& clock_of(int variant);
    void settle_seek(const Inputs& in, bool drawn_last_frame);
    void follow_playhead(const Inputs& in);
    void decide(Pick& p, const Inputs& in);
    void ask_seek(Pick& p, int sequence, const Inputs& in, Out& out);
    void make_pick(std::size_t line, std::size_t point, std::string how, const Inputs& in, Out& out);
    bool pick_attribution(std::size_t n, std::string how, const Inputs& in, Out& out);
    bool pick_at_x(float x, std::optional<float> y, float x0, float width, std::string how,
                   const Inputs& in, Out& out);
    void run_qa_picks(const Inputs& in, float x0, float width, Out& out);
    const Envelope& envelope_of(std::size_t line, int columns);
    void build_lanes(float width);
    bool lane_shows(const timeline::Event& e) const;
    std::string variant_text(int variant) const;
    std::string unknown_line() const;
    std::vector<std::string> card_lines(const Pick& p) const;
    std::string seek_line(const Pick& p) const;
    void draw_card(const Inputs& in, Out& out, float w, float h);

    std::string document_;
    bool        load_pending_ = false;   // document_ set, its load not started
    bool        loading_      = false;
    std::string load_error_;              // the last load's refusal
    timeline::Loader                          loader_;
    std::shared_ptr<const timeline::Snapshot> snap_;
    std::vector<PanelLine>                    lines_;

    // The match, for the open stream it was made with.
    const timeline::OpenStream*          match_open_ = nullptr;
    std::optional<timeline::StreamMatch> match_;
    bool                                 override_ = false;   // another stream's document, shown anyway
    std::vector<std::optional<timeline::VariantClock>> clocks_;   // per variant, made when first needed

    // The playhead.
    int                                    drawn_frame_ = -2;   // ImGui's frame count at the last draw
    int                                    last_entry_  = -1;   // the player's entry on screen, as last given
    int                                    play_variant_ = -1;
    timeline::PlayheadState                play_state_;
    std::optional<timeline::PlayheadFrame> playhead_;
    // No playhead_ because its sequence isn't known (D-124 (c)): the
    // sequences the PTS on screen lies in, as the tracker gave them; empty
    // otherwise.
    std::vector<int>                       playhead_unknown_;
    double                                 playhead_pts_ = 0.0;   // the frame on screen it was found for
    std::uint64_t          seen_frame_ = 0, seen_seek_ = 0;
    std::int64_t           seen_step_  = 0;
    bool                   seen_any_   = false;   // seen_* hold the player's values
    std::optional<OwnSeek> own_seek_;

    // The view and what it shows.
    timeline::View                           view_;
    std::vector<Envelope>                    env_;   // per line
    // Per line, the runs of positions over which its curve was drawn at the
    // last draw, within the view, for --timeline-report; empty for a hidden
    // line. drawn_valid_: the curves have been drawn since the snapshot.
    std::vector<std::vector<std::pair<double, double>>> drawn_;
    bool                                                drawn_valid_ = false;
    std::array<Lane, timeline::kSourceCount> lanes_;
    double lanes_t0_ = 0.0, lanes_t1_ = -1.0;
    float  lanes_w_  = 0.0f;
    bool   lanes_dirty_ = true;
    float  curves_y0_ = 0.0f, curves_y1_ = 1.0f;   // the curves' area at the last draw, screen px
    std::optional<Pick>         pick_;
    std::optional<PendingClick> pending_click_;
    float  height_ = -1.0f;   // the user's, dragged by the top edge; < 0: automatic

    // QA (--ui-timeline).
    bool                                     qa_fit_ = false;
    std::optional<std::pair<double, double>> qa_zoom_;
    std::optional<std::vector<std::size_t>>  qa_entries_;   // variants=<entry>,...; nullopt: all
    std::optional<std::vector<std::string>>  qa_curves_;    // curves=<curve>,...; nullopt: by variants=
    std::vector<QaPick>                      qa_picks_;
    bool                                     qa_view_due_ = false;   // fit= or zoom= not applied yet
};
