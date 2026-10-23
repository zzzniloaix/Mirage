"""Synthetic clips for the color tag cases of docs/DECISIONS.md D-016.

D-016 has Mirage convert the distorted frames' color matrix and range the
way FFmpeg's filter graph does, untagged streams included. This script
makes the inputs that show what FFmpeg does: pairs of short clips whose
pixels are the same in every case and whose color tags differ. Scoring
them with run_delta.sh shows, in FFmpeg's verbose log, what its section 5
filter graph negotiates for each case and converts, and how Mirage's
scores compare.

usage: python3 -I -B eval/consistency/color_cases.py <clips_dir> [--ffmpeg FFMPEG]
                                                     [--ffprobe FFPROBE] [--frames N]

Writes into <clips_dir>, a new or empty directory:

  ref_<tags>.mkv, dis_<tags>.mkv  the clips: FFmpeg's testsrc2 pattern, the
                       references at 1920x1080, the distorted inputs scaled
                       to 640x360 (like bipbop's gear5 and gear2), yuv420p,
                       uncompressed in Matroska, tagged with setparams
  pairs.txt            the cases FFmpeg's graph scores, for run_delta.sh --pairs
  pairs_refused.txt    the cases FFmpeg's graph refuses to build
  clips.txt            for every clip: the command that made it, its stream
                       tags as ffprobe reads them back, and the MD5 of its
                       decoded frames (the same for every clip of one size)

The pairs files name the clips by their paths; clips.txt shows <clips_dir>
in their place and <ffmpeg> / <ffprobe> for the programs. Python 3
standard library only.

Exit status: 0 the clips were made and read back with the intended tags;
1 a program failed or a clip's tags or pixels are not as intended;
2 bad arguments.
"""

import argparse
import collections
import os
import shlex
import subprocess
import sys

DEFAULT_FFMPEG = "/opt/homebrew/opt/ffmpeg-full/bin/ffmpeg"   # docs/DECISIONS.md D-009

REF_SIZE = (1920, 1080)
DIS_SIZE = (640, 360)
RATE = "30000/1001"
DEFAULT_FRAMES = 10

# setparams option -> ffprobe's stream field. setparams leaves a tag it is
# not given as the pattern has it: unspecified.
TAG_FIELDS = (("colorspace", "color_space"), ("range", "color_range"),
              ("color_primaries", "color_primaries"), ("color_trc", "color_transfer"),
              ("chroma_location", "chroma_location"), ("field_mode", "field_order"))

# What ffprobe reads back for a tag setparams was not given, or was given as
# "unknown".
UNTAGGED = {"color_space": "unknown", "color_range": "unknown", "color_primaries": "unknown",
            "color_transfer": "unknown", "chroma_location": "unspecified",
            "field_order": "progressive"}

# ffprobe's value for a setparams value, where the two names differ.
PROBED_AS = {("color_transfer", "bt470bg"): "bt470bg", ("field_order", "tff"): "tb",
             ("color_range", "unknown"): "unknown", ("color_space", "unknown"): "unknown"}

Clip = collections.namedtuple("Clip", "name size tags")   # tags: ((setparams option, value), ...)
Case = collections.namedtuple("Case", "label ref dis about")


def clip(role, tags):
    """A clip of `role` ("ref" or "dis") with setparams tags given as
    "option=value" strings; it is named after them."""
    pairs = tuple(tuple(t.split("=", 1)) for t in tags)
    name = f"{role}_" + "-".join(v for _, v in pairs)
    return Clip(name, REF_SIZE if role == "ref" else DIS_SIZE, pairs)


