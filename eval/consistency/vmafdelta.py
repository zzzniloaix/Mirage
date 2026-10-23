"""Delta math and reports for the section 5 consistency check (run_delta.py).

Compares Mirage's per-frame VMAF scores, from a mirage_vmaf report
(src/VMAFReport.h), with the log of FFmpeg's libvmaf filter (log_fmt=json)
for the same pair, frame by frame, and renders the delta report of each pair
and of a whole run as JSON and Markdown. It also builds the commands both
sides run, and holds the helpers for the files a run leaves behind
(placeholders for local paths, xz, SHA256SUMS).

Nothing here starts a process; tests/test_vmafdelta.py covers all of it.
Python 3 standard library only.
"""

import collections
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


def filtergraph(width, height, scaler, model, threads, frames=None, relabel=None,
                log_path="ffmpeg.json"):
    """FFmpeg's side of a pair: the filter graph of Phase 0's diagnostic
    comparison (eval/baseline/diagnostics/ffmpeg_libvmaf_gear2_vs_gear5_first300.cmd.txt)
    with the reference's size. Input 0 is the distorted video, input 1 the
    reference. With `frames`, trim passes only the first `frames` frames of
    each input on, so that libvmaf scores exactly that many pairs, and the
    last of them has no successor, as with mirage_vmaf --frames.
    `relabel` (relabel_filter()) goes before the scale filter."""
    trim = f"trim=end_frame={frames}," if frames else ""
    tags = f"{relabel}," if relabel else ""
    return (f"[0:v]{trim}{tags}scale={width}:{height}:flags={scaler},format=yuv420p[d];"
            f"[1:v]{trim}format=yuv420p[r];"
            f"[d][r]libvmaf=model=version={model}:n_threads={threads}"
            f":log_fmt=json:log_path={log_path}")


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


def forwarded_options(supported, scaler, model):
    """The scoring settings passed on to mirage_vmaf, for those of them it
    has an option for (`supported`, from help_options()). Without such an
    option mirage_vmaf scores with its fixed setting, and the settings check
    in evaluate_pair() compares that with what FFmpeg was given."""
    argv = []
    if "--scaler" in supported:
        argv += ["--scaler", scaler]
    if "--model" in supported:
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


