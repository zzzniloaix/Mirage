"""Delta math and reports for the section 5 consistency check (run_delta.py).

Compares Mirage's per-frame VMAF scores, from a mirage_vmaf report
(src/VMAFReport.h), with the log of FFmpeg's libvmaf filter (log_fmt=json)
for the same pair, pair by pair: the two sides' pairs are matched by their
frames' positions in the inputs and their timestamps (docs/DECISIONS.md
D-028), FFmpeg's taken from the showinfo filters its graph logs every frame
with. It renders the delta report of each pair and of a whole run as JSON
and Markdown. It also builds the commands both sides run, and holds the
helpers for the files a run leaves behind (placeholders for local paths, xz,
SHA256SUMS).

Nothing here starts a process; tests/test_vmafdelta.py covers all of it.
Python 3 standard library only.
"""

import collections
import fractions
import hashlib
import json
import lzma
import math
import os
import re
import shlex

REPORT_VERSION = 1

# docs/DECISIONS.md D-019: a frame fails if |Mirage - FFmpeg| > 0.01.
DEFAULT_THRESHOLD = 0.01

# How many of the frames with the largest |delta| a report lists.
WORST_FRAMES = 5

# How many runs of over-threshold frames a Markdown report spells out.
MARKDOWN_RANGES = 20

# The pooled values both sides give as libvmaf pools them: mirage_vmaf's
# report (the harmonic mean since D-027) and FFmpeg's libvmaf log.
POOLED = ("mean", "min", "harmonic_mean")

VARIANT_PLAIN = "plain"
VARIANT_RELABEL = "diagnostic-relabel"

VARIANT_TEXT = {
    VARIANT_PLAIN: "plain FFmpeg: the section 5 comparison",
    VARIANT_RELABEL: ("DIAGNOSTIC, not the section 5 comparison: FFmpeg with the distorted "
                      "input relabeled to the reference's color_space and color_range tags "
                      "where they differ (setparams before scale), so that FFmpeg does not "
                      "convert them"),
}

Pair = collections.namedtuple("Pair", "label ref dis")

_LABEL = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*\Z")


def parse_pairs(text):
    """The pairs of a pairs file: one "<label> <reference> <distorted>" per
    line, separated by white space; blank lines and lines starting with "#"
    are skipped. A label names the pair's output directory, so labels are
    unique and use only letters, digits, "_", "." and "-". Raises ValueError."""
    pairs, seen = [], set()
    for n, line in enumerate(text.splitlines(), 1):
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        fields = s.split()
        if len(fields) != 3:
            raise ValueError(f"line {n}: expected <label> <reference> <distorted>, "
                             f"found {len(fields)} fields")
        label, ref, dis = fields
        if not _LABEL.match(label):
            raise ValueError(f"line {n}: label {label!r} may only use letters, digits, "
                             "'_', '.' and '-', and starts with a letter or digit")
        if label in seen:
            raise ValueError(f"line {n}: label {label!r} is used twice")
        seen.add(label)
        pairs.append(Pair(label, ref, dis))
    if not pairs:
        raise ValueError("the pairs file has no pairs")
    return pairs


# ── Commands ─────────────────────────────────────────────────────────────────

PROBE_FIELDS = ("codec_name", "width", "height", "pix_fmt", "color_space", "color_range",
                "color_primaries", "color_transfer", "time_base", "r_frame_rate",
                "avg_frame_rate", "start_time")


def probe_command(ffprobe, url):
    """ffprobe's parameters of the first video stream of `url`, as JSON."""
    return [ffprobe, "-hide_banner", "-v", "error", "-select_streams", "v:0",
            "-show_entries", "stream=" + ",".join(PROBE_FIELDS), "-of", "json", url]


def parse_probe(text):
    """The stream in probe_command()'s output: a dict with every key of
    PROBE_FIELDS, None where ffprobe gave no value. Raises ValueError."""
    try:
        data = json.loads(text)
    except ValueError as e:
        raise ValueError(f"ffprobe's output is not JSON: {e}") from None
    streams = data.get("streams") if isinstance(data, dict) else None
    if not streams or not isinstance(streams[0], dict):
        raise ValueError("ffprobe found no video stream")
    info = {k: streams[0].get(k) for k in PROBE_FIELDS}
    for k in ("width", "height"):
        if not isinstance(info[k], int) or isinstance(info[k], bool) or info[k] <= 0:
            raise ValueError(f"ffprobe gave no {k}")
    return info


# Tag values that say nothing about the color space or range.
_UNTAGGED = (None, "", "unknown", "unspecified")


def relabel_filter(ref, dis):
    """For the diagnostic relabel variant: a setparams filter that tags the
    distorted frames with the reference's color_space and color_range where
    the two inputs' tags (ffprobe's) differ, so that FFmpeg's scale filter
    has nothing to convert. Returns (filter or None, warnings)."""
    opts, warnings = [], []
    for key, opt in (("color_space", "colorspace"), ("color_range", "range")):
        r, d = ref.get(key), dis.get(key)
        if r == d:
            continue
        if r in _UNTAGGED:
            warnings.append(f"the reference has no {key} tag, so the distorted input's "
                            f"{key} ({d}) is not relabeled")
            continue
        opts.append(f"{opt}={r}")
    return ("setparams=" + ":".join(opts) if opts else None), warnings


# FFmpeg's libvmaf log gives its frames no timestamps, so its graph logs
# every frame with a showinfo filter: at the end of each input's chain, and
# after libvmaf, which passes on the distorted frame of each of its events
# (docs/plans/frame-alignment.md, "Where the code changes"; ffmpeg_pairing()).
# checksum=0 leaves out the planes' checksums and statistics, which the
# comparison doesn't use. The filter changes no frame.
SHOWINFO = "showinfo=checksum=0"


def filtergraph(width, height, scaler, model, threads, frames=None, relabel=None,
                log_path="ffmpeg.json", model_file=None):
    """FFmpeg's side of a pair: the filter graph of Phase 0's diagnostic
    comparison (eval/baseline/diagnostics/ffmpeg_libvmaf_gear2_vs_gear5_first300.cmd.txt)
    with the reference's size, and SHOWINFO at the end of both inputs'
    chains and after libvmaf. Input 0 is the distorted video, input 1 the
    reference. With `frames`, trim passes only the first `frames` frames of
    each input on, so that libvmaf scores exactly that many pairs, and the
    last of them has no successor, as with mirage_vmaf --frames.
    `relabel` (relabel_filter()) goes before the scale filter. `model` is a
    built-in model's version; `model_file`, a path filter_safe() accepts,
    is a model file to load instead (model=path=)."""
    trim = f"trim=end_frame={frames}," if frames else ""
    tags = f"{relabel}," if relabel else ""
    which = f"path={model_file}" if model_file else f"version={model}"
    return (f"[0:v]{trim}{tags}scale={width}:{height}:flags={scaler},format=yuv420p,"
            f"{SHOWINFO}[d];"
            f"[1:v]{trim}format=yuv420p,{SHOWINFO}[r];"
            f"[d][r]libvmaf=model={which}:n_threads={threads}"
            f":log_fmt=json:log_path={log_path},{SHOWINFO}")


# Characters with a meaning in a filter graph or its option lists, and white
# space; a path given to filtergraph() as a value must have none of them.
_GRAPH_SPECIAL = set(":\\'[],;=|%")


def filter_safe(value):
    """Whether `value` can stand as an option value in filtergraph() as it is."""
    return bool(value) and not any(c in _GRAPH_SPECIAL or c.isspace() for c in value)


def ffmpeg_command(ffmpeg, ref, dis, graph):
    """The FFmpeg run of a pair: verbose, for the lines in which the scale
    filter states its input and output color space; no audio, since only
    video is scored."""
    return [ffmpeg, "-hide_banner", "-nostdin", "-nostats", "-v", "verbose",
            "-i", dis, "-i", ref, "-an", "-lavfi", graph, "-f", "null", "-"]


def mirage_command(mirage_vmaf, ref, dis, out="mirage.json", frames=None, options=()):
    """The mirage_vmaf run of a pair; `options` are further arguments."""
    argv = [mirage_vmaf, "--ref", ref, "--dis", dis, "--out", out]
    if frames:
        argv += ["--frames", str(frames)]
    return argv + list(options)


def help_options(help_text):
    """The options a --help text lists: words starting with "--" at the
    start of an indented line."""
    return {m.group(1) for m in re.finditer(r"(?m)^[ \t]+(--[a-z][a-z0-9-]*)", help_text)}


def forwarded_options(supported, scaler, model, model_file=None):
    """The scoring settings passed on to mirage_vmaf, for those of them it
    has an option for (`supported`, from help_options()): the scaler, and
    the built-in `model` or else the `model_file`. Without such an option
    mirage_vmaf scores with its fixed setting, and the settings check in
    evaluate_pair() compares that with what FFmpeg was given."""
    argv = []
    if "--scaler" in supported:
        argv += ["--scaler", scaler]
    if model_file:
        if "--model-file" in supported:
            argv += ["--model-file", model_file]
    elif "--model" in supported:
        argv += ["--model", model]
    return argv


def shell_line(argv, program=None):
    """argv as one shell command line, with argv[0] shown as `program` (a
    placeholder such as "<ffmpeg>") when given."""
    first = program if program is not None else shlex.quote(argv[0])
    return " ".join([first] + [shlex.quote(a) for a in argv[1:]])


# ── Local paths ──────────────────────────────────────────────────────────────

class Redactor:
    """Replaces local paths with placeholders, in text or bytes, longest
    path first. Each path is matched as given (made absolute) and as its
    real path, which differ on macOS for /tmp and /var."""

    def __init__(self, paths):
        subs = {}
        for path, placeholder in paths:
            if not path:
                continue
            for p in (os.path.abspath(path), os.path.realpath(path)):
                p = p.rstrip("/")
                if len(p) > 1:
                    subs.setdefault(p, placeholder)
        self._subs = sorted(subs.items(), key=lambda s: (-len(s[0]), s[0]))

    def text(self, s):
        """(s with the paths replaced, number of replacements)"""
        n = 0
        for path, placeholder in self._subs:
            k = s.count(path)
            if k:
                s = s.replace(path, placeholder)
                n += k
        return s, n

    def data(self, b):
        """(b with the paths replaced, number of replacements)"""
        n = 0
        for path, placeholder in self._subs:
            p = path.encode()
            k = b.count(p)
            if k:
                b = b.replace(p, placeholder.encode())
                n += k
        return b, n


