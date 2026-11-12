"""Checks the records of eval/timeline/run_all.sh against the ground truth of the ladders they
were made from, each ladder's layout.json (tests/SyntheticLadder.h; the design's P-Q), as
eval/phase3-survey/design.md §9.4 has it (E4); and replaces the local paths in a run's files.

usage: python3 -I -B eval/timeline/check.py check <out> --threshold T --window W --min-points N
                                                 --delay S [--json FILE]
       python3 -I -B eval/timeline/check.py curves <timeline.json>
       python3 -I -B eval/timeline/check.py late <layout.json>
       python3 -I -B eval/timeline/check.py redact <out> [--build DIR] [--repo DIR]
                                                  [--search-only]

check   Reads every record directory of <out>, a directory with layout.json and timeline.json:
        the timeline document mirage_vmaf timeline built and the answers of mirage_vmaf
        attribute --format json beside it, answer.<kind>-<index>.json, one per curve of the
        document, and cmd.txt, run_all.sh's log of the commands that wrote them; for the
        composite example also plan.json, the session's plan, server.log, slow_server.py's log,
        and server.port, the port it listened on. It derives from the layout, by the owner's
        decisions, what each document has to say, and compares (docs/TIMELINE_FORMAT.md has the
        layout):
        - the cases: §3.2's degraded ladder, its ad ladder in D-080's three arrangements of the
          ad's timestamps, and the composite example on the ad from the main content's origin;
        - each object's keys, every one and no other, in their order, as the reader takes them
          (D-092 (d)), but the FFmpeg libraries', whose members are free;
        - each document's schema, command and provenance, its axis (every reference frame's
          seconds on the playlist time and its media key, D-073, and the stretches that hold
          them, with tick_den, as D-099 (b) has the writer choose them), its variants (each rung
          by its master entry and URI, with its BANDWIDTH, RESOLUTION, CODECS and both ranks,
          D-075), every event of its timeline and no other (design §5.9: segments,
          discontinuities, the plain cue tags and their ad break, keyframes, libvmaf contexts,
          and a session's spans, switches and requests) with its frames, segment, seconds,
          media key, id and every member of its payload, no wall clock but a request's (D-073),
          in canonical order (the design's P-I), and its warnings, which are none;
        - the options record of each document and answer against the command line that wrote
          it, as cmd.txt has it: every member, with its value and in its order, the paths
          relative to <out> as given, the margins at "0/1" and an answer's interval, frames and
          drop null, and no other member but the cue-types and bad-crc that Q75 asks about,
          after a document's margins, whose values are left out; the report's and the plan's
          paths, the ones given; and each command line against the record: its files, the
          parameters given to this check, the report beside the ladder of the master its
          document names, and --fetch-base slow_server.py's, the port of server.port;
        - the detection and overlap records, the parameters given to them, and every drop of
          every answer: on which curve, over which reference frames, with how many points, and
          each event it lists in each of the six groups, with its offsets in frames and seconds,
          by D-078's scope and half-open overlap; the scores, which are measured, only for what
          the rules make of them (the magnitudes, the mean more than T below the baseline, the
          same drop with the same numbers in the document and in each answer, and a session's
          drop with its pair's, D-077);
        - the session: its plan against design §9.3's, its id and spans, its UTC anchor's
          form, the files its fetch asked the server for in D-104 (a)'s order, each request's
          URL on the server's port, status and bytes, how its measured times agree as
          docs/TIMELINE_FORMAT.md defines them (its duration its end less its start, its start
          and wall clock its first row's start, its first byte and end its last row's, and each
          row's start, sent, first byte and end in that order), and the duration of each
          request, at least the delay the server held it for, 0 for the others (the design's
          P-S); of the late request also its first byte at least the delay after its start, and
          its duration longer than every other request's, as test 3 asserts.
        It leaves out the values of what is measured: the wall clock, the session's UTC anchor,
        the fetch's times, each a number from 0, and their durations but against the delay and
        each other, and the scores. It leaves out as well what the layout doesn't give or names
        a local thing: hashes but their form, the plan's and that the rungs' media playlists,
        one text in the layout, have one; the absolute paths and URLs the programs wrote, but
        their last parts; the libraries' versions and the ladder report's libvmaf, which each
        answer must have as its document has them; the values of Q75's two options; and the
        text answers, answer.*.txt, which are kept for reading. Pending the owner (WP14's open
        question 3), it leaves out two more that decisions settle: the ladder report's model,
        model_source, model_sha256 and scaler, which each answer must have as its document has
        them and mirage_vmaf's defaults would give (D-025, D-024), and the numbers that name
        pairs, which it resolves rather than compares and the report's plan would give
        (D-092 (b)). It prints each difference with its kind, record, file and place, then a
        summary line; --json writes the same as JSON.
curves  Prints the --curve of each curve of a timeline document, one to a line: each pair's,
        pair:<k> in the order of the pairs' numbers (D-092 (b)), then each session's,
        session:<id> (D-111 (a)).
late    Prints the path, from the master's directory, of the segment that the composite
        example's server answers late: the low rung's first ad segment (design §9.3).
redact  Replaces, in every file under <out> but the media segments (.ts) and its own output,
        each local directory by its placeholder, as P-011 has the eval scripts replace them:
        <out> itself, --build's <build>, --repo's <repo>, the home directory's <home> and the
        temporary directory's ($TMPDIR, else /tmp) <tmp>, each as given and by its real path,
        the longest first. Then it searches the same files for anything local left: those
        directories, the system's temporary directory by its real path, the user name and the
        host name. It prints each file's replacements, each find and a summary line. With
        --search-only it replaces nothing and only searches, as run_all.sh does last, once
        every other file of <out> is written.

Python 3 standard library only.

Exit status: check: 0 no difference; 1 a difference, each listed; 2 bad arguments, or no
record to check. curves, late: 0; 2 an input they can't read. redact: 0 nothing local left;
3 a file still holds local information, each listed (don't commit it); 2 bad arguments.
"""

import argparse
import hashlib
import json
import math
import os
import posixpath
import pwd
import re
import shlex
import socket
import sys
from fractions import Fraction

# The groups of an attribution, in the work order's §3.1's order (D-078).
SOURCES = ("vmaf", "network", "playlist", "scte35", "player", "decoder")

# MPEG-TS's 90 kHz clock, on which the generator writes every PTS (tests/SyntheticLadder.h).
TICKS_PER_SECOND = 90000

# A forward jump of more than this many seconds starts a discontinuity sequence of an input, as
# a backward one does (D-046; vmafalign::kForwardJump, src/VMAFAlign.h).
FORWARD_JUMP_S = 10

# Seconds are doubles computed from exact ticks; they are compared to within this.
SECONDS_TOLERANCE = 1e-9

# detection.session_rules where detection ran on a session's curve (D-111 (b);
# docs/TIMELINE_FORMAT.md).
SESSION_RULES = {"drops": "pair-drops-clipped-to-spans", "pieces": "more-than-min-points",
                 "at_switch": "two-drops", "baseline": "pair-drop",
                 "spans_of_one_variant": "one-stretch"}

# The cases of the three end-to-end results (design §9.2, §9.3): (layout kind, the ad's
# timestamps, a session's record).
CASES = (("degraded", None, False), ("ad", "main-origin", False), ("ad", "from-1s", False),
         ("ad", "continuous", False), ("ad", "main-origin", True))

KINDS = ("case", "document", "answer", "axis", "variant", "event", "id", "frames", "seconds",
         "key", "wallclock", "payload", "order", "warning", "detection", "overlap", "options",
         "drop", "score", "listing", "session", "network", "duration")

# The keys of a timeline document's objects, in their order (docs/TIMELINE_FORMAT.md): the
# reader requires every key and takes no other (D-092 (d)).
TOP_KEYS = ("schema", "schema_version", "tool", "command", "ffmpeg_libraries", "options",
            "config_file", "ladder_report", "session_plan", "axis", "variants", "sessions",
            "timeline", "detection", "overlap", "attributions", "warnings")
LADDER_REPORT_KEYS = ("path", "sha256", "scores_sha256", "status", "reference_mode", "model",
                      "model_source", "model_sha256", "scaler", "libvmaf_version",
                      "libvmaf_package_version", "master")
FILE_KEYS = ("url", "location", "sha256")   # ladder_report.master, variants[].playlist
AXIS_KEYS = ("kind", "seconds", "frame_duration", "frames", "reference", "checked", "tick_den",
             "ticks", "keys", "runs", "warnings")
REFERENCE_KEYS = ("url", "time_base", "frame_rate", "sequences")
STRETCH_KEYS = {"ticks": ("first", "frames", "first_tick", "step", "phase", "scale"),
                "keys": ("first", "frames", "sequence", "first_pts_ticks", "step", "phase",
                         "scale")}
VARIANT_KEYS = ("id", "role", "pair", "entries", "url", "bandwidth", "resolution", "codecs",
                "rank_from_top", "rank_from_bottom", "playlist", "segment_map")
PLAN_KEYS = ("path", "sha256")
SESSION_KEYS = ("id", "name", "wallclock_anchor_utc", "spans")
EVENT_KEYS = ("id", "source", "type", "variant", "session", "segment", "ref_index", "ref_end",
              "t", "t_end", "seq", "pts", "pts_ticks", "wallclock", "payload")
DETECTION_KEYS = ("threshold", "window", "min_points", "baseline", "warm_up", "context_first",
                  "curves", "session_rules")
CURVE_KEYS = ("kind", "index")
ATTRIBUTION_KEYS = ("curve", "detected", "interval", "baseline", "min", "mean",
                    "drop_magnitude", "mean_drop", "overlapping_events")
INTERVAL_KEYS = ("ref_index", "ref_end", "t", "t_end", "points")
GROUP_KEYS = ("source", "events")
FOUND_KEYS = ("event", "offset", "offset_end", "offset_frames", "offset_end_frames")

# The options run_all.sh gives each subcommand (its command lines, cmd.txt), every one of them
# each time; attribute's --auto takes no value.
HARNESS_OPTIONS = {
    "timeline": ("--report", "--out", "--session", "--fetch", "--fetch-base", "--threshold",
                 "--window", "--min-points"),
    "attribute": ("--timeline", "--report", "--auto", "--curve", "--threshold", "--window",
                  "--min-points", "--format", "--out"),
}
SESSION_OPTIONS = ("--session", "--fetch", "--fetch-base")
FLAGS = ("--auto",)

# The members a timeline document's options record may add after its margins, each only where
# it is given as built: Q75 asks how --cue-types and --bad-crc are recorded, and Q76 and Q77
# (a) their values' form (docs/TIMELINE_FORMAT.md, "Decoded SCTE-35 messages"). Their values
# are left out, so that nothing compared depends on how those questions fall.
Q75_MEMBERS = ("cue-types", "bad-crc")

# slow_server.py's address, on the port it writes to server.port.
SERVER_HOST = "127.0.0.1"

# A session's UTC anchor, RFC 3339 to the microsecond with the zone Z (D-108 (c);
# utc_anchor() in src/TimelineDoc.cpp).
ANCHOR_FORM = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{6}Z")
SHA256_FORM = re.compile(r"[0-9a-f]{64}")


class LayoutError(Exception):
    """A layout.json that isn't one of the ladders this check knows."""


class InputError(Exception):
    """A file that can't be read as JSON."""


# ── JSON ─────────────────────────────────────────────────────────────────────


def _pairs(pairs):
    keys = [k for k, _ in pairs]
    dup = sorted({k for k in keys if keys.count(k) > 1})
    if dup:
        raise ValueError(f"the key {dup[0]!r} appears twice in one object")
    return dict(pairs)


def _constant(name):
    raise ValueError(f"{name} is not JSON")


def read_json(path):
    """The JSON value in `path`, objects without duplicate keys; raises InputError."""
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f, object_pairs_hook=_pairs, parse_constant=_constant)
    except (OSError, UnicodeDecodeError, ValueError) as e:
        raise InputError(f"{os.path.basename(path)}: {e}") from None


def is_int(x):
    return isinstance(x, int) and not isinstance(x, bool)


def is_number(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x)


def is_sha256(x):
    return isinstance(x, str) and SHA256_FORM.fullmatch(x) is not None


