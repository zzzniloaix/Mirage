"""Synthetic clips for the color tag cases and the reference format cases
of docs/DECISIONS.md D-016, and the negotiation cases of D-035.

D-016 has Mirage convert the distorted frames' color matrix and range the
way FFmpeg's filter graph does, untagged streams included. This script
makes the inputs that show what FFmpeg does: pairs of short clips made
from the same pattern, whose color tags, or pixel formats, differ.
Scoring them with run_delta.sh shows, in FFmpeg's verbose log, what its
section 5 filter graph negotiates for each case and converts, and how
Mirage's scores compare.

usage: python3 -I -B eval/consistency/color_cases.py <clips_dir> [--set tags|formats|negotiation]
                                                     [--ffmpeg FFMPEG] [--ffprobe FFPROBE]
                                                     [--frames N]

Three sets of cases. --set tags, the default, is the color tag cases: every
clip is 8-bit yuv420p, and only the tags differ. --set formats is the
reference format cases: references in other pixel formats than yuv420p
(10-bit, 4:2:2, 4:4:4, and the yuvj420p that FFmpeg's H.264 decoder gives
for a full range stream), a few distorted inputs in those formats, and
YCgCo on both inputs. --set negotiation is the negotiation cases of D-035:
references that aren't yuv420p against a distorted input without a range
tag, gray or RGB, where FFmpeg's negotiation changes the distorted frames'
color.

Writes into <clips_dir>, a new or empty directory:

  ref_<tags>.mkv, dis_<tags>.mkv  the clips: FFmpeg's testsrc2 pattern, the
                       references at 1920x1080, the distorted inputs scaled
                       to 640x360 (like bipbop's gear5 and gear2), tagged with
                       setparams; 8-bit yuv420p uncompressed in Matroska,
                       unless the name says otherwise after the role, as in
                       ref_ffv1-yuv420p10le_bt709-tv.mkv (FFV1, lossless) or
                       ref_libx264-yuvj420p_bt709-pc.mkv (lossless H.264)
  pairs.txt            the cases FFmpeg's graph scores, for run_delta.sh --pairs
  pairs_refused.txt    the cases FFmpeg's graph refuses to build
  clips.txt            for every clip: the command that made it, its stream
                       tags as ffprobe reads them back, and the MD5 of its
                       decoded frames (the same for every clip of one size
                       made from one pixel format)

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

# tags: ((setparams option, value), ...); pix_fmt: the format the encoder is
# given; codec: the encoder; probed: what ffprobe reads back besides the tags
# and the pixel format, where the decoder differs, as ((field, value), ...).
Clip = collections.namedtuple("Clip", "name size tags pix_fmt codec probed",
                              defaults=("yuv420p", "rawvideo", ()))
Case = collections.namedtuple("Case", "label ref dis about")

# Further options of each encoder: FFV1 is lossless as it is, libx264 with a
# quantizer of 0.
CODEC_OPTIONS = {"rawvideo": [], "ffv1": [], "libx264": ["-qp", "0"]}


def clip(role, tags, pix_fmt="yuv420p", codec="rawvideo", probed=()):
    """A clip of `role` ("ref" or "dis") with setparams tags given as
    "option=value" strings; it is named after them, and after its codec and
    the pixel format it decodes to unless it is uncompressed yuv420p."""
    pairs = tuple(tuple(t.split("=", 1)) for t in tags)
    kind = "" if (pix_fmt, codec) == ("yuv420p", "rawvideo") else \
        f"{codec}-{dict(probed).get('pix_fmt', pix_fmt)}_"
    name = f"{role}_{kind}" + "-".join(v for _, v in pairs)
    return Clip(name, REF_SIZE if role == "ref" else DIS_SIZE, pairs, pix_fmt, codec,
                tuple(probed))


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

# The reference format cases (--set formats). FFV1 and lossless H.264 keep
# the pattern's pixels; FFmpeg's H.264 decoder gives a full range stream as
# yuvj420p, and the chroma location left when the stream doesn't state one.
H264_FULL = (("pix_fmt", "yuvj420p"), ("chroma_location", "left"))

REF10_709_TV = clip("ref", ["colorspace=bt709", "range=tv"], "yuv420p10le", "ffv1")
REF10_170M_TV = clip("ref", ["colorspace=smpte170m", "range=tv"], "yuv420p10le", "ffv1")
# An FFV1 clip made with range=unknown reads back as limited range, so the
# 10-bit reference without a matrix tag is tagged range=tv.
REF10_NOMATRIX_TV = clip("ref", ["colorspace=unknown", "range=tv"], "yuv420p10le", "ffv1")
REF422_709_TV = clip("ref", ["colorspace=bt709", "range=tv"], "yuv422p", "ffv1")
REF444_709_TV = clip("ref", ["colorspace=bt709", "range=tv"], "yuv444p", "ffv1")
REFJ_709_PC = clip("ref", ["colorspace=bt709", "range=pc"], "yuv420p", "libx264", H264_FULL)

DIS10_170M_TV = clip("dis", ["colorspace=smpte170m", "range=tv"], "yuv420p10le", "ffv1")
DISJ_709_PC = clip("dis", ["colorspace=bt709", "range=pc"], "yuv420p", "libx264", H264_FULL)

FORMAT_CASES = (
    Case("f01_10bit_bt709_vs_smpte170m", REF10_709_TV, DIS_170M_TV,
         "a 10-bit reference, another matrix"),
    Case("f02_10bit_smpte170m_vs_bt709", REF10_170M_TV, DIS_709_TV,
         "a 10-bit BT.601 reference, a BT.709 distorted input"),
    Case("f03_10bit_nomatrix_vs_bt709", REF10_NOMATRIX_TV, DIS_709_TV,
         "a 10-bit reference without a matrix tag"),
    Case("f04_10bit_bt709_vs_bt709", REF10_709_TV, DIS_709_TV,
         "a 10-bit reference, the same tags"),
    Case("f05_422_bt709_vs_smpte170m", REF422_709_TV, DIS_170M_TV,
         "a 4:2:2 reference, another matrix"),
    Case("f06_422_bt709_vs_bt709", REF422_709_TV, DIS_709_TV,
         "a 4:2:2 reference, the same tags"),
    Case("f07_444_bt709_vs_smpte170m", REF444_709_TV, DIS_170M_TV,
         "a 4:4:4 reference, another matrix"),
    Case("f08_444_bt709_vs_bt709", REF444_709_TV, DIS_709_TV,
         "a 4:4:4 reference, the same tags"),
    Case("j01_yuvj_pc_vs_tv", REFJ_709_PC, DIS_709_TV,
         "a full range H.264 reference, decoded as yuvj420p; a limited range distorted input"),
    Case("j02_yuvj_pc_vs_pc", REFJ_709_PC, DIS_709_PC,
         "a full range H.264 reference, decoded as yuvj420p; a full range distorted input"),
    Case("d01_bt709_vs_10bit_smpte170m", REF_709_TV, DIS10_170M_TV,
         "a yuv420p reference, a 10-bit distorted input with another matrix"),
    Case("d02_bt709tv_vs_yuvj_pc", REF_709_TV, DISJ_709_PC,
         "a yuv420p reference, a full range H.264 distorted input, decoded as yuvj420p"),
)

FORMAT_REFUSED = (
    Case("u03_ycgco_vs_ycgco", REF_YCGCO_TV, DIS_YCGCO_TV, "YCgCo on both inputs"),
)

# The negotiation cases (--set negotiation, DECISIONS.md D-035): a reference
# that isn't yuv420p against a distorted input whose buffer source offers
# more than one matrix or range, where FFmpeg 8.1.1's negotiation changes the
# distorted frames' color: (a) a distorted input without a range tag, against
# a full range reference, with a limited range reference as its control, and
# (b) distorted inputs that aren't plain YUV. FFV1 gives an 8-bit planar RGB
# stream back as bgr0.
DIS_GRAY_709_TV = clip("dis", ["colorspace=bt709", "range=tv"], "gray", "ffv1")
DIS_RGB_PC = clip("dis", ["colorspace=gbr", "range=pc"], "gbrp", "ffv1", (("pix_fmt", "bgr0"),))

NEGOTIATION_CASES = (
    Case("n01_yuvj_pc_vs_norange", REFJ_709_PC, DIS_709_NORANGE,
         "(a) a full range H.264 reference, decoded as yuvj420p; a distorted input without a "
         "range tag"),
    Case("n02_10bit_tv_vs_norange", REF10_709_TV, DIS_709_NORANGE,
         "(a)'s control: a limited range 10-bit reference, a distorted input without a range tag"),
    Case("n03_10bit_bt709_vs_gray", REF10_709_TV, DIS_GRAY_709_TV,
         "(b) a 10-bit reference, a gray distorted input"),
    Case("n04_10bit_bt709_vs_rgb", REF10_709_TV, DIS_RGB_PC,
         "(b) a 10-bit reference, an RGB distorted input"),
)

# Each set: its cases, its refused cases, and how its files describe it.
CaseSet = collections.namedtuple("CaseSet", "cases refused option what")
SETS = {
    "tags": CaseSet(CASES, REFUSED, "", "Color tag cases"),
    "formats": CaseSet(FORMAT_CASES, FORMAT_REFUSED, " --set formats", "Reference format cases"),
    "negotiation": CaseSet(NEGOTIATION_CASES, (), " --set negotiation", "Negotiation cases"),
}


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
            "-vf", f"{chain}format={x.pix_fmt},setparams={params}",
            "-c:v", x.codec, *CODEC_OPTIONS[x.codec], "-f", "matroska", path]


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
    want.update(width=str(x.size[0]), height=str(x.size[1]), pix_fmt=x.pix_fmt)
    want.update(x.probed)
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
    p.add_argument("--set", choices=sorted(SETS), default="tags",
                   help="which cases (default: tags, the color tag cases)")
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
    chosen = SETS[args.set]
    clips = all_clips(chosen.cases + chosen.refused)

    record = [f"# The clips of eval/consistency/color_cases.py{chosen.option}, made in "
              "<clips_dir>: for each, the command that made it, its stream as <ffprobe> reads "
              "it back, and the MD5 of its decoded frames.", ""]
    problems, md5_by_kind = [], {}
    try:
        for x in clips:
            path = os.path.join(clips_dir, x.name + ".mkv")
            make = make_command(args.ffmpeg, x, path, args.frames)
            run(make)
            probe = probe_command(ffprobe, path)
            probed_text = run(probe)
            problems += tag_problems(x, parse_probe(probed_text))
            md5 = md5_command(args.ffmpeg, path)
            digest = run(md5).strip()
            md5_by_kind.setdefault((x.size, x.pix_fmt), set()).add(digest)
            record += [f"## {x.name}.mkv", f"$ {shown(make, clips_dir, programs)}",
                       f"$ {shown(probe, clips_dir, programs)}", probed_text.rstrip(),
                       f"$ {shown(md5, clips_dir, programs)}", digest, ""]
    except (OSError, RuntimeError) as e:
        print(f"color_cases: {e}", file=sys.stderr)
        return 1
    for (size, pix_fmt), digests in sorted(md5_by_kind.items()):
        if len(digests) != 1:
            problems.append(f"the {size[0]}x{size[1]} clips made from {pix_fmt} do not all "
                            "have the same pixels")

    with open(os.path.join(clips_dir, "clips.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(record))
    with open(os.path.join(clips_dir, "pairs.txt"), "w", encoding="utf-8") as f:
        f.write(pairs_text(chosen.cases, clips_dir, f"{chosen.what} that FFmpeg's section 5 "
                                                    "graph scores (docs/DECISIONS.md D-016)."))
    with open(os.path.join(clips_dir, "pairs_refused.txt"), "w", encoding="utf-8") as f:
        f.write(pairs_text(chosen.refused, clips_dir, f"{chosen.what} that FFmpeg's section 5 "
                                                      "graph refuses (docs/DECISIONS.md D-016)."))
    for x in problems:
        print(f"color_cases: {x}", file=sys.stderr)
    print(f"color_cases: {len(clips)} clips, {len(chosen.cases)} + {len(chosen.refused)} cases "
          f"in {args.clips_dir}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
