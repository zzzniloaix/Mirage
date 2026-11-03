"""M5 of docs/PHASE2_SURVEY.md: what FFmpeg's hls demuxer gives the analyzer at an
EXT-X-DISCONTINUITY, and what mirage_vmaf's pairing (D-028) does with it.

usage: python3 -I -B eval/phase2-survey/m5_discontinuities.py <work_dir> <mirage_vmaf>
                                                              [--ffmpeg FFMPEG] [--ffprobe FFPROBE]

Makes, in <work_dir> (new or empty), HLS media playlists of two rungs, a reference at
1920x1080 and a distorted input at 640x360, each two periods of 30 frames at 30000/1001 fps,
MPEG-2 in MPEG-TS with every frame a key frame: period A from FFmpeg's testsrc2 pattern,
period B from its testsrc pattern, as content after an ad boundary would differ. The cases:

  restart    an EXT-X-DISCONTINUITY before B, whose timestamps start again where A's did
  jump       an EXT-X-DISCONTINUITY before B, whose timestamps start 60 s after A's
  continuous an EXT-X-DISCONTINUITY before B, whose timestamps go on from A's end
  segments   no discontinuity: period A only, cut by FFmpeg's hls muxer into segments of
             about 0.5 s for the reference and into one for the distorted input

For each case it records each frame's timestamp as ffprobe reads the playlists through FFmpeg's
hls demuxer, and mirage_vmaf's report of the pair: its alignment (pairs, failures,
discontinuities, libvmaf contexts) and pooled mean. Writes the record to stdout: each command,
then its output, with <work_dir>, <mirage_vmaf>, <ffmpeg> and <ffprobe> in place of paths.
"""

import argparse
import json
import os
import shlex
import subprocess
import sys

RATE = "30000/1001"
FRAME = 1001 / 30000   # seconds
SIZES = {"ref": (1920, 1080), "dis": (640, 360)}
PATTERNS = {"A": "testsrc2", "B": "testsrc"}


def args_of(argv):
    p = argparse.ArgumentParser(prog="m5_discontinuities.py")
    p.add_argument("work_dir")
    p.add_argument("mirage_vmaf")
    p.add_argument("--ffmpeg", default="/opt/homebrew/opt/ffmpeg-full/bin/ffmpeg")
    p.add_argument("--ffprobe")
    return p.parse_args(argv)


class Record:
    def __init__(self, a):
        self.a = a
        self.ffprobe = a.ffprobe or os.path.join(os.path.dirname(a.ffmpeg), "ffprobe")
        self.names = {a.work_dir: "<work_dir>", a.mirage_vmaf: "<mirage_vmaf>",
                      a.ffmpeg: "<ffmpeg>", self.ffprobe: "<ffprobe>"}
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


def encode(r, path, rung, period, frames, offset):
    w, h = SIZES[rung]
    argv = [r.a.ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-f", "lavfi",
            "-i", f"{PATTERNS[period]}=size={w}x{h}:rate={RATE}", "-frames:v", str(frames),
            "-vf", "format=yuv420p,setparams=colorspace=bt709:range=tv",
            "-c:v", "mpeg2video", "-q:v", "2", "-g", "1", "-bf", "0"]
    if offset:
        argv += ["-output_ts_offset", f"{offset:.6f}"]
    r.run(argv + ["-f", "mpegts", path])


def playlist(path, segments):
    """segments: [(file, seconds, discontinuity_before)]"""
    lines = ["#EXTM3U", "#EXT-X-VERSION:3", "#EXT-X-TARGETDURATION:2", "#EXT-X-MEDIA-SEQUENCE:0",
             "#EXT-X-PLAYLIST-TYPE:VOD"]
    for name, secs, disc in segments:
        if disc:
            lines.append("#EXT-X-DISCONTINUITY")
        lines += [f"#EXTINF:{secs:.6f},", name]
    lines.append("#EXT-X-ENDLIST")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return "\n".join(lines)