def number_matches(actual, expected):
    """Whether the JSON number `actual` is `expected`, an int or a Fraction: exactly for an int,
    to within SECONDS_TOLERANCE otherwise."""
    if not is_number(actual):
        return False
    if is_int(expected):
        return actual == expected
    return abs(actual - float(expected)) <= SECONDS_TOLERANCE


def measured(x):
    """A measured time or duration in seconds, `x` where it is a number from 0, else None: a
    value that compare_value already names as not measured."""
    return x if is_number(x) and x >= 0 else None


def plain(value):
    """`value` as JSON can write it: a Fraction as its number, a marker as its description."""
    if isinstance(value, Fraction):
        return value.numerator if value.denominator == 1 else float(value)
    if isinstance(value, dict):
        return {k: plain(v) for k, v in value.items()}
    if isinstance(value, list):
        return [plain(v) for v in value]
    if isinstance(value, (EndsWith, Anything, Measured, AtLeast)):
        return repr(value)
    return value


def text_of(value):
    """A value as a message shows it."""
    if isinstance(value, float) and value.is_integer():
        return str(int(value))
    value = plain(value)
    if isinstance(value, float) and value.is_integer():
        return str(int(value))
    return json.dumps(value)


def counted(n, word):
    return f"{n} {word}" + ("" if n == 1 else "s")


# ── The layout ───────────────────────────────────────────────────────────────


class Segment:
    def __init__(self, s, fps, rungs):
        try:
            self.index = s["index"]
            self.file = s["file"]
            self.first = s["first_frame"]
            self.frames = s["frames"]
            self.position = Fraction(str(s["position_s"]))
            self.pts_first = s["pts_first"]
            self.disc_seq = s["discontinuity_sequence"]
            self.discontinuity = s["discontinuity"]
            self.discontinuity_line = s["discontinuity_line"]
            if len(s["tags"]) != len(s["tag_lines"]):
                raise ValueError("tags and tag_lines of different lengths")
            self.tags = list(zip(s["tags"], s["tag_lines"]))
            self.uri_line = s["uri_line"]
            self.bytes = {r: s["bytes"][r] for r in rungs}
        except (KeyError, TypeError, ValueError) as e:
            raise LayoutError(f"segment {s.get('index') if isinstance(s, dict) else s}: {e!r}") \
                from None
        if not all(is_int(x) for x in (self.index, self.first, self.frames, self.pts_first,
                                        self.disc_seq, self.uri_line)) or self.frames <= 0:
            raise LayoutError(f"segment {self.index}: a field that isn't a whole number")
        self.end = self.first + self.frames
        self.extinf = Fraction(self.frames, fps)


def parse_cue(text):
    """(type, payload members) of a plain cue tag of D-069's ladders (D-070, D-076): the forms
    tests/SyntheticLadder.h writes, #EXT-X-CUE-OUT:<seconds>, #EXT-X-CUE-OUT-CONT:<elapsed>/
    <duration> and #EXT-X-CUE-IN, each with no attribute list; raises LayoutError for any
    other tag. The members follow the tag's line, in the order src/PlaylistEvents.h gives."""
    num = r"([0-9]+(?:\.[0-9]+)?)"
    if m := re.fullmatch(r"#EXT-X-CUE-OUT:" + num, text):
        return "cue_out", {"form": "CUE-OUT", "raw": text, "duration": Fraction(m.group(1)),
                           "attributes": {}}
    if m := re.fullmatch(r"#EXT-X-CUE-OUT-CONT:" + num + "/" + num, text):
        return "cue_out_cont", {"form": "CUE-OUT-CONT", "raw": text,
                                "elapsed": Fraction(m.group(1)),
                                "duration": Fraction(m.group(2)), "attributes": {}}
    if text == "#EXT-X-CUE-IN":
        return "cue_in", {"form": "CUE-IN", "raw": text, "attributes": {}}
    raise LayoutError(f"a tag this check doesn't know: {text!r}")


class Ladder:
    """A ladder's layout.json, and what follows from it: each reference frame's playlist time and
    media key, the report's discontinuity sequences, the low and control rungs, and the
    composite example's plan."""

    def __init__(self, layout):
        if not isinstance(layout, dict):
            raise LayoutError("not a JSON object")
        try:
            self.kind = layout["kind"]
            self.codec = layout["codec"]
            self.fps = layout["fps"]
            self.rungs = [{"name": r["name"], "width": r["width"], "height": r["height"],
                           "uri": r["uri"], "bandwidth": r["bandwidth"], "codecs": r["codecs"]}
                          for r in layout["rungs"]]
        except (KeyError, TypeError) as e:
            raise LayoutError(f"{e!r}") from None
        if self.kind not in ("degraded", "ad"):
            raise LayoutError(f"kind {self.kind!r}")
        if not is_int(self.fps) or self.fps <= 0 or TICKS_PER_SECOND % self.fps:
            raise LayoutError(f"fps {self.fps!r}")
        if len(self.rungs) < 3:
            raise LayoutError("fewer than three rungs")
        names = [r["name"] for r in self.rungs]
        self.segments = [Segment(s, self.fps, names) for s in layout.get("segments") or []]
        if not self.segments:
            raise LayoutError("no segments")
        self.ticks_per_frame = TICKS_PER_SECOND // self.fps
        position, first = Fraction(0), 0
        for i, s in enumerate(self.segments):
            if s.index != i or s.first != first or s.position != position:
                raise LayoutError(f"segment {s.index} doesn't follow the one before it")
            # The generator writes each EXTINF with six decimals (tests/SyntheticLadder.cpp),
            # which this check takes to be the segment's frames exactly.
            if (s.extinf * 10 ** 6).denominator != 1:
                raise LayoutError(f"segment {s.index}: an EXTINF that six decimals don't give")
            first, position = s.end, position + s.extinf
        self.frames = first
        sub = layout.get(self.kind)
        try:
            self.low = names.index(sub["rung"])
            self.control = names.index(sub["control"])
            if self.kind == "degraded":
                self.low_segments = [sub["segment"]]
                self.timestamps = None
            else:
                self.low_segments = list(sub["segments"])
                self.timestamps = sub["timestamps"]
                self.cue_out_segment = sub["cue_out_segment"]
                self.cue_in_segment = sub["cue_in_segment"]
                self.duration_s = Fraction(str(sub["duration_s"]))
        except (KeyError, TypeError, ValueError) as e:
            raise LayoutError(f"{self.kind}: {e!r}") from None
        if 0 in (self.low, self.control) or self.low == self.control:
            raise LayoutError("the low and control rungs must be two lower rungs")
        lows = [self.segment(i) for i in self.low_segments]
        if any(b.index != a.index + 1 for a, b in zip(lows, lows[1:])):
            raise LayoutError("the low-quality segments aren't consecutive")
        self.low_frames = (lows[0].first, lows[-1].end)
        self.seq_of, self.sequences = self._sequences()
        for s in self.segments:
            for text, _ in s.tags:
                parse_cue(text)
        if self.kind == "ad":
            self._check_cues()

    def segment(self, index):
        if not is_int(index) or not 0 <= index < len(self.segments):
            raise LayoutError(f"no segment {index!r}")
        return self.segments[index]

    def segment_at(self, f):
        for s in self.segments:
            if s.first <= f < s.end:
                return s
        raise LayoutError(f"no segment holds frame {f}")

    def seconds(self, f):
        """Frame f's seconds on the playlist time (D-073): its segment's position plus its PTS's
        distance from the segment's first frame; a frame past the last one frame duration more
        for each frame beyond it (docs/TIMELINE_FORMAT.md)."""
        if f >= self.frames:
            return self.seconds(self.frames - 1) + Fraction(f - self.frames + 1, self.fps)
        s = self.segment_at(f)
        return s.position + Fraction(f - s.first, self.fps)

    def pts_ticks(self, f):
        """Reference frame f's PTS, in 90 kHz ticks: every rung has the same timestamps."""
        s = self.segment_at(f)
        return s.pts_first + (f - s.first) * self.ticks_per_frame

    def _sequences(self):
        """Each reference frame's discontinuity sequence in the ladder report, by D-046's rule (a
        PTS below the frame before's, or above it by more than FORWARD_JUMP_S), and the
        sequences, each (first frame, frames, first PTS in ticks) (D-057)."""
        seq_of, seqs, prev = [], [], None
        for f in range(self.frames):
            p = self.pts_ticks(f)
            if prev is None or p < prev or p - prev > FORWARD_JUMP_S * TICKS_PER_SECOND:
                seqs.append([f, 0, p])
            seqs[-1][1] += 1
            seq_of.append(len(seqs) - 1)
            prev = p
        return seq_of, [tuple(s) for s in seqs]

    def _check_cues(self):
        out = [t for t, _ in self.segment(self.cue_out_segment).tags
               if parse_cue(t)[0] == "cue_out"]
        into = [t for t, _ in self.segment(self.cue_in_segment).tags
                if parse_cue(t)[0] == "cue_in"]
        if len(out) != 1 or len(into) != 1:
            raise LayoutError("the cue-out's or the cue-in's segment hasn't its one tag")

    def key(self, f):
        """The media key of reference frame f: (sequence, pts in ticks)."""
        return self.seq_of[f], self.pts_ticks(f)

    def tick_den(self):
        """The axis's ticks to a second (docs/TIMELINE_FORMAT.md; the axis's build in
        src/TimelineAxis.cpp): the least common multiple of the denominators of the reference's
        time base, its frame duration, half of it and every EXTINF."""
        den = math.lcm(TICKS_PER_SECOND, self.fps, 2 * self.fps)
        for s in self.segments:
            den = math.lcm(den, s.extinf.denominator)
        return den

    def stretches(self):
        """(ticks, keys): the axis's stretches as the writer chooses them for this ladder (D-099
        (b); docs/TIMELINE_FORMAT.md). Its values step by the frame duration within each run
        of frames that the layout gives, a segment's frames or more: the ticks of the playlist
        time, whose EXTINF are their segments' frames, over every frame, and the PTS between two
        restarts of the timestamps within a discontinuity sequence. The frame duration is the
        writer's first candidate, with the scale 1, and holds a run to its end, where no other
        candidate holds more frames: a step that holds two frames of a run goes on by at most
        twice the frame duration, and a restart goes back or further; so each run is one
        stretch, its step the frame duration in its values' ticks, its phase 0. Either is None
        where a run has a single frame, which the writer joins to the next frame by the whole
        step, or where the values go on by less than that after a run: forms this check doesn't
        derive, and then only the values are compared."""
        den = self.tick_den()

        def runs(values, step, key=lambda f: None):
            out = []
            for f, v in enumerate(values):
                if out and key(f) == key(f - 1) and v == values[f - 1] + step:
                    out[-1][1] += 1
                    continue
                if out and key(f) == key(f - 1) and 0 <= v - values[f - 1] <= 2 * step:
                    return None
                out.append([f, 1])
            return None if any(n < 2 for _, n in out) else out

        ticks = [self.seconds(f) * den for f in range(self.frames)]
        tick_step = Fraction(den, self.fps)
        if any(t.denominator != 1 for t in ticks + [tick_step]):
            return None, None
        tick_runs = runs(ticks, tick_step)
        pts = [self.pts_ticks(f) for f in range(self.frames)]
        pts_step = TICKS_PER_SECOND // self.fps
        key_runs = runs(pts, pts_step, key=lambda f: self.seq_of[f])
        ticks_out = None if tick_runs is None else [
            {"first": f, "frames": n, "first_tick": int(ticks[f]),
             "step": f"{int(tick_step)}/1", "phase": 0, "scale": 1} for f, n in tick_runs]
        keys_out = None if key_runs is None else [
            {"first": f, "frames": n, "sequence": self.seq_of[f], "first_pts_ticks": pts[f],
             "step": f"{pts_step}/1", "phase": 0, "scale": 1} for f, n in key_runs]
        return ticks_out, keys_out

    def ranks(self, r):
        """Rung r's ranks by BANDWIDTH, from the top and from the bottom, equal ones sharing one
        (D-075, D-099 (c))."""
        b = self.rungs[r]["bandwidth"]
        return (1 + sum(x["bandwidth"] > b for x in self.rungs),
                1 + sum(x["bandwidth"] < b for x in self.rungs))

    def resolution(self, r):
        return f"{self.rungs[r]['width']}x{self.rungs[r]['height']}"

    def codecs(self, r):
        return self.rungs[r]["codecs"] or None   # the master has no CODECS for MPEG-2

    def rung_dir(self, r):
        return posixpath.dirname(self.rungs[r]["uri"])

    def plan(self):
        """The composite example's plan (design §9.3): the control rung over the segments before
        the cue-out's, the low rung over the ad's, the control rung again from the cue-in's
        segment to the last; each span (entry, first_seq, last_seq), a rung's entry being its
        place in the master, which lists the rungs in order."""
        if self.kind != "ad":
            raise LayoutError("the composite example needs the ad ladder")
        if self.low_segments != list(range(self.cue_out_segment, self.cue_in_segment)):
            raise LayoutError("the ad's low-quality segments aren't the break's")
        return [(self.control, 0, self.cue_out_segment - 1),
                (self.low, self.cue_out_segment, self.cue_in_segment - 1),
                (self.control, self.cue_in_segment, len(self.segments) - 1)]

    def late_path(self):
        """The low rung's first ad segment, by its path from the master's directory."""
        if self.kind != "ad":
            raise LayoutError("the composite example needs the ad ladder")
        return f"{self.rung_dir(self.low)}/{self.segment(self.cue_out_segment).file}"

    def case(self, session):
        return (self.kind, self.timestamps, session)


