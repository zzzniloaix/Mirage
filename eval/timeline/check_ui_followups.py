"""Checks ui-qa-followups.sh's record of the timeline panel's QA cases for WP13b and WP13c
(DECISIONS.md D-124 (a), (c), D-125 (b) 6; eval/phase3-survey/ui.md §5.3), in the manner of
eval/timeline/ui-qa-2026-11-16/check_ui.py.

usage: python3 -I -B check_ui_followups.py <out>

For each case it reads the panel's report (--timeline-report), the player's log and the ladder's
layout.json (tests/SyntheticLadder.h, the generator's ground truth), and for the unscored case the
session's document and mirage_vmaf attribute's answer to the query of the pick's card
(<out>/unscored/run.attribute.txt), and checks:

- inside-ad, the panel first seeing the player's frames inside the ad whose timestamps restart at
  the main content's origin (D-124 (c), Q93 (c)): the document loaded and matched the open master
  by its SHA-256, its entries in order, the top rung on screen; the panel closed and opened again
  by --speed-seq, then paused, the opening and the pause while the ad played; on opening, the
  panel's log line of no playhead, for the PTS of the ad's frame played then, which lies in the
  sequences the line names, those the layout gives it, two or more; no seek and no switch in the
  run, and no playhead known again before the report, which says that there is none and why, for
  the frame paused on, the ad's, lying in those sequences; the snapshot, after the report, of the
  same frame;
- unscored, the panel on a session's document whose span over the ad isn't scored (D-124 (a),
  Q91 (b)): the document's session spans, the one over the ad's segments played by entry 0 and
  marked scored false, the others scored; the document loaded and matched, the playhead on the
  frame on screen; the session's curve alone shown, the view 2-6 s, and the curve drawn where its
  spans are scored and nowhere over the one that isn't, which the report names; the pick inside
  that span picking nothing, and the one after it picking the session's frame there, outside the
  span, its card's lines mirage_vmaf attribute's for the same query, and its seek as the layout
  has it, landing on the frame;
- step-back, a step back from the first frame of the ad whose timestamps restart at the main
  content's origin (D-125 (b) 6, Q99 6 (a)), in its two runs: position 2 s the start of the
  segment before the ad, 25 frames before the ad's first by the layout; the document loaded and
  matched; no switch; the paused seek there showing that segment's first frame, and the playhead
  known on it; the 25 frame steps forward showing the frames after it, the last the ad's first,
  where the PTS step back, with no line of no playhead on the way, the tracker following them
  into the ad's sequence (D-046); the step back from that frame, asking for 1.5 frames before
  it, showing the frame that the scrub decoder's seek by PTS lands on by the layout, which the
  log's PTS confirms. Run run: the report, after the step, showing the playhead on that frame,
  in the sequence the tracker's simulation of the step's landing gives, or none where it can't
  tell the landing and the PTS lies in more than one sequence; the snapshot after it, of the
  same frame. Run fwd: the 50 frame steps forward after the step back showing the playback
  pipeline's frames after the ad's first, as src/main.cpp reads, the step having left the
  pipeline where it was, the last the main content's first frame after the ad, whose PTS in the
  log no frame 50 after the step's own has; no line of no playhead after the step; the report
  showing the playhead on that last frame, in its sequence; the snapshot after it.

The sequences are the report's (D-057), as the aligner splits an input (D-046, src/VMAFAlign.h):
a new one where the PTS step back, or forward by more than 10 s; a PTS lies in each sequence with
a frame within half a frame of it, as src/TimelineMatch.h's track_playhead() takes it. The frame
shown at a time after the first frame is the one played at that position at 1x from the stream's
start, these runs seeking nothing before it, within TOLERANCE_FRAMES, one frame, as WP13's record
of the panel's QA supports it (eval/timeline/ui-qa-2026-11-16/): on the same ladder, the frame on
screen at a --speed-seq event is the one played at the time its log line gives (extras/keys.log:
snap@4, logged at t=4.00, shows pts=11.000, the frame played 4.00 s after the first frame;
ad-main-origin/run1.log: pause@2, logged at t=2.04, pauses on pts=12.040, the frame played 2.04 s
after it), and that time is written to the hundredth of a second, so that the frame played then
can be the one before or after the frame on screen. The seek decision is check_ui.py's, from the
layout alone: see its head. The step-back case times nothing: its frames are those of a seek and
of frame steps, each logged with the PTS it shows; where it lands, the scrub decoder's seek and
the tracker's simulation of it are src/ScrubDecoder.cpp's and src/TimelineMatch.cpp's, from the
layout and the first packet's DTS LEAD_FRAMES before the start (Ladder.scrub_shown(),
Ladder.landing()).

Prints a line per check and a summary; exit status 0 when every check passes, 1 otherwise.
Standard library only.
"""

