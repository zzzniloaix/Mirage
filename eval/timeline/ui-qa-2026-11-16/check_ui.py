"""Checks run.sh's record of the timeline panel's QA run (eval/phase3-survey/ui.md §5.3).

usage: python3 -I -B check_ui.py <out>

For each run it reads the panel's report (--timeline-report), the player's log, the ladder's
layout.json (tests/SyntheticLadder.h, the generator's ground truth) and mirage_vmaf attribute's
answer to the query of the pick's card (run.sh's <run>.attribute.txt), and checks:

- the document loaded, and matched the open master by its SHA-256 with its entries in order
  (eval/phase3-survey/ui.md §2.3); in run 2, the variant on screen is the rung switched to;
- the playhead: the frame on screen's PTS is within half a frame of the report's PTS of the frame
  it was matched to (the design's R16); after run 3's switch inside the ad whose timestamps restart
  at the main content's origin, its frame is the main content's frame of that PTS, where the
  Demuxer's reposition by PTS took playback, not the ad's frame of the same PTS;
- the pick's card: its lines from the interval's head on are those of mirage_vmaf attribute's
  answer to the same query, line by line; in run 1, it is the document's drop, and lists the
  degraded segment's URI for the degraded ladder (test 1) and the cue-out and the discontinuity of
  the ad's first segment for the ad ladders (test 2; design §9.2); in the composite example's run,
  the session's drop with its late segment's request, its times and its bytes, and the session's
  switch by its rungs;
- the seek: the decision the report gives against the one D-114 (b) and the open Q88's
  recommendation give for the picked frame on these ladders, worked out here from the layout
  alone, on the rung on screen; for a seek, the log's "seek landed at pts" within half a frame of
  the picked frame's PTS; for an offer, no seek in the log after the pick, and the offered PTS the
  start of the segment the decision names; in run 3, the offer made again for the rung switched to;
  in extras/drag, no seek asked for while the seek bar was dragged.

The decision, as src/TimelineMatch.h reads FFmpeg 8.1.1's hls demuxer, for D-069's shapes: 1 s
segments of 25 frames, each segment's first frame its keyframe, the first packet's DTS up to two
frames before the start (libx264's reorder delay, the streams' video_delay). A frame is reachable
by a seek to its PTS when its segment's timestamps continue from the stream's start (its first PTS
the start plus its playlist position), its PTS isn't before the start, and the demuxer finds its
own segment for every such lead: the EXTINF added up from the first DTS. Otherwise the decision
offers the latest segment start, at or before the frame's segment, that a seek reaches. A seek by
PTS that the demuxer places so, the switch's reposition among them, plays on from the segment it
finds, in the playlist's order.

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
# Run 2's rung, by its master entry, as run.sh switches to it; run 3's, of the ad ladder from
# the main content's origin.
RUN2_ENTRY = {"degraded": 1, "ad-main-origin": 2, "ad-from-1s": 2, "ad-continuous": 2}
RUN3_ENTRY = 2
LEAD_FRAMES = 2


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class Ladder:
    def __init__(self, layout):
        self.fps = layout["fps"]
        self.segments = layout["segments"]
        self.start = self.segments[0]["pts_first_s"]
        self.layout = layout

    def segment_of(self, frame):
        for k, s in enumerate(self.segments):
            if s["first_frame"] <= frame < s["first_frame"] + s["frames"]:
                return k
        raise ValueError(f"frame {frame} is in no segment")

    def pts(self, k, offset):
        return self.segments[k]["pts_first_s"] + offset / self.fps

    def found(self, target, lead):
        """The segment the hls demuxer finds for a seek to PTS `target`, the first DTS `lead`
        seconds before the start: the EXTINF added up from it."""
        pos_us = round((target - self.start + lead) * 1e6)
        k = 0
        for j, s in enumerate(self.segments):
            if round(s["position_s"] * 1e6) <= pos_us:
                k = j
        return k

    def reachable(self, k, offset):
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
        """("seek", PTS, segment) or ("offer", PTS of the segment start offered, its index)."""
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

    def played_after(self, target, pts):
        """The frame at PTS `pts` once a seek by PTS to `target` has played on from the segment
        the demuxer finds for it: the first such frame in the playlist's order; None where the
        leads don't agree on the segment, or no frame has that PTS."""
        found = {self.found(target, lead) for lead in (0.0, LEAD_FRAMES / self.fps)}
        if len(found) != 1:
            return None
        half = 0.5 / self.fps
        for k in range(found.pop(), len(self.segments)):
            for offset in range(self.segments[k]["frames"]):
                if abs(self.pts(k, offset) - pts) <= half:
                    return self.segments[k]["first_frame"] + offset
        return None


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