# ── What a document has to say ───────────────────────────────────────────────


class EndsWith:
    def __init__(self, suffix):
        self.suffix = suffix

    def __repr__(self):
        return f"a text ending with {self.suffix!r}"


class Anything:
    """A value nothing gives, which any value matches: a playlist's bytes without the server's
    log, whose own difference is listed."""

    def __repr__(self):
        return "anything"


class Measured:
    """A time the fetch measured (the design's P-S): any number from 0, its value left out."""

    def __repr__(self):
        return "a measured number from 0"


class AtLeast:
    """A measured duration that has to be at least the delay injected into it (the design's
    P-S): the time the server held the response head, or 0."""

    def __init__(self, seconds):
        self.seconds = seconds

    def __repr__(self):
        return f"at least {text_of(self.seconds)} s"


class Expected:
    """An event the layout gives: everything but its id and its measured members."""

    def __init__(self, source, type_, rung, session, segment, ref_index, ref_end, payload):
        self.source, self.type, self.rung, self.session = source, type_, rung, session
        self.segment, self.ref_index, self.ref_end = segment, ref_index, ref_end
        self.payload = payload

    def base(self):
        """What an event of the document is matched by, with its frames: its source, type, rung
        and session."""
        return (self.source, self.type, self.rung, self.session)

    def name(self, ladder):
        rung = "-" if self.rung is None else ladder.rungs[self.rung]["name"]
        seg = "" if self.segment is None else f", segment {self.segment}"
        return f"{self.source}/{self.type} ({rung}{seg}, frame {self.ref_index})"


def offline_events(ladder):
    """The events of the ladder alone (design §5.9; D-074, D-076, D-081 (c), (f)): for every
    rung, read from its playlist with its segment map, its segments, its EXT-X-DISCONTINUITY
    tags, its plain cue tags and their ad break, and a keyframe at each segment's first frame,
    since each segment has an encoder of its own whose GOP is the segment
    (tests/SyntheticLadder.h); for each pair, a libvmaf context at each discontinuity sequence
    of the report but the first (D-037, D-046), on its distorted rung. Each payload is whole,
    its members in the order src/PlaylistEvents.h and src/TimelineBuild.h give: the playlists
    carry no EXT-X-PROGRAM-DATE-TIME, EXT-X-GAP, EXT-X-MAP or EXT-X-BYTERANGE, and their cue
    tags no attribute list."""
    out = []
    for r, rung in enumerate(ladder.rungs):
        d = ladder.rung_dir(r)
        for s in ladder.segments:
            out.append(Expected("playlist", "segment", r, None, s.index, s.first, s.end, {
                "line": s.uri_line, "uri": s.file, "url": EndsWith(f"/{d}/{s.file}"),
                "media_sequence": s.index, "extinf": s.extinf, "byte_range": None,
                "discontinuity_sequence": s.disc_seq, "pdt": None, "pdt_explicit": False,
                "playlist_time": s.position, "bytes": s.bytes[rung["name"]], "gap": False,
                "map": False}))
            if s.discontinuity:
                out.append(Expected("playlist", "discontinuity", r, None, s.index, s.first, None,
                                    {"line": s.discontinuity_line,
                                     "discontinuity_sequence": s.disc_seq}))
            for text, line in s.tags:
                type_, members = parse_cue(text)
                out.append(Expected("scte35", type_, r, None, s.index, s.first, None,
                                    {"line": line, **members}))
            out.append(Expected("decoder", "keyframe", r, None, s.index, s.first, None,
                                {"index": s.first}))
        if ladder.kind == "ad":
            co, ci = ladder.segment(ladder.cue_out_segment), ladder.segment(ladder.cue_in_segment)
            conts = [parse_cue(t)[1]["duration"]
                     for s in ladder.segments[co.index:ci.index] for t, _ in s.tags
                     if parse_cue(t)[0] == "cue_out_cont"]
            out_line = next(line for t, line in co.tags if parse_cue(t)[0] == "cue_out")
            in_line = next(line for t, line in ci.tags if parse_cue(t)[0] == "cue_in")
            out.append(Expected("scte35", "ad_break", r, None, co.index, co.first, ci.first, {
                "cue_out_line": out_line, "cue_in_line": in_line, "end": "cue_in",
                "durations": {"duration": ladder.duration_s},
                "cue_out_cont_durations": conts}))
        if r != 0:
            for k, (first, _, _) in enumerate(ladder.sequences[1:], start=1):
                out.append(Expected("vmaf", "context_start", r, None, None, first, None,
                                    {"context": k, "first_index": first}))
    return out


class SessionTruth:
    """The composite example as the layout gives it: the plan, its spans on the reference's
    frames, the session's events and the files its fetch asks for, in D-104 (a)'s order."""

    def __init__(self, ladder, base, master_name, served_bytes, delays):
        self.spans = []
        for entry, first, last in ladder.plan():
            self.spans.append((entry, first, last, ladder.segment(first).first,
                               ladder.segment(last).end))
        self.order = []    # the paths the fetch asks for, from the master's directory
        self.events = []

        def request(type_, rung, segment, ref_index, ref_end, path, size):
            # One request per file, unredirected, its row the chain's only one (D-104 (a)). The
            # times are measured: each is a number from 0, and the duration at least the delay
            # the server held the file's response head for, 0 for a file it didn't hold.
            url = f"{base}/{path}"
            size = Anything() if size is None else size
            row = {"url": url, "start": Measured(), "sent": Measured(),
                   "first_byte": Measured(), "end": Measured(), "status": 200,
                   "status_text": "OK", "bytes": size, "content_length": size, "failed": False,
                   "error": None}
            payload = {"url": url, "range": None, "status": 200, "status_text": "OK",
                       "bytes": size, "start": Measured(), "first_byte": Measured(),
                       "end": Measured(), "duration": AtLeast(delays.get(path, 0)),
                       "failed": False, "error": None, "origin": "fetch", "chain": [row]}
            self.events.append(Expected("network", type_, rung, 0, segment, ref_index, ref_end,
                                        payload))
            self.order.append(path)

        def rung_object(r):
            top, bottom = ladder.ranks(r)
            return {"entry": r, "uri": ladder.rungs[r]["uri"],
                    "bandwidth": ladder.rungs[r]["bandwidth"], "resolution": ladder.resolution(r),
                    "rank_from_top": top, "rank_from_bottom": bottom}

        request("playlist_request", None, None, self.spans[0][3], None, master_name,
                served_bytes.get(master_name))
        played, prev = set(), None
        for entry, first, last, ref_index, ref_end in self.spans:
            self.events.append(Expected("player", "variant_span", entry, 0, None, ref_index,
                                        ref_end, {**rung_object(entry), "first_seq": first,
                                                  "last_seq": last, "scored": entry != 0}))
            if prev is not None and prev != entry:
                self.events.append(Expected("player", "variant_switch", entry, 0, None, ref_index,
                                            None, {"from": rung_object(prev),
                                                   "to": rung_object(entry), "origin": "plan"}))
            if entry not in played:
                played.add(entry)
                uri = ladder.rungs[entry]["uri"]
                request("playlist_request", entry, None, ref_index, None, uri,
                        served_bytes.get(uri))
            for seq in range(first, last + 1):
                s = ladder.segment(seq)
                path = f"{ladder.rung_dir(entry)}/{s.file}"
                request("segment_request", entry, seq, s.first, s.end, path,
                        s.bytes[ladder.rungs[entry]["name"]])
            prev = entry


def meets(e, a, b):
    """D-078's half-open overlap of an event with [a, b)."""
    if e.ref_end is None:
        return a <= e.ref_index < b
    return e.ref_index < b and e.ref_end > a


def listing(events, curve, interval, spans):
    """The events an attribution on `curve` over `interval` lists (D-078): for a pair's curve,
    ("pair", rung), the offline events of its distorted rung and of the reference; for the
    session's, ("session", 0), the session's own events, the reference's, and the offline events
    of the rung each span plays over the part of the interval the span covers, for each span
    that meets the interval (in_session_scope(), src/TimelineQuery.cpp)."""
    a, b = interval
    if curve[0] == "pair":
        return [e for e in events
                if e.session is None and e.rung in (0, curve[1]) and meets(e, a, b)]
    out = []
    for e in events:
        if e.session == 0 or (e.session is None and e.rung == 0):
            if meets(e, a, b):
                out.append(e)
        elif e.session is None:
            if any(e.rung == entry and s < b and t > a and meets(e, max(a, s), min(b, t))
                   for entry, _, _, s, t in spans):
                out.append(e)
    return out


# ── Comparing ────────────────────────────────────────────────────────────────


class Differences:
    def __init__(self, shown):
        self.items = []
        self.shown = shown   # a text with the run's local paths as placeholders

    def add(self, kind, where, text):
        assert kind in KINDS, kind
        self.items.append({"kind": kind, "where": where, "text": self.shown(text)})


def check_keys(diffs, kind, where, obj, keys):
    """Records a difference of `kind` at `where` for the keys of `keys` that the object `obj`
    lacks, for each key it has beyond them, and, with the same keys, for another order
    (docs/TIMELINE_FORMAT.md gives each object's keys in order); returns whether `obj` is an
    object with every key."""
    if not isinstance(obj, dict):
        diffs.add(kind, where, f"{text_of(obj)}, expected an object")
        return False
    missing = [k for k in keys if k not in obj]
    extra = [k for k in obj if k not in keys]
    if missing:
        diffs.add(kind, where, f"no {', '.join(missing)}")
    if extra:
        diffs.add(kind, where, f"{', '.join(extra)}: not in the format")
    if not missing and not extra and tuple(obj) != tuple(keys):
        diffs.add(kind, where, f"its keys in the order {', '.join(obj)}, expected "
                  f"{', '.join(keys)}")
    return not missing


def compare_value(diffs, kind, where, actual, expected):
    """Records a difference of `kind` at `where` when `actual` isn't `expected`: a number to
    within the tolerance, a Fraction as seconds, the markers as they say, lists item by item and
    objects key by key, every key and no other, in their order."""
    if isinstance(expected, Anything):
        return
    if isinstance(expected, Measured):
        if not is_number(actual) or actual < 0:
            diffs.add(kind, where, f"{text_of(actual)}, expected {expected!r}")
    elif isinstance(expected, AtLeast):
        if not is_number(actual) or actual < expected.seconds:
            held = (f"less than the {text_of(expected.seconds)} s the server held the response "
                    "head" if expected.seconds else "not a duration from 0 s")
            diffs.add("duration", where, f"{text_of(actual)} s, {held}")
    elif isinstance(expected, EndsWith):
        if not (isinstance(actual, str) and actual.endswith(expected.suffix)):
            diffs.add(kind, where, f"{text_of(actual)}, expected {expected!r}")
    elif isinstance(expected, dict):
        check_keys(diffs, kind, where, actual, tuple(expected))
        if isinstance(actual, dict):
            for k, v in expected.items():
                if k in actual:
                    compare_value(diffs, kind, f"{where}.{k}", actual[k], v)
    elif isinstance(expected, list):
        if not isinstance(actual, list) or len(actual) != len(expected):
            diffs.add(kind, where, f"{text_of(actual)}, expected {len(expected)} items")
            return
        for i, (x, y) in enumerate(zip(actual, expected)):
            compare_value(diffs, kind, f"{where}[{i}]", x, y)
    elif isinstance(expected, (Fraction, int)) and not isinstance(expected, bool):
        if not number_matches(actual, expected):
            diffs.add(kind, where, f"{text_of(actual)}, expected {text_of(expected)}")
    elif actual != expected or type(actual) is not type(expected):
        diffs.add(kind, where, f"{text_of(actual)}, expected {text_of(expected)}")