import json
import os
import re
import sys

CASES = "media/attribution_acceptance__a_degraded_segment_and_an_ad_boundary"
EXAMPLE = ("media/attribution_acceptance__test_3__the_work_order_s_example_on_a_scripted_session_whose_"
           "ad_segment_is_answered_late")
LEAD_FRAMES = 2            # check_ui.py's: the first packet's DTS up to two frames before the start
FORWARD_JUMP_S = 10.0      # vmafalign::kForwardJump (src/VMAFAlign.h; D-046)
TOLERANCE_FRAMES = 1       # between an event's logged time and the frame on screen then (the head)
PANEL_E = "event 12 69"    # --speed-seq's log of panel:E (main.cpp: SpeedStep::Panel, the key 'E')
PAUSE = "event 1 0"        # --speed-seq's log of pause (SpeedStep::Pause)
VIEW = (2.0, 6.0)          # the unscored case's zoom=
PICK_INSIDE = 4.0          # its picks' positions, as ui-qa-followups.sh gives them
PICK_AFTER = 5.5
SEEK_TO = 2.0              # the step-back case's seek:, a position, as ui-qa-followups.sh gives it
STEPS_TO_AD = 25           # its frame steps forward from there to the ad's first frame
STEPS_AFTER = 50           # run fwd's frame steps forward after the step back
BACK_FRAMES = 1.5          # main.cpp's step back asks the scrub decoder for 1.5 frames before the frame on screen
SCRUB_TOL = 0.001          # the scrub decoder shows the first frame at or after its request within 1 ms
EPS = 1e-6                 # the report's positions are printed to the millisecond


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class Ladder:
    """A ladder's layout: its segments, and its frames in playlist order with their PTS and the
    sequences the aligner splits them into."""

    def __init__(self, layout):
        self.fps = layout["fps"]
        self.segments = layout["segments"]
        self.start = self.segments[0]["pts_first_s"]
        self.layout = layout
        self.frames = []   # (pts, sequence) per frame
        sequence, last = 0, None
        for s in self.segments:
            for offset in range(s["frames"]):
                pts = s["pts_first_s"] + offset / self.fps
                if last is not None and (pts < last - 1e-9 or pts - last > FORWARD_JUMP_S):
                    sequence += 1
                self.frames.append((pts, sequence))
                last = pts

    def segment_of(self, frame):
        for k, s in enumerate(self.segments):
            if s["first_frame"] <= frame < s["first_frame"] + s["frames"]:
                return k
        raise ValueError(f"frame {frame} is in no segment")

    def pts(self, k, offset):
        return self.segments[k]["pts_first_s"] + offset / self.fps

    def in_ad(self, frame):
        return self.segments[self.segment_of(frame)]["period"] == "ad"

    def ad_span(self):
        """The ad's positions, from its first segment's start to its last's end, and its frames."""
        ad = [s for s in self.segments if s["period"] == "ad"]
        return (float(ad[0]["position_s"]), ad[-1]["position_s"] + ad[-1]["frames"] / self.fps,
                ad[0]["first_frame"], ad[-1]["first_frame"] + ad[-1]["frames"])

    def lies_in(self, pts):
        """The sequences with a frame within half a frame of `pts`, ascending."""
        half = 0.5 / self.fps
        return sorted({seq for p, seq in self.frames if abs(p - pts) <= half + 1e-9})

    def played_at(self, t):
        """The frame on screen `t` seconds after the first frame, played at 1x from the start."""
        for s in self.segments:
            if s["position_s"] <= t < s["position_s"] + s["frames"] / self.fps:
                return s["first_frame"] + int((t - s["position_s"]) * self.fps + 1e-9)
        return None

    def found(self, target, lead):
        """check_ui.py's: the segment the hls demuxer finds for a seek to PTS `target`, the first
        DTS `lead` seconds before the start, the EXTINF added up from it."""
        pos_us = round((target - self.start + lead) * 1e6)
        k = 0
        for j, s in enumerate(self.segments):
            if round(s["position_s"] * 1e6) <= pos_us:
                k = j
        return k

    def reachable(self, k, offset):
        """check_ui.py's: whether a seek to the frame's PTS shows it."""
        s = self.segments[k]
        if self.pts(k, offset) < self.start - 1e-9:
            return False
        if abs(s["pts_first_s"] - self.start - s["position_s"]) > 1e-6:
            return False   # its timestamps restart: an ad's
        for lead in (0.0, LEAD_FRAMES / self.fps):
            if self.found(self.pts(k, offset), lead) != k:
                return False   # the demuxer finds another segment
        return True

    def decision(self, frame):
        """check_ui.py's: ("seek", PTS, segment) or ("offer", PTS of the segment start offered, its
        index)."""
        k = self.segment_of(frame)
        offset = frame - self.segments[k]["first_frame"]
        if self.reachable(k, offset):
            return ("seek", self.pts(k, offset), k)
        for j in range(k, -1, -1):
            if j == k and offset == 0:
                continue
            if self.reachable(j, 0):
                return ("offer", self.pts(j, 0), j)
        return ("none", None, None)

    def sought(self, target):
        """Where a seek to PTS `target` goes: into [start, start + duration - 0.5], as the Demuxer and
        the scrub decoder clamp it (src/Demuxer.cpp, src/ScrubDecoder.cpp)."""
        end = self.start + sum(s["frames"] for s in self.segments) / self.fps
        return min(max(target, self.start), max(self.start, end - 0.5))

    def landed(self, sought, target, lead_us, tol):
        """The frame a seek by PTS to `sought` lands on, the first DTS `lead_us` microseconds before
        the start, as src/TimelineMatch.cpp's simulate_us() has it: the segment the hls demuxer finds
        from the start, the EXTINF added up; the first segment from it whose timestamps lie as far
        past the start as it does; there its first frame where `tol` is None, else the first frame
        whose PTS is at least `target` less `tol`, or the last of all. None where it finds none."""
        pos = round(sought * 1e6) - (round(self.start * 1e6) - lead_us)
        k = next((j for j, s in enumerate(self.segments)
                  if pos < round((s["position_s"] + s["frames"] / self.fps) * 1e6)), None)
        if pos < 0 or k is None:
            return None
        at = round(self.segments[k]["position_s"] * 1e6)
        frm = next((j for j in range(k, len(self.segments))
                    if round((self.segments[j]["pts_first_s"] - self.start) * 1e6) >= at), None)
        if frm is None:
            return None
        first = self.segments[frm]["first_frame"]
        if tol is None:
            return first
        return next((i for i in range(first, len(self.frames)) if self.frames[i][0] >= target - tol - 1e-9),
                    len(self.frames) - 1)

    def scrub_shown(self, target):
        """The frame the scrub decoder shows for a request at PTS `target` where no keyframe it has
        read lies at or before the target, so that it seeks by PTS (src/ScrubDecoder.cpp): the target
        clamped into the stream, the first DTS LEAD_FRAMES frames before the start, as libx264's
        segments have it, and the first frame decoded there at or after the target, within 1 ms."""
        return self.landed(self.sought(target), target, round(LEAD_FRAMES / self.fps * 1e6), SCRUB_TOL)

    def landing(self, target):
        """The sequence the panel's tracker takes a seek by PTS to `target` to land in
        (src/TimelineMatch.cpp, landing_sequence()): that of the frame the seek shows for every lead
        of the first packet's DTS the seek decision allows, none and each segment end within
        LEAD_FRAMES frames of the position sought (seek_leads(); SeekClock::video_delay, two frames
        on D-069's libx264 ladders), from the first frame at or after the target within half a frame,
        or from the found segment's first where the target lies before the start; None where one lead
        shows no frame, or two show frames of two sequences."""
        sought = self.sought(target)
        p0 = round(sought * 1e6) - round(self.start * 1e6)
        reach = round(LEAD_FRAMES / self.fps * 1e6)
        ends = [round((s["position_s"] + s["frames"] / self.fps) * 1e6) for s in self.segments]
        tol = None if target < self.start else 0.5 / self.fps
        sequences = set()
        for lead in [0] + [e - p0 for e in ends if 0 < e - p0 <= reach]:
            frame = self.landed(sought, target, lead, tol)
            if frame is None:
                return None
            sequences.add(self.frames[frame][1])
        return sequences.pop() if len(sequences) == 1 else None

    def nearest_in(self, sequence, pts):
        """The frame of `sequence` nearest `pts`, the earlier of two as near (track_playhead())."""
        frames = [i for i, (_, s) in enumerate(self.frames) if s == sequence]
        return min(frames, key=lambda i: (abs(self.frames[i][0] - pts), i)) if frames else None