def main(argv=None):
    a = args_of(argv)
    a.work_dir = os.path.abspath(a.work_dir)
    a.mirage_vmaf = os.path.abspath(a.mirage_vmaf)
    if os.path.exists(a.work_dir) and os.listdir(a.work_dir):
        print(f"m5_discontinuities: {a.work_dir} is not empty", file=sys.stderr)
        return 2
    os.makedirs(a.work_dir, exist_ok=True)
    r = Record(a)
    r.lines += ["# M5 of docs/PHASE2_SURVEY.md: what FFmpeg's hls demuxer gives at an "
                "EXT-X-DISCONTINUITY, and what mirage_vmaf's pairing (D-028) does with it. "
                "Made by eval/phase2-survey/m5_discontinuities.py; each command, then its output.",
                ""]
    r.run([a.mirage_vmaf, "--help"], quiet=True)
    r.run([a.ffmpeg, "-hide_banner", "-version"])
    cases = {
        "restart": 0.0,
        "jump": 60.0,
        "continuous": 30 * FRAME,
    }
    for case, b_offset in cases.items():
        d = os.path.join(a.work_dir, case)
        os.makedirs(d)
        r.lines += [f"## {case}", ""]
        for rung in ("ref", "dis"):
            encode(r, os.path.join(d, f"{rung}_A.ts"), rung, "A", 30, 0.0)
            encode(r, os.path.join(d, f"{rung}_B.ts"), rung, "B", 30, b_offset)
            text = playlist(os.path.join(d, f"{rung}.m3u8"),
                            [(f"{rung}_A.ts", 30 * FRAME, False), (f"{rung}_B.ts", 30 * FRAME, True)])
            r.lines += [f"{rung}.m3u8:", text, ""]
        frames_and_pair(r, d)
    # Rungs whose segments differ, no discontinuity: FFmpeg's hls muxer cuts period A into
    # segments of about 0.5 s for the reference and keeps it in one for the distorted input.
    d = os.path.join(a.work_dir, "segments")
    os.makedirs(d)
    r.lines += ["## segments", ""]
    for rung, secs in (("ref", "0.5"), ("dis", "2")):
        w, h = SIZES[rung]
        r.run([a.ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-f", "lavfi",
               "-i", f"{PATTERNS['A']}=size={w}x{h}:rate={RATE}", "-frames:v", "30",
               "-vf", "format=yuv420p,setparams=colorspace=bt709:range=tv",
               "-c:v", "mpeg2video", "-q:v", "2", "-g", "1", "-bf", "0",
               "-f", "hls", "-hls_time", secs, "-hls_list_size", "0", "-hls_playlist_type", "vod",
               "-hls_segment_filename", os.path.join(d, f"{rung}_%d.ts"),
               os.path.join(d, f"{rung}.m3u8")])
        with open(os.path.join(d, f"{rung}.m3u8")) as f:
            r.lines += [f"{rung}.m3u8:", f.read().rstrip("\n"), ""]
    frames_and_pair(r, d)
    sys.stdout.write("\n".join(r.lines) + "\n")
    return 0


def frames_and_pair(r, d):
    for rung in ("ref", "dis"):
        r.run([r.ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
               "frame=pts_time,pkt_dts_time", "-of", "csv=p=0", os.path.join(d, f"{rung}.m3u8")])
    out = os.path.join(d, "mirage.json")
    r.run([r.a.mirage_vmaf, "--ref", os.path.join(d, "ref.m3u8"), "--dis",
           os.path.join(d, "dis.m3u8"), "--out", out])
    if os.path.exists(out):
        with open(out) as f:
            rep = json.load(f)
        al = rep.get("alignment") or {}
        keep = {k: al.get(k) for k in ("paired", "failed", "failure_rate", "exceeded",
                                       "unmatched_reference", "extra_distorted",
                                       "after_discontinuity", "discontinuities", "vmaf_contexts")}
        keep["failures"] = (al.get("failures") or [])[:6]
        keep["frames_scored"] = rep.get("frames_scored")
        keep["pooled_mean"] = (rep.get("pooled") or {}).get("mean")
        r.lines += ["mirage_vmaf's report (alignment, frames scored, pooled mean):",
                    json.dumps(keep, indent=1), ""]


if __name__ == "__main__":
    sys.exit(main())