REF_709_TV = clip("ref", ["colorspace=bt709", "range=tv"])
REF_UNTAGGED = clip("ref", ["colorspace=unknown", "range=unknown"])
REF_170M_TV = clip("ref", ["colorspace=smpte170m", "range=tv"])
REF_2020_TV = clip("ref", ["colorspace=bt2020nc", "range=tv"])
REF_709_PC = clip("ref", ["colorspace=bt709", "range=pc"])
REF_709_NORANGE = clip("ref", ["colorspace=bt709", "range=unknown"])
REF_709_TV_709 = clip("ref", ["colorspace=bt709", "range=tv", "color_primaries=bt709",
                              "color_trc=bt709"])
REF_YCGCO_TV = clip("ref", ["colorspace=ycgco", "range=tv"])

DIS_170M_TV = clip("dis", ["colorspace=smpte170m", "range=tv"])
DIS_709_TV = clip("dis", ["colorspace=bt709", "range=tv"])
DIS_UNTAGGED = clip("dis", ["colorspace=unknown", "range=unknown"])
DIS_470BG_TV = clip("dis", ["colorspace=bt470bg", "range=tv"])
DIS_709_PC = clip("dis", ["colorspace=bt709", "range=pc"])
DIS_709_NORANGE = clip("dis", ["colorspace=bt709", "range=unknown"])
DIS_170M_PC = clip("dis", ["colorspace=smpte170m", "range=pc"])
DIS_709_TV_470BG = clip("dis", ["colorspace=bt709", "range=tv", "color_primaries=bt470bg",
                                "color_trc=bt470bg"])
DIS_709_TV_TOPLEFT = clip("dis", ["colorspace=bt709", "range=tv", "chroma_location=topleft"])
DIS_709_TV_TFF = clip("dis", ["colorspace=bt709", "range=tv", "field_mode=tff"])
DIS_YCGCO_TV = clip("dis", ["colorspace=ycgco", "range=tv"])

# The cases FFmpeg's section 5 graph scores. Each label names the
# reference's and the distorted input's tags.
CASES = (
    Case("m01_bt709_vs_smpte170m", REF_709_TV, DIS_170M_TV,
         "another matrix, as bipbop's gear2 and gear1"),
    Case("m02_bt709_vs_bt709", REF_709_TV, DIS_709_TV,
         "the same tags, as bipbop's gear4 and gear3"),
    Case("m03_bt709_vs_untagged", REF_709_TV, DIS_UNTAGGED,
         "a distorted input without matrix and range tags"),
    Case("m04_untagged_vs_bt709", REF_UNTAGGED, DIS_709_TV,
         "a reference without matrix and range tags"),
    Case("m05_untagged_vs_smpte170m", REF_UNTAGGED, DIS_170M_TV,
         "a reference without tags, a BT.601 distorted input"),
    Case("m06_untagged_vs_untagged", REF_UNTAGGED, DIS_UNTAGGED,
         "no tags on either input"),
    Case("m07_smpte170m_vs_bt470bg", REF_170M_TV, DIS_470BG_TV,
         "two names of BT.601"),
    Case("m08_bt2020nc_vs_bt709", REF_2020_TV, DIS_709_TV,
         "a BT.2020 reference"),
    Case("r01_tv_vs_pc", REF_709_TV, DIS_709_PC, "a full range distorted input"),
    Case("r02_pc_vs_tv", REF_709_PC, DIS_709_TV, "a full range reference"),
    Case("r03_norange_vs_pc", REF_709_NORANGE, DIS_709_PC,
         "a reference without a range tag, a full range distorted input"),
    Case("r04_pc_vs_norange", REF_709_PC, DIS_709_NORANGE,
         "a full range reference, a distorted input without a range tag"),
    Case("r05_norange_vs_tv", REF_709_NORANGE, DIS_709_TV,
         "a reference without a range tag, a limited range distorted input"),
    Case("r06_tv_vs_norange", REF_709_TV, DIS_709_NORANGE,
         "a distorted input without a range tag"),
    Case("x01_bt709tv_vs_smpte170mpc", REF_709_TV, DIS_170M_PC,
         "another matrix and another range"),
    Case("x02_primaries_transfer", REF_709_TV_709, DIS_709_TV_470BG,
         "other primaries and transfer tags on the distorted input"),
    Case("x03_chroma_topleft", REF_709_TV, DIS_709_TV_TOPLEFT,
         "a distorted input tagged with top-left chroma siting"),
    Case("x04_interlaced_tff", REF_709_TV, DIS_709_TV_TFF,
         "a distorted input flagged interlaced, top field first"),
)