class Checks:
    def __init__(self):
        self.n = 0
        self.failed = []

    def check(self, ok, what):
        self.n += 1
        print(f"{'ok  ' if ok else 'FAIL'} {what}")
        if not ok:
            self.failed.append(what)


def first(pattern, text):
    return re.search(pattern, text, re.M)


def frames_text(n):
    """A count of frames in words: "1 frame", "2 frames"."""
    return f"{n} frame" + ("" if n == 1 else "s")


def sequences_of(text):
    """The numbers of "0 and 1", "0, 1 and 2"."""
    return [int(s) for s in re.findall(r"\d+", text)]


SEQUENCES = r"([0-9]+(?:(?:, | and )[0-9]+)*)"


def loaded_and_matched(c, name, report):
    """The load, the match and the variant on screen's entry, or None."""
    c.check(first(r"^load: loaded: ", report) is not None, f"{name}: the document loaded")
    c.check(first(r"^match: master SHA-256, entries by their order$", report) is not None,
            f"{name}: matched by the master's SHA-256, its entries in order")
    screen = first(r"^variant on screen: player entry (\d+): entry (\d+) ", report)
    c.check(screen is not None and int(screen.group(1)) == 0,
            f"{name}: on screen entry 0, the top rung the player starts on"
            + (f" (the report: entry {screen.group(1)})" if screen else " (the report: none)"))