def uptime(pattern, log):
    """The t= of the log's first line matching `pattern`, or None."""
    m = first(pattern + r".*\(t=([0-9.]+)\)$", log)
    return float(m.group(1)) if m else None


def common(c, name, report, lad):
    """The load, the match and the playhead; the variant on screen's entry, or None."""
    half = 0.5 / lad.fps
    c.check(first(r"^load: loaded: ", report) is not None, f"{name}: the document loaded")
    c.check(first(r"^match: master SHA-256, entries by their order$", report) is not None,
            f"{name}: matched by the master's SHA-256, its entries in order")
    screen = first(r"^variant on screen: player entry (\d+): entry (\d+) ", report)
    c.check(screen is not None, f"{name}: a variant of the document on screen")
    play = first(r"^playhead: reference frame (\d+) .* the frame on screen at PTS ([0-9.]+) s, "
                 r"([+-][0-9.]+) s from the report's PTS of that frame$", report)
    c.check(play is not None and abs(float(play.group(3))) <= half,
            f"{name}: the playhead's frame within half a frame of the frame on screen"
            + (f" ({play.group(3)} s, reference frame {play.group(1)})" if play else ""))
    return (int(screen.group(1)) if screen else None), play


def card_vs_cli(c, name, report, answer):
    """The card's lines from the interval's head on, against mirage_vmaf attribute's block."""
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


def check_seek(c, name, report, log, lad, frame, entry=None):
    """The seek decision against the layout's, and what the log shows after the pick."""
    half = 0.5 / lad.fps
    want = lad.decision(frame)
    seek = first(r"^Seek: to PTS ([0-9.]+) s", report)
    offer = first(r"^Seek: the frame isn't reachable by a seek: .* Offered: PTS ([0-9.]+) s, the start of "
                  r"segment (\d+) of entry (\d+) ", report)
    after = log[log.index("timeline: --ui-timeline pick="):] if "timeline: --ui-timeline pick=" in log else ""
    landed = first(r"--speed-seq: seek landed at pts ([0-9.]+) ", after)
    if want[0] == "seek":
        c.check(seek is not None and abs(float(seek.group(1)) - want[1]) < 1e-3,
                f"{name}: frame {frame} sought by its PTS, {want[1]:.3f} s"
                + (f" (the report: {seek.group(1)} s)" if seek else " (the report: no seek)"))
        c.check(landed is not None and abs(float(landed.group(1)) - want[1]) <= half,
                f"{name}: the seek landed within half a frame of {want[1]:.3f} s"
                + (f" (the log: {landed.group(1)})" if landed else " (no landing in the log)"))
        c.check(first(r"^Seek: to PTS .*: sent and made$", report) is not None,
                f"{name}: the card says the seek was made")
    else:
        c.check(offer is not None and abs(float(offer.group(1)) - want[1]) < 1e-3
                and int(offer.group(2)) == want[2] and (entry is None or int(offer.group(3)) == entry),
                f"{name}: frame {frame} not reachable by a seek, segment {want[2]}'s start offered, "
                f"PTS {want[1]:.3f} s" + (f", of entry {entry}" if entry is not None else "")
                + (f" (the report: {offer.group(1)} s, segment {offer.group(2)} of entry {offer.group(3)})"
                   if offer else " (the report: no offer)"))
        c.check(landed is None, f"{name}: no seek after the pick" +
                (f" (the log: one landed at {landed.group(1)})" if landed else ""))


def picked(c, name, report):
    pick = first(r"^  Curve (\S+) \(([^)]*)\): reference frame (\d+) at ", report)
    c.check(pick is not None, f"{name}: a frame picked")
    return int(pick.group(3)) if pick else None