def compare_scores(mirage, ffmpeg, threshold, worst=WORST_FRAMES):
    """Frame-by-frame comparison over the frames both lists have, with
    delta = Mirage - FFmpeg. A frame is over the threshold if
    |delta| > threshold (D-019)."""
    n = min(len(mirage), len(ffmpeg))
    delta = [mirage[i] - ffmpeg[i] for i in range(n)]
    size = [abs(d) for d in delta]
    over = [i for i in range(n) if size[i] > threshold]
    order = sorted(range(n), key=lambda i: (-size[i], i))[:worst]
    return {
        "compared": n,
        "max_abs": size[order[0]] if n else None,
        "max_abs_index": order[0] if n else None,
        "mean_abs": _mean(size),
        "mean_signed": _mean(delta),
        "over_threshold": len(over),
        "over_threshold_ranges": index_ranges(over),
        "worst": [{"index": i, "mirage": mirage[i], "ffmpeg": ffmpeg[i], "delta": delta[i]}
                  for i in order],
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


def mirage_conversions(conv):
    """The same for the dis_conversion of a mirage_vmaf report (src/VMAFReport.h)."""
    return sorted({f"{_tags(c.get('from') or {})} -> {_tags(c.get('to') or {})}"
                   for c in conv if isinstance(c, dict)})


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


def evaluate_pair(pair, params, sides):
    """The delta report of one pair, ready for json.dumps.

    pair    Pair
    params  {"variant", "frames" (None = every frame), "threshold", "model",
             "scaler", "threads"}
    sides   {"probe": {"ref": info or None, "dis": info or None, "errors": [...]},
             "relabel": setparams filter or None, "relabel_warnings": [...],
             "mirage": {"run", "report", "error"}, "ffmpeg": {"run", "log", "error", "stderr"}}
            where a run is {"exit_code", "timed_out", "timeout_s", "error",
            "elapsed_s", ...} or None if the side was not run, report and log
            are the parsed JSON outputs (None if missing), and stderr is
            FFmpeg's verbose log.

    The pair passes only if both sides ran to completion, mirage_vmaf's
    settings are the ones FFmpeg was given, both scored the same number of
    frames (more than none), and no frame has |delta| > threshold.
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
    if rep is not None:
        for key, want in (("model", params["model"]), ("scaler", params["scaler"]),
                          ("n_threads", params["threads"]), ("n_subsample", 1),
                          ("frame_limit", params["frames"])):
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
        differ = sum(1 for f in timed if f["ref_pts"] != f["dis_pts"])
        if differ:
            warn.append(f"{differ} of mirage_vmaf's {len(frames)} pairs are frames with "
                        "different PTS; FFmpeg's libvmaf filter pairs frames by timestamp, "
                        "so the two sides may have paired other frames")
        if len(timed) != len(frames):
            warn.append(f"{len(frames) - len(timed)} of mirage_vmaf's pairs have no PTS "
                        "for one of their frames")
        mirage_info = {
            "exit_code": m_side["run"]["exit_code"],
            "elapsed_s": m_side["run"].get("elapsed_s"),
            **{k: rep.get(k) for k in ("tool", "libvmaf_version", "libvmaf_package_version",
                                       "model", "scaler", "n_threads", "n_subsample",
                                       "scored_as", "frame_limit", "frames_scored")},
            "ref_ended": (rep.get("ref") or {}).get("ended"),
            "dis_ended": (rep.get("dis") or {}).get("ended"),
            "first_ref_pts": float(timed[0]["ref_pts"]) if timed else None,
            "last_ref_pts": float(timed[-1]["ref_pts"]) if timed else None,
            "pairs_with_different_pts": differ,
        }

    # FFmpeg's log: libvmaf's version and how the scale filter converted.
    stderr = f_side.get("stderr") or ""
    scales = scale_lines(stderr)
    inputs = graph_inputs(stderr)
    ffmpeg_info = None
    if log is not None:
        ffmpeg_info = {"exit_code": f_side["run"]["exit_code"],
                       "elapsed_s": f_side["run"].get("elapsed_s"),
                       "libvmaf_version": log.get("version")}
        if rep is not None and log.get("version") != rep.get("libvmaf_version"):
            warn.append(f"libvmaf versions differ: mirage_vmaf {rep.get('libvmaf_version')!r}, "
                        f"FFmpeg {log.get('version')!r}")
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

    # How each side converted the distorted frames' color (D-016). Only a
    # mirage_vmaf that converts records dis_conversion.
    mirage_conv = rep.get("dis_conversion") if rep is not None else None
    if not isinstance(mirage_conv, list):
        mirage_conv = None
    if mirage_conv is not None and log is not None and scale_conversions(scales):
        mine, theirs = mirage_conversions(mirage_conv), scale_conversions(scales)
        if mine != theirs:
            warn.append("the distorted frames' color conversions differ: mirage_vmaf "
                        f"{', '.join(mine) or 'none'}, FFmpeg's scale filter "
                        f"{', '.join(theirs)}")

    # The scores.
    delta, n_mirage, n_ffmpeg, pooled = None, None, None, None
    if rep is not None and log is not None:
        try:
            m, f = mirage_scores(rep), ffmpeg_scores(log)
        except ValueError as e:
            fail.append(str(e))
        else:
            n_mirage, n_ffmpeg = len(m), len(f)
            delta = compare_scores(m, f, params["threshold"])
            if n_mirage != n_ffmpeg:
                fail.insert(0, f"FRAME COUNT MISMATCH: Mirage scored {n_mirage} frame pairs, "
                               f"FFmpeg {n_ffmpeg}; only the first {delta['compared']} are "
                               "compared")
            if delta["compared"] == 0:
                fail.append("no frame pair to compare")
            if params["frames"] and min(n_mirage, n_ffmpeg) < params["frames"]:
                warn.append(f"fewer frame pairs than --frames {params['frames']}")
            if delta["over_threshold"]:
                fail.append(f"{delta['over_threshold']} of {delta['compared']} frames have "
                            f"|delta| > {params['threshold']}")
            mp = rep.get("pooled") or {}
            fp = (log.get("pooled_metrics") or {}).get("vmaf") or {}
            pooled = {"mirage": {k: mp.get(k) if is_number(mp.get(k)) else None
                                 for k in ("mean", "min")},
                      "ffmpeg": {k: fp.get(k) if is_number(fp.get(k)) else None
                                 for k in ("mean", "min")}}
            pooled["difference"] = {
                k: (pooled["mirage"][k] - pooled["ffmpeg"][k])
                if pooled["mirage"][k] is not None and pooled["ffmpeg"][k] is not None else None
                for k in ("mean", "min")}

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
        "pooled": pooled,
        "relabel": sides.get("relabel"),
        "ffmpeg_color": {"graph_inputs": inputs, "scale": scales,
                         "conversions": conversions(scales)},
        "mirage_color": {"dis_conversion": mirage_conv},
        "inputs": {"ref": {"ffprobe": ref_probe, "mirage_vmaf": (rep or {}).get("ref")},
                   "dis": {"ffprobe": dis_probe, "mirage_vmaf": (rep or {}).get("dis")}},
        "mirage_vmaf": mirage_info,
        "ffmpeg": ffmpeg_info,
    }


def pair_summary(r):
    """The part of a pair's report that the overall report repeats."""
    d = r["delta"] or {}
    return {
        "pair": r["pair"], "status": r["status"], "fail_reasons": r["fail_reasons"],
        "warnings": r["warnings"], "frames": r["frames"],
        "max_abs": d.get("max_abs"), "max_abs_index": d.get("max_abs_index"),
        "mean_abs": d.get("mean_abs"), "mean_signed": d.get("mean_signed"),
        "over_threshold": d.get("over_threshold"), "pooled": r["pooled"],
        "relabel": r["relabel"], "ffmpeg_conversions": r["ffmpeg_color"]["conversions"],
        "ffmpeg_distorted_csp": _distorted_csp(r),
        "mirage_distorted_csp": _mirage_csp(r),
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


def _distorted_csp(r):
    """The distorted input's color space before and after FFmpeg's scale."""
    scales = [s for s in r["ffmpeg_color"]["scale"] if s["filter"].startswith("Parsed_scale")]
    seen = []
    for s in scales:
        t = f"{s['in']['color_space']} -> {s['out']['color_space']}"
        if t not in seen:
            seen.append(t)
    return "; ".join(seen) if seen else "-"


def _mirage_csp(r):
    """The distorted frames' color space before and after mirage_vmaf's conversion."""
    conv = (r.get("mirage_color") or {}).get("dis_conversion")
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
              f"| mean of the compared frames | {num(d.get('mean_mirage'))} "
              f"| {num(d.get('mean_ffmpeg'))} | {num(d.get('mean_signed'))} |",
              f"| libvmaf version (run time) | {cell(m.get('libvmaf_version', '-'))} "
              f"| {cell(f.get('libvmaf_version', '-'))} | |",
              f"| exit status | {num(m.get('exit_code'))} | {num(f.get('exit_code'))} | |",
              f"| wall time, s | {num(m.get('elapsed_s'), 1)} | {num(f.get('elapsed_s'), 1)} | |"]

    if d.get("compared"):
        last = d["compared"] - 1
        lines += ["", f"Per frame, over the {d['compared']} compared frames (index 0-{last}):",
                  f"- max \\|delta\\|: {num(d['max_abs'])} (index {d['max_abs_index']})",
                  f"- mean \\|delta\\|: {num(d['mean_abs'])}",
                  f"- mean delta: {num(d['mean_signed'])}",
                  f"- frames with \\|delta\\| > {r['threshold']}: {d['over_threshold']}"
                  + (f" (index {ranges_text(d['over_threshold_ranges'])})"
                     if d["over_threshold"] else ""),
                  "", "Largest \\|delta\\|:", "",
                  "| index | Mirage | FFmpeg | delta |", "|---|---|---|---|"]
        lines += [f"| {w['index']} | {num(w['mirage'])} | {num(w['ffmpeg'])} | "
                  f"{num(w['delta'])} |" for w in d["worst"]]

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
        lines += ["", f"mirage_vmaf: model {m.get('model')}, scaler {m.get('scaler')}, "
                  f"n_threads {m.get('n_threads')}, n_subsample {m.get('n_subsample')}, "
                  f"libvmaf package {m.get('libvmaf_package_version')}; reference PTS "
                  f"{num(m.get('first_ref_pts'))} to {num(m.get('last_ref_pts'))} s; "
                  f"pairs with different PTS: {m.get('pairs_with_different_pts')}."]
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
              "used the same settings, scored the same number of frames and no frame fails.",
              f"- Settings given to FFmpeg: model {p['model']}, scaler {p['scaler']}, "
              f"n_threads {p['threads']}."]
    mv = o.get("mirage_vmaf") or {}
    env = o.get("environment") or {}
    git = o.get("git") or {}
    if mv:
        lines.append(f"- mirage_vmaf: sha256 {mv.get('sha256')}; libvmaf "
                     f"{mv.get('libvmaf_version')} at run time, package "
                     f"{mv.get('libvmaf_package_version')}.")
    if env:
        lines.append(f"- FFmpeg: {env.get('ffmpeg_version')}; libvmaf package "
                     f"{env.get('ffmpeg_libvmaf_package_version')}.")
        lines.append(f"- Machine: {env.get('os')}, {env.get('cpu')}.")
    if git:
        state = lambda clean: "clean" if clean else "NOT CLEAN"  # noqa: E731
        lines.append(f"- Code: commit {git.get('head_start')}; the working tree was "
                     f"{state(git.get('clean_start'))} at the start of the run and "
                     f"{state(git.get('clean_end'))} at its end (environment.txt)"
                     + ("" if git.get("head_start") == git.get("head_end")
                        else f"; HEAD MOVED to {git.get('head_end')}") + ".")
    lines.append(f"- Run: {o.get('started_utc')} to {o.get('ended_utc')} (UTC).")

    lines += ["", "| pair | status | frame pairs (Mirage / FFmpeg) | max \\|delta\\| "
              "| mean \\|delta\\| | frames over | pooled mean Mirage | pooled mean FFmpeg "
              "| difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |",
              "|---|---|---|---|---|---|---|---|---|---|---|"]
    for s in o["pairs"]:
        pooled = s["pooled"] or {}
        lines.append(
            f"| {s['pair']} | {s['status']} | {_frames_cell(s['frames'])} | {num(s['max_abs'])} "
            f"| {num(s['mean_abs'])} | {num(s['over_threshold'])} "
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