def card_vs_cli(c, name, report, answer):
    """check_ui.py's: the card's lines from the interval's head on, against mirage_vmaf
    attribute's block."""
    card = [line[4:] for line in report[report.index("card:\n"):].splitlines()[1:] if line.startswith("    ")]
    head = first(r"^  (reference frames .*)$", report[report.index("card:\n"):])
    block = first(r"^(?:Drop \d+|Interval): (.*)$", answer)
    if head is None or block is None:
        c.check(False, f"{name}: the card's lines are mirage_vmaf attribute's (no head or no answer)")
        return
    cli = answer[block.end():].splitlines()
    cli = [line[2:] for line in cli if line.startswith("  ")]
    same = head.group(1) == block.group(1) and card == cli
    differ = ""
    if not same:
        pairs = [(head.group(1), block.group(1))] + list(zip(card, cli))
        bad = next(((a, b) for a, b in pairs if a != b), (f"{len(card)} lines", f"{len(cli)} lines"))
        differ = f": the panel {bad[0]!r}, the CLI {bad[1]!r}"
    c.check(same, f"{name}: the card's {len(card) + 1} lines are mirage_vmaf attribute's" + differ)


def check_inside_ad(c, out, lad):
    """The panel opened inside the ad whose timestamps restart at the main content's origin."""
    name = "inside-ad"
    report = read(os.path.join(out, "inside-ad", "run.txt"))
    log = read(os.path.join(out, "inside-ad", "run.log"))
    tol = TOLERANCE_FRAMES / lad.fps
    ad_from, ad_to, _, _ = lad.ad_span()
    loaded_and_matched(c, name, report)

    # Closed, opened again, then paused: the opening and the pause while the ad played.
    toggles = list(re.finditer(rf"--speed-seq: t=([0-9.]+) {PANEL_E}$", log, re.M))
    pause = first(rf"--speed-seq: t=([0-9.]+) {PAUSE}$", log)
    t_open = float(toggles[1].group(1)) if len(toggles) == 2 else None
    t_pause = float(pause.group(1)) if pause else None
    ordered = t_open is not None and t_pause is not None and toggles[1].start() < pause.start()
    c.check(ordered and ad_from <= t_open <= t_pause < ad_to,
            f"{name}: the panel closed, opened again and paused while the ad played, {ad_from:g}-{ad_to:g} s after "
            f"the first frame (the log: {len(toggles)} toggles, opened at {t_open} s, paused at {t_pause} s)")
    c.check(first(r"(seek|step) landed at pts|switch-trace: request|--speed-seq: seek to", log) is None,
            f"{name}: no seek, no step and no switch in the run")
    if not ordered:
        return

    # On opening, no playhead, for the ad's frame played then.
    after_open = log[toggles[1].end():]
    lost = re.search(rf"timeline: no playhead: the frame's PTS, ([0-9.]+) s, lies in sequences {SEQUENCES}: ",
                     after_open)
    f0 = lad.played_at(t_open)
    if lost and f0 is not None:
        x0 = float(lost.group(1))
        named = sequences_of(lost.group(2))
        c.check(lad.in_ad(f0) and abs(x0 - lad.frames[f0][0]) <= tol + EPS,
                f"{name}: on opening, no playhead, for PTS {x0:.3f} s, that of the ad's frame {f0} played then, "
                f"{lad.frames[f0][0]:.3f} s, within {frames_text(TOLERANCE_FRAMES)}")
        c.check(len(named) >= 2 and named == lad.lies_in(x0),
                f"{name}: PTS {x0:.3f} s lies in sequences {named}, the layout's {lad.lies_in(x0)}, more than one")
    else:
        c.check(False, f"{name}: on opening, the panel's log line of no playhead"
                + ("" if lost else " (none after the second toggle)"))

    # No playhead known again before the report, which says none and why, for the ad's frame
    # paused on; the snapshot after it, of the same frame.
    written = first(r"--timeline-report: wrote \S+ \(t=[0-9.]+\)$", log)
    paused = first(r"playback paused", log)
    c.check(written is not None and paused is not None and pause.start() < paused.start() < written.start(),
            f"{name}: the pause, then the report written")
    if written is None:
        return
    c.check("timeline: the playhead known again" not in log[toggles[1].end():written.start()],
            f"{name}: no playhead known again between the opening and the report")
    c.check(first(r"^playhead: reference frame ", report) is None, f"{name}: the report gives no playhead's frame")
    unknown = first(rf"^playhead: unknown: the frame on screen at PTS ([0-9.]+) s lies in sequences {SEQUENCES} "
                    r"of the variant on screen, .*\(D-124 \(c\)\)$", report)
    fp = lad.played_at(t_pause)
    if unknown and fp is not None:
        x = float(unknown.group(1))
        named = sequences_of(unknown.group(2))
        c.check(lad.in_ad(fp) and abs(x - lad.frames[fp][0]) <= tol + EPS,
                f"{name}: the report's PTS on screen, {x:.6f} s, that of the ad's frame {fp} paused on, "
                f"{lad.frames[fp][0]:.3f} s, within {frames_text(TOLERANCE_FRAMES)}")
        c.check(len(named) >= 2 and named == lad.lies_in(x),
                f"{name}: the report: no playhead, PTS {x:.6f} s lying in sequences {named}, the layout's "
                f"{lad.lies_in(x)}, and why (D-124 (c))")
        snap = re.search(r"ui-snapshot: wrote \S+ \(.*\) — pts=([0-9.]+) .*\(paused\)$", log[written.end():], re.M)
        c.check(snap is not None and abs(float(snap.group(1)) - x) < 1e-3,
                f"{name}: the snapshot after the report, of the frame paused on"
                + (f" (the log: pts={snap.group(1)})" if snap else " (no snapshot after the report)"))
    else:
        c.check(False, f"{name}: the report's line of no playhead and why" + ("" if unknown else " (none)"))