# The cases FFmpeg's graph refuses: libswscale has no YCgCo matrix.
REFUSED = (
    Case("u01_bt709_vs_ycgco", REF_709_TV, DIS_YCGCO_TV, "a YCgCo distorted input"),
    Case("u02_ycgco_vs_bt709", REF_YCGCO_TV, DIS_709_TV, "a YCgCo reference"),
)


def all_clips(cases=CASES + REFUSED):
    """Every clip the cases use, once each, references first, in case order."""
    seen = {}
    for c in cases:
        for x in (c.ref, c.dis):
            seen.setdefault(x.name, x)
    return sorted(seen.values(), key=lambda x: (x.name[:3] != "ref",
                                                list(seen).index(x.name)))


def make_command(ffmpeg, x, path, frames=DEFAULT_FRAMES):
    """The ffmpeg run that makes clip `x` at `path`."""
    w, h = REF_SIZE
    chain = "" if x.size == REF_SIZE else f"scale={x.size[0]}:{x.size[1]}:flags=bicubic,"
    params = ":".join(f"{k}={v}" for k, v in x.tags)
    return [ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-f", "lavfi",
            "-i", f"testsrc2=size={w}x{h}:rate={RATE}", "-frames:v", str(frames),
            "-vf", f"{chain}format=yuv420p,setparams={params}",
            "-c:v", "rawvideo", "-f", "matroska", path]


def probe_command(ffprobe, path):
    return [ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
            "stream=width,height,pix_fmt," + ",".join(f for _, f in TAG_FIELDS),
            "-of", "default=noprint_wrappers=1", path]


def md5_command(ffmpeg, path):
    """The MD5 of the decoded frames' pixels: tags do not enter it."""
    return [ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-i", path,
            "-map", "0:v:0", "-c:v", "rawvideo", "-f", "md5", "-"]


def expected_tags(x):
    """ffprobe's stream fields for clip `x`: its setparams tags, the rest untagged."""
    want = dict(UNTAGGED)
    for opt, field in TAG_FIELDS:
        for k, v in x.tags:
            if k == opt:
                want[field] = PROBED_AS.get((field, v), v)
    want.update(width=str(x.size[0]), height=str(x.size[1]), pix_fmt="yuv420p")
    return want


def parse_probe(text):
    """ffprobe's default=noprint_wrappers=1 output as a dict."""
    return dict(line.split("=", 1) for line in text.splitlines() if "=" in line)


def tag_problems(x, probed):
    """How the tags ffprobe read back differ from those clip `x` should have."""
    want = expected_tags(x)
    return [f"{x.name}: {k} is {probed.get(k)!r}, not {v!r}"
            for k, v in want.items() if probed.get(k) != v]


def pairs_text(cases, clips_dir, title):
    """A pairs file for run_delta.sh: '<label> <reference> <distorted>' per line."""
    lines = [f"# {title}", "# Made by eval/consistency/color_cases.py; <label> <reference> "
             "<distorted>. The cases:"]
    lines += [f"#   {c.label}: {c.about}" for c in cases]
    lines += [f"{c.label} {os.path.join(clips_dir, c.ref.name + '.mkv')} "
              f"{os.path.join(clips_dir, c.dis.name + '.mkv')}" for c in cases]
    return "\n".join(lines) + "\n"


def shown(argv, clips_dir, programs):
    """A command line for clips.txt: <clips_dir> for the directory, the
    programs by their placeholders."""
    out = []
    for a in argv:
        a = programs.get(a, a)
        if a.startswith(clips_dir + os.sep):
            out.append("<clips_dir>/" + a[len(clips_dir) + 1:])
        else:
            out.append(a if a.startswith("<") else shlex.quote(a))
    return " ".join(out)