def check_run(c, out, ladder, run, lad):
    name = f"{ladder}/{run}"
    report = read(os.path.join(out, ladder, f"{run}.txt"))
    log = read(os.path.join(out, ladder, f"{run}.log"))
    entry, _ = common(c, name, report, lad)
    if run == "run2":
        c.check(entry == RUN2_ENTRY[ladder],
                f"{name}: on screen the rung switched to, entry {RUN2_ENTRY[ladder]} (the report: entry {entry})")
    frame = picked(c, name, report)
    if frame is None:
        return
    card_vs_cli(c, name, report, read(os.path.join(out, ladder, f"{run}.attribute.txt")))
    card = report[report.index("card:"):]
    if run == "run1":
        c.check("In the document's attribution 0, a detected drop:" in card,
                f"{name}: the card is the document's attribution 0")
        if lad.layout["kind"] == "degraded":
            k = lad.layout["degraded"]["segment"]
            uri = lad.segments[k]["file"]
            c.check(re.search(rf"^    playlist segment +entry \d+, segment {k} at .*: {re.escape(uri)}, EXTINF",
                              card, re.M) is not None,
                    f"{name}: the card lists the degraded segment's URI, {uri} (test 1)")
        else:
            ad = next(k for k, s in enumerate(lad.segments) if s["period"] == "ad")
            for what in ("scte35   cue_out ", "playlist discontinuity "):
                c.check(re.search(rf"^    {what} *entry \d+, segment {ad} at ", card, re.M) is not None,
                        f"{name}: the card lists the {what.split()[1]} of the ad's first segment, "
                        f"segment {ad} (test 2)")
    check_seek(c, name, report, log, lad, frame)


def check_run3(c, out, lad):
    """The ad ladder from the main content's origin, switched inside the ad after a pick there."""
    name = "ad-main-origin/run3"
    report = read(os.path.join(out, "ad-main-origin", "run3.txt"))
    log = read(os.path.join(out, "ad-main-origin", "run3.log"))
    entry, play = common(c, name, report, lad)
    c.check(entry == RUN3_ENTRY, f"{name}: on screen the rung switched to, entry {RUN3_ENTRY} (the report: entry {entry})")
    switched = uptime(r"switch-trace: first new frame presented ", log)
    paused = uptime(r"playback paused", log)
    written = uptime(r"--timeline-report: wrote ", log)
    c.check(None not in (switched, paused, written) and switched < paused < written,
            f"{name}: the switch's first frame shown, then the pause, then the report "
            f"(the log: t={switched}, t={paused}, t={written})")
    request = first(r"switch-trace: request video -> stream \d+ at pts ([0-9.]+) ", log)
    repo = first(r"switch-trace: repositioned to ([0-9.]+) ", log)
    ad = [k for k, s in enumerate(lad.segments) if s["period"] == "ad"]
    if request and repo and play:
        at = float(request.group(1))
        in_ad = any(lad.pts(k, 0) - 1e-9 <= at < lad.pts(k, lad.segments[k]["frames"]) for k in ad)
        c.check(in_ad, f"{name}: the switch requested inside the ad, at PTS {at:.3f}, the ad's PTS restarting "
                       f"at the main content's origin")
        want = lad.played_after(float(repo.group(1)), float(play.group(2)))
        got = int(play.group(1))
        c.check(want is not None and got == want and not any(lad.segment_of(got) == k for k in ad),
                f"{name}: after the reposition to {repo.group(1)} the playhead at reference frame {want}, the "
                f"main content's frame of PTS {play.group(2)}, not the ad's (the report: reference frame {got})")
    else:
        c.check(False, f"{name}: the switch's request, its reposition and the playhead in the log and the report")
    frame = picked(c, name, report)
    if frame is None:
        return
    card_vs_cli(c, name, report, read(os.path.join(out, "ad-main-origin", "run3.attribute.txt")))
    c.check(first(r"^Seek: .*; not taken, offered again for this variant$", report) is not None,
            f"{name}: the decision made again for the rung switched to, its offer not taken")
    check_seek(c, name, report, log, lad, frame, entry=RUN3_ENTRY)


