"""Pooling definitions computed on committed per-frame VMAF scores (work order section 1.5).

How Mirage pools per-frame scores was decision #6 of docs/VMAF_GAP_REPORT.md,
deferred by D-004 and made by D-027. This measures what the candidate
definitions give on per-frame scores already committed in the repository:

  --gui-report FILE  the JSON the player's analyzer writes (write_json),
                     e.g. eval/baseline/bipbop16x9/report.vmaf.json, whose
                     per-frame scores have 4 decimals
  --delta-run DIR    an eval/consistency/run_delta.sh output directory: per
                     pair, mirage_vmaf's report (full precision) and FFmpeg's
                     libvmaf log (6 decimals, with libvmaf's own pooled
                     min, max, mean and harmonic mean)

For each series of per-frame scores it gives the minimum and where it
occurs, p1, p5, p10, p25 and p50 under three definitions (DEFINITIONS), two
forms of the harmonic mean, and, where the source has them, the values
libvmaf pooled itself. With --check-pooled it also computes, for Mirage's
series of a delta run, the pooled values as D-027 defines them and
mirage_vmaf computes them (POOLED_D027, src/VMAFPool.h), and checks them
against those mirage_vmaf's report gives, at full precision. It writes
PREFIX.json and PREFIX.md, with the inputs' SHA-256 and the git state of the
repository at the start and end of the run.

usage: pooling.py (--gui-report FILE | --delta-run DIR)... [--check-pooled] --out PREFIX

Python 3 standard library only; run it with python3 -I -B. Exit status: 0
the reports were written; 1 an input is missing or unreadable; 2 bad
arguments.
"""

import argparse
import fractions
import hashlib
import json
import lzma
import math
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.realpath(__file__))
sys.dont_write_bytecode = True

REPORT_VERSION = 1

# The percentiles work order section 1.5 asks for.
PERCENTILES = (1, 5, 10, 25, 50)

DEFINITIONS = {
    "today": "sorted[floor(n*p/100)], counting from 0: the rule of the analyzer's "
             "vmaf_p5 until D-027 (src/VMAFAnalyzer.cpp, sorted[n * 5 / 100])",
    "nearest-rank": "sorted[ceil(n*p/100) - 1], counting from 0: the nearest-rank method",
    "linear": "sorted[k] + (h - k) * (sorted[k + 1] - sorted[k]) with h = (n - 1) * p / 100 "
              "and k = floor(h), counting from 0: linear interpolation between the closest "
              "ranks (Hyndman and Fan's definition 7)",
}

HARMONIC = {
    "shifted": "n / sum(1 / (x + 1)) - 1",
    "plain": "n / sum(1 / x), undefined when a frame scores 0",
}

# The pooled values mirage_vmaf reports since D-027, as it computes them
# (src/VMAFPool.h): libvmaf's mean, harmonic mean and minimum, summed frame by
# frame in index order as libvmaf sums them (where "mean" and "shifted" above
# sum exactly, with math.fsum); the minimum without the first frame; and the
# "linear" percentiles.
POOLED_D027 = {
    "mean": "sum / n, summed in frame order (libvmaf's VMAF_POOL_METHOD_MEAN)",
    "harmonic_mean": "n / sum(1 / (x + 1)) - 1, summed in frame order (libvmaf's "
                     "VMAF_POOL_METHOD_HARMONIC_MEAN)",
    "min": "the smallest score (libvmaf's VMAF_POOL_METHOD_MIN)",
    "min_without_first_frame": "the smallest score of frames 1 to n - 1; null for one frame",
    **{f"p{p}": "the linear definition" for p in PERCENTILES},
}


class InputError(Exception):
    pass


# ── Definitions ──────────────────────────────────────────────────────────────