def check_unscored(c, out, lad):
    """A session's document whose span over the ad plays the reference, which isn't scored."""
    name = "unscored"
    report = read(os.path.join(out, "unscored", "run.txt"))
    log = read(os.path.join(out, "unscored", "run.log"))
    doc = json.loads(read(os.path.join(out, "unscored", "timeline.json")))
    ad_from, ad_to, ad_first, ad_end = lad.ad_span()
    span = (ad_from, ad_to)   # the ad's positions: the span's on the reference's playlist time (D-073)

    # The document: the span over the ad played by entry 0 and not scored, the others scored.
    spans = doc["sessions"][0]["spans"] if len(doc.get("sessions", [])) == 1 else []
    unscored = [s for s in spans if s["scored"] is False]
    c.check(len(spans) == 3 and len(unscored) == 1 and unscored[0]["entry"] == 0
            and (unscored[0]["ref_index"], unscored[0]["ref_end"]) == (ad_first, ad_end)
            and all(s["scored"] is True for s in spans if s is not unscored[0]),
            f"{name}: the document's session span over the ad, reference frames {ad_first}-{ad_end - 1}, played by "
            f"entry 0 and not scored, its other spans scored (D-105 (b))")

    loaded_and_matched(c, name, report)
    play = first(r"^playhead: reference frame (\d+) .* the frame on screen at PTS ([0-9.]+) s, "
                 r"([+-][0-9.]+) s from the report's PTS of that frame$", report)
    c.check(play is not None and abs(float(play.group(3))) <= 0.5 / lad.fps,
            f"{name}: the playhead's frame within half a frame of the frame on screen")
    c.check(first(r"^view: 2\.000 s to 6\.000 s$", report) is not None, f"{name}: the view {VIEW[0]:g}-{VIEW[1]:g} s")
    c.check(first(r"^curves shown: session:0 \(session:0\)$", report) is not None,
            f"{name}: the session's curve alone shown")
    c.check(first(rf"^  session:0: not scored over reference frames {ad_first}-{ad_end - 1} \({ad_end - ad_first} "
                  rf"frames\), {span[0]:.3f}-{span[1]:.3f} s: outside its data, no curve drawn and no point picked "
                  r"there \(D-124 \(a\)\)$", report) is not None,
            f"{name}: the report names the span that isn't scored, {span[0]:.3f}-{span[1]:.3f} s")

    # Where the curve was drawn: over its scored frames in the view, nowhere over the span.
    drawn = first(r"^curves drawn, at the last draw:\n  session:0: (.*)$", report)
    runs = [(float(a), float(b)) for a, b in re.findall(r"([0-9.]+)-([0-9.]+) s", drawn.group(1))] if drawn else []
    want = [(VIEW[0], span[0]), (span[1], VIEW[1])]
    c.check(drawn is not None and bool(runs) and all(b <= span[0] + EPS or a >= span[1] - EPS for a, b in runs),
            f"{name}: no curve drawn over the span, {span[0]:.3f}-{span[1]:.3f} s (the report: {runs})")
    c.check(len(runs) == len(want) and all(abs(a - wa) <= EPS and abs(b - wb) <= EPS
                                           for (a, b), (wa, wb) in zip(runs, want)),
            f"{name}: the curve drawn over the rest of the view, {want} (the report: {runs})")

    # The pick inside the span picks nothing; the one after it the session's frame there.
    c.check(re.search(r"timeline: --ui-timeline pick=4@3 at [0-9.]+ s after the first frame: nothing to pick there",
                      log) is not None and span[0] <= PICK_INSIDE < span[1],
            f"{name}: the pick at {PICK_INSIDE:g} s, inside the span, picks nothing (D-124 (a))")
    pick = first(r"^  Curve (\S+) \(([^)]*)\): reference frame (\d+) at ([0-9.]+) s", report)
    frame = int(pick.group(3)) if pick else None
    c.check(pick is not None and pick.group(1) == "session:0" and not ad_first <= frame < ad_end
            and abs(float(pick.group(4)) - PICK_AFTER) <= 1.0 / lad.fps,
            f"{name}: the pick at {PICK_AFTER:g} s picks the session's frame there, outside the span"
            + (f" (the report: {pick.group(1)}, reference frame {frame} at {pick.group(4)} s)" if pick else ""))
    if pick is None:
        return
    card_vs_cli(c, name, report, read(os.path.join(out, "unscored", "run.attribute.txt")))

    # Its seek, on entry 0, the reference, on screen, as the layout has it (check_ui.py's
    # check_seek(), a seek's branch).
    want = lad.decision(frame)
    seek = first(r"^Seek: to PTS ([0-9.]+) s", report)
    marker = f"timeline: --ui-timeline pick={PICK_AFTER:g}@3.5 at "
    after = log[log.index(marker):] if marker in log else ""
    landed = first(r"--speed-seq: seek landed at pts ([0-9.]+) ", after)
    c.check(want[0] == "seek" and seek is not None and abs(float(seek.group(1)) - want[1]) < 1e-3,
            f"{name}: frame {frame} sought by its PTS, {want[1]:.3f} s"
            + (f" (the report: {seek.group(1)} s)" if seek else " (the report: no seek)"))
    c.check(landed is not None and want[1] is not None and abs(float(landed.group(1)) - want[1]) <= 0.5 / lad.fps,
            f"{name}: the seek landed within half a frame of the frame's PTS"
            + (f" (the log: {landed.group(1)})" if landed else " (no landing in the log)"))
    c.check(first(r"^Seek: to PTS .*: sent and made$", report) is not None, f"{name}: the card says the seek was made")


