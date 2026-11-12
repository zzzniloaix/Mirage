"""Unit tests of eval/timeline/check.py against fake records, of its redact helper, and of
slow_server.py and run_all.sh's argument checks. The fake records are made here: a small
ladder's layout.json, three rungs of four segments of four frames at 4 fps, the timeline
documents and answers that mirage_vmaf would write for it, written out in the layout of
docs/TIMELINE_FORMAT.md with this file's own arithmetic, not check.py's, and the cmd.txt that
run_all.sh would write beside them. One record of each case matches; then each test changes one
field of one kind that check.py checks and expects that difference, and only it, a measured
duration below the delay among them; and a record whose measured values are all changed, each
as another run could measure it, matches still.

python3 -I -B -m unittest discover -s eval/timeline/tests -v
"""

import contextlib
import copy
import hashlib
import io
import json
import os
import pwd
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
TIMELINE = os.path.dirname(HERE)
sys.dont_write_bytecode = True
sys.path.insert(0, TIMELINE)
import check  # noqa: E402

FPS = 4
FRAMES_PER_SEGMENT = 4
SEGMENTS = 4
TICKS = 90000 // FPS
ORIGIN = 10 * 90000   # the main content's first PTS, as the generator has it
RUNGS = [("v360", 640, 360, 3000, "avc1.64001e"), ("v270", 480, 270, 2000, "avc1.640015"),
         ("v180", 320, 180, 1000, "avc1.64000c")]
PORT = 8000
BASE = f"http://127.0.0.1:{PORT}"
DELAY = 2.3
PARAMS = ["--threshold", "6", "--window", "50", "--min-points", "12", "--delay", str(DELAY)]
SOURCES = ["vmaf", "network", "playlist", "scte35", "player", "decoder"]
SESSION_RULES = {"drops": "pair-drops-clipped-to-spans", "pieces": "more-than-min-points",
                 "at_switch": "two-drops", "baseline": "pair-drop",
                 "spans_of_one_variant": "one-stretch"}
SCORES = {"baseline": 90.5, "min": 20.25, "mean": 30.5, "drop_magnitude": 70.25,
          "mean_drop": 60.0}
# The fake records' <out>, a path of no machine, and where the tests would keep the ladders
# under it: the acceptance cases' test case and the composite example's.
OUT = "/fake/out"
CASES_DIR = "media/attribution_acceptance__fake_cases"
SESSION_DIR = "media/attribution_acceptance__fake_session"


