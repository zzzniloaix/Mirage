"""After M5 of docs/PHASE2_SURVEY.md, its gaps: what mirage_vmaf's pairing does when an input's
playlist lacks segments and has no EXT-X-DISCONTINUITY tag, as when FFmpeg's hls demuxer skips
segments it can't read.

usage: python3 -I -B eval/phase2-survey/m5_gaps.py <work_dir> <mirage_vmaf> [<mirage_vmaf> ...]
                                                   [--ffmpeg FFMPEG] [--ffprobe FFPROBE]

Makes, in <work_dir> (new or empty), 36 s of FFmpeg's testsrc2 pattern at 30000/1001 fps, a
reference at 640x360 and a distorted input at 320x180, MPEG-2 in MPEG-TS with every frame a key
frame, cut by FFmpeg's hls muxer into segments of about 2 s; then media playlists that leave
out six segments in a row, about 12 s, more than the 10 s jump D-028's rule takes for a
discontinuity, without a tag:

  ref_gap.m3u8   the reference without its segments 4 to 9
  dis_gap.m3u8   the distorted input without its segments 10 to 15
  dis_same.m3u8  the distorted input without its segments 4 to 9

The cases, each scored by every <mirage_vmaf> given:

  one    ref_gap.m3u8 against the distorted input's whole playlist
  both   ref_gap.m3u8 against dis_gap.m3u8: each input lacks other segments
  same   ref_gap.m3u8 against dis_same.m3u8: both lack the same segments

For each, mirage_vmaf's alignment counts and each input's start as its report gives them, and
its pairs whose two frames' times (the report's ref_pts and dis_pts) differ by more than half a
frame: how many, the first and the last, and the mean score of those pairs and of the others. Writes
the record to stdout: each command, then its output, with <work_dir>, <mirage_vmaf_N>,
<ffmpeg> and <ffprobe> in place of paths.
"""

import argparse
import json
import os
import shlex
import subprocess
import sys

RATE = "30000/1001"
SIZES = {"ref": (640, 360), "dis": (320, 180)}
GAPS = {"ref_gap": ("ref", range(4, 10)), "dis_gap": ("dis", range(10, 16)),
        "dis_same": ("dis", range(4, 10))}
CASES = {"one": ("ref_gap", "dis"), "both": ("ref_gap", "dis_gap"), "same": ("ref_gap", "dis_same")}


def args_of(argv):
    p = argparse.ArgumentParser(prog="m5_gaps.py")
    p.add_argument("work_dir")
    p.add_argument("mirage_vmaf", nargs="+")
    p.add_argument("--ffmpeg", default="/opt/homebrew/opt/ffmpeg-full/bin/ffmpeg")
    p.add_argument("--ffprobe")
    return p.parse_args(argv)


class Record:
    def __init__(self, a):
        self.a = a
        self.ffprobe = a.ffprobe or os.path.join(os.path.dirname(a.ffmpeg), "ffprobe")
        self.names = {a.work_dir: "<work_dir>", a.ffmpeg: "<ffmpeg>", self.ffprobe: "<ffprobe>"}
        for i, m in enumerate(a.mirage_vmaf, 1):
            self.names[m] = f"<mirage_vmaf_{i}>"
        self.lines = []

    def shown(self, text):
        for path, name in sorted(self.names.items(), key=lambda x: -len(x[0])):
            text = text.replace(path, name)
        return text

    def run(self, argv, quiet=False):
        p = subprocess.run(argv, stdin=subprocess.DEVNULL, capture_output=True, text=True)
        self.lines.append("$ " + self.shown(" ".join(shlex.quote(x) for x in argv)))
        if not quiet:
            out = (p.stdout + p.stderr).rstrip("\n")
            if out:
                self.lines.append(self.shown(out))
        self.lines.append(f"(exit status {p.returncode})")
        self.lines.append("")
        return p


def media_playlist(path):
    """The playlist's header lines and its (EXTINF line, segment file) entries."""
    with open(path) as f:
        lines = f.read().splitlines()
    head, entries, i = [], [], 0
    while i < len(lines):
        if lines[i].startswith("#EXTINF:"):
            entries.append((lines[i], lines[i + 1]))
            i += 2
        elif lines[i] == "#EXT-X-ENDLIST":
            i += 1
        else:
            head.append(lines[i])
            i += 1
    return head, entries