def check_session(c, out, lad):
    """The composite example: the session's drop."""
    name = "session/run1"
    report = read(os.path.join(out, "session", "run1.txt"))
    log = read(os.path.join(out, "session", "run1.log"))
    common(c, name, report, lad)
    frame = picked(c, name, report)
    if frame is None:
        return
    card_vs_cli(c, name, report, read(os.path.join(out, "session", "run1.attribute.txt")))
    card = report[report.index("card:"):]
    c.check(first(r"^  Curve session:0 \(a session\): ", card) is not None and
            "In the document's attribution 1, a detected drop:" in card,
            f"{name}: the card is the session's drop, the document's attribution 1")
    ad = next(k for k, s in enumerate(lad.segments) if s["period"] == "ad")
    late = first(rf"^    network  segment_request  entry \d+, segment {ad} at .*: \S+, requested at [0-9.]+ s: "
                 r"([0-9.]+) s from request to last byte \([0-9.]+ s to the response head\), \d+ bytes, 200 OK$", card)
    c.check(late is not None and float(late.group(1)) >= 1.0,
            f"{name}: the card lists the ad's first segment's request, answered late, with its times and bytes"
            + (f" ({late.group(1)} s to its last byte)" if late else ""))
    c.check(first(r"^    player   variant_switch   .*: entry \d+ \(\S+, \d+x\d+, BANDWIDTH \d+; rank \d+ from the top, "
                  r"\d+ from the bottom\) -> entry \d+ \(", card) is not None,
            f"{name}: the card names the session's switch by its rungs")
    check_seek(c, name, report, log, lad, frame)


def check_drag(c, out, lad):
    """A frame a seek reaches, picked while a seek-bar drag holds the seeks."""
    name = "extras/drag"
    report = read(os.path.join(out, "extras", "drag.txt"))
    log = read(os.path.join(out, "extras", "drag.log"))
    frame = picked(c, name, report)
    if frame is None:
        return
    want = lad.decision(frame)
    c.check(want[0] == "seek" and first(rf"^Seek: to PTS {want[1]:.3f} s, .*: not sent: the seek bar was being "
                                        r"dragged, and its release seeks$", report) is not None,
            f"{name}: frame {frame}, which a seek to {want[1]:.3f} s reaches, not sent while the seek bar was dragged")
    half = 0.5 / lad.fps
    after = log[log.index("timeline: --ui-timeline pick="):] if "timeline: --ui-timeline pick=" in log else ""
    landings = [float(m) for m in re.findall(r"--speed-seq: seek landed at pts ([0-9.]+) ", after)]
    c.check(want[1] is not None and all(abs(t - want[1]) > half for t in landings),
            f"{name}: no seek landed at the picked frame (the log's landings after the pick: {landings})")


def main(argv):
    if len(argv) != 2:
        print("usage: check_ui.py <out>", file=sys.stderr)
        return 2
    out = argv[1]
    c = Checks()
    ladders = {}
    for ladder in ("degraded", "ad-main-origin", "ad-from-1s", "ad-continuous"):
        ladders[ladder] = Ladder(json.loads(read(os.path.join(out, CASES, ladder, "layout.json"))))
        for run in ("run1", "run2"):
            check_run(c, out, ladder, run, ladders[ladder])
    check_run3(c, out, ladders["ad-main-origin"])
    check_session(c, out, Ladder(json.loads(read(os.path.join(out, EXAMPLE, "ad-main-origin", "layout.json")))))
    check_drag(c, out, ladders["ad-continuous"])
    keys = read(os.path.join(out, "extras", "keys.log"))
    c.check("key: Esc — closing the timeline" in keys, "extras/keys: Esc closes the timeline panel")
    after = read(os.path.join(out, "extras", "ruler-after.txt"))
    c.check(re.search(r"^match: variant, entries not matched$", after, re.M) is not None,
            "extras/ruler-after: the media playlist opened alone matched as its variant")
    c.check(re.search(r"^variant on screen: player entry -1: entry 2 ", after, re.M) is not None,
            "extras/ruler-after: on screen entry 2, the playlist opened")
    summary = f"{c.n - len(c.failed)} of {c.n} checks passed"
    print(f"check_ui.py: {'PASS' if not c.failed else 'FAIL'}: {summary}")
    return 0 if not c.failed else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