def playhead_on(c, name, report, lad, frame, pts, told):
    """The report's playhead for the frame on screen, `frame` at `pts`, the sequence `told` by where
    the step landed, or None where the landing can't be told: on that frame, in that sequence, or,
    told nothing, in the one sequence the PTS lies in; none, and why, where it lies in more."""
    play = first(r"^playhead: reference frame (\d+) at .*, sequence (\d+); the frame on screen at PTS ([0-9.]+) s, "
                 r"([+-][0-9.]+) s from the report's PTS of that frame$", report)
    unknown = first(rf"^playhead: unknown: the frame on screen at PTS ([0-9.]+) s lies in sequences {SEQUENCES} "
                    r"of the variant on screen, .*\(D-124 \(c\)\)$", report)
    lies = lad.lies_in(pts)
    if told is None and len(lies) == 1:
        told = lies[0]
    if told is None:
        given = unknown.group(0) if unknown else play.group(0) if play else "none"
        c.check(play is None and unknown is not None and abs(float(unknown.group(1)) - pts) < 1e-3
                and sequences_of(unknown.group(2)) == lies,
                f"{name}: the step's landing not told, the report gives no playhead, PTS {pts:.3f} s lying in "
                f"sequences {lies} (the report: {given})")
        return
    want = lad.nearest_in(told, pts)
    c.check(want == frame, f"{name}: in sequence {told}, the PTS on screen, {pts:.3f} s, is frame {frame}'s, the one "
                           f"on screen (the layout: frame {want})")
    given = (f"frame {play.group(1)}, sequence {play.group(2)}, PTS {play.group(3)}, {play.group(4)} s" if play
             else "no playhead's frame")
    c.check(play is not None and int(play.group(1)) == frame and int(play.group(2)) == told
            and abs(float(play.group(3)) - pts) < 1e-3 and abs(float(play.group(4))) <= 0.5 / lad.fps,
            f"{name}: the report's playhead on frame {frame}, sequence {told}, the frame on screen at PTS {pts:.3f} s "
            f"(the report: {given})")


