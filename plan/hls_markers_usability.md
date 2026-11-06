# The HLS marker check: four usability fixes

The owner checked encrypted CMAF packages with the marker check of
`feat/hls-markers` (`mirage_hlscheck --markers-only --marker-tag …`, and the
player's Conformance tab by path). Each package had one video variant, about
110 audio renditions and about 60 subtitle playlists, CBCS-encrypted, with
segment URIs that could not be fetched. The check found what it should.
These are the rough edges the owner reported on 2026-11-06, each with what
happens, why, the fix, the alternatives, the risks and the check. D-095
covers sections 1 to 3 and 5, which change no render or decode path (D-007).
Section 4 is the owner's choice (D-102). Line numbers are those of commit
`ee046dc`.

**Implemented** in `2eab999`. The owner decided section 4 as the agent
recommended, option (A) (D-102). P-086 lists the choices beyond this plan.
The record is `plan/engineer_tools_runs/hls-markers-usability-2026-11-06/`.

## 1. The Conformance check needs something playing

**What happens.** `mirage --ui-net "…,hls=<master>"` with no media argument
stays on the launcher. The inspector never shows, no check runs and
`--ui-snapshot` never fires. A local clip has to play for the URL mode to be
reached at all: the 2026-11-05 record played `v1/s0.ts` for it
(`plan/engineer_tools_runs/hls-markers-2026-11-05/record.txt`, line 114).
Opening the encrypted master itself fails, and the launcher shows the error
and nothing more.

**Why.**
- `--ui-net` is applied by `run_session` (`src/main.cpp:1843-1847`).
- The launcher's loop (`src/main.cpp:1049-1068`) draws `draw_launcher` and
  nothing else. `N` sets `s_show_network` there too (`on_key`,
  `src/main.cpp:282`), but nothing draws the inspector before a session.
- `--ui-snapshot` is taken in `run_session`, after the first video frame.
- FFmpeg's hls demuxer takes the master's first `EXT-X-KEY` URI as an AES
  key, and the open fails (`docs/AI_CONTRIBUTIONS.md`, "HLS break markers
  and encrypted segments"). `poll_open` then puts the error on the launcher
  (`src/main.cpp:780`, or `:765` for a stalled network open).

**Fix.** The open path is unchanged. Everything below happens on the
launcher, before a session or after an open has failed.
1. The launcher draws the inspector when `s_show_network` is set, docked on
   the right as in playback, with nothing playing:
   `NetInspector::Inputs{ .log = &net_logger }`, no manifest.
   `net_logger.poll()` runs each launcher frame, as in `run_session`. The
   launcher card centres in the space left of the inspector. Esc closes the
   inspector there, and a width the user drags is saved as in `run_session`
   (`persist_prefs`).
2. `--ui-net` with no media argument is applied once, before the launcher's
   first frame, and opens the inspector. `run_session` still applies it to
   every session, as now.
3. `--ui-snapshot <png> <frame>` with no media argument, or after a failed
   open, captures the launcher's window. It waits until the inspector's QA
   work is idle (`NetInspector::qa_idle()`: no `hls=` / `dash=` start
   pending, no check running, the last result taken in), then for
   `<frame>` more frames. It gives up waiting after 300 s with a warning,
   then captures and exits, as the session's capture does. `conform=1` needs
   a playing master, so the launcher does not wait for it.
4. The launcher card's "OPEN MEDIA" row gets a link at its right: "Check a
   stream without playing it (N)". It opens the inspector on its
   Conformance tab, with the URL field focused.
5. When an open fails and the source is an HLS or DASH playlist by its name
   (`ManifestParser::is_manifest`), the error banner gets a "Check it"
   button. It opens the Conformance tab on that URL or path, in HLS or DASH
   mode, and starts the check as `hls=` and `dash=` do. Both use one new
   function, `NetInspector::check_url()`.
   - The banner's text is cut to the space left of the button
     (`ellipsize`), and the full text shows on hover.
6. A test: `tests/hlsmarkers_gui.sh` makes the package
   `tests/hlsmarkers_demo.sh` makes. It runs the player with no media
   argument, `--ui-net "markers_only=1,marker_tags=EXT-X-CUE-MARKER,
   conform_out=…,hls=<master>"` and `--ui-snapshot`. It checks that the
   player exits with status 0, that the PNG is written, and that the tab's
   JSON is `mirage_hlscheck --markers-only --json`'s byte for byte, on the
   same path.

**Alternatives.**
- Only the QA flag, with no launcher link and no banner button. That fixes
  the scripted use, but the inspector stays unreachable by hand.
- Open the inspector when a manifest fails to open, without a button. That
  would hide the error card behind a sidebar the user did not ask for.
- A fallback in the open path that plays what it can of a DRM stream. D-097
  already set that aside: it needs a D-007 plan.

**Risks.**
- The inspector's other tabs with no session. Network lists the check's own
  requests (initiator "inspector"). Manifest and Alignment show their empty
  states (`in.media_url` is empty). Their code already takes a null log and
  manifest (`src/NetInspector.cpp:120-125`, `NetInspectorAlign.cpp:95`).
  The record will show each one.
- `Out::seek_to` has no session to seek. In URL mode with nothing playing,
  `draw_markers` already gets `seekable` false (`url != in.media_url`,
  `src/NetInspectorConform.cpp:647`), so the seek button is not drawn.
- A result in URL mode survives `reset()` (`src/NetInspector.cpp:41-46`), so
  a check made on the launcher is still there when a file then plays.
- A failed open under `--ui-snapshot` used to wait for the alarm. It now
  captures the launcher and exits with status 0. Scripted runs that meant
  to time out there get a PNG instead.
- `N` typed into the launcher's URL field must not toggle the inspector.
  `on_key` already returns while ImGui wants the keyboard
  (`src/main.cpp:252`).

**Check.** A scripted run with no media argument and the snapshot (fix 6).
A run with an encrypted master the player cannot open, given as the media:
the launcher's error with "Check it" (snapshot). The Network, Manifest and
Alignment tabs on the launcher (snapshots).

## 2. `marker_open=<n>` counts events the cards do not show

**What happens.** `marker_open=8` opened the 8th splice point, while
`finding=<n>` is 0-based. Nothing says which base `marker_open` uses.

**Why.** `marker_open=<n>` sets `marker_ev_open_ = n`
(`src/NetInspectorConform.cpp:361-364`). That is an index into
`MarkerReport::events`, which also holds the dateranges that are not splice
points: a chapter, a programme. Those have no card (`:742`). In the demo
package `chapter-1` comes first, so `marker_open=1` opens `break-1`, the
first card, and the number reads as 1-based. On a stream with no such
event it is 0-based, and on one with several it is neither.

**Fix.** `marker_open=<n>` opens the n-th splice-point card, 0-based, as
`finding=<n>` is. The comment on `qa_apply` (`src/NetInspector.h:67-82`) says
"0-based" for both. The mapping is a pure function,
`hls::splice_event(const MarkerReport&, int n)` (`src/HlsMarkers.h`). It
returns the event's index, or −1 past the last card, and is unit-tested.
The 2026-11-05 record's `marker_open=1` reads `marker_open=0` from now on.
P-086 says so.

**Alternative.** Keep the index and document it as "the n-th event,
counting those without a card". Fewer changes, but it is a number no one
can read off the screen.

## 3. Rendition names are cut short in the cards

**What happens.** In a card's table the Rendition column shows "audio
aud/…" cut at the same place for each rendition. With a hundred audio
renditions you cannot tell which row is which without widening the panel.

**Why.**
- The column is 130 px wide (`src/NetInspectorConform.cpp:806`).
- The label is "audio GROUP-ID/NAME" or "subtitles GROUP-ID/NAME"
  (`src/HlsConformProbe.cpp:705-714`).
- ImGui clips the text at the column's edge (`:816`, `:834`).
- The renditions of one group often share the head of their NAME and differ
  at its tail: a language, the channels, a role.

**Fix.**
- The column takes the width of the card's widest label, in the room the
  other columns leave, and 130 px at least, as now. The offsets and the
  gauge stay in view.
- A label still wider is cut in the middle, keeping its head and its tail.
  That is `ui::ellipsize_middle` in `src/UiFormat.h`: pure, UTF-8-safe, with
  the measure passed in, and unit-tested.
- The full label shows in a tooltip on hover. The same goes for the
  "missing" rows (`:834`).

**Alternatives.** Wrap the label onto two lines. Or show only the NAME's
tail. Rows of two heights are harder to scan, and the tail alone loses the
group.

## 4. A missing marker is a warning, a misplaced one an error (owner's choice)

**What happens.** Take a rendition that marks the other breaks with a tag
but not this one, where its timeline reaches the break. It gets a warning
(`src/HlsMarkers.cpp:313-316`; the card's `worse(sev, Warning)`,
`src/NetInspectorConform.cpp:750`; the page's "Missing" pill,
`src/HlsReportHtml.cpp:434` and `:451`). A tag one segment off is an error
(`src/HlsMarkers.cpp:272-276`). An interstitial's or SCTE-35 `START-DATE`
off a segment boundary is a warning (`:258-260`).

**Why it matters.** A player that splits audio from video at a packager's
tag in that rendition has no break there at all. That is at least as bad as
a break one segment out.

**Options.**
- (A) As bad as a misplaced marker of its kind. A missing tag (one of
  `--marker-tag`) is an error. A missing `EXT-X-DATERANGE` is a warning, as
  a `START-DATE` off a boundary is.
- (B) Always an error.
- (C) A warning by default, raised by an option (`--missing-marker error`
  and a chip in the tab).
- (D) As now.

The agent recommends (A): each kind of marker is graded the same whether
it is missing or misplaced. With (A) or (B), the exit status is 1 for such
a stream.

**Decided** (D-102, 2026-11-06): (A).

**Fix, once decided.**
- The finding's severity.
- `MarkerEvent` gets each missing rendition's severity, next to `missing`.
- The JSON gets `"missing_severity"` next to `"missing"`. The page uses it,
  and reads a report without it as warnings.
- The card and the page take their colour from it.
- The test "analyze_markers — on boundaries, missing markers, other dates"
  (`tests/test_hls_markers.cpp:135`) follows the decision, and a case with
  a missing `EXT-X-DATERANGE` is added.

## 5. The command line's summary is for reading

**What happens.** The summary line reads "1 error, …" or "2 errors, …", so
`grep -E '^[0-9]+ errors'` misses the singular
(`tools/mirage_hlscheck.cpp:128`, `:174`).

**Fix.** `--help` says that scripts should read the exit status and
`--json`, and that the text is for people and may change. The text stays as
it is (`tools/mirage_hlscheck.cpp:75`).

## Records

- D-102: the owner's choice in section 4.
- P-086: how sections 1 to 5 are implemented.
- A run record in `plan/engineer_tools_runs/`.
- The unit tests of both builds at the last code commit.
- `docs/AI_CONTRIBUTIONS.md`.
- The README's launcher and Conformance tab lines.