def local_strings(text, needles):
    """The names of the needles ((string, name) pairs) that occur in text."""
    return sorted({name for s, name in needles if s and s in text})


# ── Scores ───────────────────────────────────────────────────────────────────

def is_number(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def mirage_scores(report):
    """The per-frame scores of a mirage_vmaf report, in index order. Raises
    ValueError unless its frames are indexed 0..n-1 and all have a score."""
    frames = report.get("frames")
    if not isinstance(frames, list):
        raise ValueError("the mirage_vmaf report has no frames")
    scores = []
    for i, f in enumerate(frames):
        index = f.get("index") if isinstance(f, dict) else None
        if index != i:
            raise ValueError(f"mirage_vmaf frame {i} has index {index!r}")
        if not is_number(f.get("vmaf")):
            raise ValueError(f"mirage_vmaf frame {i} has no score")
        scores.append(float(f["vmaf"]))
    return scores


def ffmpeg_scores(log):
    """The per-frame scores of FFmpeg's libvmaf log, in frame order. Raises
    ValueError unless its frames are numbered 0..n-1 and all have a score."""
    frames = log.get("frames")
    if not isinstance(frames, list):
        raise ValueError("FFmpeg's libvmaf log has no frames")
    scores = []
    for i, f in enumerate(frames):
        num = f.get("frameNum") if isinstance(f, dict) else None
        if num != i:
            raise ValueError(f"FFmpeg's libvmaf log frame {i} has frameNum {num!r}")
        v = (f.get("metrics") or {}).get("vmaf")
        if not is_number(v):
            raise ValueError(f"FFmpeg's libvmaf log frame {i} has no vmaf score")
        scores.append(float(v))
    return scores


def index_ranges(indices):
    """Ascending indices as [first, last] runs: [0, 1, 2, 5] -> [[0, 2], [5, 5]]."""
    runs = []
    for i in indices:
        if runs and i == runs[-1][1] + 1:
            runs[-1][1] = i
        else:
            runs.append([i, i])
    return runs


def _mean(values):
    return math.fsum(values) / len(values) if values else None


def compare_scores(mirage, ffmpeg, threshold, worst=WORST_FRAMES, indices=None,
                   ffmpeg_indices=None):
    """Frame-by-frame comparison over the frames both lists have, with
    delta = Mirage - FFmpeg. A frame is over the threshold if
    |delta| > threshold (D-019). `indices` and `ffmpeg_indices` name the
    compared frames' pairs on each side (mirage_vmaf's pair index, the
    frameNum of FFmpeg's log) when they aren't the lists' positions, as for
    pairs matched by compare_pairings()."""
    n = min(len(mirage), len(ffmpeg))
    index = indices if indices is not None else range(n)
    other = ffmpeg_indices if ffmpeg_indices is not None else range(n)
    delta = [mirage[i] - ffmpeg[i] for i in range(n)]
    size = [abs(d) for d in delta]
    over = [index[i] for i in range(n) if size[i] > threshold]
    order = sorted(range(n), key=lambda i: (-size[i], i))[:worst]
    return {
        "compared": n,
        "max_abs": size[order[0]] if n else None,
        "max_abs_index": index[order[0]] if n else None,
        "mean_abs": _mean(size),
        "mean_signed": _mean(delta),
        "over_threshold": len(over),
        "over_threshold_ranges": index_ranges(over),
        "worst": [{"index": index[i], "ffmpeg_index": other[i], "mirage": mirage[i],
                   "ffmpeg": ffmpeg[i], "delta": delta[i]} for i in order],
        "mean_mirage": _mean(mirage[:n]),
        "mean_ffmpeg": _mean(ffmpeg[:n]),
    }


# ── FFmpeg's verbose log ─────────────────────────────────────────────────────

_SCALE = re.compile(
    r"^\[(?P<filter>(?:Parsed_scale|auto_scale)_\d+) @ [^\]]*\] "
    r"w:(?P<in_w>\d+) h:(?P<in_h>\d+) fmt:(?P<in_fmt>\S+) csp:(?P<in_csp>\S+) "
    r"range:(?P<in_range>\S+) sar:\S+ -> "
    r"w:(?P<out_w>\d+) h:(?P<out_h>\d+) fmt:(?P<out_fmt>\S+) csp:(?P<out_csp>\S+) "
    r"range:(?P<out_range>\S+) sar:\S+(?: flags:(?P<flags>0x[0-9A-Fa-f]+))?")

_GRAPH_INPUT = re.compile(
    r"^\[graph \d+ input from stream (?P<file>\d+):(?P<stream>\d+) @ [^\]]*\] "
    r".*\bcsp:(?P<csp>\S+) range:(?P<range>\S+)")

_COLOR_LINE = re.compile(
    r"^(?:\[(?:Parsed_scale|auto_scale)_\d+ @ [^\]]*\] w:\d+ .* -> "   # scale: input -> output
    r"|\[graph \d+ input from stream \d+:\d+ @ [^\]]*\] .*\bcsp:"       # a filter graph input
    r"|\[swscaler @ [^\]]*\] "                                          # swscale's notes
    r"|\[Parsed_libvmaf_\d+ @ [^\]]*\] .*different color ranges"       # libvmaf: ranges differ
    r"|(?:\[[^\]]*\] )*Impossible to convert between the formats "     # no graph could be built
    r"|\s*Stream #\d+:\d+.*: Video: )")                                  # input / output streams

# libswscale's scaler algorithms (enum SwsFlags, swscale.h), by the names the
# scale filter takes in flags=. A scale line's flags hold one of them.
SWS_ALGORITHMS = {
    "fast_bilinear": 0x1, "bilinear": 0x2, "bicubic": 0x4, "experimental": 0x8,
    "neighbor": 0x10, "area": 0x20, "bicublin": 0x40, "gauss": 0x80,
    "sinc": 0x100, "lanczos": 0x200, "spline": 0x400,
}
_SWS_ALGORITHM_MASK = 0x7FF


def colorspace_lines(text):
    """The lines of an FFmpeg verbose log that state color spaces: the input
    and output video streams, the inputs of the filter graph, each scale
    filter's input -> output, swscale's notes, the libvmaf filter's warning
    that its two inputs' frames have different color ranges, and FFmpeg's
    error when no conversion between two filters' formats exists."""
    return [line for line in text.splitlines() if _COLOR_LINE.match(line)]


def scale_lines(text):
    """The scale filters' input -> output lines of an FFmpeg verbose log, parsed."""
    out = []
    for line in text.splitlines():
        m = _SCALE.match(line)
        if not m:
            continue
        g = m.groupdict()
        side = lambda p: {"width": int(g[p + "_w"]), "height": int(g[p + "_h"]),  # noqa: E731
                          "pix_fmt": g[p + "_fmt"], "color_space": g[p + "_csp"],
                          "color_range": g[p + "_range"]}
        out.append({"filter": g["filter"], "in": side("in"), "out": side("out"),
                    "flags": g["flags"]})
    return out


def graph_inputs(text):
    """The color tags of the filter graph's inputs in an FFmpeg verbose log:
    [{"input", "stream", "color_space", "color_range"}], input 0 being the
    distorted video and input 1 the reference."""
    out = []
    for line in text.splitlines():
        m = _GRAPH_INPUT.match(line)
        if m:
            out.append({"input": int(m["file"]), "stream": int(m["stream"]),
                        "color_space": m["csp"], "color_range": m["range"]})
    return out


def conversions(scales):
    """What the scale filters convert besides the size, once each:
    ["Parsed_scale_1: color_space smpte170m -> bt709"]."""
    notes = []
    for s in scales:
        for key in ("color_space", "color_range", "pix_fmt"):
            if s["in"][key] != s["out"][key]:
                note = f"{s['filter']}: {key} {s['in'][key]} -> {s['out'][key]}"
                if note not in notes:
                    notes.append(note)
    return notes


def _tags(side):
    return f"{_tag(side.get('color_space'))}/{_tag(side.get('color_range'))}"


def scale_conversions(scales):
    """The distorted input's scale filter's tags in -> out, once each and
    sorted: ["smpte170m/tv -> bt709/tv"]."""
    return sorted({f"{_tags(s['in'])} -> {_tags(s['out'])}" for s in scales
                   if s["filter"].startswith("Parsed_scale")})


def inserted_conversions(scales):
    """The same for the scale filters FFmpeg inserts itself (auto_scale_N),
    which, in a graph FFmpeg builds, convert a reference that isn't yuv420p."""
    return sorted({f"{_tags(s['in'])} -> {_tags(s['out'])}" for s in scales
                   if s["filter"].startswith("auto_scale")})


def mirage_conversions(conv):
    """The same for the dis_conversion or ref_conversion of a mirage_vmaf
    report (src/VMAFReport.h)."""
    return sorted({f"{_tags(c.get('from') or {})} -> {_tags(c.get('to') or {})}"
                   for c in conv if isinstance(c, dict)})


# A library line of `ffmpeg -version`: the version the program was built
# with, then the one it runs with.
_LIBRARY_LINE = re.compile(r"^(lib\w+)\s+\d+\.\s*\d+\.\s*\d+ / \s*(\d+)\.\s*(\d+)\.\s*(\d+)\s*$",
                           re.M)


def library_versions(version_text):
    """The run-time version of each library in the output of `ffmpeg
    -version`: {"libavutil": "60.26.101", ...}."""
    return {m[1]: f"{int(m[2])}.{int(m[3])}.{int(m[4])}"
            for m in _LIBRARY_LINE.finditer(version_text)}


# ── Pairing by timestamp (D-028) ─────────────────────────────────────────────

# libavutil's AV_TIME_BASE: microseconds, the unit of ffmpeg's input starts.
AV_TIME_BASE = 1000000

# How many of the pairs only one side scored, and of the frames the two sides
# treat differently, a pair's delta.json lists; all of them are counted.
LISTED = 1000

# Not anchored at a line's start: ffmpeg's threads share the log, and a
# message can follow another thread's line before it ends.
_SHOWINFO = re.compile(r"\[Parsed_showinfo_(\d+) @ [^\]]*\] (?:config in time_base: (\d+)/(\d+),"
                       r"|n: *(\d+) pts: *(NOPTS|-?\d+) )")
_INPUT_FILE = re.compile(r"^Input #(\d+), ")
_INPUT_START = re.compile(r"^  Duration: .*?, start: (-?)(\d+)\.(\d{6})")
_TS_CORRECTION = re.compile(r"\] timestamp discontinuity ")

INPUTS = ("reference", "distorted")


def showinfo_log(text):
    """What each showinfo filter of an FFmpeg verbose log passed on:
    {index: {"time_base": Fraction or None, "configs": n, "frames": [pts]}}
    by the filter's index in the parsed graph (Parsed_showinfo_N), its link's
    time base, how often it was configured, and the pts of its frames in
    their order, None for a frame without one. A filter configured again, as
    ffmpeg does when it rebuilds its filter graph, numbers its frames from 0
    again. Raises ValueError when a filter's frames aren't numbered 0, 1,
    2, ... from its configuration on, as it logged them."""
    out, counted = {}, {}
    for m in _SHOWINFO.finditer(text):
        k = int(m[1])
        f = out.setdefault(k, {"time_base": None, "configs": 0, "frames": []})
        if m[2] is not None:
            f["time_base"] = fractions.Fraction(int(m[2]), int(m[3])) \
                if int(m[2]) and int(m[3]) else None
            f["configs"] += 1
            counted[k] = 0
            continue
        if int(m[4]) != counted.get(k, 0):
            raise ValueError(f"Parsed_showinfo_{k} logs frame n:{m[4]} after "
                             f"{counted.get(k, 0)} frames")
        counted[k] = int(m[4]) + 1
        f["frames"].append(None if m[5] == "NOPTS" else int(m[5]))
    return out


def input_starts(text):
    """Each input file's start as FFmpeg's verbose log states it, in
    microseconds: {input number: start, or None when it has none}, from the
    "Duration: ..., start: ..." line after each "Input #N" line. Without
    -copyts ffmpeg measures each input's timestamps from it
    (fftools/ffmpeg_demux.c, ts_offset)."""
    out, current = {}, None
    for line in text.splitlines():
        m = _INPUT_FILE.match(line)
        if m:
            current = int(m[1])
        elif current is not None and line.startswith("  Duration: "):
            m = _INPUT_START.match(line)
            out[current] = None if not m else \
                (-1 if m[1] else 1) * (int(m[2]) * AV_TIME_BASE + int(m[3]))
            current = None
    return out


def rescale(value, src, dst):
    """av_rescale_q(value, src, dst): `value` in time base `src` as a whole
    number of ticks of `dst`, the nearest one, halves away from zero."""
    x = value * fractions.Fraction(src) / fractions.Fraction(dst)
    n = math.floor(abs(x) + fractions.Fraction(1, 2))
    return n if x >= 0 else -n


def framesync_time_base(a, b):
    """The time base in which FFmpeg's framesync compares two inputs'
    timestamps: av_gcd_q(a, b, AV_TIME_BASE / 2, AV_TIME_BASE_Q)
    (libavfilter/framesync.c, ff_framesync_configure)."""
    lcm = a.denominator // math.gcd(a.denominator, b.denominator) * b.denominator
    if lcm < AV_TIME_BASE // 2:
        return fractions.Fraction(math.gcd(a.numerator, b.numerator), lcm)
    return fractions.Fraction(1, AV_TIME_BASE)


def framesync_events(dis, ref):
    """The events of FFmpeg's libvmaf filter, which syncs its two inputs with
    framesync's dual-input mode under its defaults (libavfilter/framesync.c
    and vf_libvmaf.c of FFmpeg 8.1.1; `ffmpeg -h filter=libvmaf`). `dis`,
    the main input, and `ref` are the times of each input's frames in the
    order it passes them on, in framesync's time base, None for a frame
    without a timestamp, which framesync takes as the lowest time
    (AV_NOPTS_VALUE).

    In time order, both inputs' frames become their input's current frame,
    those of equal time together. While the distorted input lasts, each of
    its frames makes an event with the current reference frame: none before
    the reference's first frame, and its last one once it has ended
    (eof_action=repeat). Once the distorted input has ended, each later
    reference frame makes an event with the last distorted frame. Returns
    (distorted position, reference position or None, time, whether a
    reference frame made the event)."""
    low = -math.inf
    i = j = 0
    cur_d = cur_r = None
    events = []
    while i < len(dis) or j < len(ref):
        nd = (low if dis[i] is None else dis[i]) if i < len(dis) else math.inf
        nr = (low if ref[j] is None else ref[j]) if j < len(ref) else math.inf
        t = min(nd, nr)
        main = i < len(dis)   # framesync's sync level: the distorted input's frames lead
        made, by_reference = False, False
        if nr == t:
            cur_r, j = j, j + 1
            made = by_reference = not main
        if nd == t:
            cur_d, i = i, i + 1
            made, by_reference = True, False
        if made and cur_d is not None:
            events.append((cur_d, cur_r, t, by_reference))
    return events


def _seconds(ticks, tb):
    return None if ticks is None or tb is None else ticks * tb


def ffmpeg_pairing(text, scored):
    """FFmpeg's pairs, from its verbose log `text`: the three showinfo
    filters of filtergraph() log the frames of the distorted input's chain,
    the reference's and libvmaf's output, in that order of their indices.
    framesync_events() on the first two must give the frames of the third,
    and `scored`, the number of frames of FFmpeg's libvmaf log, must be the
    number of events with a reference frame: libvmaf scores those, in their
    order, and passes the others on unscored. Times are in seconds from each
    input's start, as ffmpeg measures them (without -copyts).

    Returns (pairing, None), the pairing {"time_base", "frames" (each
    input's frame times), "pairs" [{"frame", "ref", "dis", "ref_time",
    "dis_time", "by_reference"}], "unscored" (distorted positions),
    "corrections"}, or (None, why FFmpeg's pairs can't be established)."""
    try:
        filters = showinfo_log(text)
    except ValueError as e:
        return None, str(e)
    if len(filters) != 3:
        return None, (f"its log has the frames of {len(filters)} showinfo filters, not of the "
                      "3 its filter graph has")
    dis_f, ref_f, out_f = (filters[k] for k in sorted(filters))
    if any(f["configs"] != 1 for f in (dis_f, ref_f, out_f)):
        return None, "its filter graph was configured more than once"
    if any(f["time_base"] is None for f in (dis_f, ref_f, out_f)):
        return None, "its log doesn't give the time base of every showinfo filter"
    tb = framesync_time_base(dis_f["time_base"], ref_f["time_base"])

    def common(f):
        return [None if p is None else rescale(p, f["time_base"], tb) for p in f["frames"]]
    events = framesync_events(common(dis_f), common(ref_f))
    expected = [None if t == -math.inf else rescale(t, tb, out_f["time_base"])
                for _, _, t, _ in events]
    if expected != out_f["frames"]:
        return None, (f"the {len(events)} events its libvmaf filter's framesync makes of the "
                      f"{len(dis_f['frames'])} distorted and {len(ref_f['frames'])} reference "
                      f"frames its log shows are not the {len(out_f['frames'])} frames the "
                      "filter passed on")
    pairs, unscored = [], []
    for i, j, _, by_reference in events:
        if j is None:
            unscored.append(i)
            continue
        pairs.append({"frame": len(pairs), "ref": j, "dis": i,
                      "ref_time": _seconds(ref_f["frames"][j], ref_f["time_base"]),
                      "dis_time": _seconds(dis_f["frames"][i], dis_f["time_base"]),
                      "by_reference": by_reference})
    if len(pairs) != scored:
        return None, (f"its libvmaf log has {scored} frames, but {len(pairs)} of the events of "
                      "its libvmaf filter's framesync have a reference frame")
    return {"time_base": {"reference": ref_f["time_base"], "distorted": dis_f["time_base"],
                          "framesync": tb},
            "frames": {"reference": [_seconds(p, ref_f["time_base"]) for p in ref_f["frames"]],
                       "distorted": [_seconds(p, dis_f["time_base"]) for p in dis_f["frames"]]},
            "pairs": pairs, "unscored": unscored,
            "corrections": len(_TS_CORRECTION.findall(text))}, None


def _time_base(text):
    """A time base as mirage_vmaf's report writes it ("1/90000"), or None."""
    m = re.fullmatch(r"(\d+)/(\d+)", text or "")
    return fractions.Fraction(int(m[1]), int(m[2])) if m and int(m[1]) and int(m[2]) else None


def _ticks(seconds, tb):
    """A time mirage_vmaf wrote in seconds (ts * time base, P-006) as the
    whole number of ticks of `tb` it came from."""
    return round(fractions.Fraction(seconds) / tb)


def mirage_pairing(report, starts=None):
    """mirage_vmaf's pairs, with their frames' positions in their inputs
    (ref_index, dis_index; a report without them, from a mirage_vmaf before
    D-028, paired the inputs' frames in decode order, so the pair's index
    for both) and their times in seconds from their inputs' starts: the
    starts of the report's alignment, or, for a report without one,
    `starts` ({"reference": µs, "distorted": µs}: FFmpeg's input starts,
    which ffmpeg measures each input's times from, as mirage_vmaf does
    since D-028), rescaled to each input's time base as ffmpeg rescales them.

    Returns {"pairs": [{"index", "ref", "dis", "ref_time", "dis_time"}],
    "start": {input: seconds or None}, "start_from": {input: where from},
    "failures": [{"input", "index", "time", "reason"}], "counts_failures"}."""
    alignment = report.get("alignment") if isinstance(report.get("alignment"), dict) else None
    tbs, start_ticks, start_from = {}, {}, {}
    for name, key in zip(INPUTS, ("ref", "dis")):
        tb = _time_base((report.get(key) or {}).get("time_base"))
        tbs[name] = tb
        if alignment is not None:
            s = (alignment.get(name) or {}).get("start")
            start_ticks[name] = _ticks(s, tb) if is_number(s) and tb else None
            start_from[name] = ("the report's alignment, start_from "
                                f"{(alignment.get(name) or {}).get('start_from')}")
        else:
            s = (starts or {}).get(name)
            start_ticks[name] = rescale(s, fractions.Fraction(1, AV_TIME_BASE), tb) \
                if s is not None and tb else None
            start_from[name] = "FFmpeg's log, for a report without an alignment"

    def rel(seconds, name):
        tb, s = tbs[name], start_ticks[name]
        if not is_number(seconds) or tb is None or s is None:
            return None
        return (_ticks(seconds, tb) - s) * tb
    pairs = []
    frames = report.get("frames") if isinstance(report.get("frames"), list) else []
    for k, f in enumerate(frames):
        f = f if isinstance(f, dict) else {}
        ref, dis = f.get("ref_index", k), f.get("dis_index", k)
        pairs.append({"index": f.get("index", k), "ref": ref, "dis": dis,
                      "ref_time": rel(f.get("ref_pts"), "reference"),
                      "dis_time": rel(f.get("dis_pts"), "distorted")})
    failures = []
    for f in (alignment or {}).get("failures") or []:
        if isinstance(f, dict) and f.get("input") in INPUTS:
            failures.append({"input": f["input"], "index": f.get("index"),
                             "time": rel(f.get("pts"), f["input"]), "reason": f.get("reason")})
    return {"pairs": pairs, "failures": failures, "counts_failures": alignment is not None,
            "start": {n: _seconds(start_ticks[n], tbs[n]) for n in INPUTS},
            "start_from": start_from}


def _partners(pairs):
    """{(input, position): [the positions of the other input's frames it is paired with]}"""
    out = {}
    for p in pairs:
        out.setdefault(("reference", p["ref"]), []).append(p["dis"])
        out.setdefault(("distorted", p["dis"]), []).append(p["ref"])
    return out


def _apart(p):
    if p["ref_time"] is None or p["dis_time"] is None:
        return None
    return abs(p["ref_time"] - p["dis_time"])


def _treatment(other, partners, reason=None, unscored=False, known=True, counts=False):
    """What one side does with a frame, in words; `counts`: the side counts
    failures, so a frame in no pair that it doesn't count isn't one."""
    if reason:
        return f"failed: {reason}"
    if partners:
        text = f"paired with {other} " + ", ".join(str(p) for p in partners)
        return text + (f" (in {len(partners)} pairs)" if len(partners) > 1 else "")
    if unscored:
        return "passed on unscored: no reference frame yet"
    if not known:
        return "not among its frames"
    return "in no pair, not a failure" if counts else "in no pair"


def compare_pairings(mine, theirs, tolerance=None):
    """mirage_vmaf's pairing (mirage_pairing()) against FFmpeg's
    (ffmpeg_pairing()). Two pairs are the same pair when their frames have
    the same positions in the inputs, each pair matched once, in Mirage's
    order; a matched pair whose frames' times differ between the sides is
    listed. Beyond the pairs, each frame that one side pairs with other
    frames than the other side does, or that mirage_vmaf counts as failed,
    which FFmpeg never does, is listed with what each side does with it.
    `tolerance` (seconds, or None) counts each side's pairs whose two frames
    are further apart than it."""
    m_pairs, f_pairs = mine["pairs"], theirs["pairs"]
    by_frames = {}
    for k, p in enumerate(f_pairs):
        by_frames.setdefault((p["ref"], p["dis"]), []).append(k)
    matched, only_m = [], []
    for i, p in enumerate(m_pairs):
        ks = by_frames.get((p["ref"], p["dis"]))
        if ks:
            matched.append((i, ks.pop(0)))
        else:
            only_m.append(i)
    used = {k for _, k in matched}
    only_f = [k for k in range(len(f_pairs)) if k not in used]
    other_times = [(i, k) for i, k in matched
                   if (m_pairs[i]["ref_time"], m_pairs[i]["dis_time"])
                   != (f_pairs[k]["ref_time"], f_pairs[k]["dis_time"])]

    m_part, f_part = _partners(m_pairs), _partners(f_pairs)
    failed = {(f["input"], f["index"]): f for f in mine["failures"]}
    unscored = {("distorted", i) for i in theirs["unscored"]}
    known = {(n, i) for n in INPUTS for i in range(len(theirs["frames"][n]))}
    frames = []
    for key in sorted(set(m_part) | set(f_part) | set(failed) | unscored,
                      key=lambda x: (INPUTS.index(x[0]), x[1] if isinstance(x[1], int) else -1)):
        mp, fp = sorted(m_part.get(key, [])), sorted(f_part.get(key, []))
        fail = failed.get(key)
        if mp == fp and not fail:
            continue
        name, pos = key
        other = INPUTS[1 - INPUTS.index(name)]
        time = theirs["frames"][name][pos] if key in known else (fail or {}).get("time")
        frames.append({"input": name, "index": pos, "time": time,
                       "mirage": _treatment(other, mp, (fail or {}).get("reason"),
                                            counts=mine.get("counts_failures", False)),
                       "ffmpeg": _treatment(other, fp, unscored=key in unscored,
                                            known=key in known)})

    def side(pairs, partners):
        """What one side did with the frames, in counts."""
        out = {"pairs": len(pairs)}
        for name in INPUTS:
            out[f"{name}_in_several_pairs"] = sum(1 for (n, _), ps in partners.items()
                                                  if n == name and len(ps) > 1)
        out["pairs_beyond_tolerance"] = None if tolerance is None else \
            sum(1 for p in pairs if _apart(p) is not None and _apart(p) > tolerance)
        return out
    f_side = side(f_pairs, f_part)
    for name in INPUTS:
        f_side[f"{name}_in_no_pair"] = sum(1 for i in range(len(theirs["frames"][name]))
                                           if (name, i) not in f_part)
    f_side["unscored"] = len(theirs["unscored"])
    f_side["by_reference"] = sum(1 for p in f_pairs if p["by_reference"])
    return {
        "matched": matched,
        "both": len(matched),
        "only_mirage": [{k: m_pairs[i][k] for k in ("index", "ref", "dis", "ref_time", "dis_time")}
                        for i in only_m],
        "only_ffmpeg": [{k: f_pairs[j][k] for k in ("frame", "ref", "dis", "ref_time", "dis_time",
                                                    "by_reference")} for j in only_f],
        "other_times": [{"index": m_pairs[i]["index"], "frame": f_pairs[k]["frame"],
                         "ref": m_pairs[i]["ref"], "dis": m_pairs[i]["dis"],
                         "mirage": {"ref_time": m_pairs[i]["ref_time"],
                                    "dis_time": m_pairs[i]["dis_time"]},
                         "ffmpeg": {"ref_time": f_pairs[k]["ref_time"],
                                    "dis_time": f_pairs[k]["dis_time"]}}
                        for i, k in other_times],
        "frames": frames,
        "mirage": side(m_pairs, m_part),
        "ffmpeg": f_side,
    }


def _float(v):
    return None if v is None else float(v)


def _listed(entries, limit=LISTED):
    """Entries for delta.json, with their times as floats, at most `limit`."""
    out = []
    for e in entries[:limit]:
        out.append({k: (_float(v) if isinstance(v, fractions.Fraction) else
                        {kk: _float(vv) for kk, vv in v.items()} if isinstance(v, dict) else v)
                    for k, v in e.items()})
    return out


# ── One pair ─────────────────────────────────────────────────────────────────

def _run_problem(name, run, result, error):
    """Why a side's run gave nothing to compare, or None."""
    if run is None:
        return f"{name} was not run" + (f": {error}" if error else "")
    if run.get("error"):
        return f"{name} could not be started: {run['error']}"
    if run.get("timed_out"):
        return f"{name} was stopped after {run.get('timeout_s')} s"
    if run.get("exit_code") != 0:
        return f"{name} exited with status {run.get('exit_code')}"
    if result is None:
        return f"{name} left no usable output" + (f": {error}" if error else "")
    return None


def _tag(v):
    return "unknown" if v in (None, "") else v


# mirage_vmaf's input fields and ffprobe's names for them.
_INPUT_FIELDS = (("width", "width"), ("height", "height"), ("pix_fmt", "pix_fmt"),
                 ("color_space", "color_space"), ("color_range", "color_range"),
                 ("color_primaries", "color_primaries"), ("color_trc", "color_transfer"),
                 ("time_base", "time_base"))


def _n(count, word, plural=None):
    """"1 frame", "2 frames"."""
    return f"{count} {word if count == 1 else plural or word + 's'}"


def _rate(text):
    """A frame rate as "num/den" (mirage_vmaf's alignment, ffprobe), or None."""
    m = re.fullmatch(r"(\d+)/(\d+)", text or "")
    return fractions.Fraction(int(m[1]), int(m[2])) if m and int(m[1]) and int(m[2]) else None


def _tolerance(rep, ref_probe):
    """Half the reference's frame duration, in seconds as a Fraction, the
    tolerance of D-028's rule A: from the reference's frame rate in
    mirage_vmaf's alignment, else, for a report from before D-028, from
    ffprobe's r_frame_rate of the reference. (tolerance or None, its source)."""
    a = rep.get("alignment") if isinstance(rep.get("alignment"), dict) else {}
    rate = _rate((a.get("reference") or {}).get("frame_rate"))
    if rate:
        return 1 / (2 * rate), "the reference's frame rate in mirage_vmaf's alignment"
    rate = _rate((ref_probe or {}).get("r_frame_rate"))
    if rate:
        return 1 / (2 * rate), "ffprobe's r_frame_rate of the reference"
    return None, None


# The counts of mirage_vmaf's alignment (src/VMAFReport.h) a delta report repeats.
MIRAGE_ALIGNMENT = ("failed", "failure_rate", "threshold", "exceeded", "unmatched_reference",
                    "extra_distorted", "duplicate_distorted", "no_timestamp",
                    "after_discontinuity", "discontinuities", "off_grid_reference",
                    "off_grid_distorted")

# Why mirage_vmaf counts a frame as failed, as its alignment names it.
FAILURE_REASONS = ("unmatched_reference", "extra_distorted", "duplicate_distorted",
                   "no_timestamp", "after_discontinuity")

FFMPEG_RULE = ("framesync: each distorted frame with the reference frame at or before its "
               "time; the last frame of an input that has ended repeated")


def _alignment_report(rep, mine, theirs, cmp, starts, tolerance, tolerance_from):
    """The "alignment" of a pair's delta report: both sides' pairing in
    counts, and the pairs and frames in which they differ."""
    a = rep.get("alignment") if isinstance(rep.get("alignment"), dict) else None
    m_side = dict(cmp["mirage"])
    if a is not None:
        m_side.update(rule=a.get("rule"), grid=a.get("grid"),
                      **{k: a.get(k) for k in MIRAGE_ALIGNMENT},
                      vmaf_contexts=len(a.get("vmaf_contexts") or []))
        for name, off in (("reference", "off_grid_reference"), ("distorted", "off_grid_distorted")):
            failed = sum(1 for x in mine["failures"] if x["input"] == name)
            m_side[f"{name}_in_no_pair"] = failed + (a.get(off) or 0)
    else:
        m_side.update(rule="decode order (a mirage_vmaf from before D-028, whose report has "
                           "no alignment)", reference_in_no_pair=None, distorted_in_no_pair=None)
    m_side.update(start={n: _float(v) for n, v in mine["start"].items()},
                  start_from=mine["start_from"])
    f_side = dict(cmp["ffmpeg"])
    f_side.update(rule=FFMPEG_RULE,
                  start={"reference": None if starts.get(1) is None else starts[1] / AV_TIME_BASE,
                         "distorted": None if starts.get(0) is None else starts[0] / AV_TIME_BASE},
                  time_base={k: f"{v.numerator}/{v.denominator}"
                             for k, v in theirs["time_base"].items()},
                  frames={n: len(theirs["frames"][n]) for n in INPUTS},
                  corrections=theirs["corrections"])
    return {
        "compared_by": "the same reference and distorted frames (their positions in the inputs), "
                       "at the same timestamps (seconds from each input's start)",
        "tolerance": _float(tolerance), "tolerance_from": tolerance_from,
        "mirage": m_side, "ffmpeg": f_side,
        "pairs": {"both": cmp["both"], "only_mirage": len(cmp["only_mirage"]),
                  "only_ffmpeg": len(cmp["only_ffmpeg"]), "other_times": len(cmp["other_times"])},
        "only_mirage": _listed(cmp["only_mirage"]),
        "only_ffmpeg": _listed(cmp["only_ffmpeg"]),
        "other_times": _listed(cmp["other_times"]),
        "frames_treated_differently": len(cmp["frames"]),
        "frames": _listed(cmp["frames"]),
    }


def evaluate_pair(pair, params, sides):
    """The delta report of one pair, ready for json.dumps.

    pair    Pair
    params  {"variant", "frames" (None = every frame), "threshold", "model",
             "scaler", "threads", "model_file"} where "model" is the built-in
            model's version, or None when "model_file" ({"path", "sha256"},
            else None or absent) names a model file
    sides   {"probe": {"ref": info or None, "dis": info or None, "errors": [...]},
             "relabel": setparams filter or None, "relabel_warnings": [...],
             "mirage": {"run", "report", "error"},
             "ffmpeg": {"run", "log", "error", "stderr", "libvmaf_package_version",
                        "ffmpeg_libraries"}}
            where a run is {"exit_code", "timed_out", "timeout_s", "error",
            "elapsed_s", ...} or None if the side was not run, report and log
            are the parsed JSON outputs (None if missing), stderr is FFmpeg's
            verbose log, libvmaf_package_version that of the libvmaf FFmpeg
            links, and ffmpeg_libraries the run-time versions of FFmpeg's
            libraries (library_versions(); both from environment.txt).

    The pair passes only if both sides ran to completion, mirage_vmaf's
    settings are the ones FFmpeg was given, both scored the same frame pairs
    at the same timestamps (more than none; compare_pairings()), and no
    frame has |delta| > threshold. What each side counted as failed, or
    did differently with a frame, is reported in "alignment", and warned
    about; it doesn't fail the pair unless the pairs differ.
    """
    fail, warn = [], []
    probe = sides.get("probe") or {}
    ref_probe, dis_probe = probe.get("ref"), probe.get("dis")
    fail.extend(probe.get("errors") or [])
    warn.extend(sides.get("relabel_warnings") or [])

    m_side, f_side = sides.get("mirage") or {}, sides.get("ffmpeg") or {}
    rep, log = m_side.get("report"), f_side.get("log")
    for name, side, result in (("mirage_vmaf", m_side, rep), ("FFmpeg", f_side, log)):
        problem = _run_problem(name, side.get("run"), result, side.get("error"))
        if problem:
            fail.append(problem)
    if (m_side.get("run") or {}).get("exit_code") != 0:
        rep = None   # an incomplete run has no report (P-006); ignore a stale one
    if (f_side.get("run") or {}).get("exit_code") != 0:
        log = None

    # mirage_vmaf's settings against FFmpeg's.
    mirage_info = None
    model_file = params.get("model_file")
    if rep is not None:
        wanted = [("scaler", params["scaler"]), ("n_threads", params["threads"]),
                  ("n_subsample", 1), ("frame_limit", params["frames"])]
        if model_file:
            wanted += [("model_source", "file"), ("model_sha256", model_file["sha256"])]
        else:
            wanted.insert(0, ("model", params["model"]))
            if "model_source" in rep:   # a mirage_vmaf that has the model options
                wanted.insert(1, ("model_source", "built-in"))
        for key, want in wanted:
            if rep.get(key) != want:
                fail.append(f"SETTINGS MISMATCH: mirage_vmaf's report has {key} "
                            f"{rep.get(key)!r}, FFmpeg's run {want!r}")
        scored = rep.get("scored_as") or {}
        if scored.get("pix_fmt") != "yuv420p" or scored.get("bit_depth") != 8:
            fail.append(f"SETTINGS MISMATCH: mirage_vmaf scored {scored.get('pix_fmt')!r} at "
                        f"{scored.get('bit_depth')!r} bits, FFmpeg 'yuv420p' at 8 bits")
        if ref_probe and (scored.get("width"), scored.get("height")) != \
                (ref_probe["width"], ref_probe["height"]):
            fail.append(f"SETTINGS MISMATCH: mirage_vmaf scored at {scored.get('width')}x"
                        f"{scored.get('height')}, FFmpeg at {ref_probe['width']}x"
                        f"{ref_probe['height']}")
        frames = rep.get("frames") if isinstance(rep.get("frames"), list) else []
        if rep.get("frames_scored") != len(frames):
            fail.append(f"mirage_vmaf's report gives frames_scored {rep.get('frames_scored')!r} "
                        f"but lists {len(frames)} frames")
        for side, info in (("ref", ref_probe), ("dis", dis_probe)):
            mine = rep.get(side) or {}
            for mk, pk in _INPUT_FIELDS:
                if info is not None and _tag(mine.get(mk)) != _tag(info.get(pk)):
                    warn.append(f"{side}: mirage_vmaf's decoder has {mk} {mine.get(mk)!r}, "
                                f"ffprobe {pk} {info.get(pk)!r}")
        timed = [f for f in frames if isinstance(f, dict)
                 and is_number(f.get("ref_pts")) and is_number(f.get("dis_pts"))]
        # Since D-028 the pairs are compared with FFmpeg's by their frames'
        # timestamps ("alignment" below); this count is of the PTS as written.
        differ = sum(1 for f in timed if f["ref_pts"] != f["dis_pts"])
        if len(timed) != len(frames):
            warn.append(f"{len(frames) - len(timed)} of mirage_vmaf's pairs have no PTS "
                        "for one of their frames")
        # What mirage_vmaf itself knows can make its scores differ from FFmpeg's.
        own = rep.get("warnings") if isinstance(rep.get("warnings"), list) else []
        warn.extend(f"mirage_vmaf: {w}" for w in own)
        mirage_info = {
            "exit_code": m_side["run"]["exit_code"],
            "elapsed_s": m_side["run"].get("elapsed_s"),
            **{k: rep.get(k) for k in ("tool", "reference_mode", "libvmaf_version",
                                       "libvmaf_package_version", "ffmpeg_libraries", "model",
                                       "model_source", "model_sha256", "scaler", "n_threads",
                                       "n_subsample", "scored_as", "frame_limit",
                                       "frames_scored")},
            "ref_ended": (rep.get("ref") or {}).get("ended"),
            "dis_ended": (rep.get("dis") or {}).get("ended"),
            "first_ref_pts": float(timed[0]["ref_pts"]) if timed else None,
            "last_ref_pts": float(timed[-1]["ref_pts"]) if timed else None,
            "pairs_with_different_pts": differ,
            "warnings": own,
        }

    # FFmpeg's log: libvmaf's version and how the scale filter converted.
    stderr = f_side.get("stderr") or ""
    scales = scale_lines(stderr)
    inputs = graph_inputs(stderr)
    ffmpeg_info = None
    if log is not None:
        ffmpeg_info = {"exit_code": f_side["run"]["exit_code"],
                       "elapsed_s": f_side["run"].get("elapsed_s"),
                       "libvmaf_version": log.get("version"),
                       "libvmaf_package_version": f_side.get("libvmaf_package_version"),
                       "ffmpeg_libraries": f_side.get("ffmpeg_libraries")}
        if rep is not None and log.get("version") != rep.get("libvmaf_version"):
            warn.append(f"libvmaf versions differ: mirage_vmaf {rep.get('libvmaf_version')!r}, "
                        f"FFmpeg {log.get('version')!r}")
        # D-021: the FFmpeg libraries both sides ran with, where both name them.
        mine_libs = rep.get("ffmpeg_libraries") if rep is not None else None
        theirs_libs = f_side.get("ffmpeg_libraries")
        if isinstance(mine_libs, dict) and isinstance(theirs_libs, dict):
            for name, version in mine_libs.items():
                if name in theirs_libs and theirs_libs[name] != version:
                    warn.append(f"FFmpeg library versions differ: {name} mirage_vmaf "
                                f"{version!r}, FFmpeg {theirs_libs[name]!r}")
        dis_scales = [s for s in scales if s["filter"].startswith("Parsed_scale")]
        if not dis_scales:
            warn.append("FFmpeg's log has no line in which the distorted input's scale "
                        "filter states its input and output")
        for s in dis_scales:
            if ref_probe and (s["out"]["width"], s["out"]["height"]) != \
                    (ref_probe["width"], ref_probe["height"]):
                warn.append(f"{s['filter']} scales to {s['out']['width']}x{s['out']['height']}")
            want = SWS_ALGORITHMS.get(params["scaler"])
            if want is not None and s["flags"] and \
                    int(s["flags"], 16) & _SWS_ALGORITHM_MASK != want:
                warn.append(f"{s['filter']} has flags {s['flags']}, not those of "
                            f"{params['scaler']!r}")
        for g in inputs:
            info = dis_probe if g["input"] == 0 else ref_probe
            if info is not None and (_tag(g["color_space"]), _tag(g["color_range"])) != \
                    (_tag(info.get("color_space")), _tag(info.get("color_range"))):
                warn.append(f"FFmpeg's filter graph input {g['input']} has csp "
                            f"{g['color_space']} range {g['color_range']}, ffprobe "
                            f"{info.get('color_space')} {info.get('color_range')}")

    # How each side converted the distorted frames' color (D-016), and the
    # reference's (D-022). Only a mirage_vmaf that converts records
    # dis_conversion, and only one that converts the reference ref_conversion.
    mirage_conv = rep.get("dis_conversion") if rep is not None else None
    if not isinstance(mirage_conv, list):
        mirage_conv = None
    if mirage_conv is not None and log is not None and scale_conversions(scales):
        mine, theirs = mirage_conversions(mirage_conv), scale_conversions(scales)
        if mine != theirs:
            warn.append("the distorted frames' color conversions differ: mirage_vmaf "
                        f"{', '.join(mine) or 'none'}, FFmpeg's scale filter "
                        f"{', '.join(theirs)}")
    mirage_ref_conv = rep.get("ref_conversion") if rep is not None else None
    if not isinstance(mirage_ref_conv, list):
        mirage_ref_conv = None
    if mirage_ref_conv is not None and log is not None:
        mine, theirs = mirage_conversions(mirage_ref_conv), inserted_conversions(scales)
        if mine != theirs:
            warn.append("the reference frames' color conversions differ: mirage_vmaf "
                        f"{', '.join(mine) or 'none'}, FFmpeg's inserted scale filter "
                        f"{', '.join(theirs) or 'none'}")

    # The scores, of the pairs both sides scored (D-028).
    delta, n_mirage, n_ffmpeg, pooled, alignment = None, None, None, None, None
    if rep is not None and log is not None:
        try:
            m, f = mirage_scores(rep), ffmpeg_scores(log)
        except ValueError as e:
            fail.append(str(e))
        else:
            n_mirage, n_ffmpeg = len(m), len(f)
            theirs, why = ffmpeg_pairing(stderr, n_ffmpeg)
            if theirs is None:
                fail.append(f"FFmpeg's pairs could not be established from its log: {why}; "
                            "without them the two sides' scores are not compared")
            else:
                starts = input_starts(stderr)   # input 0 is the distorted video
                mine = mirage_pairing(rep, {"reference": starts.get(1),
                                            "distorted": starts.get(0)})
                tolerance, tolerance_from = _tolerance(rep, ref_probe)
                cmp = compare_pairings(mine, theirs, tolerance)
                matched = cmp.pop("matched")
                delta = compare_scores([m[i] for i, _ in matched], [f[k] for _, k in matched],
                                       params["threshold"],
                                       indices=[mine["pairs"][i]["index"] for i, _ in matched],
                                       ffmpeg_indices=[k for _, k in matched])
                alignment = _alignment_report(rep, mine, theirs, cmp, starts, tolerance,
                                              tolerance_from)
                if n_mirage != n_ffmpeg:
                    fail.insert(0, f"FRAME COUNT MISMATCH: Mirage scored {n_mirage} frame pairs, "
                                   f"FFmpeg {n_ffmpeg}; only the {delta['compared']} pairs both "
                                   "scored are compared")
                if cmp["only_mirage"] or cmp["only_ffmpeg"]:
                    fail.append(f"PAIRING MISMATCH: {len(cmp['only_mirage'])} of Mirage's "
                                f"{n_mirage} pairs are not among FFmpeg's, and "
                                f"{len(cmp['only_ffmpeg'])} of FFmpeg's {n_ffmpeg} not among "
                                "Mirage's (the same pair: the same reference and distorted "
                                "frames); only the pairs both scored are compared")
                if cmp["other_times"]:
                    fail.append(f"TIMESTAMP MISMATCH: {len(cmp['other_times'])} of the pairs both "
                                "scored have other timestamps on the two sides")
                if cmp["frames"]:
                    warn.append(f"the two sides' pairing treats {_n(len(cmp['frames']), 'frame')} "
                                "differently, Mirage's failures included (the pair's delta.md, "
                                "\"Pairing by timestamp\")")
                if theirs["corrections"]:
                    jumps = _n(theirs["corrections"], "timestamp discontinuity",
                               "timestamp discontinuities")
                    warn.append(f"FFmpeg corrected {jumps} of its inputs (its log), which "
                                "mirage_vmaf doesn't (D-028)")
                if delta["compared"] == 0:
                    fail.append("no frame pair to compare")
            if params["frames"] and min(n_mirage, n_ffmpeg) < params["frames"]:
                warn.append(f"fewer frame pairs than --frames {params['frames']}")
            if delta and delta["over_threshold"]:
                fail.append(f"{delta['over_threshold']} of {delta['compared']} frames have "
                            f"|delta| > {params['threshold']}")
            mp = rep.get("pooled") or {}
            fp = (log.get("pooled_metrics") or {}).get("vmaf") or {}
            pooled = {"mirage": {k: mp.get(k) if is_number(mp.get(k)) else None
                                 for k in POOLED},
                      "ffmpeg": {k: fp.get(k) if is_number(fp.get(k)) else None
                                 for k in POOLED}}
            pooled["difference"] = {
                k: (pooled["mirage"][k] - pooled["ffmpeg"][k])
                if pooled["mirage"][k] is not None and pooled["ffmpeg"][k] is not None else None
                for k in POOLED}

    return {
        "report_version": REPORT_VERSION,
        "pair": pair.label,
        "reference": pair.ref,
        "distorted": pair.dis,
        "variant": params["variant"],
        "status": "FAIL" if fail else "PASS",
        "fail_reasons": fail,
        "warnings": warn,
        "threshold": params["threshold"],
        "frame_limit": params["frames"],
        "frames": {"mirage": n_mirage, "ffmpeg": n_ffmpeg,
                   "compared": delta["compared"] if delta else 0,
                   "counts_match": n_mirage is not None and n_mirage == n_ffmpeg},
        "delta": delta,
        "alignment": alignment,
        "pooled": pooled,
        "relabel": sides.get("relabel"),
        "ffmpeg_color": {"graph_inputs": inputs, "scale": scales,
                         "conversions": conversions(scales)},
        "mirage_color": {"dis_conversion": mirage_conv, "ref_conversion": mirage_ref_conv},
        "inputs": {"ref": {"ffprobe": ref_probe, "mirage_vmaf": (rep or {}).get("ref")},
                   "dis": {"ffprobe": dis_probe, "mirage_vmaf": (rep or {}).get("dis")}},
        "mirage_vmaf": mirage_info,
        "ffmpeg": ffmpeg_info,
    }


def pair_summary(r):
    """The part of a pair's report that the overall report repeats."""
    d = r["delta"] or {}
    a = r.get("alignment")
    return {
        "pair": r["pair"], "status": r["status"], "fail_reasons": r["fail_reasons"],
        "warnings": r["warnings"], "frames": r["frames"],
        "pairing": None if not a else {
            **a["pairs"], "frames_treated_differently": a["frames_treated_differently"],
            "mirage_failed": a["mirage"].get("failed"),
            "mirage_failure_rate": a["mirage"].get("failure_rate")},
        "max_abs": d.get("max_abs"), "max_abs_index": d.get("max_abs_index"),
        "mean_abs": d.get("mean_abs"), "mean_signed": d.get("mean_signed"),
        "over_threshold": d.get("over_threshold"), "pooled": r["pooled"],
        "relabel": r["relabel"], "ffmpeg_conversions": r["ffmpeg_color"]["conversions"],
        "ffmpeg_distorted_csp": _distorted_csp(r),
        "mirage_distorted_csp": _mirage_csp(r),
        "ffmpeg_reference_csp": _distorted_csp(r, "auto_scale"),
        "mirage_reference_csp": _mirage_csp(r, "ref_conversion"),
    }


def overall_report(params, pair_reports, run):
    """The report of a whole run; `run` holds what the run recorded about
    itself (command, times, git state, environment, mirage_vmaf, files)."""
    failed = [r["pair"] for r in pair_reports if r["status"] != "PASS"]
    return {
        "report_version": REPORT_VERSION,
        "tool": "eval/consistency/run_delta.py",
        "variant": params["variant"],
        "status": "FAIL" if failed else "PASS",
        "pairs_total": len(pair_reports),
        "pairs_failed": failed,
        "parameters": params,
        **run,
        "pairs": [pair_summary(r) for r in pair_reports],
    }


def to_json(obj):
    """JSON text of a report: indented, numbers at full precision."""
    return json.dumps(obj, indent=2, allow_nan=False) + "\n"


# ── Markdown ─────────────────────────────────────────────────────────────────

def num(v, digits=6):
    """A number for the Markdown reports; "-" when there is none."""
    if v is None:
        return "-"
    if isinstance(v, bool):
        return "yes" if v else "no"
    if isinstance(v, int):
        return str(v)
    s = f"{v:.{digits}f}"
    return s[1:] if s.startswith("-") and not s.strip("-0.") else s   # no "-0.000000"


def cell(s):
    """Text for a Markdown table cell."""
    return str(s).replace("|", "\\|").replace("\n", " ")


def ranges_text(ranges, limit=MARKDOWN_RANGES):
    parts = [f"{a}" if a == b else f"{a}-{b}" for a, b in ranges[:limit]]
    if len(ranges) > limit:
        parts.append(f"... ({len(ranges) - limit} more runs; all of them in delta.json)")
    return ", ".join(parts)


def count_mismatch(frames):
    """Whether both sides scored, but not the same number of frame pairs."""
    return (frames["mirage"] is not None and frames["ffmpeg"] is not None
            and frames["mirage"] != frames["ffmpeg"])


def _frames_cell(frames):
    text = f"{num(frames['mirage'])} / {num(frames['ffmpeg'])}"
    return text + (" **MISMATCH**" if count_mismatch(frames) else "")


def _scale_text(s):
    i, o = s["in"], s["out"]
    return (f"{s['filter']}: {i['width']}x{i['height']} {i['pix_fmt']} csp:{i['color_space']} "
            f"range:{i['color_range']} -> {o['width']}x{o['height']} {o['pix_fmt']} "
            f"csp:{o['color_space']} range:{o['color_range']}"
            + (f" flags:{s['flags']}" if s["flags"] else ""))


def _distorted_csp(r, prefix="Parsed_scale"):
    """The distorted input's color space before and after FFmpeg's scale;
    with prefix "auto_scale", the reference's before and after the scale
    filter FFmpeg inserts."""
    scales = [s for s in r["ffmpeg_color"]["scale"] if s["filter"].startswith(prefix)]
    seen = []
    for s in scales:
        t = f"{s['in']['color_space']} -> {s['out']['color_space']}"
        if t not in seen:
            seen.append(t)
    return "; ".join(seen) if seen else "-"


def _mirage_csp(r, key="dis_conversion"):
    """The distorted frames' color space before and after mirage_vmaf's
    conversion; with key "ref_conversion", the reference's."""
    conv = (r.get("mirage_color") or {}).get(key)
    if conv is None:
        return "-"
    seen = []
    for c in conv:
        if not isinstance(c, dict):
            continue
        t = (f"{_tag((c.get('from') or {}).get('color_space'))} -> "
             f"{_tag((c.get('to') or {}).get('color_space'))}")
        if t not in seen:
            seen.append(t)
    return "; ".join(seen) if seen else "-"


def _conversion_line(c):
    """One dis_conversion entry of a mirage_vmaf report, for the Markdown report."""
    changed = [k for k, key in (("matrix", "matrix_changed"), ("range", "range_changed"))
               if c.get(key)]
    first, n = c.get("first_index"), c.get("pairs")
    return (f"{_tags(c.get('from') or {})} -> {_tags(c.get('to') or {})}: "
            + (" and ".join(changed) + " changed" if changed else "matrix and range unchanged")
            + f"; {num(n)} pairs from index {num(first)}")


def _model_text(m):
    """The model of a mirage_vmaf report (or its summary), for the Markdown reports."""
    if m.get("model_source") == "file":
        return f"model file {m.get('model')} (sha256 {m.get('model_sha256')})"
    return f"model {m.get('model')}" + (" (built-in)" if m.get("model_source") else "")


def _params_model_text(p):
    """The model FFmpeg was given, for the Markdown reports."""
    f = p.get("model_file")
    return f"model file {f['path']} (sha256 {f['sha256']})" if f else f"model {p['model']}"


def _more(shown, total, listed=LISTED):
    """The line after a list the Markdown report cuts short."""
    if total <= shown:
        return []
    where = "all of them" if total <= listed else f"the first {listed}"
    return [f"- ... ({total - shown} more; {where} in delta.json)"]


def _frame(name, index, time):
    return f"{name} {index} ({num(time)} s)"


def _alignment_markdown(a):
    """The pairing section of a pair's Markdown report (D-028)."""
    if not a:
        return []
    m, f, p = a["mirage"], a["ffmpeg"], a["pairs"]
    tol = "the tolerance" + (f", {num(a['tolerance'])} s," if a["tolerance"] is not None else "")

    def starts(side):
        return ", ".join(f"{n} {num(side['start'].get(n))}" for n in INPUTS)
    if m.get("failed") is not None:
        kinds = ", ".join(f"{k} {m[k]}" for k in FAILURE_REASONS if m.get(k))
        failed = (f"{m['failed']}" + (f" ({kinds})" if kinds else "") + "; failure rate "
                  f"{num(m.get('failure_rate'))}, {'above' if m.get('exceeded') else 'not above'}"
                  f" the threshold {m.get('threshold')}")
    else:
        failed = "- (no alignment in the report)"
    rows = [
        ("rule", cell(m["rule"]) + (f", grid: {m['grid']}" if m.get("grid") else ""),
         cell(f["rule"])),
        ("times from each input's start (s)",
         starts(m) + f"; {cell(m['start_from']['reference'])}",
         starts(f) + "; its log's input starts"),
        ("scored pairs", num(m["pairs"]), num(f["pairs"])),
        ("pairs both scored", num(p["both"]), num(p["both"])),
        ("pairs only this side scored", num(p["only_mirage"]), num(p["only_ffmpeg"])),
        ("pairs both scored at other timestamps", num(p["other_times"]), num(p["other_times"])),
        ("reference frames in no pair", num(m.get("reference_in_no_pair")),
         num(f["reference_in_no_pair"])),
        ("reference frames in more than one pair", num(m["reference_in_several_pairs"]),
         num(f["reference_in_several_pairs"])),
        ("distorted frames in no pair", num(m.get("distorted_in_no_pair")),
         num(f["distorted_in_no_pair"])),
        ("distorted frames in more than one pair", num(m["distorted_in_several_pairs"]),
         num(f["distorted_in_several_pairs"])),
        (f"pairs whose frames are more than {tol} apart", num(m["pairs_beyond_tolerance"]),
         num(f["pairs_beyond_tolerance"])),
        ("frames off the coarser grid (reference / distorted)",
         f"{num(m.get('off_grid_reference'))} / {num(m.get('off_grid_distorted'))}", "-"),
        ("distorted frames passed on unscored (no reference frame yet)", "-", num(f["unscored"])),
        ("pairs a reference frame made after the distorted input ended", "-",
         num(f["by_reference"])),
        ("timestamp discontinuities", f"{num(m.get('discontinuities'))} found, none corrected",
         f"{num(f['corrections'])} corrected (its log)"),
        ("libvmaf contexts", num(m.get("vmaf_contexts")), "1"),
        ("frames counted as failed", failed, "none: FFmpeg counts no failures"),
    ]
    lines = ["", "Pairing by timestamp (docs/DECISIONS.md D-028; eval/consistency/README.md): "
                 "a pair of one side is a pair of the other when it has the same reference and "
                 "distorted frames, by their positions in the inputs, and the same timestamps; "
                 f"the tolerance comes from {a['tolerance_from'] or '-'}.", "",
             "| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) |", "|---|---|---|"]
    lines += [f"| {label} | {mine} | {theirs} |" for label, mine, theirs in rows]
    for key, title, which in (("only_mirage", "Pairs only Mirage scored (its pair index)", "index"),
                              ("only_ffmpeg", "Pairs only FFmpeg scored (its frame)", "frame")):
        if a[key]:
            lines += ["", f"{title}:"]
            for e in a[key][:MARKDOWN_RANGES]:
                lines.append(f"- {e[which]}: {_frame('reference', e['ref'], e['ref_time'])} with "
                             f"{_frame('distorted', e['dis'], e['dis_time'])}"
                             + (", made by the reference frame after the distorted input ended"
                                if e.get("by_reference") else ""))
            lines += _more(min(len(a[key]), MARKDOWN_RANGES), p[key])
    if a["other_times"]:
        lines += ["", "Pairs both scored at other timestamps (Mirage's pair index / FFmpeg's "
                      "frame: the reference frame's and the distorted frame's times on each side):"]
        for e in a["other_times"][:MARKDOWN_RANGES]:
            mt, ft = e["mirage"], e["ffmpeg"]
            lines.append(f"- {e['index']} / {e['frame']}, reference {e['ref']} with distorted "
                         f"{e['dis']}: Mirage {num(mt['ref_time'])} / {num(mt['dis_time'])} s, "
                         f"FFmpeg {num(ft['ref_time'])} / {num(ft['dis_time'])} s")
        lines += _more(min(len(a["other_times"]), MARKDOWN_RANGES), p["other_times"])
    if a["frames"]:
        lines += ["", "Frames the two sides treat differently, Mirage's failures included "
                      "(times from each input's start):", "",
                  "| input | frame | time (s) | Mirage | FFmpeg |", "|---|---|---|---|---|"]
        lines += [f"| {e['input']} | {e['index']} | {num(e['time'])} | {cell(e['mirage'])} | "
                  f"{cell(e['ffmpeg'])} |" for e in a["frames"][:MARKDOWN_RANGES]]
        lines += _more(min(len(a["frames"]), MARKDOWN_RANGES), a["frames_treated_differently"])
    return lines


def pair_markdown(r):
    """The Markdown delta report of one pair."""
    lines = [f"# {r['pair']}: Mirage vs FFmpeg's libvmaf filter", ""]
    lines.append(f"**{r['status']}**" + ("" if r["status"] == "PASS" else ":"))
    lines += [f"- {cell(x)}" for x in r["fail_reasons"]]
    lines += ["", f"- Variant: {VARIANT_TEXT[r['variant']]}."]
    if r["relabel"]:
        lines.append(f"- Relabel inserted before FFmpeg's scale: `{r['relabel']}`")
    elif r["variant"] == VARIANT_RELABEL:
        lines.append("- No relabel: the inputs' color_space and color_range tags are the same.")
    lines.append(f"- Reference: {r['reference']}")
    lines.append(f"- Distorted: {r['distorted']}")
    lines.append("- Frames: " + (f"the first {r['frame_limit']} frame pairs."
                                 if r["frame_limit"] else "every frame pair."))
    lines.append(f"- A frame fails if \\|delta\\| > {r['threshold']}, with delta = Mirage - FFmpeg "
                 "(docs/DECISIONS.md D-019).")

    m, f, p = r["mirage_vmaf"] or {}, r["ffmpeg"] or {}, r["pooled"] or {}
    pm, pf, pd = p.get("mirage") or {}, p.get("ffmpeg") or {}, p.get("difference") or {}
    d = r["delta"] or {}
    lines += ["", "| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |",
              "|---|---|---|---|",
              f"| frame pairs scored | {num(r['frames']['mirage'])} | {num(r['frames']['ffmpeg'])}"
              f" | {'**MISMATCH**' if count_mismatch(r['frames']) else ''} |",
              f"| pooled mean (libvmaf) | {num(pm.get('mean'))} | {num(pf.get('mean'))} "
              f"| {num(pd.get('mean'))} |",
              f"| pooled min (libvmaf) | {num(pm.get('min'))} | {num(pf.get('min'))} "
              f"| {num(pd.get('min'))} |",
              f"| pooled harmonic mean (libvmaf) | {num(pm.get('harmonic_mean'))} "
              f"| {num(pf.get('harmonic_mean'))} | {num(pd.get('harmonic_mean'))} |",
              f"| mean of the compared frames | {num(d.get('mean_mirage'))} "
              f"| {num(d.get('mean_ffmpeg'))} | {num(d.get('mean_signed'))} |",
              f"| libvmaf version (run time) | {cell(m.get('libvmaf_version', '-'))} "
              f"| {cell(f.get('libvmaf_version', '-'))} | |",
              f"| libvmaf package version | {cell(m.get('libvmaf_package_version') or '-')} "
              f"| {cell(f.get('libvmaf_package_version') or '-')} | |"]
    m_libs = m.get("ffmpeg_libraries") if isinstance(m.get("ffmpeg_libraries"), dict) else {}
    f_libs = f.get("ffmpeg_libraries") if isinstance(f.get("ffmpeg_libraries"), dict) else {}
    for name in list(m_libs) + [n for n in f_libs if n not in m_libs]:
        lines.append(f"| {cell(name)} version (run time) | {cell(m_libs.get(name, '-'))} "
                     f"| {cell(f_libs.get(name, '-'))} | |")
    lines += [
              f"| exit status | {num(m.get('exit_code'))} | {num(f.get('exit_code'))} | |",
              f"| wall time, s | {num(m.get('elapsed_s'), 1)} | {num(f.get('elapsed_s'), 1)} | |"]

    lines += _alignment_markdown(r.get("alignment"))
    if d.get("compared"):
        lines += ["", f"Per frame, over the {d['compared']} frame pairs both sides scored "
                      "(by Mirage's pair index):",
                  f"- max \\|delta\\|: {num(d['max_abs'])} (index {d['max_abs_index']})",
                  f"- mean \\|delta\\|: {num(d['mean_abs'])}",
                  f"- mean delta: {num(d['mean_signed'])}",
                  f"- frames with \\|delta\\| > {r['threshold']}: {d['over_threshold']}"
                  + (f" (index {ranges_text(d['over_threshold_ranges'])})"
                     if d["over_threshold"] else ""),
                  "", "Largest \\|delta\\|:", "",
                  "| index | FFmpeg's frame | Mirage | FFmpeg | delta |", "|---|---|---|---|---|"]
        lines += [f"| {w['index']} | {num(w.get('ffmpeg_index'))} | {num(w['mirage'])} | "
                  f"{num(w['ffmpeg'])} | {num(w['delta'])} |" for w in d["worst"]]

    color = r["ffmpeg_color"]
    lines += ["", "FFmpeg's color handling (its verbose log; the lines are in "
              "ffmpeg_colorspace_lines.txt):"]
    lines += [f"- filter graph input {g['input']} "
              f"({'distorted' if g['input'] == 0 else 'reference'}): "
              f"csp:{g['color_space']} range:{g['color_range']}" for g in color["graph_inputs"]]
    lines += [f"- {_scale_text(s)}" for s in color["scale"]]
    lines.append("- Converts: " + (", ".join(color["conversions"]) if color["conversions"]
                                   else "nothing besides the size"))
    conv = (r.get("mirage_color") or {}).get("dis_conversion")
    lines += ["", "Mirage's color handling of the distorted frames (mirage_vmaf's report, "
              "dis_conversion; DECISIONS.md D-016):"]
    if conv is None:
        lines.append("- not in the report: this mirage_vmaf does not record a conversion")
    else:
        entries = [f"- {_conversion_line(c)}" for c in conv if isinstance(c, dict)]
        lines += entries or ["- no pair was converted"]
    ref_conv = (r.get("mirage_color") or {}).get("ref_conversion")
    lines += ["", "Mirage's color handling of the reference frames (mirage_vmaf's report, "
              "ref_conversion; DECISIONS.md D-022):"]
    if ref_conv is None:
        lines.append("- not in the report: this mirage_vmaf does not record one")
    else:
        entries = [f"- {_conversion_line(c)}" for c in ref_conv if isinstance(c, dict)]
        lines += entries or ["- none: the reference's frames are used as they are"]

    lines += ["", "Inputs (ffprobe / mirage_vmaf's decoder):", "",
              "| | reference | distorted |", "|---|---|---|"]
    for mk, pk in _INPUT_FIELDS:
        row = []
        for side in ("ref", "dis"):
            probe = r["inputs"][side]["ffprobe"] or {}
            mine = r["inputs"][side]["mirage_vmaf"] or {}
            a, b = probe.get(pk), mine.get(mk)
            row.append(cell(f"{a}" if not mine or _tag(a) == _tag(b) else f"{a} / {b}"))
        lines.append(f"| {mk} | {row[0]} | {row[1]} |")
    if m:
        lines += ["", f"mirage_vmaf: {_model_text(m)}, scaler {m.get('scaler')}, "
                  f"n_threads {m.get('n_threads')}, n_subsample {m.get('n_subsample')}, "
                  f"libvmaf package {m.get('libvmaf_package_version')}"
                  + (f", reference mode {m['reference_mode']}" if m.get("reference_mode")
                     else "")
                  + f"; reference PTS {num(m.get('first_ref_pts'))} to "
                  f"{num(m.get('last_ref_pts'))} s; pairs with different PTS: "
                  f"{m.get('pairs_with_different_pts')}."]
    if r["warnings"]:
        lines += ["", "Warnings:"] + [f"- {cell(w)}" for w in r["warnings"]]
    lines += ["", "The commands are in cmd.txt, the raw outputs next to this file "
              "(eval/consistency/README.md lists them)."]
    return "\n".join(lines) + "\n"


def overall_markdown(o):
    """The Markdown report of a whole run."""
    p = o["parameters"]
    n_failed = len(o["pairs_failed"])
    lines = ["# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter", ""]
    lines.append(f"**Overall: {o['status']}**" + (f": {n_failed} of {o['pairs_total']} pairs fail."
                                                   if n_failed else
                                                   f": all {o['pairs_total']} pairs pass."))
    lines += ["", f"- Variant: {VARIANT_TEXT[o['variant']]}.",
              "- Frames: " + (f"the first {p['frames']} frame pairs of each pair (--frames "
                              f"{p['frames']})." if p["frames"] else "every frame pair."),
              f"- A frame fails if \\|delta\\| > {p['threshold']}, with delta = Mirage - FFmpeg "
              "(docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, "
              "used the same settings, scored the same frame pairs at the same timestamps "
              "(D-028) and no frame fails.",
              f"- Settings given to FFmpeg: {_params_model_text(p)}, scaler {p['scaler']}, "
              f"n_threads {p['threads']}."]
    mv = o.get("mirage_vmaf") or {}
    fv = o.get("ffmpeg_libvmaf") or {}
    env = o.get("environment") or {}
    git = o.get("git") or {}
    if mv:
        lines.append(f"- mirage_vmaf: sha256 {mv.get('sha256')}; libvmaf "
                     f"{mv.get('libvmaf_version')} at run time, package "
                     f"{mv.get('libvmaf_package_version')}.")
    libs = [(who, x) for who, x in (("mirage_vmaf", mv.get("ffmpeg_libraries")),
                                    ("FFmpeg", env.get("ffmpeg_libraries") if env else None))
            if isinstance(x, dict) and x]
    if libs:
        lines.append("- FFmpeg libraries at run time: " + "; ".join(
            f"{who} " + ", ".join(f"{n} {v}" for n, v in x.items()) for who, x in libs) + ".")
    if env:
        lines.append(f"- FFmpeg: {env.get('ffmpeg_version')}; libvmaf "
                     + (f"{fv.get('libvmaf_version')} at run time (its libvmaf logs), "
                        if fv.get("libvmaf_version") is not None else "")
                     + f"package {env.get('ffmpeg_libvmaf_package_version')}.")
        lines.append(f"- Machine: {env.get('os')}, {env.get('cpu')}.")
    if git:
        state = lambda clean: "clean" if clean else "NOT CLEAN"  # noqa: E731
        lines.append(f"- Code: commit {git.get('head_start')}; the working tree was "
                     f"{state(git.get('clean_start'))} at the start of the run and "
                     f"{state(git.get('clean_end'))} at its end (environment.txt)"
                     + ("" if git.get("head_start") == git.get("head_end")
                        else f"; HEAD MOVED to {git.get('head_end')}") + ".")
    lines.append(f"- Run: {o.get('started_utc')} to {o.get('ended_utc')} (UTC).")

    lines += ["", "| pair | status | frame pairs (Mirage / FFmpeg) | pairs both scored / only "
              "Mirage / only FFmpeg | Mirage's alignment failures (rate) | max \\|delta\\| "
              "| mean \\|delta\\| | frames over | pooled mean Mirage | pooled mean FFmpeg "
              "| difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |",
              "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for s in o["pairs"]:
        pooled = s["pooled"] or {}
        pr = s.get("pairing") or {}
        pairs = (f"{num(pr.get('both'))} / {num(pr.get('only_mirage'))} / "
                 f"{num(pr.get('only_ffmpeg'))}") if pr else "-"
        failures = (f"{num(pr.get('mirage_failed'))} ({num(pr.get('mirage_failure_rate'))})"
                    if pr.get("mirage_failed") is not None else "-")
        lines.append(
            f"| {s['pair']} | {s['status']} | {_frames_cell(s['frames'])} | {pairs} | {failures} "
            f"| {num(s['max_abs'])} | {num(s['mean_abs'])} | {num(s['over_threshold'])} "
            f"| {num((pooled.get('mirage') or {}).get('mean'))} "
            f"| {num((pooled.get('ffmpeg') or {}).get('mean'))} "
            f"| {num((pooled.get('difference') or {}).get('mean'))} "
            f"| {cell(s['ffmpeg_distorted_csp'])} | {cell(s['mirage_distorted_csp'])} |")
    if any(s["relabel"] for s in o["pairs"]):
        lines += ["", "Relabels inserted before FFmpeg's scale:"]
        lines += [f"- {s['pair']}: `{s['relabel']}`" for s in o["pairs"] if s["relabel"]]
    failing = [s for s in o["pairs"] if s["fail_reasons"]]
    if failing:
        lines += ["", "Why pairs fail:"]
        for s in failing:
            lines += [f"- {s['pair']}: {cell(x)}" for x in s["fail_reasons"]]
    warned = [s for s in o["pairs"] if s["warnings"]]
    if warned or o.get("warnings"):
        lines += ["", "Warnings:"]
        lines += [f"- {cell(w)}" for w in o.get("warnings") or []]
        for s in warned:
            lines += [f"- {s['pair']}: {cell(w)}" for w in s["warnings"]]
    lines += ["", "Each pair's directory has its delta report (delta.md, delta.json), the "
              "commands (cmd.txt) and both sides' raw outputs. environment.txt records the "
              "machine, the programs and the git state; SHA256SUMS covers every file."]
    return "\n".join(lines) + "\n"


# ── Files ────────────────────────────────────────────────────────────────────

def xz(data):
    """`data` as the contents of an .xz file, compressed as xz -9 does."""
    return lzma.compress(data, format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC64, preset=9)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256sums(root, skip=("SHA256SUMS",)):
    """A SHA256SUMS file for every file under `root`, by path relative to it,
    in the format `shasum -a 256 -c` reads."""
    rows = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in filenames:
            rel = os.path.relpath(os.path.join(dirpath, name), root).replace(os.sep, "/")
            if rel not in skip:
                rows.append((rel, sha256_file(os.path.join(dirpath, name))))
    rows.sort()
    return "".join(f"{h}  {rel}\n" for rel, h in rows)