def check_step_back(c, out, lad):
    """A step back from the first frame of the ad whose timestamps restart at the main content's
    origin (D-125 (b) 6, Q99 6 (a)), and in run fwd the playback pipeline's frames after it."""
    _, _, ad_first, _ = lad.ad_span()
    before = lad.segments[lad.segment_of(ad_first) - 1]
    f_seek = before["first_frame"]
    c.check(abs(before["position_s"] - SEEK_TO) < EPS and f_seek + STEPS_TO_AD == ad_first,
            f"step-back: position {SEEK_TO:g} s starts the segment before the ad, frame {f_seek}, {STEPS_TO_AD} "
            f"frames before the ad's first, frame {ad_first} (the layout)")
    for run in ("run", "fwd"):
        name = f"step-back/{run}"
        report = read(os.path.join(out, "step-back", f"{run}.txt"))
        log = read(os.path.join(out, "step-back", f"{run}.log"))
        loaded_and_matched(c, name, report)
        c.check(first(r"switch-trace: request", log) is None, f"{name}: no switch in the run")

        # The paused seek to the segment before the ad: its first frame, and the playhead known on it.
        landed = first(r"--speed-seq: seek landed at pts ([0-9.]+) ", log)
        c.check(first(rf"--speed-seq: seek to position {SEEK_TO:.3f} \(pts [0-9.]+\)$", log) is not None
                and landed is not None and abs(float(landed.group(1)) - lad.frames[f_seek][0]) < 1e-3,
                f"{name}: the paused seek to position {SEEK_TO:g} s showed frame {f_seek}, PTS "
                f"{lad.frames[f_seek][0]:.3f} s" + (f" (the log: {landed.group(1)})" if landed else " (no landing)"))
        if landed is None:
            continue
        # Lost before it, where the panel first saw the main content's first 2 s, whose PTS the ad
        # repeats, the playhead is known again there; else the seek's frame was its first sight.
        lost = "timeline: no playhead" in log[:landed.start()]
        known = re.search(r"timeline: the playhead known again: sequence (\d+), reference frame (\d+), at PTS ",
                          log[landed.start():])
        c.check(not lost or (known is not None
                             and (int(known.group(1)), int(known.group(2))) == (lad.frames[f_seek][1], f_seek)),
                f"{name}: the playhead known from the seek's frame on, frame {f_seek}, sequence "
                f"{lad.frames[f_seek][1]}" + ("" if lost else ", the panel's first sight"))

        # The frame steps forward to the ad's first frame, with no line of no playhead on the way.
        back = re.search(r"--speed-seq: back step landed at pts ([0-9.]+) \S+ stream -?\d+ \(from ([0-9.]+), "
                         r"request ([0-9.]+), [0-9]+ ms\)$", log[landed.end():], re.M)
        to_back = log[landed.end():landed.end() + back.start()] if back else log[landed.end():]
        steps = [float(m.group(1)) for m in re.finditer(r"--speed-seq: fwd step landed at pts ([0-9.]+) ", to_back)]
        want = [lad.frames[f_seek + k][0] for k in range(1, STEPS_TO_AD + 1)]
        c.check(len(steps) == STEPS_TO_AD and all(abs(a - b) < 1e-3 for a, b in zip(steps, want)),
                f"{name}: {STEPS_TO_AD} frame steps forward showed frames {f_seek + 1}-{ad_first}, the last the ad's "
                f"first at PTS {lad.frames[ad_first][0]:.3f} s (the log: {len(steps)} steps"
                + (f", the last at {steps[-1]:.3f} s)" if steps else ")"))
        c.check("timeline: no playhead" not in to_back,
                f"{name}: no line of no playhead through the steps, the playhead following them into the ad's "
                f"sequence {lad.frames[ad_first][1]} where its PTS step back (D-046)")
        if back is None:
            c.check(False, f"{name}: the step back's landing in the log")
            continue

        # The step back from the ad's first frame, 1.5 frames before it: before the stream's start, so
        # that no keyframe is at or before it and the scrub decoder seeks by PTS, clamped to the start.
        x, y, z = float(back.group(1)), float(back.group(2)), float(back.group(3))
        c.check(abs(y - lad.frames[ad_first][0]) < 1e-3 and abs(z - (y - BACK_FRAMES / lad.fps)) < 1e-3
                and z < lad.start,
                f"{name}: the step back from the ad's first frame, PTS {y:.3f} s, asked for {z:.3f} s, "
                f"{BACK_FRAMES:g} frames before it and before the stream's start, {lad.start:.3f} s")
        shown = lad.scrub_shown(z)
        c.check(shown is not None and abs(x - lad.frames[shown][0]) < 1e-3,
                f"{name}: the step back showed PTS {x:.3f} s, frame {shown}'s by the layout, where the scrub "
                f"decoder's seek lands")
        if shown is None:
            continue
        tail = log[landed.end() + back.end():]
        written = first(r"--timeline-report: wrote \S+ \(t=[0-9.]+\)$", tail)
        if run == "run":
            # The report after the step: the playhead the tracker gives for the step's landing.
            c.check(written is not None, f"{name}: the report written after the step back")
            playhead_on(c, name, report, lad, shown, x, lad.landing(z))
            on_screen = x
        else:
            # The playback pipeline's frames after the ad's first, which the step back didn't move,
            # the last the main content's first after the ad: the playhead on it, and no line of no
            # playhead after the step.
            steps = [(m, float(m.group(1)))
                     for m in re.finditer(r"--speed-seq: fwd step landed at pts ([0-9.]+) ", tail)]
            want = [lad.frames[ad_first + k][0] for k in range(1, STEPS_AFTER + 1)]
            last = ad_first + STEPS_AFTER
            c.check(len(steps) == STEPS_AFTER and all(abs(p - w) < 1e-3 for (_, p), w in zip(steps, want))
                    and not lad.in_ad(last) and lad.in_ad(last - 1),
                    f"{name}: {STEPS_AFTER} frame steps forward after the step back showed the pipeline's frames "
                    f"{ad_first + 1}-{last}, the last the main content's first after the ad at PTS "
                    f"{lad.frames[last][0]:.3f} s (the log: {len(steps)} steps"
                    + (f", the last at {steps[-1][1]:.3f} s)" if steps else ")"))
            c.check(written is not None and bool(steps) and steps[-1][0].start() < written.start(),
                    f"{name}: the report written after the last step")
            c.check("timeline: no playhead" not in tail[:written.start() if written else len(tail)],
                    f"{name}: no line of no playhead after the step back")
            on_screen = steps[-1][1] if steps else None
            if on_screen is not None:
                playhead_on(c, name, report, lad, last, on_screen, lad.frames[last][1])
        snap = re.search(r"ui-snapshot: wrote \S+ \(.*\) — pts=([0-9.]+) .*\(paused\)$",
                         tail[written.end():] if written else "", re.M)
        c.check(snap is not None and on_screen is not None and abs(float(snap.group(1)) - on_screen) < 1e-3,
                f"{name}: the snapshot after the report, of the frame on screen"
                + (f" (the log: pts={snap.group(1)})" if snap else " (no snapshot after the report)"))


def main(argv):
    if len(argv) != 2:
        print("usage: check_ui_followups.py <out>", file=sys.stderr)
        return 2
    out = argv[1]
    c = Checks()
    check_inside_ad(c, out, Ladder(json.loads(read(os.path.join(out, CASES, "ad-main-origin", "layout.json")))))
    check_unscored(c, out, Ladder(json.loads(read(os.path.join(out, EXAMPLE, "ad-main-origin", "layout.json")))))
    check_step_back(c, out, Ladder(json.loads(read(os.path.join(out, CASES, "ad-main-origin", "layout.json")))))
    summary = f"{c.n - len(c.failed)} of {c.n} checks passed"
    print(f"check_ui_followups.py: {'PASS' if not c.failed else 'FAIL'}: {summary}")
    return 0 if not c.failed else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