def pct_today(order, p):
    return order[len(order) * p // 100]


def pct_nearest_rank(order, p):
    k = -(-len(order) * p // 100)   # ceil(n * p / 100)
    return order[max(k, 1) - 1]


def pct_linear(order, p):
    h = fractions.Fraction((len(order) - 1) * p, 100)
    k = math.floor(h)
    if k + 1 >= len(order):
        return order[k]
    return order[k] + float(h - k) * (order[k + 1] - order[k])


PCT = {"today": pct_today, "nearest-rank": pct_nearest_rank, "linear": pct_linear}


def harmonic_shifted(scores):
    return len(scores) / math.fsum(1.0 / (x + 1.0) for x in scores) - 1.0


def harmonic_plain(scores):
    if any(x <= 0 for x in scores):
        return None
    return len(scores) / math.fsum(1.0 / x for x in scores)


def libvmaf_mean(scores):
    """libvmaf's pooled mean: the scores summed in frame order, over n."""
    total = 0.0
    for x in scores:
        total += x
    return total / len(scores)


def libvmaf_harmonic_mean(scores):
    """libvmaf's harmonic mean, n / sum(1 / (x + 1)) - 1, summed in frame order."""
    inverse = 0.0
    for x in scores:
        inverse += 1.0 / (x + 1.0)
    return len(scores) / inverse - 1.0


def d027_pooled(scores):
    """The pooled values of POOLED_D027 for one series of scores, in frame order."""
    order = sorted(scores)
    out = {"mean": libvmaf_mean(scores), "harmonic_mean": libvmaf_harmonic_mean(scores),
           "min": order[0],
           "min_without_first_frame": min(scores[1:]) if len(scores) > 1 else None}
    out.update({f"p{p}": pct_linear(order, p) for p in PERCENTILES})
    return out


def series_stats(scores):
    """Minimum, first frame, percentiles and means of one series of scores."""
    if not scores:
        raise InputError("a series has no scores")
    n = len(scores)
    order = sorted(scores)
    lowest = order[0]
    at_min = [i for i, v in enumerate(scores) if v == lowest]
    rest = scores[1:]
    rest_min = min(rest) if rest else None
    pct = {}
    for p in PERCENTILES:
        values = {name: f(order, p) for name, f in PCT.items()}
        values["largest_difference"] = max(values.values()) - min(values.values())
        pct[f"p{p}"] = values
    return {
        "frames": n,
        "mean": math.fsum(scores) / n,
        "min": lowest,
        "max": order[-1],
        "min_first_index": at_min[0],
        "frames_at_min": len(at_min),
        "first_frame": scores[0],
        "min_without_first_frame": rest_min,
        "min_without_first_frame_index": 1 + rest.index(rest_min) if rest else None,
        "percentiles": pct,
        "harmonic_mean": {"shifted": harmonic_shifted(scores), "plain": harmonic_plain(scores),
                          "frames_at_or_below_zero": sum(1 for x in scores if x <= 0)},
    }


# ── Inputs ───────────────────────────────────────────────────────────────────

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_json(path):
    """(the JSON of `path`, or of `path`.xz when only that exists; the file read)"""
    for name, opener in ((path, open), (path + ".xz", lzma.open)):
        if os.path.exists(name):
            try:
                with opener(name, "rt", encoding="utf-8") as f:
                    return json.load(f), name
            except (OSError, ValueError, lzma.LZMAError) as e:
                raise InputError(f"{name}: {e}") from None
    raise InputError(f"{path} (or {os.path.basename(path)}.xz) is missing")


def is_number(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def gui_series(path):
    """The series of a player's VMAF report (write_json): one per result
    with scores; results with an error are listed as skipped."""
    data, used = read_json(path)
    series, skipped = [], []
    for r in data.get("results") or []:
        scores = r.get("per_frame") or []
        if r.get("error") or not scores:
            skipped.append({"label": r.get("label"), "error": r.get("error")})
            continue
        if not all(is_number(v) for v in scores):
            raise InputError(f"{path}: {r.get('label')!r} has a score that is not a number")
        series.append({
            "label": r.get("label"),
            "scores": [float(v) for v in scores],
            "precision": "4 decimals (write_json)",
            "recorded": {"mean": r.get("vmaf_mean"), "min": r.get("vmaf_min"),
                         "p5": r.get("vmaf_p5")},
        })
    return {"kind": "gui-report", "files": [used], "series": series, "skipped": skipped}


def delta_series(run_dir, check_pooled=False):
    """The series of a run_delta.py run: per pair, Mirage's scores (with
    libvmaf's pooled mean and min from mirage_vmaf's report) and FFmpeg's
    (with libvmaf's pooled values and frame 0's motion from its log). With
    `check_pooled`, Mirage's series also hold the report's pooled values of
    POOLED_D027 ("reported"; None for a value the report lacks)."""
    overall, used = read_json(os.path.join(run_dir, "delta.json"))
    files, series = [used], []
    for s in overall.get("pairs") or []:
        pdir = os.path.join(run_dir, s["pair"])
        rep, f1 = read_json(os.path.join(pdir, "mirage.json"))
        log, f2 = read_json(os.path.join(pdir, "ffmpeg.json"))
        files += [f1, f2]
        m = [f.get("vmaf") for f in rep.get("frames") or []]
        frames = log.get("frames") or []
        f = [(fr.get("metrics") or {}).get("vmaf") for fr in frames]
        if not m or not f or not all(is_number(v) for v in m + f):
            raise InputError(f"{pdir}: missing or non-numeric scores")
        pooled = (log.get("pooled_metrics") or {}).get("vmaf") or {}
        motion = [(fr.get("metrics") or {}).get("integer_motion2") for fr in frames]
        first = frames[0].get("metrics") or {}
        mirage = {
            "label": f"{s['pair']}, Mirage",
            "scores": [float(v) for v in m],
            "precision": "full (mirage_vmaf's report)",
            "recorded": {"mean": (rep.get("pooled") or {}).get("mean"),
                         "min": (rep.get("pooled") or {}).get("min"),
                         "source": "libvmaf's vmaf_score_pooled in mirage_vmaf"},
        }
        if check_pooled:
            mirage["reported"] = {k: (rep.get("pooled") or {}).get(k) for k in POOLED_D027}
        series.append(mirage)
        series.append({
            "label": f"{s['pair']}, FFmpeg",
            "scores": [float(v) for v in f],
            "precision": "6 decimals (FFmpeg's libvmaf log)",
            "recorded": {k: pooled.get(k) for k in ("min", "max", "mean", "harmonic_mean")}
                        | {"source": "libvmaf's pooled_metrics in FFmpeg's log"},
            "motion": {"frame0_integer_motion2": first.get("integer_motion2"),
                       "frame0_integer_motion": first.get("integer_motion"),
                       "frames_with_integer_motion2_zero":
                           sum(1 for v in motion if v == 0)},
        })
    return {"kind": "delta-run", "files": files, "series": series, "skipped": []}


def git_state(path):
    """(HEAD, whether the working tree is clean) of the repository holding
    `path`, or (None, None) outside one."""
    try:
        head = subprocess.run(["git", "-C", path, "rev-parse", "HEAD"], capture_output=True,
                              text=True, timeout=60, stdin=subprocess.DEVNULL)
        status = subprocess.run(["git", "-C", path, "status", "--porcelain"],
                                capture_output=True, text=True, timeout=60,
                                stdin=subprocess.DEVNULL)
    except (OSError, subprocess.SubprocessError):
        return None, None
    if head.returncode != 0 or status.returncode != 0:
        return None, None
    return head.stdout.strip(), status.stdout == ""


def repo_root(path):
    try:
        out = subprocess.run(["git", "-C", path, "rev-parse", "--show-toplevel"],
                             capture_output=True, text=True, timeout=60,
                             stdin=subprocess.DEVNULL)
    except (OSError, subprocess.SubprocessError):
        return None
    return out.stdout.strip() if out.returncode == 0 else None


def shown(path, root):
    """A path relative to the repository when inside it, else only its name."""
    path = os.path.realpath(path)
    if root:
        root = os.path.realpath(root)
        if path.startswith(root.rstrip(os.sep) + os.sep):
            return os.path.relpath(path, root)
    return "<outside the repository>/" + os.path.basename(path)


# ── Reports ──────────────────────────────────────────────────────────────────

def build_report(sources, git_start, git_end, root):
    out = {"report_version": REPORT_VERSION, "tool": "eval/pooling/pooling.py",
           "definitions": {"percentiles": DEFINITIONS, "harmonic_mean": HARMONIC}
                          | ({"pooled_d027": POOLED_D027} if any(
                              "reported" in s for src in sources for s in src["series"]) else {}),
           "git": {"head_start": git_start[0], "clean_start": git_start[1],
                   "head_end": git_end[0], "clean_end": git_end[1]},
           "sources": []}
    for src in sources:
        entry = {"kind": src["kind"], "label": src["label"],
                 "inputs": [{"path": shown(f, root), "sha256": sha256_file(f)}
                            for f in src["files"]],
                 "skipped": src["skipped"], "series": []}
        for s in src["series"]:
            st = series_stats(s["scores"])
            rec = s["recorded"]
            checks = {}
            if "p5" in rec and is_number(rec.get("p5")):
                checks["recorded_p5_equals_today_at_4_decimals"] = \
                    f"{rec['p5']:.4f}" == f"{st['percentiles']['p5']['today']:.4f}"
            if is_number(rec.get("harmonic_mean")):
                checks["shifted_harmonic_mean_equals_libvmaf_at_6_decimals"] = \
                    f"{st['harmonic_mean']['shifted']:.6f}" == f"{rec['harmonic_mean']:.6f}"
            if is_number(rec.get("min")):
                checks["recorded_min_equals_min"] = rec["min"] == st["min"]
            out_series = {"label": s["label"], "precision": s["precision"], "stats": st,
                          "recorded": rec, "motion": s.get("motion"), "checks": checks}
            if "reported" in s:
                mine = d027_pooled(s["scores"])
                equal = {k: s["reported"][k] == mine[k] for k in POOLED_D027}
                out_series["d027"] = {"computed": mine, "reported": s["reported"],
                                      "equal_at_full_precision": equal}
                checks["mirage_vmaf_pooled_values_equal_d027_at_full_precision"] = \
                    all(equal.values())
            entry["series"].append(out_series)
        out["sources"].append(entry)
    return out


def fmt(v, digits):
    if v is None:
        return "-"
    if isinstance(v, bool):
        return "yes" if v else "no"
    if isinstance(v, int):
        return str(v)
    s = f"{v:.{digits}f}"
    return s[1:] if s.startswith("-") and not s.strip("-0.") else s


def full(v):
    """A number in full: the shortest text that reads back as the same double."""
    return "-" if v is None else repr(v)


def markdown(r):
    lines = ["# Pooling definitions on committed per-frame scores", "",
             "Work order section 1.5; the owner's decision #6 (D-004, made by D-027). Made by "
             "`eval/pooling/pooling.py`"
             + (f" at commit {r['git']['head_start']}" if r["git"]["head_start"] else "")
             + (", working tree clean at the start and end of the run."
                if r["git"]["clean_start"] and r["git"]["clean_end"]
                else ", WORKING TREE NOT CLEAN during the run."), "",
             "Percentile definitions (n frames, scores sorted ascending):"]
    lines += [f"- **{k}**: {v}." for k, v in DEFINITIONS.items()]
    lines += ["", "Harmonic mean, two forms:"] + \
             [f"- **{k}**: {v}." for k, v in HARMONIC.items()]
    if any(s.get("d027") for src in r["sources"] for s in src["series"]):
        lines += ["", "The pooled values of D-027, as mirage_vmaf computes them:"] + \
                 [f"- **{k}**: {v}." for k, v in POOLED_D027.items()]
    for src in r["sources"]:
        digits = 4 if src["kind"] == "gui-report" else 6
        lines += ["", f"## {src['label']}", ""]
        lines += [f"- input: `{i['path']}` (sha256 {i['sha256']})" for i in src["inputs"]]
        lines += [f"- skipped: {s['label']!r}: {s['error']}" for s in src["skipped"]]
        lines += ["", "### Minimum and the first frame", "",
                  "| series | frames | mean | min | first index at the min | frames at the min "
                  "| first frame's score | min without the first frame (index) |",
                  "|---|---|---|---|---|---|---|---|"]
        for s in src["series"]:
            st = s["stats"]
            lines.append(f"| {s['label']} | {st['frames']} | {fmt(st['mean'], digits)} "
                         f"| {fmt(st['min'], digits)} | {st['min_first_index']} "
                         f"| {st['frames_at_min']} | {fmt(st['first_frame'], digits)} "
                         f"| {fmt(st['min_without_first_frame'], digits)} "
                         f"({st['min_without_first_frame_index']}) |")
        lines += ["", "### Percentiles", "",
                  "| series | percentile | today | nearest-rank | linear | largest difference |",
                  "|---|---|---|---|---|---|"]
        for s in src["series"]:
            for p, v in s["stats"]["percentiles"].items():
                lines.append(f"| {s['label']} | {p} | {fmt(v['today'], digits)} "
                             f"| {fmt(v['nearest-rank'], digits)} | {fmt(v['linear'], digits)} "
                             f"| {fmt(v['largest_difference'], digits)} |")
        lines += ["", "### Means", "",
                  "| series | mean | libvmaf's pooled mean | harmonic, shifted | harmonic, plain "
                  "| frames at or below 0 | libvmaf's harmonic mean |",
                  "|---|---|---|---|---|---|---|"]
        for s in src["series"]:
            st, rec = s["stats"], s["recorded"]
            hm = st["harmonic_mean"]
            lines.append(f"| {s['label']} | {fmt(st['mean'], 6)} | {fmt(rec.get('mean'), 6)} "
                         f"| {fmt(hm['shifted'], 6)} "
                         f"| {fmt(hm['plain'], 6) if hm['plain'] is not None else 'undefined'} "
                         f"| {hm['frames_at_or_below_zero']} "
                         f"| {fmt(rec.get('harmonic_mean'), 6)} |")
        moved = [s for s in src["series"] if s.get("motion")]
        if moved:
            lines += ["", "### Motion of the first frame (FFmpeg's libvmaf log)", "",
                      "| series | integer_motion2, frame 0 | integer_motion, frame 0 "
                      "| frames with integer_motion2 = 0 |", "|---|---|---|---|"]
            for s in moved:
                mo = s["motion"]
                lines.append(f"| {s['label']} | {fmt(mo['frame0_integer_motion2'], 6)} "
                             f"| {fmt(mo['frame0_integer_motion'], 6)} "
                             f"| {mo['frames_with_integer_motion2_zero']} |")
        d027 = [s for s in src["series"] if s.get("d027")]
        if d027:
            lines += ["", "### The pooled values of D-027: mirage_vmaf's report against "
                          "this script", "",
                      "Each value as mirage_vmaf computes it (src/VMAFPool.h), from the "
                      "report's per-frame scores, against the report's own, both written in "
                      "full (the shortest digits that read back as the same double).", "",
                      "| series | value | pooling.py | mirage_vmaf's report | equal |",
                      "|---|---|---|---|---|"]
            for s in d027:
                d = s["d027"]
                lines += [f"| {s['label']} | {k} | {full(d['computed'][k])} "
                          f"| {full(d['reported'][k])} "
                          f"| {fmt(d['equal_at_full_precision'][k], 0)} |" for k in POOLED_D027]
        checked = [s for s in src["series"] if s["checks"]]
        if checked:
            lines += ["", "### Checks", ""]
            for s in checked:
                lines += [f"- {s['label']}: {k.replace('_', ' ')}: {fmt(v, 0)}"
                          for k, v in s["checks"].items()]
    return "\n".join(lines) + "\n"


def parse_args(argv):
    p = argparse.ArgumentParser(prog="pooling.py",
                                description="Pooling definitions computed on committed "
                                            "per-frame VMAF scores (work order section 1.5).")
    p.add_argument("--gui-report", action="append", default=[], metavar="FILE",
                   help="a report of the player's VMAF analyzer (write_json)")
    p.add_argument("--delta-run", action="append", default=[], metavar="DIR",
                   help="an eval/consistency/run_delta.sh output directory")
    p.add_argument("--check-pooled", action="store_true",
                   help="also compute the pooled values of D-027 for Mirage's series of each "
                        "--delta-run and check them against those of mirage_vmaf's report")
    p.add_argument("--out", required=True, metavar="PREFIX",
                   help="writes PREFIX.json and PREFIX.md")
    args = p.parse_args(argv)
    if not args.gui_report and not args.delta_run:
        p.error("give at least one --gui-report or --delta-run")
    return args


def main(argv=None):
    args = parse_args(argv)
    root = repo_root(HERE)
    start = git_state(HERE)
    try:
        sources = []
        for path in args.gui_report:
            src = gui_series(path)
            src["label"] = f"The player's VMAF report {shown(path, root)}"
            sources.append(src)
        for path in args.delta_run:
            src = delta_series(path, args.check_pooled)
            src["label"] = f"The run_delta.sh run {shown(path, root)}"
            sources.append(src)
    except (InputError, OSError, KeyError) as e:
        print(f"pooling: {e}", file=sys.stderr)
        return 1
    report = build_report(sources, start, git_state(HERE), root)
    with open(args.out + ".json", "w", encoding="utf-8") as f:
        f.write(json.dumps(report, indent=2, allow_nan=False) + "\n")
    with open(args.out + ".md", "w", encoding="utf-8") as f:
        f.write(markdown(report))
    print(f"pooling: wrote {args.out}.json and {args.out}.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