def run(argv):
    p = subprocess.run(argv, stdin=subprocess.DEVNULL, capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"{os.path.basename(argv[0])} exited with status {p.returncode}: "
                           f"{p.stderr.strip()}")
    return p.stdout


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="color_cases.py",
        description="Make the synthetic clips and pairs files of the color tag cases "
                    "(docs/DECISIONS.md D-016) in CLIPS_DIR.")
    p.add_argument("clips_dir", help="a new or empty directory")
    p.add_argument("--ffmpeg", default=DEFAULT_FFMPEG, help=f"default: {DEFAULT_FFMPEG}")
    p.add_argument("--ffprobe", help="default: the ffprobe next to --ffmpeg")
    p.add_argument("--frames", type=int, default=DEFAULT_FRAMES,
                   help=f"frames per clip (default: {DEFAULT_FRAMES})")
    args = p.parse_args(argv)
    if args.frames < 1:
        p.error("--frames must be at least 1")
    return args


def main(argv=None):
    args = parse_args(argv)
    clips_dir = os.path.abspath(args.clips_dir)
    if os.path.exists(clips_dir) and (not os.path.isdir(clips_dir) or os.listdir(clips_dir)):
        print(f"color_cases: {args.clips_dir} exists and is not an empty directory",
              file=sys.stderr)
        return 2
    os.makedirs(clips_dir, exist_ok=True)
    ffprobe = args.ffprobe or os.path.join(os.path.dirname(args.ffmpeg), "ffprobe")
    programs = {args.ffmpeg: "<ffmpeg>", ffprobe: "<ffprobe>"}

    record = ["# The clips of eval/consistency/color_cases.py, made in <clips_dir>: for "
              "each, the command that made it, its stream as <ffprobe> reads it back, and "
              "the MD5 of its decoded frames.", ""]
    problems, md5_by_size = [], {}
    try:
        for x in all_clips():
            path = os.path.join(clips_dir, x.name + ".mkv")
            make = make_command(args.ffmpeg, x, path, args.frames)
            run(make)
            probe = probe_command(ffprobe, path)
            probed_text = run(probe)
            problems += tag_problems(x, parse_probe(probed_text))
            md5 = md5_command(args.ffmpeg, path)
            digest = run(md5).strip()
            md5_by_size.setdefault(x.size, set()).add(digest)
            record += [f"## {x.name}.mkv", f"$ {shown(make, clips_dir, programs)}",
                       f"$ {shown(probe, clips_dir, programs)}", probed_text.rstrip(),
                       f"$ {shown(md5, clips_dir, programs)}", digest, ""]
    except (OSError, RuntimeError) as e:
        print(f"color_cases: {e}", file=sys.stderr)
        return 1
    for size, digests in sorted(md5_by_size.items()):
        if len(digests) != 1:
            problems.append(f"the {size[0]}x{size[1]} clips do not all have the same pixels")

    with open(os.path.join(clips_dir, "clips.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(record))
    with open(os.path.join(clips_dir, "pairs.txt"), "w", encoding="utf-8") as f:
        f.write(pairs_text(CASES, clips_dir, "Color tag cases that FFmpeg's section 5 "
                                             "graph scores (docs/DECISIONS.md D-016)."))
    with open(os.path.join(clips_dir, "pairs_refused.txt"), "w", encoding="utf-8") as f:
        f.write(pairs_text(REFUSED, clips_dir, "Color tag cases that FFmpeg's section 5 "
                                               "graph refuses (docs/DECISIONS.md D-016)."))
    for x in problems:
        print(f"color_cases: {x}", file=sys.stderr)
    print(f"color_cases: {len(all_clips())} clips, {len(CASES)} + {len(REFUSED)} cases "
          f"in {args.clips_dir}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