def pairs_summary(rep):
    al = rep.get("alignment") or {}
    keep = {k: al.get(k) for k in ("paired", "failed", "failure_rate", "exceeded",
                                   "unmatched_reference", "extra_distorted",
                                   "after_discontinuity", "discontinuities", "vmaf_contexts")}
    keep["start"] = {k: (al.get(k) or {}).get("start") for k in ("reference", "distorted")}
    frames = rep.get("frames") or []
    # Both rungs come from the same encode settings, so a pair of the same picture has the same
    # time on both sides (the report's ref_pts and dis_pts, in seconds); the tolerance is half a
    # frame.
    half = 0.5 * 1001 / 30000
    off = [f for f in frames if abs(f["ref_pts"] - f["dis_pts"]) > half]
    on = [f for f in frames if abs(f["ref_pts"] - f["dis_pts"]) <= half]
    keep["frames_scored"] = rep.get("frames_scored")
    keep["pairs_whose_pts_differ"] = len(off)
    if off:
        keep["first_of_them"] = off[0]
        keep["last_of_them"] = off[-1]
        keep["their_mean_vmaf"] = round(sum(f["vmaf"] for f in off) / len(off), 6)
    if on:
        keep["mean_vmaf_of_the_others"] = round(sum(f["vmaf"] for f in on) / len(on), 6)
    return keep


def main(argv=None):
    a = args_of(argv)
    a.work_dir = os.path.abspath(a.work_dir)
    a.mirage_vmaf = [os.path.abspath(m) for m in a.mirage_vmaf]
    if os.path.exists(a.work_dir) and os.listdir(a.work_dir):
        print(f"m5_gaps: {a.work_dir} is not empty", file=sys.stderr)
        return 2
    os.makedirs(a.work_dir, exist_ok=True)
    r = Record(a)
    r.lines += ["# After M5 of docs/PHASE2_SURVEY.md, its gaps: an input's playlist without some "
                "of its segments and without a tag, as when FFmpeg's hls demuxer skips segments "
                "it can't read. Made by eval/phase2-survey/m5_gaps.py; each command, then its "
                "output.",
                ""]
    for m in a.mirage_vmaf:
        r.run(["shasum", "-a", "256", m])
    r.run([a.ffmpeg, "-hide_banner", "-version"])
    for rung in ("ref", "dis"):
        w, h = SIZES[rung]
        r.run([a.ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-f", "lavfi",
               "-i", f"testsrc2=size={w}x{h}:rate={RATE}", "-t", "36",
               "-vf", "format=yuv420p,setparams=colorspace=bt709:range=tv",
               "-c:v", "mpeg2video", "-q:v", "2", "-g", "1", "-bf", "0",
               "-f", "hls", "-hls_time", "2", "-hls_list_size", "0", "-hls_playlist_type", "vod",
               "-hls_segment_filename", os.path.join(a.work_dir, f"{rung}_%d.ts"),
               os.path.join(a.work_dir, f"{rung}.m3u8")])
        with open(os.path.join(a.work_dir, f"{rung}.m3u8")) as f:
            r.lines += [f"{rung}.m3u8:", f.read().rstrip("\n"), ""]
    for name, (rung, left_out) in GAPS.items():
        head, entries = media_playlist(os.path.join(a.work_dir, f"{rung}.m3u8"))
        kept = [e for k, e in enumerate(entries) if k not in left_out]
        text = "\n".join(head + [x for e in kept for x in e] + ["#EXT-X-ENDLIST"])
        with open(os.path.join(a.work_dir, f"{name}.m3u8"), "w") as f:
            f.write(text + "\n")
        r.lines += [f"{name}.m3u8:", text, ""]
    for case, (ref, dis) in CASES.items():
        r.lines += [f"## {case}: {ref}.m3u8 against {dis}.m3u8", ""]
        for i, m in enumerate(a.mirage_vmaf, 1):
            out = os.path.join(a.work_dir, f"{case}_{i}.json")
            r.run([m, "--ref", os.path.join(a.work_dir, f"{ref}.m3u8"),
                   "--dis", os.path.join(a.work_dir, f"{dis}.m3u8"), "--out", out])
            if os.path.exists(out):
                with open(out) as f:
                    rep = json.load(f)
                r.lines += [f"<mirage_vmaf_{i}>'s report: the alignment, and the pairs whose "
                            "frames' timestamps differ:", json.dumps(pairs_summary(rep), indent=1), ""]
    sys.stdout.write("\n".join(r.lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