def canonical_key(e):
    """The canonical order of the design's P-I (src/TimelineModel.h): by first frame, then end, a
    point at its frame, then source, type, variant and id, shorter ids first."""
    end = e["ref_end"] if e["ref_end"] is not None else e["ref_index"]
    variant = -1 if e["variant"] is None else e["variant"]
    return (e["ref_index"], end, SOURCES.index(e["source"]), e["type"], variant, len(e["id"]),
            e["id"])


class Document:
    """One timeline document or answer, read and checked against the layout."""

    def __init__(self, path, name, ladder, diffs):
        self.path, self.name, self.ladder, self.diffs = path, name, ladder, diffs
        self.ok = False
        self.rung_of = {}       # variant index -> rung
        self.variant_of = {}    # rung -> variant index
        self.expected_of = {}   # event id -> the layout's event it is matched with
        try:
            self.doc = read_json(path)
        except InputError as e:
            diffs.add("document", name, str(e))
            return
        self.ok = self._structure()

    def add(self, kind, where, text):
        self.diffs.add(kind, f"{self.name}: {where}" if where else self.name, text)

    def at(self, where):
        return f"{self.name}: {where}"

    def quietly(self, method, *args):
        """`method`'s result, its differences left out: an answer's variants and events, which
        are the queried document's, checked there once."""
        saved, self.diffs = self.diffs, Differences(lambda text: text)
        try:
            return method(*args)
        finally:
            self.diffs = saved

    def _structure(self):
        d = self.doc
        if not check_keys(self.diffs, "document", self.name, d, TOP_KEYS):
            return False
        for k, t in (("variants", list), ("timeline", list), ("attributions", list),
                     ("sessions", list), ("warnings", list), ("axis", dict), ("options", dict),
                     ("ladder_report", dict), ("overlap", dict)):
            if not isinstance(d[k], t):
                self.add("document", k, f"not a JSON {'array' if t is list else 'object'}")
                return False
        for i, e in enumerate(d["timeline"]):
            if not check_keys(self.diffs, "document", self.at(f"timeline[{i}]"), e, EVENT_KEYS):
                return False
            if e["source"] not in SOURCES or not is_int(e["ref_index"]) or \
                    not (e["ref_end"] is None or is_int(e["ref_end"])) or \
                    not isinstance(e["id"], str) or not isinstance(e["type"], str) or \
                    not (e["variant"] is None or is_int(e["variant"])) or \
                    not isinstance(e["payload"], dict):
                self.add("document", f"timeline[{i}]",
                         "an event whose fields aren't of their kinds")
                return False
        ids = [e["id"] for e in d["timeline"]]
        if len(set(ids)) != len(ids):
            self.add("document", "timeline", "two events with one id")
            return False
        return True

    # ── variants ──

    def check_variants(self):
        ladder, d = self.ladder, self.doc
        found = {}
        for i, v in enumerate(d["variants"]):
            where = f"variants[{i}]"
            check_keys(self.diffs, "variant", self.at(where), v, VARIANT_KEYS)
            if not isinstance(v, dict) or v.get("id") != i:
                self.add("variant", where, "not a variant with its index as its id")
                continue
            rungs = [r for r, rung in enumerate(ladder.rungs)
                     if v.get("entries") == [r] and isinstance(v.get("url"), str)
                     and v["url"].endswith("/" + rung["uri"])]
            if len(rungs) != 1:
                self.add("variant", where, f"entries {text_of(v.get('entries'))} and url "
                         f"{text_of(v.get('url'))} name no rung of the layout (D-075)")
                continue
            r = rungs[0]
            if r in found:
                self.add("variant", where, f"a second variant of {ladder.rungs[r]['name']}")
                continue
            found[r] = i
            self.rung_of[i] = r
            top, bottom = ladder.ranks(r)
            expected = {"role": "reference" if r == 0 else "distorted",
                        "bandwidth": ladder.rungs[r]["bandwidth"],
                        "resolution": ladder.resolution(r), "codecs": ladder.codecs(r),
                        "rank_from_top": top, "rank_from_bottom": bottom, "segment_map": "ok"}
            for k, want in expected.items():
                compare_value(self.diffs, "variant", self.at(f"{where}.{k}"), v.get(k), want)
            pair = v.get("pair")
            if (r == 0) != (pair is None) or not (pair is None or is_int(pair)):
                self.add("variant", f"{where}.pair",
                         f"{text_of(pair)}: the reference has none, each lower rung a number")
            pl = v.get("playlist")
            if check_keys(self.diffs, "variant", self.at(f"{where}.playlist"), pl, FILE_KEYS):
                if pl["url"] != v.get("url"):
                    self.add("variant", f"{where}.playlist.url",
                             f"{text_of(pl['url'])}, not the url the variant was read from")
                compare_value(self.diffs, "variant", self.at(f"{where}.playlist.location"),
                              pl["location"], EndsWith("/" + ladder.rungs[r]["uri"]))
                if not is_sha256(pl["sha256"]):
                    self.add("variant", f"{where}.playlist.sha256",
                             f"{text_of(pl['sha256'])}, not a SHA-256")
        for r, rung in enumerate(ladder.rungs):
            if r not in found:
                self.add("variant", "variants", f"no variant of the rung {rung['name']}")
        pairs = [v.get("pair") for v in d["variants"] if isinstance(v, dict)]
        if len([p for p in pairs if p is not None]) != len({p for p in pairs if p is not None}):
            self.add("variant", "variants", "two variants of one pair")
        shas = {(v.get("playlist") or {}).get("sha256") for v in d["variants"]
                if isinstance(v, dict)}
        if len(shas) > 1:
            self.add("variant", "variants",
                     "the rungs' media playlists differ, where the layout's share one text")
        self.variant_of = found
        return len(found) == len(ladder.rungs)

    def pair_of_rung(self, r):
        i = self.variant_of.get(r)
        return None if i is None else self.doc["variants"][i].get("pair")

    def rung_of_pair(self, k):
        for i, v in enumerate(self.doc["variants"]):
            if isinstance(v, dict) and v.get("pair") == k and is_int(k):
                return self.rung_of.get(i)
        return None

    def curve_key(self, c, kind=None, where=None):
        """("pair", rung) or ("session", index) of a curve reference; None when it names none.
        With `kind`, also records a difference for a curve reference of other keys."""
        if kind is not None:
            check_keys(self.diffs, kind, self.at(where), c, CURVE_KEYS)
        if not isinstance(c, dict):
            return None
        if c.get("kind") == "pair":
            r = self.rung_of_pair(c.get("index"))
            return None if r is None else ("pair", r)
        if c.get("kind") == "session" and is_int(c.get("index")) and \
                0 <= c["index"] < len(self.doc["sessions"]):
            return ("session", c["index"])
        return None

    def curve_text(self, key):
        if key[0] == "pair":
            return f"pair:{self.pair_of_rung(key[1])} ({self.ladder.rungs[key[1]]['name']})"
        return f"session:{key[1]}"

    # ── axis ──

    def check_axis(self):
        ladder, axis = self.ladder, self.doc["axis"]
        check_keys(self.diffs, "axis", self.at("axis"), axis, AXIS_KEYS)
        expected = {"kind": "reference-frames", "seconds": "playlist-time",
                    "checked": "segment-map", "frame_duration": f"1/{ladder.fps}",
                    "frames": ladder.frames, "runs": [], "warnings": []}
        for k, want in expected.items():
            compare_value(self.diffs, "warning" if k in ("runs", "warnings") else "axis",
                          self.at(f"axis.{k}"), axis.get(k), want)
        ref = axis.get("reference")
        if check_keys(self.diffs, "axis", self.at("axis.reference"), ref, REFERENCE_KEYS):
            compare_value(self.diffs, "axis", self.at("axis.reference.url"), ref["url"],
                          EndsWith("/" + ladder.rungs[0]["uri"]))
            compare_value(self.diffs, "axis", self.at("axis.reference.time_base"),
                          ref["time_base"], f"1/{TICKS_PER_SECOND}")
            compare_value(self.diffs, "axis", self.at("axis.reference.frame_rate"),
                          ref["frame_rate"], f"{ladder.fps}/1")
            seqs = [{"first_index": f, "first_pts": Fraction(p, TICKS_PER_SECOND),
                     "first_pts_ticks": p, "frames": n} for f, n, p in ladder.sequences]
            compare_value(self.diffs, "axis", self.at("axis.reference.sequences"),
                          ref["sequences"], seqs)
        tick_den = axis.get("tick_den")
        ticks = self._expand(axis.get("ticks"), "ticks", "first_tick")
        keys = self._expand(axis.get("keys"), "keys", "first_pts_ticks")
        right = set()   # the stretches whose values are the layout's
        if not is_int(tick_den) or tick_den <= 0:
            self.add("axis", "axis.tick_den", f"{text_of(tick_den)}, not a whole number from 1")
        else:
            # The form the writer gives the values: its ticks to a second (D-099 (b)).
            compare_value(self.diffs, "axis", self.at("axis.tick_den"), tick_den,
                          ladder.tick_den())
            if ticks is not None:
                bad = [f for f in range(ladder.frames)
                       if not is_int(ticks[f][1])
                       or Fraction(ticks[f][1], tick_den) != ladder.seconds(f)]
                if bad:
                    f = bad[0]
                    at = text_of(Fraction(ticks[f][1], tick_den)) if is_int(ticks[f][1]) \
                        else text_of(ticks[f][1])
                    self.add("seconds", "axis.ticks", f"{len(bad)} frames off the playlist time, "
                             f"the first frame {f} at {at} s, expected "
                             f"{text_of(ladder.seconds(f))} s")
                else:
                    right.add("ticks")
        if keys is not None:
            bad = [f for f in range(ladder.frames) if keys[f] != ladder.key(f)]
            if bad:
                f = bad[0]
                self.add("key", "axis.keys", f"{len(bad)} frames with another media key, the "
                         f"first frame {f} with (sequence, PTS ticks) {keys[f]}, expected "
                         f"{ladder.key(f)}")
            else:
                right.add("keys")
        # Where the values are right, the stretches that hold them as the writer chooses them
        # (D-099 (b)): the same axis gives the same stretches.
        for name, want in zip(("ticks", "keys"), ladder.stretches()):
            got = axis.get(name)
            if name in right and want is not None and got != want:
                k = next((k for k, (g, w) in enumerate(zip(got, want)) if g != w),
                         min(len(got), len(want)))
                first = (f"stretch {k}, {text_of(got[k])}, expected {text_of(want[k])}"
                         if k < min(len(got), len(want)) else
                         f"stretch {k}, {text_of(got[k]) if k < len(got) else 'none'}, expected "
                         f"{text_of(want[k]) if k < len(want) else 'none'}")
                self.add("axis", f"axis.{name}",
                         f"{len(got)} stretch{'' if len(got) == 1 else 'es'}, expected "
                         f"{len(want)}, not the ones D-099 (b) has the writer choose for these "
                         f"values: the first difference at {first}")

    def _expand(self, stretches, name, first_member):
        """Each frame's (sequence or None, value) from an axis's stretches
        (docs/TIMELINE_FORMAT.md): frame first + k at first_value + scale x floor((phase + k x
        num) / den); None, with a difference, where they don't cover the frames one after the
        other."""
        out = []
        if not isinstance(stretches, list):
            self.add("axis", f"axis.{name}", "not a list of stretches")
            return None
        for i, s in enumerate(stretches):
            if not check_keys(self.diffs, "axis", self.at(f"axis.{name}[{i}]"), s,
                              STRETCH_KEYS[name]):
                return None
            try:
                num, den = (int(x) for x in s["step"].split("/"))
                first, frames, phase, scale = s["first"], s["frames"], s["phase"], s["scale"]
                v0 = s[first_member]
                if first != len(out) or not all(is_int(x) for x in (frames, phase, scale)) \
                        or den <= 0 or frames < 1:
                    raise ValueError("not a stretch after the one before it")
            except (KeyError, TypeError, ValueError, AttributeError) as e:
                self.add("axis", f"axis.{name}[{i}]", f"not a stretch of the axis: {e}")
                return None
            seq = s.get("sequence")
            for k in range(frames):
                value = None if v0 is None else v0 + scale * ((phase + k * num) // den)
                out.append((seq, value) if name == "keys" else (None, value))
        if len(out) != self.ladder.frames:
            self.add("axis", f"axis.{name}", f"{len(out)} frames, expected {self.ladder.frames}")
            return None
        return out

    # ── events ──

    def event_base(self, e):
        rung = None if e["variant"] is None else self.rung_of.get(e["variant"], "?")
        return (e["source"], e["type"], rung, e["session"])

    def check_events(self, expected):
        """The document's events against the layout's, each matched by its source, type, rung
        and session: with the layout's event at the same frames where there is one, else with
        the next of those left, in the order of their frames; then their order and their
        ids."""
        ladder, events = self.ladder, self.doc["timeline"]
        actual_groups, expected_groups = {}, {}
        for i, e in enumerate(events):
            actual_groups.setdefault(self.event_base(e), []).append(i)
        for x in expected:
            expected_groups.setdefault(x.base(), []).append(x)

        def at(ref_index, ref_end):
            return (ref_index, ref_index if ref_end is None else ref_end)

        self.expected_of = {}
        for base, idx in actual_groups.items():
            idx.sort(key=lambda i: at(events[i]["ref_index"], events[i]["ref_end"]))
            free = sorted(expected_groups.get(base, []), key=lambda x: at(x.ref_index, x.ref_end))
            matched, rest = [], []
            for i in idx:
                frames = (events[i]["ref_index"], events[i]["ref_end"])
                k = next((k for k, x in enumerate(free) if (x.ref_index, x.ref_end) == frames),
                         None)
                if k is None:
                    rest.append(i)
                else:
                    matched.append((i, free.pop(k)))
            matched += list(zip(rest, free))
            for i in rest[len(free):]:
                self.add("event", f"timeline[{i}]",
                         f"{events[i]['id']}: an event the layout doesn't have")
            expected_groups[base] = free[len(rest):]
            for i, x in sorted(matched, key=lambda m: m[0]):
                self.expected_of[events[i]["id"]] = x
                self._compare_event(i, events[i], x)
        for wanted in expected_groups.values():
            for x in sorted(wanted, key=lambda x: at(x.ref_index, x.ref_end)):
                self.add("event", "timeline", f"no event {x.name(ladder)}")
        keys = [canonical_key(e) for e in events]
        for i in range(1, len(keys)):
            if keys[i] < keys[i - 1]:
                self.add("order", f"timeline[{i}]",
                         f"{events[i]['id']} comes before {events[i - 1]['id']} in canonical "
                         "order (the design's P-I)")
                break
        # Ids (docs/TIMELINE_FORMAT.md; assign_ids(), src/TimelineModel.h):
        # <source>/<type>/<variant>/<n>, '-' for no variant, n counting from 0 the events of
        # its source, type and variant in canonical order, which is the document's.
        counts = {}
        for i, e in enumerate(events):
            k = (e["source"], e["type"], e["variant"])
            n = counts.get(k, 0)
            counts[k] = n + 1
            want = f"{e['source']}/{e['type']}/{'-' if e['variant'] is None else e['variant']}/{n}"
            if e["id"] != want:
                self.add("id", f"timeline[{i}]", f"{e['id']}, expected {want}")

    def _compare_event(self, i, e, x):
        ladder = self.ladder
        where = f"timeline[{i}] {e['id']}"
        # The media sequence number of its segment, or null (docs/TIMELINE_FORMAT.md).
        compare_value(self.diffs, "event", self.at(f"{where}.segment"), e["segment"], x.segment)
        if e["ref_index"] != x.ref_index or e["ref_end"] != x.ref_end:
            self.add("frames", where, f"reference frames [{e['ref_index']}, {e['ref_end']}), "
                     f"expected [{x.ref_index}, {x.ref_end})")
            return
        compare_value(self.diffs, "seconds", self.at(f"{where}.t"), e["t"],
                      ladder.seconds(x.ref_index))
        compare_value(self.diffs, "seconds", self.at(f"{where}.t_end"), e["t_end"],
                      None if x.ref_end is None else ladder.seconds(x.ref_end))
        seq, ticks = ladder.key(x.ref_index)
        compare_value(self.diffs, "key", self.at(f"{where}.seq"), e["seq"], seq)
        compare_value(self.diffs, "key", self.at(f"{where}.pts_ticks"), e["pts_ticks"], ticks)
        compare_value(self.diffs, "key", self.at(f"{where}.pts"), e["pts"],
                      Fraction(ticks, TICKS_PER_SECOND))
        # Only a session fetch's requests are observed (D-073): their wall clock is measured,
        # every other event has none.
        if x.source == "network":
            compare_value(self.diffs, "wallclock", self.at(f"{where}.wallclock"), e["wallclock"],
                          Measured())
        elif e["wallclock"] is not None:
            self.add("wallclock", f"{where}.wallclock", f"{text_of(e['wallclock'])}, expected null")
        kind = "network" if x.source == "network" else "payload"
        compare_value(self.diffs, kind, self.at(f"{where}.payload"), e["payload"], x.payload)

    # ── detection, overlap, options ──

    def check_parameters(self, curves, params, session_curve):
        """The records of what the document ran with: detection's on `curves` with the pinned
        parameters (D-077; the design's P-R), the overlap's (D-078), and no config file; its
        options record is check_options()'s."""
        det = self.doc["detection"]
        if not check_keys(self.diffs, "detection", self.at("detection"), det, DETECTION_KEYS):
            return
        T, W, N = params["threshold"], params["window"], params["min_points"]
        expected = {"threshold": T, "window": W, "min_points": N, "baseline": "median-non-low",
                    "warm_up": max(1, W // 2), "context_first": "no-special-case",
                    "session_rules": SESSION_RULES if session_curve else None}
        for k, want in expected.items():
            compare_value(self.diffs, "detection", self.at(f"detection.{k}"), det[k],
                          Fraction(want) if isinstance(want, float) else want)
        listed = det["curves"] if isinstance(det["curves"], list) else []
        got = [self.curve_key(c, "detection", f"detection.curves[{i}]")
               for i, c in enumerate(listed)]
        if got != curves:
            self.add("detection", "detection.curves",
                     f"{', '.join('?' if c is None else self.curve_text(c) for c in got) or 'none'}"
                     f", expected {', '.join(self.curve_text(c) for c in curves)}")
        compare_value(self.diffs, "overlap", self.at("overlap"), self.doc["overlap"],
                      {"rule": "half-open", "margin_before": "0/1", "margin_after": "0/1",
                       "scope": "curve-inputs"})
        compare_value(self.diffs, "options", self.at("config_file"), self.doc["config_file"],
                      None)

    def check_options(self, expected, extra=()):
        """The options record against `expected`, the one its command line gives: every member
        and no other, in their order, each with its value; `extra` names members that may follow
        them, each once and in that order, whose values are left out (Q75_MEMBERS)."""
        opts = self.doc["options"]
        keys = tuple(expected) + tuple(k for k in extra if k in opts)
        check_keys(self.diffs, "options", self.at("options"), opts, keys)
        for k, want in expected.items():
            if k in opts:
                compare_value(self.diffs, "options", self.at(f"options.{k}"), opts[k], want)

    # ── attributions ──

    def check_attributions(self, curves, expected_events, spans, params):
        """The drops of the curves in `curves`, in their order, against the layout's: one on the
        low rung's curve and one on the session's, over the low-quality segments' frames, none
        on the control's; and what each lists. Returns {curve: the drop's numbers}."""
        ladder, atts = self.ladder, self.doc["attributions"]
        want = []
        for c in curves:
            if c == ("pair", ladder.low) or c[0] == "session":
                want.append((c, ladder.low_frames))
        got = []
        for i, a in enumerate(atts):
            ok = check_keys(self.diffs, "drop", self.at(f"attributions[{i}]"), a,
                            ATTRIBUTION_KEYS)
            c = self.curve_key(a["curve"], "drop", f"attributions[{i}].curve") if ok else None
            if c is None or not check_keys(self.diffs, "drop",
                                           self.at(f"attributions[{i}].interval"), a["interval"],
                                           INTERVAL_KEYS):
                self.add("drop", f"attributions[{i}]",
                         "not an attribution on a curve of the document")
                return {}
            got.append((c, (a["interval"]["ref_index"], a["interval"]["ref_end"])))
        if [g[0] for g in got] != [w[0] for w in want]:
            self.add("drop", "attributions",
                     f"drops on {', '.join(self.curve_text(c) for c, _ in got) or 'no curve'}, "
                     f"expected one on {', '.join(self.curve_text(c) for c, _ in want)}"
                     " and none on the other curves")
            return {}
        numbers = {}
        for i, ((c, _), (_, frames)) in enumerate(zip(got, want)):
            numbers[c] = self._check_drop(i, atts[i], c, frames, expected_events, spans, params)
        return numbers

    def _check_drop(self, i, a, curve, frames, expected_events, spans, params):
        ladder = self.ladder
        where = f"attributions[{i}] ({self.curve_text(curve)})"
        first, end = frames
        iv = a["interval"]
        if a["detected"] is not True:
            self.add("drop", f"{where}.detected", f"{text_of(a['detected'])}, expected true")
        if iv["ref_index"] != first or iv["ref_end"] != end:
            segs = ladder.low_segments
            self.add("drop", f"{where}.interval",
                     f"reference frames [{iv['ref_index']}, {iv['ref_end']}), expected "
                     f"[{first}, {end}), the low rung {ladder.rungs[ladder.low]['name']}'s "
                     f"segment{'s' if len(segs) > 1 else ''} {', '.join(map(str, segs))}")
            return None
        compare_value(self.diffs, "seconds", self.at(f"{where}.interval.t"), iv["t"],
                      ladder.seconds(first))
        compare_value(self.diffs, "seconds", self.at(f"{where}.interval.t_end"), iv["t_end"],
                      ladder.seconds(end))
        compare_value(self.diffs, "drop", self.at(f"{where}.interval.points"), iv["points"],
                      end - first)
        # The scores are measured: only what the rules make of them (D-077).
        nums = {k: a[k] for k in ("baseline", "min", "mean", "drop_magnitude", "mean_drop")}
        if not all(is_number(v) for v in nums.values()):
            self.add("score", where, f"scores that aren't numbers: {text_of(nums)}")
        else:
            if nums["drop_magnitude"] != nums["baseline"] - nums["min"] or \
                    nums["mean_drop"] != nums["baseline"] - nums["mean"]:
                self.add("score", where, "drop_magnitude and mean_drop aren't the baseline less "
                         "the minimum and the mean (Q27 (c))")
            if not nums["min"] <= nums["mean"] or not nums["mean_drop"] > params["threshold"]:
                self.add("score", where, "a drop whose mean isn't more than T below its "
                         "baseline, or whose minimum is above its mean (D-077)")
        self._check_listing(where, a, curve, frames, expected_events, spans)
        return nums

    def _check_listing(self, where, a, curve, frames, expected_events, spans):
        ladder, events = self.ladder, self.doc["timeline"]
        by_id = {e["id"]: e for e in events}
        groups = a["overlapping_events"]
        if not isinstance(groups, list) or \
                [g.get("source") if isinstance(g, dict) else None for g in groups] != list(SOURCES):
            self.add("listing", f"{where}.overlapping_events",
                     "not the six groups in §3.1's order (D-078, D-092 (d))")
            return
        first, end = frames
        want = listing(expected_events, curve, frames, spans)
        for g, source in zip(groups, SOURCES):
            gw = f"{where}.overlapping_events[{source}]"
            check_keys(self.diffs, "listing", self.at(gw), g, GROUP_KEYS)
            entries = g.get("events")
            if not isinstance(entries, list):
                self.add("listing", gw, "not a list of events")
                continue
            listed = {}
            for j, entry in enumerate(entries):
                check_keys(self.diffs, "listing", self.at(f"{gw}[{j}]"), entry, FOUND_KEYS)
                ev = by_id.get(entry.get("event") if isinstance(entry, dict) else None)
                if ev is None or ev["source"] != source:
                    self.add("listing", f"{gw}[{j}]",
                             f"{text_of(entry)}: no event of this group in the timeline")
                    continue
                x = self.expected_of.get(ev["id"])
                if x is None:
                    continue   # an event the layout doesn't have, which check_events() names
                if id(x) in listed:
                    self.add("listing", f"{gw}[{j}]", f"{ev['id']} listed twice")
                    continue
                listed[id(x)] = x
                rel = self.at(f"{gw}[{j}] {ev['id']}")
                compare_value(self.diffs, "frames", f"{rel}.offset_frames",
                              entry.get("offset_frames"), x.ref_index - first)
                compare_value(self.diffs, "frames", f"{rel}.offset_end_frames",
                              entry.get("offset_end_frames"),
                              None if x.ref_end is None else x.ref_end - first)
                compare_value(self.diffs, "seconds", f"{rel}.offset", entry.get("offset"),
                              ladder.seconds(x.ref_index) - ladder.seconds(first))
                compare_value(self.diffs, "seconds", f"{rel}.offset_end", entry.get("offset_end"),
                              None if x.ref_end is None
                              else ladder.seconds(x.ref_end) - ladder.seconds(first))
            for x in want:
                if x.source == source and id(x) not in listed:
                    self.add("listing", gw, f"doesn't list {x.name(ladder)}")
            for x in listed.values():
                if not any(x is y for y in want):
                    self.add("listing", gw, f"lists {x.name(ladder)}, which D-078 leaves out")
            keys = [(e.get("offset_frames"), canonical_key(by_id[e["event"]]))
                    for e in entries if isinstance(e, dict) and e.get("event") in by_id
                    and is_int(e.get("offset_frames"))]
            if keys != sorted(keys):
                self.add("order", gw, "not by offset, then in canonical order (D-078)")

    # ── warnings ──

    def check_warnings(self):
        if self.doc["warnings"]:
            self.add("warning", "warnings", f"{counted(len(self.doc['warnings']), 'warning')}, "
                     f"the first {text_of(self.doc['warnings'][0])}; the layout's ladders make "
                     "none")


# ── A record ─────────────────────────────────────────────────────────────────


def sha256_file(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def read_server_log(path, diffs, name):
    rows = []
    try:
        with open(path, encoding="utf-8") as f:
            for n, line in enumerate(f):
                row = json.loads(line)
                if not isinstance(row, dict) or not isinstance(row.get("path"), str):
                    raise ValueError(f"line {n + 1} isn't a request")
                rows.append(row)
    except (OSError, ValueError) as e:
        diffs.add("network", f"{name}/server.log", f"can't be read: {e}")
        return None
    # In the order the server read the requests.
    return sorted(rows, key=lambda r: r["n"] if is_int(r.get("n")) else len(rows))


def read_commands(rec_dir, rec_name, diffs):
    """The mirage_vmaf commands of a record's cmd.txt, as run_all.sh writes each, "$ " and its
    words as a shell takes them, by the file each wrote, its --out, relative to <out>:
    {out: (subcommand, {option: value}, line number)}, a flag's value True. A cmd.txt that
    can't be read, and a command line of the two subcommands with a word that isn't an option
    or an option given twice, or two that wrote one file, are differences."""
    where = f"{rec_name}/cmd.txt"
    try:
        with open(os.path.join(rec_dir, "cmd.txt"), encoding="utf-8") as f:
            lines = f.read().splitlines()
    except OSError as e:
        diffs.add("options", where, f"can't be read: {e.strerror}")
        return {}
    except UnicodeDecodeError as e:
        diffs.add("options", where, f"can't be read: {e}")
        return {}
    commands = {}
    for n, line in enumerate(lines, start=1):
        if not line.startswith("$ "):
            continue
        try:
            words = shlex.split(line[2:])
        except ValueError as e:
            diffs.add("options", f"{where}:{n}", f"not a command line: {e}")
            continue
        if len(words) < 2 or posixpath.basename(words[0]) != "mirage_vmaf" or \
                words[1] not in HARNESS_OPTIONS:
            continue
        opts, k = {}, 2
        while k < len(words):
            w = words[k]
            if w in opts:
                diffs.add("options", f"{where}:{n}", f"{w} given twice")
            if w in FLAGS:
                opts[w], k = True, k + 1
            elif w.startswith("--") and k + 1 < len(words):
                opts[w], k = words[k + 1], k + 2
            else:
                diffs.add("options", f"{where}:{n}", f"{text_of(w)}: not an option with its value")
                break
        out = opts.get("--out")
        if out in commands:
            diffs.add("options", f"{where}:{n}", f"a second command that wrote {text_of(out)}")
        commands[out] = (words[1], opts, n)
    return commands


def options_record(sub, opts):
    """(the options record, None) that mirage_vmaf `sub` writes for the options `opts` of the
    harness's command line, in its order (timeline_options() and attribute_options(),
    src/TimelineCli.cpp), or (None, what the command line has or lacks that the harness's
    don't). Paths as given, relative to <out>; the threshold the number given, W and N whole
    numbers; the margins, which the harness doesn't give, at their default, "0/1"; an answer's
    query --auto, so its interval, frames and drop null; a session's options only with
    --session (D-111 (e))."""
    allowed = [k for k in HARNESS_OPTIONS[sub]
               if sub != "timeline" or "--session" in opts or k not in SESSION_OPTIONS]
    other = [k for k in opts if k not in allowed]
    missing = [k for k in allowed if k not in opts]
    if other or missing:
        return None, ", ".join(([f"{', '.join(other)} given"] if other else []) +
                               ([f"no {', '.join(missing)}"] if missing else [])) + \
            ": not a command line of run_all.sh's, whose options record this check derives"
    try:
        T = Fraction(opts["--threshold"])
        W, N = int(opts["--window"]), int(opts["--min-points"])
    except (TypeError, ValueError):
        return None, "--threshold, --window or --min-points isn't a number"
    margins = {"margin-before": "0/1", "margin-after": "0/1"}
    if sub == "timeline":
        record = {"report": opts["--report"], "out": opts["--out"]}
        if "--session" in opts:
            record.update({"session": opts["--session"], "fetch": opts["--fetch"],
                           "fetch-base": opts["--fetch-base"]})
        record.update({"threshold": T, "window": W, "min-points": N, **margins})
        return record, None
    return {"timeline": opts["--timeline"], "interval": None, "frames": None, "drop": None,
            "auto": True, "report": opts["--report"], "curve": opts["--curve"],
            "threshold": T, "window": W, "min-points": N, **margins,
            "format": opts["--format"], "out": opts["--out"]}, None


def check_command(doc, commands, out, sub, want, params, diffs, rec_name):
    """The command line of cmd.txt that wrote `out`, against what the record has it give,
    `want`, {option: value}, and the parameters given to this check (D-077; the design's
    P-R); then the options record of `doc`, the file it wrote, against the one that command
    line gives, Q75_MEMBERS allowed after a timeline document's margins. Returns the command
    line's options, or None where cmd.txt has none."""
    where = f"{rec_name}/cmd.txt"
    c = commands.get(out)
    if c is None or c[0] != sub:
        diffs.add("options", where, f"no mirage_vmaf {sub} command that wrote {out}: its "
                  "options record can't be compared")
        return None
    _, opts, line = c
    at = f"{where}:{line} ({sub} --out {out})"
    for k, v in want.items():
        if opts.get(k) != v:
            diffs.add("options", at, f"{k} {text_of(opts.get(k))}, expected {text_of(v)}")
    for k, name in (("--threshold", "threshold"), ("--window", "window"),
                    ("--min-points", "min_points")):
        try:
            ok = float(Fraction(opts[k])) == float(params[name])
        except (KeyError, TypeError, ValueError, ZeroDivisionError):
            ok = False
        if not ok:
            diffs.add("options", at, f"{k} {text_of(opts.get(k))}, not the "
                      f"{text_of(params[name])} given to this check")
    record, problem = options_record(sub, opts)
    if problem:
        diffs.add("options", at, problem)
    else:
        doc.check_options(record, Q75_MEMBERS if sub == "timeline" else ())
    return opts


def check_report_path(doc, report, kind, rec_name, session, of_record=True):
    """The report's path in `doc`, the one its command line gave, `report`; and, with
    `of_record`, for the document, that one the record's report: the one the tests keep beside
    the ladder of the master the document names (the design's P-U), <ladder>.json for an
    acceptance case, whose record run_all.sh names after its ladder, <ladder>.session.json for
    the composite example's. An answer's --report is its document's (check_answer())."""
    path = doc.doc["ladder_report"].get("path")
    if path != report:
        doc.add(kind, "ladder_report.path", f"{text_of(path)}, not the --report given, "
                f"{text_of(report)}")
    if not of_record:
        return
    suffix = ".session.json" if session else ".json"
    name = posixpath.basename(report)
    ladder = name[:-len(suffix)] if name.endswith(suffix) and len(name) > len(suffix) else None
    if ladder is not None and not session and (ladder.endswith(".session") or ladder != rec_name):
        ladder = None
    master = doc.doc["ladder_report"].get("master")
    url = master.get("url") if isinstance(master, dict) else None
    want = f"/{posixpath.join(posixpath.dirname(report), str(ladder), 'master.m3u8')}"
    if ladder is None or not (isinstance(url, str) and url.endswith(want)):
        what = "the composite example's, <ladder>.session.json" if session else \
            f"the case's, {rec_name}.json"
        doc.add("document", "ladder_report.path", f"{text_of(report)}, not {what} beside the "
                "ladder of the master the document names (the design's P-U)")


def read_port(rec_dir, rec_name, diffs):
    """The port slow_server.py listened on, from the record's server.port; None, with a
    difference, where it can't be read."""
    try:
        with open(os.path.join(rec_dir, "server.port"), encoding="utf-8") as f:
            text = f.read().strip()
    except OSError as e:
        diffs.add("network", f"{rec_name}/server.port", f"can't be read: {e.strerror}")
        return None
    except UnicodeDecodeError as e:
        diffs.add("network", f"{rec_name}/server.port", f"can't be read: {e}")
        return None
    if not re.fullmatch(r"[0-9]{1,5}", text) or not 0 < int(text) < 65536:
        diffs.add("network", f"{rec_name}/server.port", f"{text_of(text)}, not a port")
        return None
    return int(text)


def check_ladder_report(doc, diffs):
    """The report the document names (D-072): a complete one in top-rung mode (D-002), its
    master the ladder's, its hashes SHA-256s; its path is check_report_path()'s. Its hashes'
    values and libvmaf are the report's own, which the layout doesn't give; its model,
    model_source, model_sha256 and scaler are left out too, pending the owner (WP14's open
    question 3), whose alternative compares them with mirage_vmaf's defaults (D-025 and D-024:
    vmaf_v0.6.1, built in, model_sha256 null, and bicubic)."""
    lr = doc.doc["ladder_report"]
    if not check_keys(diffs, "document", doc.at("ladder_report"), lr, LADDER_REPORT_KEYS):
        return
    compare_value(diffs, "document", doc.at("ladder_report"),
                  {k: lr[k] for k in ("status", "reference_mode")},
                  {"status": "complete", "reference_mode": "top-rung"})
    for k in ("sha256", "scores_sha256"):
        if not is_sha256(lr[k]):
            doc.add("document", f"ladder_report.{k}", f"{text_of(lr[k])}, not a SHA-256")
    master = lr["master"]
    if check_keys(diffs, "document", doc.at("ladder_report.master"), master, FILE_KEYS):
        for k in ("url", "location"):
            compare_value(diffs, "document", doc.at(f"ladder_report.master.{k}"), master[k],
                          EndsWith("/master.m3u8"))
        if not is_sha256(master["sha256"]):
            doc.add("document", "ladder_report.master.sha256",
                    f"{text_of(master['sha256'])}, not a SHA-256")


def check_record(rec_dir, rec_name, params, diffs):
    """Checks one record directory; returns (its case or None, the documents it read, the
    ladder's encoder or None)."""
    try:
        ladder = Ladder(read_json(os.path.join(rec_dir, "layout.json")))
    except (InputError, LayoutError) as e:
        diffs.add("case", f"{rec_name}/layout.json", f"not a ladder of §3.2's tests: {e}")
        return None, 0, None
    session = os.path.exists(os.path.join(rec_dir, "plan.json"))
    commands = read_commands(rec_dir, rec_name, diffs)
    doc = Document(os.path.join(rec_dir, "timeline.json"), f"{rec_name}/timeline.json", ladder,
                   diffs)
    if not doc.ok:
        return ladder.case(session), 1, ladder.codec
    d = doc.doc
    compare_value(diffs, "document", doc.at("schema"), d["schema"], "mirage-timeline")
    compare_value(diffs, "document", doc.at("schema_version"), d["schema_version"], 1)
    compare_value(diffs, "document", doc.at("tool"), d["tool"], "mirage_vmaf")
    compare_value(diffs, "document", doc.at("command"), d["command"], "timeline")
    check_ladder_report(doc, diffs)
    if not doc.check_variants():
        return ladder.case(session), 1, ladder.codec
    doc.check_axis()
    doc.check_warnings()

    expected = offline_events(ladder)
    truth, port = None, None
    if session:
        port = read_port(rec_dir, rec_name, diffs)
        truth = check_session(rec_dir, rec_name, ladder, doc, params, diffs, port)
        if truth is None:
            return ladder.case(session), 1, ladder.codec
        expected += truth.events
    elif d["sessions"] or d["session_plan"] is not None:
        doc.add("session", "sessions", "a session in the document of a ladder alone")
    doc.check_events(expected)
    if session:
        check_request_times(doc)
    spans = truth.spans if truth else []

    # The pairs by the numbers the document gives them, resolved rather than compared: which
    # number each lower rung has is left out, pending the owner (WP14's open question 3),
    # whose alternative compares it with the report's plan, each variant's pairs[].index in
    # master order (D-092 (b)).
    lower = sorted((r for r in range(1, len(ladder.rungs))),
                   key=lambda r: (doc.pair_of_rung(r) is None, doc.pair_of_rung(r) or 0))
    curves = [("pair", r) for r in lower] + ([("session", 0)] if session else [])
    doc.check_parameters(curves, params, session)
    # The command line that built it, from <out> with paths relative to it (run_all.sh): its
    # options record, the report and the plan it names, and the server it fetched from.
    want = {}
    if session:
        want = {"--session": f"{rec_name}/plan.json", "--fetch": "on"}
        if port is not None:
            want["--fetch-base"] = f"http://{SERVER_HOST}:{port}"
    opts = check_command(doc, commands, f"{rec_name}/timeline.json", "timeline", want, params,
                         diffs, rec_name)
    report = opts.get("--report") if opts else None
    if isinstance(report, str):
        check_report_path(doc, report, "document", rec_name, session)
    plan = opts.get("--session") if opts else None
    if session and isinstance(plan, str) and isinstance(d["session_plan"], dict) and \
            d["session_plan"].get("path") != plan:
        doc.add("session", "session_plan.path", f"{text_of(d['session_plan'].get('path'))}, "
                f"not the --session given, {text_of(plan)}")
    numbers = doc.check_attributions(curves, expected, spans, params)
    if session and isinstance(numbers.get(("pair", ladder.low)), dict) and \
            isinstance(numbers.get(("session", 0)), dict) and \
            numbers[("pair", ladder.low)] != numbers[("session", 0)]:
        doc.add("score", "attributions", "the session's drop hasn't its pair's drop's numbers, "
                "where it holds the whole of it (D-077, rule (iii))")
    count = 1
    for c in curves:
        count += check_answer(rec_dir, rec_name, ladder, doc, c, expected, spans, params, numbers,
                              diffs, commands, report if isinstance(report, str) else None)
    return ladder.case(session), count, ladder.codec


def curve_file(doc, c):
    if c[0] == "pair":
        return f"answer.pair-{doc.pair_of_rung(c[1])}.json"
    return f"answer.session-{doc.doc['sessions'][c[1]].get('id')}.json"


def check_answer(rec_dir, rec_name, ladder, doc, curve, expected, spans, params, numbers, diffs,
                 commands, report):
    """attribute --auto --format json's answer on one curve (D-099 (c)): the queried document's
    timeline in the layout of a timeline document, its record of detection on that curve alone,
    the drops detection found again, the same as the document's, and its options record, the
    one its command line gives, with the document's report, `report`, where it is known."""
    fname = curve_file(doc, curve)
    path = os.path.join(rec_dir, fname)
    if not os.path.exists(path):
        diffs.add("answer", f"{rec_name}/{fname}", f"no answer on {doc.curve_text(curve)}")
        return 0
    ans = Document(path, f"{rec_name}/{fname}", ladder, diffs)
    if not ans.ok:
        return 1
    a = ans.doc
    compare_value(diffs, "answer", ans.at("command"), a["command"], "attribute")
    for k in ("schema", "schema_version", "tool", "axis", "variants", "sessions", "session_plan",
              "timeline", "warnings"):
        if a[k] != doc.doc[k]:
            ans.add("answer", k, "not the queried document's (docs/TIMELINE_FORMAT.md)")
    # The report it read: the document's, by both hashes and its provenance; its path is the
    # one attribute was given (check_report_path(), below).
    if {k: v for k, v in a["ladder_report"].items() if k != "path"} != \
            {k: v for k, v in doc.doc["ladder_report"].items() if k != "path"}:
        ans.add("answer", "ladder_report", "not the report the document names, by its hashes "
                "and its provenance")
    # Its variants and events are the document's, checked there: here they only name what
    # the answer's drops list.
    if not ans.quietly(ans.check_variants):
        return 1
    ans.quietly(ans.check_events, expected)
    ans.check_parameters([curve], params, curve[0] == "session")
    want_curve = f"pair:{doc.pair_of_rung(curve[1])}" if curve[0] == "pair" else \
        f"session:{doc.doc['sessions'][curve[1]].get('id')}"
    want = {"--timeline": f"{rec_name}/timeline.json", "--auto": True, "--curve": want_curve,
            "--format": "json"}
    if report is not None:
        want["--report"] = report
    opts = check_command(ans, commands, f"{rec_name}/{fname}", "attribute", want, params, diffs,
                         rec_name)
    if opts and isinstance(opts.get("--report"), str):
        check_report_path(ans, opts["--report"], "answer", rec_name, curve[0] == "session",
                          of_record=False)
    got = ans.check_attributions([curve], expected, spans, params)
    if isinstance(got.get(curve), dict) and isinstance(numbers.get(curve), dict) and \
            got[curve] != numbers[curve]:
        ans.add("score", "attributions", "not the numbers of the document's drop on the same "
                "curve, which detection found with the same parameters")
    return 1


def check_session(rec_dir, rec_name, ladder, doc, params, diffs, port):
    """The composite example's session (design §9.3; D-068, D-104, D-105, D-108, D-111): the
    plan, the document's session and spans, and the server's log; its requests' URLs are on
    slow_server.py's port, `port` (the document's --fetch-base where it is None). Returns its
    SessionTruth, or None."""
    d = doc.doc
    try:
        plan = read_json(os.path.join(rec_dir, "plan.json"))
        want = [{"entry": e, "first_seq": f, "last_seq": l} for e, f, l in ladder.plan()]
    except (InputError, LayoutError) as e:
        diffs.add("session", f"{rec_name}/plan.json", str(e))
        return None
    if not isinstance(plan, dict) or plan.get("spans") != want:
        diffs.add("session", f"{rec_name}/plan.json",
                  f"spans {text_of(plan.get('spans') if isinstance(plan, dict) else plan)}, "
                  f"expected design §9.3's {text_of(want)}")
    sp = d["session_plan"]
    if check_keys(diffs, "session", doc.at("session_plan"), sp, PLAN_KEYS) and \
            sp["sha256"] != sha256_file(os.path.join(rec_dir, "plan.json")):
        doc.add("session", "session_plan.sha256", "not the SHA-256 of the record's plan")
    if len(d["sessions"]) != 1:
        doc.add("session", "sessions", f"{len(d['sessions'])} sessions, expected one")
        return None
    s = d["sessions"][0]
    if not check_keys(diffs, "session", doc.at("sessions[0]"), s, SESSION_KEYS):
        return None
    # Its id is its place among the sessions, as text (D-111 (a)).
    compare_value(diffs, "session", doc.at("sessions[0].id"), s["id"], "0")
    if s["name"] != (plan.get("name") if isinstance(plan, dict) else None):
        doc.add("session", "sessions[0].name", f"{text_of(s['name'])}, not the plan's name")
    # Its files were fetched, so it has its anchor, in the form of D-108 (c); its value is
    # measured.
    anchor = s["wallclock_anchor_utc"]
    if not (isinstance(anchor, str) and ANCHOR_FORM.fullmatch(anchor)):
        doc.add("wallclock", "sessions[0].wallclock_anchor_utc",
                f"{text_of(anchor)}, expected the UTC time the fetch started, "
                "YYYY-MM-DDTHH:MM:SS.ffffffZ (D-108 (c))")

    # The server's, which the options record and the command line name too (check_command()).
    base = f"http://{SERVER_HOST}:{port}" if port is not None else \
        str(d["options"].get("fetch-base"))
    master = d["ladder_report"].get("master")
    master_name = posixpath.basename(master.get("url", "")) if isinstance(master, dict) else ""

    rows = read_server_log(os.path.join(rec_dir, "server.log"), diffs, rec_name)
    served = {r["path"]: r.get("bytes") for r in rows or []}
    late = ladder.late_path()
    truth = SessionTruth(ladder, base, master_name, served, {late: params["delay"]})
    if rows is not None:
        check_server_log(rows, truth, late, params, diffs, rec_name)

    spans_want = [{"variant": doc.variant_of.get(e), "entry": e, "first_seq": f, "last_seq": l,
                   "ref_index": a, "ref_end": b, "scored": e != 0}
                  for e, f, l, a, b in truth.spans]
    compare_value(diffs, "session", doc.at("sessions[0].spans"), s["spans"], spans_want)
    return truth


def check_server_log(rows, truth, late, params, diffs, rec_name):
    """What slow_server.py was asked and did: each of the session's files once, in D-104 (a)'s
    order, each answered 200, and the late segment alone held, for the delay given."""
    paths = [r["path"] for r in rows]
    if paths != truth.order:
        diffs.add("network", f"{rec_name}/server.log",
                  f"the server was asked for {', '.join(paths) or 'nothing'}; expected each of "
                  f"the session's files once, in D-104 (a)'s order: {', '.join(truth.order)}")
    for r in rows:
        if r.get("status") != 200:
            diffs.add("network", f"{rec_name}/server.log",
                      f"{r['path']} answered {text_of(r.get('status'))}")
    held = {r["path"]: r.get("delay") for r in rows if r.get("delay")}
    if held != {late: params["delay"]}:
        diffs.add("network", f"{rec_name}/server.log",
                  f"held {text_of(held)}, expected {late} alone, for {text_of(params['delay'])} s "
                  "(design §9.3)")


def check_request_times(doc):
    """How each request's measured times agree, as docs/TIMELINE_FORMAT.md defines them (the
    session part's network events): its duration its end less its start, to within the
    tolerance of seconds; its start and its wall clock its first row's start; its first byte and
    its end its last row's; and each row's start, sent, first byte and end in that order. Of a
    file the server held (its expected duration AtLeast a delay above 0), also its first byte at
    least the delay after its start, since slow_server.py holds the response head, and its
    duration longer than every other request's, as test 3 asserts
    (tests/test_attribution_session.cpp). A value that isn't a number from 0, which
    compare_value names, takes part in none of these."""
    held, others = [], []
    for i, e in enumerate(doc.doc["timeline"]):
        x = doc.expected_of.get(e["id"])
        if x is None or x.source != "network":
            continue
        where = f"timeline[{i}] {e['id']}"
        p = e["payload"]
        rows = [r for r in p.get("chain") or [] if isinstance(r, dict)] \
            if isinstance(p.get("chain"), list) else []
        first, last = (rows[0], rows[-1]) if rows else ({}, {})
        start, head, end, duration = (measured(p.get(k)) for k in ("start", "first_byte", "end",
                                                                  "duration"))

        def agree(kind, member, value, other, what):
            if value is not None and other is not None and \
                    abs(value - other) > SECONDS_TOLERANCE:
                doc.add(kind, f"{where}.{member}", f"{text_of(value)} s, not {what}, "
                        f"{text_of(round(other, 6))} s (docs/TIMELINE_FORMAT.md)")

        if start is not None and end is not None:
            agree("duration", "payload.duration", duration, end - start, "its end less its start")
        agree("network", "payload.start", start, measured(first.get("start")),
              "its first row's start")
        agree("network", "payload.first_byte", head, measured(last.get("first_byte")),
              "its last row's first byte")
        agree("network", "payload.end", end, measured(last.get("end")), "its last row's end")
        agree("wallclock", "wallclock", measured(e.get("wallclock")),
              measured(first.get("start")), "its first row's start")
        for j, r in enumerate(rows):
            times = [(k, measured(r.get(k))) for k in ("start", "sent", "first_byte", "end")]
            times = [(k, v) for k, v in times if v is not None]
            for (k1, a), (k2, b) in zip(times, times[1:]):
                if b < a:
                    doc.add("network", f"{where}.payload.chain[{j}]",
                            f"its {k2}, {text_of(b)} s, before its {k1}, {text_of(a)} s")
                    break
        delay = x.payload["duration"].seconds
        if delay:
            if start is not None and head is not None and \
                    head - start < delay - SECONDS_TOLERANCE:
                doc.add("duration", f"{where}.payload.first_byte",
                        f"{text_of(round(head - start, 6))} s after its start, less than the "
                        f"{text_of(delay)} s the server held its response head")
            held.append((where, duration))
        elif duration is not None:
            others.append((e["id"], duration))
    for where, duration in held:
        longer = [(n, d) for n, d in others if duration is not None and d >= duration]
        if longer:
            doc.add("duration", f"{where}.payload.duration",
                    f"{text_of(duration)} s, not longer than every other request's: "
                    f"{longer[0][0]} took {text_of(longer[0][1])} s (test 3)")


def find_records(out):
    return sorted(n for n in os.listdir(out)
                  if os.path.isfile(os.path.join(out, n, "layout.json"))
                  and os.path.isfile(os.path.join(out, n, "timeline.json")))


def run_check(a):
    out = os.path.abspath(a.out)
    if not os.path.isdir(out):
        print(f"check.py: {a.out} is not a directory", file=sys.stderr)
        return 2
    records = find_records(out)
    if not records:
        print(f"check.py: no record in {a.out} (a directory with layout.json and timeline.json)",
              file=sys.stderr)
        return 2
    home = os.path.expanduser("~")

    def shown(text):
        for path, name in sorted({out: "<out>", os.path.realpath(out): "<out>",
                                  home: "<home>", os.path.realpath(home): "<home>"}.items(),
                                 key=lambda x: -len(x[0])):
            if len(path) > 1:
                text = text.replace(path, name)
        return text

    params = {"threshold": a.threshold, "window": a.window, "min_points": a.min_points,
              "delay": a.delay}
    diffs = Differences(shown)
    found, summary = {}, []
    for rec in records:
        before = len(diffs.items)
        case, docs, codec = check_record(os.path.join(out, rec), rec, params, diffs)
        if case is not None:
            found.setdefault(case, []).append(rec)
        summary.append({"record": rec, "case": list(case) if case else None, "codec": codec,
                        "documents": docs, "differences": len(diffs.items) - before})
    for case in CASES:
        recs = found.get(case, [])
        what = f"{case[0]}" + (f", the ad's timestamps {case[1]}" if case[1] else "") + \
            (", the composite example's session" if case[2] else "")
        if len(recs) != 1:
            diffs.add("case", "", f"{len(recs)} records of the case {what}"
                      + (f" ({', '.join(recs)})" if recs else "") + ", expected one")
    for case, recs in found.items():
        if case not in CASES:
            diffs.add("case", ", ".join(recs), f"a case this check doesn't know: {text_of(case)}")

    docs = sum(s["documents"] for s in summary)
    lines = [f"check.py: the records of <out>: {', '.join(records)}; T {text_of(a.threshold)}, "
             f"W {a.window}, N {a.min_points}, the late segment held {text_of(a.delay)} s"]
    for s in summary:
        lines.append(f"{s['record']}" + (f" ({s['codec']})" if s["codec"] else "") + ": "
                     + f"{counted(s['documents'], 'document')}, "
                     + (counted(s["differences"], "difference") if s["differences"]
                        else "no difference"))
    for x in diffs.items:
        lines.append(f"  [{x['kind']}] {x['where']}: {x['text']}")
    kinds = sorted({x["kind"] for x in diffs.items})
    verdict = "FAIL" if diffs.items else "PASS"
    if diffs.items:
        lines.append(f"check.py: FAIL: {counted(len(diffs.items), 'difference')} "
                     f"({', '.join(kinds)}) in {counted(len(records), 'record')}, "
                     f"{counted(docs, 'document')}")
    else:
        lines.append(f"check.py: PASS: {counted(len(records), 'record')}, "
                     f"{counted(docs, 'document')}, every answer as the layouts give it")
    print("\n".join(lines))
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump({"verdict": verdict, "parameters": params, "records": summary,
                       "differences": diffs.items, "kinds": kinds}, f, indent=1)
            f.write("\n")
    return 1 if diffs.items else 0


# ── The helpers run_all.sh uses ──────────────────────────────────────────────


def run_curves(a):
    try:
        d = read_json(a.timeline)
        pairs = sorted(v["pair"] for v in d["variants"] if v.get("pair") is not None)
        ids = [s["id"] for s in d["sessions"]]
        if not all(is_int(k) for k in pairs) or not all(isinstance(i, str) for i in ids):
            raise ValueError("a pair's number or a session's id of another kind")
    except (InputError, KeyError, TypeError, AttributeError, ValueError) as e:
        print(f"check.py curves: {a.timeline}: {e}", file=sys.stderr)
        return 2
    for k in pairs:
        print(f"pair:{k}")
    for i in ids:
        print(f"session:{i}")
    return 0


def run_late(a):
    try:
        print(Ladder(read_json(a.layout)).late_path())
    except (InputError, LayoutError) as e:
        print(f"check.py late: {a.layout}: {e}", file=sys.stderr)
        return 2
    return 0


def own_outputs():
    """(device, inode) of this process's standard output and error, which redact leaves alone:
    run_all.sh writes its log into the directory it redacts."""
    out = set()
    for stream in (sys.stdout, sys.stderr):
        try:
            st = os.fstat(stream.fileno())
            out.add((st.st_dev, st.st_ino))
        except (OSError, ValueError, AttributeError):
            pass
    return out


def run_redact(a):
    out = os.path.abspath(a.out)
    if not os.path.isdir(out):
        print(f"check.py redact: {a.out} is not a directory", file=sys.stderr)
        return 2
    for name, path in (("--build", a.build), ("--repo", a.repo)):
        if path is not None and not os.path.isdir(path):
            print(f"check.py redact: {name} {path} is not a directory", file=sys.stderr)
            return 2
    home = os.path.expanduser("~")
    tmp = os.environ.get("TMPDIR") or os.path.join(os.sep, "tmp")
    subs = {}
    for path, placeholder in ((out, "<out>"), (a.build, "<build>"), (a.repo, "<repo>"),
                              (home, "<home>"), (tmp, "<tmp>")):
        if not path:
            continue
        for p in (os.path.abspath(path), os.path.realpath(path)):
            p = p.rstrip(os.sep)
            if len(p) > 1:
                subs.setdefault(p, placeholder)
    subs = sorted(subs.items(), key=lambda s: (-len(s[0]), s[0]))
    # What would still be local after the replacements (P-011): the directories, the system's
    # temporary directory by its real path, the user name and the host name.
    needles = [(p, "a local path") for p, _ in subs]
    system_tmp = os.path.realpath(os.path.join(os.sep, "tmp")) + os.sep
    if len(system_tmp) > 2:
        needles.append((system_tmp, "a local path"))
    try:
        user = pwd.getpwuid(os.getuid()).pw_name
    except KeyError:
        user = ""
    host = socket.gethostname()
    needles += [(user, "the user name")] if len(user) >= 3 else []
    needles += [(h, "the host name") for h in sorted({host, host.split(".")[0]}) if len(h) >= 3]

    own = own_outputs()
    replaced, finds, files = {}, [], 0
    for dirpath, dirnames, names in os.walk(out):
        dirnames.sort()
        for name in sorted(names):
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, out)
            if name.endswith(".ts") or os.path.islink(path) or not os.path.isfile(path):
                continue
            st = os.stat(path)
            if (st.st_dev, st.st_ino) in own:
                continue
            files += 1
            with open(path, "rb") as f:
                data = f.read()
            n = 0
            for p, placeholder in subs if not a.search_only else ():
                k = data.count(p.encode())
                if k:
                    data = data.replace(p.encode(), placeholder.encode())
                    n += k
            if n:
                with open(path, "wb") as f:
                    f.write(data)
                replaced[rel] = n
            text = data.decode("utf-8", "replace")
            for what in sorted({what for s, what in needles if s in text}):
                finds.append((rel, what))
    name = "check.py redact" + (" --search-only" if a.search_only else "")
    for rel, n in sorted(replaced.items()):
        print(f"{name}: {rel}: {counted(n, 'replacement')}")
    for rel, what in finds:
        print(f"{name}: {rel} still holds {what}")
    total = sum(replaced.values())
    left = sorted({rel for rel, _ in finds})
    done = f"{counted(files, 'file')} of <out> searched" if a.search_only else \
        f"{counted(total, 'replacement')} in {counted(len(replaced), 'file')} of " \
        f"{counted(files, 'file')} in <out>"
    print(f"{name}: {done}; "
          + (f"{counted(len(left), 'file')} still {'holds' if len(left) == 1 else 'hold'} local "
             "information: don't commit them" if left else "nothing local left"))
    return 3 if left else 0


def positive_int(text):
    v = int(text)
    if v < 1:
        raise argparse.ArgumentTypeError(f"{text} isn't a whole number from 1")
    return v


def non_negative(text):
    v = float(text)
    if not math.isfinite(v) or v < 0:
        raise argparse.ArgumentTypeError(f"{text} isn't a number from 0")
    return v


def parse_args(argv):
    p = argparse.ArgumentParser(prog="check.py", description="Check eval/timeline/run_all.sh's "
                                "records against their ladders' layouts.")
    sub = p.add_subparsers(dest="command", required=True)
    c = sub.add_parser("check", help="check the records of a run_all.sh output directory")
    c.add_argument("out", help="run_all.sh's output directory")
    c.add_argument("--threshold", type=non_negative, required=True, metavar="T")
    c.add_argument("--window", type=positive_int, required=True, metavar="W")
    c.add_argument("--min-points", type=positive_int, required=True, metavar="N")
    c.add_argument("--delay", type=non_negative, required=True, metavar="S",
                   help="the seconds the composite example's server held the late segment")
    c.add_argument("--json", metavar="FILE", help="also write the result as JSON")
    k = sub.add_parser("curves", help="print the --curve of each curve of a timeline document")
    k.add_argument("timeline")
    t = sub.add_parser("late", help="print the composite example's late segment's path")
    t.add_argument("layout")
    r = sub.add_parser("redact", help="replace the local paths in a run_all.sh output directory")
    r.add_argument("out", help="run_all.sh's output directory")
    r.add_argument("--build", metavar="DIR", help="the build directory, shown as <build>")
    r.add_argument("--repo", metavar="DIR", help="the repository, shown as <repo>")
    r.add_argument("--search-only", action="store_true",
                   help="replace nothing: only search for anything local left")
    return p.parse_args(argv)


def main(argv=None):
    a = parse_args(sys.argv[1:] if argv is None else argv)
    return {"check": run_check, "curves": run_curves, "late": run_late,
            "redact": run_redact}[a.command](a)


if __name__ == "__main__":
    sys.exit(main())