class FakeLadder:
    """A ladder as tests/SyntheticLadder.h would lay it out: the degraded one, v270's segment 2
    low; or the ad one, v180's ad in segments 1 and 2 between EXT-X-DISCONTINUITY tags, with
    EXT-X-CUE-OUT, EXT-X-CUE-OUT-CONT and EXT-X-CUE-IN."""

    def __init__(self, kind, timestamps=None):
        self.kind, self.timestamps = kind, timestamps
        self.name = "degraded" if kind == "degraded" else f"ad-{timestamps}"
        self.low, self.control = (1, 2) if kind == "degraded" else (2, 1)
        self.low_frames = (8, 12) if kind == "degraded" else (4, 12)
        segs, line = [], 7
        for i in range(SEGMENTS):
            ad = kind == "ad" and i in (1, 2)
            if kind == "degraded":
                file, pts = f"seg_{i:03d}.ts", ORIGIN + i * 90000
            elif ad:
                file = f"ad_{i - 1:03d}.ts"
                pts = {"main-origin": ORIGIN + (i - 1) * 90000,
                       "from-1s": 90000 * i, "continuous": ORIGIN + i * 90000}[timestamps]
            else:
                file, pts = f"main_{i:03d}.ts", ORIGIN + i * 90000
            disc = kind == "ad" and i in (1, 3)
            tags = []
            if kind == "ad":
                tags = {1: ["#EXT-X-CUE-OUT:2.000"], 2: ["#EXT-X-CUE-OUT-CONT:1.000/2.000"],
                        3: ["#EXT-X-CUE-IN"]}.get(i, [])
            disc_line = None
            if disc:
                disc_line, line = line, line + 1
            tag_lines = list(range(line, line + len(tags)))
            line += len(tags)
            segs.append({
                "index": i, "file": file, "period": "ad" if ad else "main",
                "first_frame": i * FRAMES_PER_SEGMENT, "frames": FRAMES_PER_SEGMENT,
                "position_s": i, "pts_first": pts, "pts_first_s": pts / 90000,
                "source_first": i * FRAMES_PER_SEGMENT, "discontinuity": disc,
                "discontinuity_sequence": (0 if i == 0 else 1 if i < 3 else 2) if kind == "ad"
                else 0,
                "discontinuity_line": disc_line, "tags": tags, "tag_lines": tag_lines,
                "extinf_line": line, "uri_line": line + 1,
                "q": {r[0]: 22 for r in RUNGS},
                "bytes": {r[0]: 1000 * (3 - k) + 10 * i for k, r in enumerate(RUNGS)}})
            line += 2
        self.segments = segs
        # The report's discontinuity sequences, worked out by hand: a backward jump at the
        # ad's start from the main content's origin; from 1 s, a backward one there and a
        # forward one of 10.25 s at its end; none where the timestamps go on.
        self.sequences = {
            None: [(0, 16, ORIGIN)], "continuous": [(0, 16, ORIGIN)],
            "main-origin": [(0, 4, ORIGIN), (4, 12, ORIGIN)],
            "from-1s": [(0, 4, ORIGIN), (4, 8, 90000), (12, 4, ORIGIN + 3 * 90000)],
        }[timestamps]
        # The axis's key stretches (D-099 (b)), worked out by hand too: (first, frames,
        # sequence, first PTS), one wherever the PTS stop following the frame duration, at the
        # ad's start and at its end, but where the ad from 1 s goes on into its second segment.
        self.key_stretches = {
            None: [(0, 16, 0, ORIGIN)], "continuous": [(0, 16, 0, ORIGIN)],
            "main-origin": [(0, 4, 0, ORIGIN), (4, 8, 1, ORIGIN), (12, 4, 1, ORIGIN + 3 * 90000)],
            "from-1s": [(0, 4, 0, ORIGIN), (4, 8, 1, 90000), (12, 4, 2, ORIGIN + 3 * 90000)],
        }[timestamps]

    def layout(self):
        sub = {"rung": RUNGS[self.low][0], "control": RUNGS[self.control][0]}
        if self.kind == "degraded":
            sub.update({"segment": 2, "q_normal": 22, "q_low": 46})
        else:
            sub.update({"segments": [1, 2], "cue_out_segment": 1, "cue_in_segment": 3,
                        "duration_s": 2, "timestamps": self.timestamps,
                        "ad_origin_pts": self.segments[1]["pts_first"], "content": "calm",
                        "q_normal": 22, "q_low": 46})
        return {"kind": self.kind, "codec": "libx264", "source": "noise", "fps": FPS,
                "segment_frames": FRAMES_PER_SEGMENT, "x264_sei": "", "ffmpeg_libraries": {},
                self.kind: sub,
                "rungs": [{"name": n, "width": w, "height": h, "uri": f"{n}/index.m3u8",
                           "bandwidth": b, "average_bandwidth": b - 100, "codecs": c,
                           "bytes": 4000} for n, w, h, b, c in RUNGS],
                "segments": self.segments}

    def segment_of(self, f):
        return self.segments[min(f, 15) // FRAMES_PER_SEGMENT]

    def seq(self, f):
        return max(k for k, (first, _, _) in enumerate(self.sequences) if first <= f)

    def pts_ticks(self, f):
        s = self.segment_of(f)
        return s["pts_first"] + (f - s["first_frame"]) * TICKS


def ranks(r):
    b = RUNGS[r][3]
    return 1 + sum(x[3] > b for x in RUNGS), 1 + sum(x[3] < b for x in RUNGS)


class FakeRun:
    """What run_all.sh would leave for one ladder: the timeline document, its answers on each
    curve, cmd.txt, and for the composite example its plan, the server's log and its port."""

    def __init__(self, ladder, session=False):
        self.lad, self.session = ladder, session
        self.rec = "session" if session else ladder.name
        kept = SESSION_DIR if session else CASES_DIR
        self.dir = f"{OUT}/{kept}/{ladder.name}"
        self.report = f"{kept}/{ladder.name}{'.session' if session else ''}.json"
        self.events = []
        self.offline()
        if session:
            self.spans = [(1, 0, 0, 0, 4), (2, 1, 2, 4, 12), (1, 3, 3, 12, 16)]
            self.served = []
            self.online()
        self.assign_ids()

    def event(self, source, type_, variant, ref_index, ref_end, payload, segment=None,
              session=None, wallclock=None):
        lad = self.lad
        self.events.append({
            "id": None, "source": source, "type": type_, "variant": variant, "session": session,
            "segment": segment, "ref_index": ref_index, "ref_end": ref_end, "t": ref_index / FPS,
            "t_end": None if ref_end is None else ref_end / FPS, "seq": lad.seq(ref_index),
            "pts": lad.pts_ticks(ref_index) / 90000, "pts_ticks": lad.pts_ticks(ref_index),
            "wallclock": wallclock, "payload": payload})

    def offline(self):
        lad = self.lad
        for r, rung in enumerate(RUNGS):
            for s in lad.segments:
                f, i = s["first_frame"], s["index"]
                self.event("playlist", "segment", r, f, f + 4, {
                    "line": s["uri_line"], "uri": s["file"],
                    "url": f"{self.dir}/{rung[0]}/{s['file']}", "media_sequence": i, "extinf": 1,
                    "byte_range": None, "discontinuity_sequence": s["discontinuity_sequence"],
                    "pdt": None, "pdt_explicit": False, "playlist_time": i,
                    "bytes": s["bytes"][rung[0]], "gap": False, "map": False}, segment=i)
                if s["discontinuity"]:
                    self.event("playlist", "discontinuity", r, f, None,
                               {"line": s["discontinuity_line"],
                                "discontinuity_sequence": s["discontinuity_sequence"]}, segment=i)
                for text, line in zip(s["tags"], s["tag_lines"]):
                    if text.startswith("#EXT-X-CUE-OUT-CONT:"):
                        self.event("scte35", "cue_out_cont", r, f, None, {
                            "line": line, "form": "CUE-OUT-CONT", "raw": text, "elapsed": 1,
                            "duration": 2, "attributes": {}}, segment=i)
                    elif text.startswith("#EXT-X-CUE-OUT:"):
                        self.event("scte35", "cue_out", r, f, None, {
                            "line": line, "form": "CUE-OUT", "raw": text, "duration": 2,
                            "attributes": {}}, segment=i)
                    else:
                        self.event("scte35", "cue_in", r, f, None, {
                            "line": line, "form": "CUE-IN", "raw": text, "attributes": {}},
                            segment=i)
                self.event("decoder", "keyframe", r, f, None, {"index": f}, segment=i)
            if lad.kind == "ad":
                self.event("scte35", "ad_break", r, 4, 12, {
                    "cue_out_line": lad.segments[1]["tag_lines"][0],
                    "cue_in_line": lad.segments[3]["tag_lines"][0], "end": "cue_in",
                    "durations": {"duration": 2}, "cue_out_cont_durations": [2]}, segment=1)
            if r:
                for k, (first, _, _) in enumerate(lad.sequences[1:], start=1):
                    self.event("vmaf", "context_start", r, first, None,
                               {"context": k, "first_index": first})

    def online(self):
        lad, clock = self.lad, [0.001]

        def request(type_, variant, ref_index, ref_end, path, size, segment=None):
            start = clock[0]
            took = 2.5 if path == "v180/ad_000.ts" else 0.002
            end = start + took
            clock[0] = end + 0.001
            url = f"{BASE}/{path}"
            self.event("network", type_, variant, ref_index, ref_end, {
                "url": url, "range": None, "status": 200, "status_text": "OK", "bytes": size,
                "start": start, "first_byte": end - 0.0005, "end": end, "duration": end - start,
                "failed": False, "error": None, "origin": "fetch",
                "chain": [{"url": url, "start": start, "sent": start + 0.0001,
                           "first_byte": end - 0.0005, "end": end, "status": 200,
                           "status_text": "OK", "bytes": size, "content_length": size,
                           "failed": False, "error": None}]},
                segment=segment, session=0, wallclock=start)
            self.served.append({"path": path, "status": 200, "bytes": size,
                                "delay": DELAY if path == "v180/ad_000.ts" else 0.0,
                                "read": start, "head": end, "end": end})

        def rung(r):
            top, bottom = ranks(r)
            return {"entry": r, "uri": f"{RUNGS[r][0]}/index.m3u8", "bandwidth": RUNGS[r][3],
                    "resolution": f"{RUNGS[r][1]}x{RUNGS[r][2]}", "rank_from_top": top,
                    "rank_from_bottom": bottom}

        request("playlist_request", None, 0, None, "master.m3u8", 300)
        played, prev = set(), None
        for entry, first, last, a, b in self.spans:
            self.event("player", "variant_span", entry, a, b, {
                **rung(entry), "first_seq": first, "last_seq": last, "scored": True}, session=0)
            if prev is not None and prev != entry:
                self.event("player", "variant_switch", entry, a, None, {
                    "from": rung(prev), "to": rung(entry), "origin": "plan"}, session=0)
            if entry not in played:
                played.add(entry)
                request("playlist_request", entry, a, None, f"{RUNGS[entry][0]}/index.m3u8", 250)
            for i in range(first, last + 1):
                s = lad.segments[i]
                request("segment_request", entry, s["first_frame"], s["first_frame"] + 4,
                        f"{RUNGS[entry][0]}/{s['file']}", s["bytes"][RUNGS[entry][0]], segment=i)
            prev = entry

    def assign_ids(self):
        def key(e):
            end = e["ref_end"] if e["ref_end"] is not None else e["ref_index"]
            v = -1 if e["variant"] is None else e["variant"]
            return (e["ref_index"], end, SOURCES.index(e["source"]), e["type"], v)

        self.events.sort(key=key)
        counts = {}
        for e in self.events:
            k = (e["source"], e["type"], e["variant"])
            n = counts.get(k, 0)
            counts[k] = n + 1
            v = "-" if e["variant"] is None else e["variant"]
            e["id"] = f"{e['source']}/{e['type']}/{v}/{n}"

    def curves(self):
        out = [("pair", 1), ("pair", 2)]   # v270 is pair 0, v180 pair 1
        return out + ([("session", 0)] if self.session else [])

    @staticmethod
    def curve_ref(c):
        return {"kind": "pair", "index": c[1] - 1} if c[0] == "pair" else \
            {"kind": "session", "index": 0}

    @staticmethod
    def curve_name(c):
        return f"pair:{c[1] - 1}" if c[0] == "pair" else "session:0"

    def in_scope(self, e, curve, a, b):
        def meets(lo, hi):
            if e["ref_end"] is None:
                return lo <= e["ref_index"] < hi
            return e["ref_index"] < hi and e["ref_end"] > lo
        if curve[0] == "pair":
            return e["session"] is None and e["variant"] in (0, curve[1]) and meets(a, b)
        if e["session"] == 0 or e["variant"] == 0:
            return meets(a, b)
        return any(e["variant"] == entry and meets(max(a, lo), min(b, hi))
                   for entry, _, _, lo, hi in self.spans)

    def attribution(self, curve):
        a, b = self.lad.low_frames
        groups = []
        for source in SOURCES:
            evs = [e for e in self.events
                   if e["source"] == source and self.in_scope(e, curve, a, b)]
            groups.append({"source": source, "events": [{
                "event": e["id"], "offset": (e["ref_index"] - a) / FPS,
                "offset_end": None if e["ref_end"] is None else (e["ref_end"] - a) / FPS,
                "offset_frames": e["ref_index"] - a,
                "offset_end_frames": None if e["ref_end"] is None else e["ref_end"] - a}
                for e in evs]})
        return {"curve": self.curve_ref(curve), "detected": True,
                "interval": {"ref_index": a, "ref_end": b, "t": a / FPS, "t_end": b / FPS,
                             "points": b - a},
                **SCORES, "overlapping_events": groups}

    def dropped(self, curves):
        return [self.attribution(c) for c in curves
                if c == ("pair", self.lad.low) or c[0] == "session"]

    def detection(self, curves):
        return {"threshold": 6, "window": 50, "min_points": 12, "baseline": "median-non-low",
                "warm_up": 25, "context_first": "no-special-case",
                "curves": [self.curve_ref(c) for c in curves],
                "session_rules": SESSION_RULES if any(c[0] == "session" for c in curves) else None}

    def plan(self):
        return {"schema": "mirage-session-plan", "schema_version": 1,
                "name": "the composite example",
                "spans": [{"entry": e, "first_seq": f, "last_seq": l}
                          for e, f, l, _, _ in self.spans]}

    def document(self, plan_sha):
        lad = self.lad
        variants = []
        for r, (n, w, h, b, c) in enumerate(RUNGS):
            top, bottom = ranks(r)
            url = f"{self.dir}/{n}/index.m3u8"
            variants.append({
                "id": r, "role": "reference" if r == 0 else "distorted",
                "pair": None if r == 0 else r - 1, "entries": [r], "url": url, "bandwidth": b,
                "resolution": f"{w}x{h}", "codecs": c, "rank_from_top": top,
                "rank_from_bottom": bottom,
                "playlist": {"url": url, "location": url, "sha256": "ab" * 32},
                "segment_map": "ok"})
        axis = {
            "kind": "reference-frames", "seconds": "playlist-time", "frame_duration": "1/4",
            "frames": 16,
            "reference": {"url": variants[0]["url"], "time_base": "1/90000", "frame_rate": "4/1",
                          "sequences": [{"first_index": f, "first_pts": p / 90000,
                                         "first_pts_ticks": p, "frames": n}
                                        for f, n, p in lad.sequences]},
            "checked": "segment-map", "tick_den": 90000,
            "ticks": [{"first": 0, "frames": 16, "first_tick": 0, "step": f"{TICKS}/1",
                       "phase": 0, "scale": 1}],
            "keys": [{"first": f, "frames": n, "sequence": q, "first_pts_ticks": p,
                      "step": f"{TICKS}/1", "phase": 0, "scale": 1}
                     for f, n, q, p in lad.key_stretches],
            "runs": [], "warnings": []}
        # As mirage_vmaf records the options of run_all.sh's command line (commands()).
        options = {"report": self.report, "out": f"{self.rec}/timeline.json"}
        if self.session:
            options.update({"session": f"{self.rec}/plan.json", "fetch": "on",
                            "fetch-base": BASE})
        options.update({"threshold": 6, "window": 50, "min-points": 12,
                        "margin-before": "0/1", "margin-after": "0/1"})
        curves = self.curves()
        return {
            "schema": "mirage-timeline", "schema_version": 1, "tool": "mirage_vmaf",
            "command": "timeline", "ffmpeg_libraries": {"libavutil": "1.2.3"},
            "options": options, "config_file": None,
            "ladder_report": {"path": self.report, "sha256": "cd" * 32,
                              "scores_sha256": "ef" * 32, "status": "complete",
                              "reference_mode": "top-rung", "model": "vmaf_v0.6.1",
                              "model_source": "built-in", "model_sha256": None,
                              "scaler": "bicubic", "libvmaf_version": "3.0.0",
                              "libvmaf_package_version": "3.1.0",
                              "master": {"url": f"{self.dir}/master.m3u8",
                                         "location": f"{self.dir}/master.m3u8",
                                         "sha256": "12" * 32}},
            "session_plan": {"path": f"{self.rec}/plan.json", "sha256": plan_sha}
            if self.session else None,
            "axis": axis, "variants": variants,
            "sessions": [{"id": "0", "name": "the composite example",
                          "wallclock_anchor_utc": "2026-01-01T00:00:00.000000Z",
                          "spans": [{"variant": e, "entry": e, "first_seq": f, "last_seq": l,
                                     "ref_index": a, "ref_end": b, "scored": True}
                                    for e, f, l, a, b in self.spans]}] if self.session else [],
            "timeline": self.events, "detection": self.detection(curves),
            "overlap": {"rule": "half-open", "margin_before": "0/1", "margin_after": "0/1",
                        "scope": "curve-inputs"},
            "attributions": self.dropped(curves), "warnings": []}

    def answer(self, doc, curve):
        a = copy.deepcopy(doc)
        name = self.curve_name(curve)
        file = f"answer.{name.replace(':', '-')}"
        a.update({"command": "attribute",
                  "options": {"timeline": f"{self.rec}/timeline.json", "interval": None,
                              "frames": None, "drop": None, "auto": True, "report": self.report,
                              "curve": name, "threshold": 6, "window": 50, "min-points": 12,
                              "margin-before": "0/1", "margin-after": "0/1", "format": "json",
                              "out": f"{self.rec}/{file}.json"},
                  "detection": self.detection([curve]), "attributions": self.dropped([curve])})
        return file, a

    def commands(self):
        """The cmd.txt run_all.sh would write for this record: the commands that wrote its
        documents and answers, run from <out>, each with its exit status."""
        lines = ["# The commands of this record, as eval/timeline/run_all.sh ran them from",
                 "# <out>, each with its exit status; <build> is the build directory "
                 "environment.txt", "# names."]
        pinned = ["--threshold", "6", "--window", "50", "--min-points", "12"]

        def ran(*words):
            lines.extend([f"$ {' '.join(words)}", "# exit status 0"])

        built = ["<build>/mirage_vmaf", "timeline", "--report", self.report, "--out",
                 f"{self.rec}/timeline.json"]
        if self.session:
            built += ["--session", f"{self.rec}/plan.json", "--fetch", "on", "--fetch-base",
                      BASE]
        ran(*built, *pinned)
        ran("python3", "-I", "-B", "<repo>/eval/timeline/check.py", "curves",
            f"{self.rec}/timeline.json")
        for c in self.curves():
            name = self.curve_name(c)
            for fmt, ext in (("json", "json"), ("text", "txt")):
                ran("<build>/mirage_vmaf", "attribute", "--timeline", f"{self.rec}/timeline.json",
                    "--report", self.report, "--auto", "--curve", name, *pinned, "--format", fmt,
                    "--out", f"{self.rec}/answer.{name.replace(':', '-')}.{ext}")
        return "\n".join(lines) + "\n"

    def write(self, out):
        rec_dir = os.path.join(out, self.rec)
        os.makedirs(rec_dir)
        dump(os.path.join(rec_dir, "layout.json"), self.lad.layout())
        with open(os.path.join(rec_dir, "cmd.txt"), "w", encoding="utf-8") as f:
            f.write(self.commands())
        plan_sha = None
        if self.session:
            dump(os.path.join(rec_dir, "plan.json"), self.plan())
            with open(os.path.join(rec_dir, "plan.json"), "rb") as f:
                plan_sha = hashlib.sha256(f.read()).hexdigest()
            with open(os.path.join(rec_dir, "server.log"), "w") as f:
                for n, row in enumerate(self.served):
                    f.write(json.dumps({"n": n, **row}) + "\n")
            with open(os.path.join(rec_dir, "server.port"), "w") as f:
                f.write(f"{PORT}\n")
        doc = self.document(plan_sha)
        dump(os.path.join(rec_dir, "timeline.json"), doc)
        for c in self.curves():
            file, ans = self.answer(doc, c)
            dump(os.path.join(rec_dir, f"{file}.json"), ans)


def dump(path, value):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(value, f, indent=1)


def make_out(out):
    """The five records of a run: §3.2's four ladders and the composite example."""
    for kind, ts in (("degraded", None), ("ad", "main-origin"), ("ad", "from-1s"),
                     ("ad", "continuous")):
        FakeRun(FakeLadder(kind, ts)).write(out)
    FakeRun(FakeLadder("ad", "main-origin"), session=True).write(out)


RECORDS = ("degraded", "ad-main-origin", "ad-from-1s", "ad-continuous", "session")


def edit(out, rec, files, change):
    """Applies `change` to the JSON of each of `files` of the record `rec`: "doc" names
    timeline.json, "all" it and every answer, any other name that file."""
    names = {"doc": ["timeline.json"],
             "all": ["timeline.json"] + sorted(n for n in os.listdir(os.path.join(out, rec))
                                               if n.startswith("answer."))}.get(files, [files])
    for n in names:
        p = os.path.join(out, rec, n)
        with open(p, encoding="utf-8") as f:
            d = json.load(f)
        change(d)
        dump(p, d)


def edit_text(out, rec, name, old, new):
    """The first `old` in the record's file `name` replaced by `new`."""
    p = os.path.join(out, rec, name)
    with open(p, encoding="utf-8") as f:
        text = f.read()
    assert old in text, (name, old)
    with open(p, "w", encoding="utf-8") as f:
        f.write(text.replace(old, new, 1))


def event(d, type_, variant, ref_index):
    return next(e for e in d["timeline"] if e["type"] == type_ and e["variant"] == variant
                and e["ref_index"] == ref_index)


def set_late_duration(d):
    for e in d["timeline"]:
        if e["type"] == "segment_request" and e["payload"]["url"].endswith("/v180/ad_000.ts"):
            e["payload"]["duration"] = 1.25


def swap_first(d):
    d["timeline"][0], d["timeline"][3] = d["timeline"][3], d["timeline"][0]


def server_status(out):
    p = os.path.join(out, "session", "server.log")
    with open(p) as f:
        rows = [json.loads(x) for x in f]
    rows[2]["status"] = 404
    with open(p, "w") as f:
        f.writelines(json.dumps(r) + "\n" for r in rows)


def request(d, path):
    """The session's request of the file `path`, from the master's directory."""
    return next(e for e in d["timeline"] if e["source"] == "network"
                and e["payload"]["url"] == f"{BASE}/{path}")


LATE = "v180/ad_000.ts"
OTHER = "v270/main_000.ts"


def rename_session(out):
    """The session's id changed to "7" in the document and its answers, the session's answer
    renamed after it, as run_all.sh would name it."""
    def change(d):
        d["sessions"][0]["id"] = "7"
        if d["command"] == "attribute" and d["options"]["curve"] == "session:0":
            d["options"]["curve"] = "session:7"
            d["options"]["out"] = "session/answer.session-7.json"
    edit(out, "session", "all", change)
    os.rename(os.path.join(out, "session", "answer.session-0.json"),
              os.path.join(out, "session", "answer.session-7.json"))
    for old in ("--curve session:0", "answer.session-0.json"):
        edit_text(out, "session", "cmd.txt", old, old.replace("0", "7"))


# The measured times of one request, each change keeping the others as they agree.

def late_times_say_0_1_s(d):
    # The review's probe: the late request's end and first byte, in its payload and its row,
    # 0.1 s after its start, its duration left as it was.
    p = request(d, LATE)["payload"]
    row = p["chain"][0]
    p["end"] = row["end"] = p["start"] + 0.1
    p["first_byte"] = row["first_byte"] = p["start"] + 0.0995


def start_not_its_row_s(d):
    p = request(d, OTHER)["payload"]
    p["start"] += 0.0001
    p["duration"] = p["end"] - p["start"]


def end_not_its_row_s(d):
    p = request(d, OTHER)["payload"]
    p["end"] += 0.0001
    p["duration"] = p["end"] - p["start"]


def first_byte_not_its_row_s(d):
    request(d, OTHER)["payload"]["first_byte"] += 0.0001


def wallclock_not_its_start(d):
    request(d, OTHER)["wallclock"] += 0.0001


def row_out_of_order(d):
    row = request(d, OTHER)["payload"]["chain"][0]
    row["sent"] = row["first_byte"] + 0.0001


def late_head_before_the_delay(d):
    p = request(d, LATE)["payload"]
    p["first_byte"] = p["chain"][0]["first_byte"] = p["start"] + 1.0


def another_request_longer(d):
    p = request(d, OTHER)["payload"]
    p["end"] = p["chain"][0]["end"] = p["start"] + 2.6
    p["duration"] = p["end"] - p["start"]


def options_in_another_order(d):
    o = d["options"]
    d["options"] = {"out": o["out"], "report": o["report"],
                    **{k: v for k, v in o.items() if k not in ("out", "report")}}


def cue_types_before_the_margins(d):
    o = d["options"]
    d["options"] = {**{k: v for k, v in o.items() if not k.startswith("margin")},
                    "cue-types": "0x22-0x23,0x30-0x37,0x44-0x47",
                    "margin-before": "0/1", "margin-after": "0/1"}


def tick_den_doubled(d):
    # The same seconds, in ticks of 1/180000 s.
    d["axis"]["tick_den"] = 180000
    d["axis"]["ticks"][0]["step"] = f"{2 * TICKS}/1"


def keys_split(d):
    # The same keys, in two stretches where the writer gives one.
    s = d["axis"]["keys"][0]
    d["axis"]["keys"] = [{**s, "frames": 8},
                         {**s, "first": 8, "frames": 8, "first_pts_ticks": ORIGIN + 8 * TICKS}]


def drop_a_frame_later(d, kind):
    for a in d["attributions"]:
        if a["curve"]["kind"] == kind:
            iv = a["interval"]
            iv.update(ref_index=iv["ref_index"] + 1, ref_end=iv["ref_end"] + 1,
                      t=iv["t"] + 1 / FPS, t_end=iv["t_end"] + 1 / FPS)


# One change per kind of field check.py checks: (kind, record, files, change), or (kind, the
# change of the output directory).
CHANGES = [
    ("case", lambda out: shutil.rmtree(os.path.join(out, "ad-continuous"))),
    ("document", "degraded", "all", lambda d: d.update(tool="another_tool")),
    ("answer", "degraded", "answer.pair-0.json", lambda d: d.update(command="timeline")),
    ("axis", "degraded", "all", lambda d: d["axis"].update(frame_duration="1/5")),
    ("variant", "degraded", "all", lambda d: d["variants"][2].update(rank_from_bottom=2)),
    # The reference's last keyframe, which no drop lists, and whose loss leaves the other
    # keyframes' ids as they are.
    ("event", "degraded", "all",
     lambda d: d["timeline"].remove(event(d, "keyframe", 0, 12))),
    ("id", "degraded", "all",
     lambda d: event(d, "keyframe", 0, 0).update(id="decoder/keyframe/0/7")),
    ("frames", "degraded", "all", lambda d: event(d, "segment", 2, 4).update(ref_end=9)),
    ("seconds", "degraded", "all", lambda d: event(d, "keyframe", 0, 0).update(t=0.5)),
    ("key", "degraded", "all", lambda d: event(d, "keyframe", 0, 0).update(pts_ticks=1)),
    ("wallclock", "degraded", "all", lambda d: event(d, "keyframe", 0, 0).update(wallclock=1.0)),
    ("payload", "degraded", "all",
     lambda d: event(d, "segment", 1, 0)["payload"].update(uri="x.ts")),
    ("order", "degraded", "all", swap_first),
    ("warning", "degraded", "all", lambda d: d.update(warnings=["a warning"])),
    ("detection", "degraded", "doc", lambda d: d["detection"].update(window=40)),
    ("overlap", "degraded", "doc", lambda d: d["overlap"].update(margin_before="1/1")),
    ("options", "degraded", "doc", lambda d: d["options"].update(threshold=5)),
    ("drop", "degraded", "doc", lambda d: d.update(attributions=[])),
    ("score", "degraded", "doc", lambda d: d["attributions"][0].update(drop_magnitude=1.5)),
    ("listing", "degraded", "answer.pair-0.json",
     lambda d: d["attributions"][0]["overlapping_events"][2]["events"].pop()),
    ("session", "session", "all", lambda d: d["sessions"][0]["spans"][1].update(ref_end=11)),
    ("network", server_status),
    ("duration", "session", "all", set_late_duration),
]

# More changes, each of a field that one of the kinds above covers: the members of payloads
# and of the document's objects, every one and no other; the options records and the command
# lines, the paths given and the axis's form; an event's segment and a drop's frames; and the
# session's measured fields, whose values are left out but not their kind, nor how they agree.
MORE_CHANGES = [
    ("payload", "degraded", "all", lambda d: event(d, "segment", 1, 0)["payload"].update(gap=True)),
    ("payload", "degraded", "all", lambda d: event(d, "segment", 1, 0)["payload"].update(extra=1)),
    ("payload", "degraded", "all", lambda d: event(d, "segment", 1, 0)["payload"].pop("map")),
    ("payload", "ad-main-origin", "all",
     lambda d: event(d, "cue_out", 2, 4)["payload"].update(attributes={"X": "1"})),
    ("payload", "ad-main-origin", "all",
     lambda d: event(d, "ad_break", 1, 4)["payload"]["durations"].update(planned_duration=2)),
    ("payload", "degraded", "all",
     lambda d: event(d, "segment", 1, 8)["payload"].update(media_sequence=3)),
    ("event", "degraded", "all", lambda d: event(d, "segment", 1, 8).update(segment=3)),
    ("document", "degraded", "all", lambda d: d.update(extra=1)),
    ("document", "degraded", "all", lambda d: event(d, "keyframe", 0, 0).update(extra=1)),
    ("document", "degraded", "all",
     lambda d: d["ladder_report"]["master"].update(url="/fake/other.m3u8")),
    ("document", "degraded", "all", lambda d: d["ladder_report"].update(sha256="x")),
    ("document", "degraded", "doc",
     lambda d: d["ladder_report"].update(path=f"{CASES_DIR}/other.json")),
    ("variant", "degraded", "all",
     lambda d: d["variants"][1]["playlist"].update(location="/fake/other.m3u8")),
    ("axis", "degraded", "all", lambda d: d["axis"]["ticks"][0].update(extra=1)),
    ("axis", "degraded", "all", tick_den_doubled),
    ("axis", "degraded", "all", keys_split),
    ("detection", "degraded", "doc", lambda d: d["detection"].update(extra=1)),
    ("drop", "degraded", "doc", lambda d: d["attributions"][0]["interval"].update(extra=1)),
    ("drop", "degraded", "all", lambda d: drop_a_frame_later(d, "pair")),
    ("drop", "session", "all", lambda d: drop_a_frame_later(d, "session")),
    ("listing", "degraded", "doc",
     lambda d: d["attributions"][0]["overlapping_events"][2]["events"][0].update(extra=1)),
    ("answer", "degraded", "answer.pair-0.json",
     lambda d: d["ladder_report"].update(model="another_model")),
    ("answer", "degraded", "answer.pair-0.json",
     lambda d: d["ladder_report"].update(sha256="ab" * 32)),
    ("answer", "degraded", "answer.pair-0.json",
     lambda d: d["ladder_report"].update(path="degraded.json")),
    ("options", "degraded", "doc", lambda d: d["options"].update(out="wrong.json")),
    ("options", "degraded", "doc", lambda d: d["options"].update({"margin-before": "1/1"})),
    ("options", "degraded", "doc", lambda d: d["options"].update(x=1)),
    ("options", "degraded", "doc", options_in_another_order),
    ("options", "degraded", "doc", cue_types_before_the_margins),
    ("options", "degraded", "answer.pair-0.json",
     lambda d: d["options"].update(interval=["0/1", "1/1"])),
    ("options", "degraded", "answer.pair-1.json", lambda d: d["options"].pop("drop")),
    ("options", "session", "doc", lambda d: d["options"].update({"fetch-base": f"{BASE}1"})),
    ("options", lambda out: edit_text(out, "degraded", "cmd.txt", "--threshold 6",
                                      "--threshold 5")),
    ("options", lambda out: edit_text(out, "degraded", "cmd.txt", "--curve pair:1",
                                      "--curve pair:0")),
    ("options", lambda out: os.remove(os.path.join(out, "degraded", "cmd.txt"))),
    ("session", rename_session),
    ("session", "session", "all", lambda d: d["sessions"][0].update(extra=1)),
    ("session", "session", "all", lambda d: d["session_plan"].update(path="plan.json")),
    ("wallclock", "session", "all",
     lambda d: d["sessions"][0].update(wallclock_anchor_utc="2026-01-01 00:00:00")),
    ("wallclock", "session", "all", lambda d: request(d, "master.m3u8").update(wallclock=None)),
    ("wallclock", "session", "all", wallclock_not_its_start),
    ("network", "session", "all",
     lambda d: request(d, "master.m3u8")["payload"].update(start=None)),
    ("network", "session", "all",
     lambda d: request(d, OTHER)["payload"]["chain"][0].update(sent=-1)),
    ("network", "session", "all", lambda d: request(d, OTHER)["payload"].update(bytes=1)),
    ("network", "session", "all", start_not_its_row_s),
    ("network", "session", "all", end_not_its_row_s),
    ("network", "session", "all", first_byte_not_its_row_s),
    ("network", "session", "all", row_out_of_order),
    ("duration", "session", "all", lambda d: request(d, OTHER)["payload"].update(duration=-0.5)),
    ("duration", "session", "all", late_times_say_0_1_s),
    ("duration", "session", "all", late_head_before_the_delay),
    ("duration", "session", "all", another_request_longer),
]


def remeasure(out):
    """Every measured value of the records changed, as another run could measure it: the
    session's anchor; each request's times on a clock that starts later and runs slower, 7.5 s
    plus 1.5 times the time, its duration its end less its start and its wall clock its start,
    so that the held file still took the longest, more than the delay; the server's times; and
    every drop's scores, changed alike in each document and answer, by D-077's rules."""
    def clock(t):
        return 7.5 + 1.5 * t

    def change(d):
        for s in d["sessions"]:
            s["wallclock_anchor_utc"] = "2031-02-03T04:05:06.123456Z"
        for e in d["timeline"]:
            if e["source"] != "network":
                continue
            p = e["payload"]
            for times in [p] + p["chain"]:
                for k in ("start", "sent", "first_byte", "end"):
                    if k in times:
                        times[k] = clock(times[k])
            p["duration"] = p["end"] - p["start"]
            e["wallclock"] = p["chain"][0]["start"]
        for a in d["attributions"]:
            a.update(baseline=95.5, min=10.25, mean=40.5, drop_magnitude=85.25, mean_drop=55.0)

    for rec in RECORDS:
        edit(out, rec, "all", change)
    p = os.path.join(out, "session", "server.log")
    with open(p) as f:
        rows = [json.loads(x) for x in f]
    for r in rows:
        r.update({k: clock(r[k]) for k in ("read", "head", "end")})
    with open(p, "w") as f:
        f.writelines(json.dumps(r) + "\n" for r in rows)


def run_check(out, *extra):
    """check.py check on `out`: (exit status, stdout, its JSON)."""
    stdout = io.StringIO()
    json_path = os.path.join(os.path.dirname(out), "check.json")
    with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(io.StringIO()):
        status = check.main(["check", out, *PARAMS, "--json", json_path, *extra])
    result = None
    if os.path.exists(json_path):
        with open(json_path) as f:
            result = json.load(f)
        os.remove(json_path)
    return status, stdout.getvalue(), result


class Check(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.pristine = os.path.join(self.tmp.name, "pristine")
        make_out(self.pristine)

    def copy(self, name):
        out = os.path.join(self.tmp.name, name)
        shutil.copytree(self.pristine, out)
        return out

    def test_matching_records_pass(self):
        status, text, result = run_check(self.copy("match"))
        self.assertEqual(status, 0, text)
        self.assertEqual(result["differences"], [])
        self.assertEqual(text.splitlines()[-1],
                         "check.py: PASS: 5 records, 16 documents, every answer as the layouts "
                         "give it")

    def changed(self, n, change):
        """check.py check on a copy of the records with `change` made: (kind, status, stdout,
        JSON)."""
        out = self.copy(f"change{n}")
        if len(change) == 2:
            change[1](out)
        else:
            edit(out, change[1], change[2], change[3])
        return (change[0], *run_check(out))

    def test_each_kind_of_field(self):
        self.assertEqual(sorted(c[0] for c in CHANGES), sorted(check.KINDS))
        for n, change in enumerate(CHANGES):
            with self.subTest(kind=change[0]):
                kind, status, text, result = self.changed(n, change)
                self.assertEqual(status, 1, text)
                self.assertEqual(set(result["kinds"]), {kind}, text)
                last = text.splitlines()[-1]
                self.assertTrue(last.startswith("check.py: FAIL: "), last)
                self.assertIn(f"[{kind}] ", text)

    def test_more_fields(self):
        for n, change in enumerate(MORE_CHANGES):
            with self.subTest(n=n, kind=change[0]):
                kind, status, text, result = self.changed(100 + n, change)
                self.assertEqual(status, 1, text)
                self.assertEqual(set(result["kinds"]), {kind}, text)

    def test_measured_values_changed_alike_pass(self):
        out = self.copy("remeasured")
        remeasure(out)
        status, text, result = run_check(out)
        self.assertEqual(status, 0, text)
        self.assertEqual(result["differences"], [])

    def test_the_options_q75_asks_about_pass_after_the_margins(self):
        # However Q75 to Q77 fall: either member, or both in their order, with any value.
        for n, members in enumerate(({"cue-types": "0x22-0x23,0x30-0x37,0x44-0x47"},
                                     {"bad-crc": "keep"},
                                     {"cue-types": "0x30-0x37", "bad-crc": "drop"})):
            with self.subTest(members=members):
                out = self.copy(f"q75-{n}")
                for rec in RECORDS:
                    edit(out, rec, "doc", lambda d: d["options"].update(members))
                status, text, result = run_check(out)
                self.assertEqual(status, 0, text)

    def test_records_whose_paths_were_replaced_pass(self):
        # As check.py redact leaves them: every path under <out> replaced but its last parts.
        out = self.copy("replaced")
        for dirpath, _, names in os.walk(out):
            for name in names:
                p = os.path.join(dirpath, name)
                with open(p, encoding="utf-8") as f:
                    text = f.read()
                with open(p, "w", encoding="utf-8") as f:
                    f.write(text.replace(OUT, "<out>"))
        status, text, result = run_check(out)
        self.assertEqual(status, 0, text)
        self.assertEqual(result["differences"], [])

    def test_a_held_request_shorter_than_its_delay_is_named(self):
        # The late request as the fetch would time it had the server held it 1.2 s: its first
        # byte then, its end 0.05 s later, its row's the same.
        out = self.copy("short")

        def change(d):
            p = request(d, LATE)["payload"]
            row = p["chain"][0]
            p["first_byte"] = row["first_byte"] = p["start"] + 1.2
            p["end"] = row["end"] = p["start"] + 1.25
            p["duration"] = 1.25

        edit(out, "session", "all", change)
        status, text, result = run_check(out)
        self.assertEqual(status, 1)
        self.assertEqual(set(result["kinds"]), {"duration"}, text)
        found = result["differences"]
        self.assertEqual(len(found), 2, text)
        self.assertTrue(all(x["where"].startswith("session/timeline.json: ") for x in found),
                        text)
        self.assertEqual(sorted(x["text"] for x in found),
                         ["1.2 s after its start, less than the 2.3 s the server held its "
                          "response head",
                          "1.25 s, less than the 2.3 s the server held the response head"])

    def test_a_longer_delay_than_the_server_held_is_a_difference(self):
        status, text, result = run_check(self.copy("longer"), "--delay", "3")
        self.assertEqual(status, 1)
        self.assertEqual(set(result["kinds"]), {"duration", "network"}, text)

    def test_a_server_port_other_than_the_fetch_s(self):
        # Every request's URL and the command line's --fetch-base then name another server.
        out = self.copy("port")
        with open(os.path.join(out, "session", "server.port"), "w") as f:
            f.write(f"{PORT + 1}\n")
        status, text, result = run_check(out)
        self.assertEqual(status, 1)
        self.assertEqual(set(result["kinds"]), {"network", "options"}, text)
        self.assertIn(f"--fetch-base \"{BASE}\", expected \"http://127.0.0.1:{PORT + 1}\"",
                      text)

    def test_a_layout_it_does_not_know(self):
        out = self.copy("unknown")
        p = os.path.join(out, "degraded", "layout.json")
        with open(p) as f:
            layout = json.load(f)
        layout["segments"][1]["tags"] = ["#EXT-X-SCTE35:CUE=abc"]
        layout["segments"][1]["tag_lines"] = [8]
        dump(p, layout)
        status, text, result = run_check(out)
        self.assertEqual(status, 1)
        self.assertIn("case", result["kinds"])
        self.assertIn("a tag this check doesn't know", text)

    def test_no_record_is_exit_status_2(self):
        empty = os.path.join(self.tmp.name, "empty")
        os.makedirs(empty)
        status, _, result = run_check(empty)
        self.assertEqual(status, 2)
        self.assertIsNone(result)

    def test_curves_and_late(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(check.main(["curves", os.path.join(self.pristine, "session",
                                                                 "timeline.json")]), 0)
            self.assertEqual(check.main(["late", os.path.join(self.pristine, "session",
                                                               "layout.json")]), 0)
        self.assertEqual(out.getvalue().split(), ["pair:0", "pair:1", "session:0",
                                                  "v180/ad_000.ts"])
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(check.main(["late", os.path.join(self.pristine, "degraded",
                                                               "layout.json")]), 2)
            self.assertEqual(check.main(["curves", os.path.join(self.pristine, "none.json")]), 2)


class Redact(unittest.TestCase):
    """check.py redact on a directory this test fills, with the paths of this machine that the
    test reads at run time, so that none is written in this file."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.out = os.path.join(self.tmp.name, "out")
        self.build = os.path.join(self.tmp.name, "build")
        self.repo = os.path.join(self.tmp.name, "repo")
        for d in (self.out, self.build, self.repo, os.path.join(self.out, "media", "v1")):
            os.makedirs(d, exist_ok=True)

    def write(self, rel, data):
        p = os.path.join(self.out, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(data if isinstance(data, bytes) else data.encode())
        return p

    def read(self, rel):
        with open(os.path.join(self.out, rel), "rb") as f:
            return f.read()

    def redact(self, *args, log="redact.txt"):
        """check.py redact, as run_all.sh runs it, its stdout into `log` in <out>."""
        with open(os.path.join(self.out, log), "wb") as f:
            r = subprocess.run([sys.executable, "-I", "-B", os.path.join(TIMELINE, "check.py"),
                                "redact", *(args or (self.out, "--build", self.build, "--repo",
                                                     self.repo))],
                               stdout=f, stderr=subprocess.PIPE, timeout=60)
        return r.returncode, self.read(log).decode()

    def test_each_directory_by_its_placeholder(self):
        home = os.path.expanduser("~")
        doc = {"url": f"{self.out}/media/v1/index.m3u8", "program": f"{self.build}/mirage_vmaf",
               "source": f"{self.repo}/tests/x.cpp:12", "real": f"{os.path.realpath(self.out)}/a",
               "home": f"{home}/a.json"}
        self.write("rec/timeline.json", json.dumps(doc))
        segment = b"\x47" + self.out.encode() + b"\x00"
        self.write("media/v1/seg_000.ts", segment)
        status, text = self.redact()
        self.assertEqual(status, 0, text)
        self.assertEqual(json.loads(self.read("rec/timeline.json")),
                         {"url": "<out>/media/v1/index.m3u8", "program": "<build>/mirage_vmaf",
                          "source": "<repo>/tests/x.cpp:12", "real": "<out>/a",
                          "home": "<home>/a.json"})
        self.assertEqual(self.read("media/v1/seg_000.ts"), segment)   # media segments as made
        lines = text.splitlines()
        self.assertIn("check.py redact: rec/timeline.json: 5 replacements", lines)
        self.assertTrue(lines[-1].startswith("check.py redact: 5 replacements in 1 file of "),
                        text)
        self.assertTrue(lines[-1].endswith("; nothing local left"), text)

    def test_what_is_left_is_exit_status_3(self):
        try:
            user = pwd.getpwuid(os.getuid()).pw_name
        except KeyError:
            user = ""
        host = socket.gethostname().split(".")[0]
        name, what = (user, "the user name") if len(user) >= 3 else (host, "the host name")
        if len(name) < 3:
            self.skipTest("neither the user name nor the host name has 3 characters")
        self.write("rec/log.txt", f"written by {name}\n")
        status, text = self.redact()
        self.assertEqual(status, 3, text)
        self.assertIn(f"check.py redact: rec/log.txt still holds {what}", text.splitlines())
        self.assertTrue(text.splitlines()[-1].endswith(
            "1 file still holds local information: don't commit them"), text)

    def test_the_search_alone_replaces_nothing_and_reads_the_first_pass_s_log(self):
        # As run_all.sh runs it last: the first pass's log, redact.txt, which that pass leaves
        # alone, is searched too, and nothing is replaced.
        self.write("rec/timeline.json", json.dumps({"url": f"{self.out}/media/v1/index.m3u8"}))
        self.write("redact.txt", f"a traceback through {self.repo}/eval/timeline/check.py\n")
        args = (self.out, "--search-only", "--build", self.build, "--repo", self.repo)
        status, text = self.redact(*args, log="search.txt")
        self.assertEqual(status, 3, text)
        lines = text.splitlines()
        self.assertIn("check.py redact --search-only: redact.txt still holds a local path", lines)
        self.assertIn("check.py redact --search-only: rec/timeline.json still holds a local path",
                      lines)
        self.assertTrue(lines[-1].startswith("check.py redact --search-only: 2 files of <out> "
                                             "searched; 2 files still hold local information"),
                        text)
        self.assertEqual(json.loads(self.read("rec/timeline.json")),
                         {"url": f"{self.out}/media/v1/index.m3u8"})
        # Once replaced, nothing is left for it to find.
        os.remove(os.path.join(self.out, "redact.txt"))
        self.assertEqual(self.redact()[0], 0)
        status, text = self.redact(*args, log="search.txt")
        self.assertEqual(status, 0, text)
        self.assertTrue(text.splitlines()[-1].endswith("searched; nothing local left"), text)

    def test_bad_arguments(self):
        for args in ((os.path.join(self.tmp.name, "none"),),
                     (self.out, "--build", os.path.join(self.tmp.name, "none"))):
            with self.subTest(args=args):
                status, _ = self.redact(*args)
                self.assertEqual(status, 2)


class SlowServer(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = os.path.join(self.tmp.name, "root")
        os.makedirs(os.path.join(self.root, "v1"))
        with open(os.path.join(self.root, "v1", "a.ts"), "wb") as f:
            f.write(b"x" * 1000)
        with open(os.path.join(self.root, "b.m3u8"), "wb") as f:
            f.write(b"#EXTM3U\n")
        self.port_file = os.path.join(self.tmp.name, "port")
        self.log = os.path.join(self.tmp.name, "log")

    def start(self, *delays):
        p = subprocess.Popen([sys.executable, "-I", "-B", os.path.join(TIMELINE, "slow_server.py"),
                              self.root, self.port_file, self.log, *delays],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.addCleanup(lambda: p.poll() is None and (p.kill(), p.wait()))
        for _ in range(200):
            if os.path.exists(self.port_file) or p.poll() is not None:
                break
            time.sleep(0.02)
        with open(self.port_file) as f:
            return p, int(f.read())

    def test_holds_the_head_of_a_path_and_logs_each_request(self):
        p, port = self.start("--delay", "v1/a.ts=0.3")
        t0 = time.monotonic()
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/v1/a.ts") as r:
            body, status = r.read(), r.status
        held = time.monotonic() - t0
        self.assertEqual((status, body), (200, b"x" * 1000))
        self.assertGreaterEqual(held, 0.3)
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/b.m3u8") as r:
            self.assertEqual(r.read(), b"#EXTM3U\n")
        for missing in ("v1", "v1/../../port", "c.ts"):
            with self.assertRaises(urllib.error.HTTPError) as e:
                urllib.request.urlopen(f"http://127.0.0.1:{port}/{missing}")
            e.exception.close()
            self.assertEqual(e.exception.code, 404)
        p.terminate()
        self.assertEqual(p.wait(timeout=10), 0)
        with open(self.log) as f:
            rows = [json.loads(x) for x in f]
        self.assertEqual([(r["n"], r["path"], r["status"], r["bytes"], r["delay"]) for r in rows],
                         [(0, "v1/a.ts", 200, 1000, 0.3), (1, "b.m3u8", 200, 8, 0.0),
                          (2, "v1", 404, 0, 0.0), (3, "v1/../../port", 404, 0, 0.0),
                          (4, "c.ts", 404, 0, 0.0)])
        self.assertGreaterEqual(rows[0]["head"] - rows[0]["read"], 0.3)

    def test_bad_arguments(self):
        script = os.path.join(TIMELINE, "slow_server.py")
        for delay in ("v1/a.ts=-1", "/v1/a.ts=1", "v1/a.ts", "v1/a.ts=x"):
            with self.subTest(delay=delay):
                r = subprocess.run([sys.executable, "-I", "-B", script, self.root, self.port_file,
                                    self.log, "--delay", delay], capture_output=True, timeout=30)
                self.assertEqual(r.returncode, 2)
                self.assertFalse(os.path.exists(self.port_file))


class RunAllArguments(unittest.TestCase):
    """run_all.sh's refusals, which need no build."""

    def run_all(self, *args):
        return subprocess.run(["sh", os.path.join(TIMELINE, "run_all.sh"), *args],
                              capture_output=True, text=True, timeout=60)

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as tmp:
            not_empty = os.path.join(tmp, "out")
            os.makedirs(not_empty)
            open(os.path.join(not_empty, "x"), "w").close()
            no_programs = os.path.join(tmp, "build")
            os.makedirs(no_programs)
            for args in ((), (tmp,), (os.path.join(tmp, "none"), os.path.join(tmp, "o1")),
                         (no_programs, os.path.join(tmp, "o2"))):
                with self.subTest(args=args):
                    r = self.run_all(*args)
                    self.assertEqual(r.returncode, 2, r.stderr)
                    self.assertFalse(os.path.exists(os.path.join(tmp, "o1")))
            for prog in ("mirage_vmaf", "mirage_tests"):
                p = os.path.join(no_programs, prog)
                with open(p, "w") as f:
                    f.write("#!/bin/sh\nexit 0\n")
                os.chmod(p, 0o755)
            r = self.run_all(no_programs, not_empty)
            self.assertEqual(r.returncode, 2, r.stderr)
            self.assertIn("isn't empty", r.stderr)


if __name__ == "__main__":
    unittest.main()
