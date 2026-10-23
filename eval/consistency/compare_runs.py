"""How far the scores of run_delta.py runs move away from those of a base run.

Compares, pair by pair and frame by frame, each run's scores with the base
run's, separately for Mirage's side (mirage_vmaf's reports) and FFmpeg's
side (its libvmaf logs): for example the runs of one set of pairs with the
bicubic and lanczos scalers against the run with bilinear (D-003), or with
other libvmaf models against vmaf_v0.6.1 (D-004). It also repeats each
run's own verdict, Mirage against FFmpeg, from the run's delta.json.

usage: compare_runs.py --base LABEL=DIR --run LABEL=DIR [--run LABEL=DIR ...] --out PREFIX

Writes PREFIX.json and PREFIX.md. A shift is run - base, per frame, over the
frames both runs scored. Run directories are shown relative to the
directory of PREFIX, which should hold them, so that nothing local is
written. Python 3 standard library only; run it with python3 -I -B.

Exit status: 0 the reports were written; 1 a run directory or one of its
files is missing or unreadable; 2 bad arguments.
"""

import argparse
import json
import lzma
import math
import os
import sys

HERE = os.path.dirname(os.path.realpath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import vmafdelta as vd  # noqa: E402

REPORT_VERSION = 1

# The parameters of a run_delta.py run, in the order the reports list them.
PARAMETERS = ("variant", "frames", "threshold", "scaler", "model", "model_file", "threads")


class InputError(Exception):
    pass


def labeled_dir(text):
    label, sep, path = text.partition("=")
    if not sep or not label or not path:
        raise argparse.ArgumentTypeError(f"{text!r} is not LABEL=DIR")
    return label, path


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="compare_runs.py",
        description="How far the per-frame scores of run_delta.py runs move from a base "
                    "run's, on Mirage's side and on FFmpeg's.")
    p.add_argument("--base", type=labeled_dir, required=True, metavar="LABEL=DIR",
                   help="the run the others are compared with")
    p.add_argument("--run", type=labeled_dir, action="append", required=True,
                   metavar="LABEL=DIR", help="a run to compare with the base run (repeatable)")
    p.add_argument("--out", required=True, metavar="PREFIX",
                   help="writes PREFIX.json and PREFIX.md")
    args = p.parse_args(argv)
    labels = [args.base[0]] + [label for label, _ in args.run]
    if len(set(labels)) != len(labels):
        p.error("every run needs its own label")
    return args


def read_json(path):
    """The JSON of `path`, or of `path`.xz when only that exists."""
    for name, opener in ((path, open), (path + ".xz", lzma.open)):
        if os.path.exists(name):
            try:
                with opener(name, "rt", encoding="utf-8") as f:
                    return json.load(f)
            except (OSError, ValueError, lzma.LZMAError) as e:
                raise InputError(f"{name}: {e}") from None
    raise InputError(f"{path} (or {os.path.basename(path)}.xz) is missing")


def load_run(label, path, shown):
    """A run directory: its delta.json, and both sides' scores of each pair."""
    delta_path = os.path.join(path, "delta.json")
    overall = read_json(delta_path)
    pairs = {}
    for s in overall.get("pairs") or []:
        name = s["pair"]
        pdir = os.path.join(path, name)
        side = {"self_check": {"status": s.get("status"), "max_abs": s.get("max_abs"),
                               "over_threshold": s.get("over_threshold"),
                               "frames": s.get("frames")},
                "mirage": None, "ffmpeg": None, "pooled_mean": {"mirage": None, "ffmpeg": None},
                "settings": None}
        try:
            rep = read_json(os.path.join(pdir, "mirage.json"))
            side["mirage"] = vd.mirage_scores(rep)
            side["pooled_mean"]["mirage"] = (rep.get("pooled") or {}).get("mean")
            side["settings"] = {k: rep.get(k) for k in ("model", "model_source", "model_sha256",
                                                        "scaler")}
        except (InputError, ValueError):
            pass   # no report: the run's own delta.json says why
        try:
            log = read_json(os.path.join(pdir, "ffmpeg.json"))
            side["ffmpeg"] = vd.ffmpeg_scores(log)
            side["pooled_mean"]["ffmpeg"] = \
                ((log.get("pooled_metrics") or {}).get("vmaf") or {}).get("mean")
        except (InputError, ValueError):
            pass
        pairs[name] = side
    return {
        "label": label,
        "dir": shown,
        "delta_json_sha256": vd.sha256_file(delta_path),
        "status": overall.get("status"),
        "parameters": {k: (overall.get("parameters") or {}).get(k) for k in PARAMETERS},
        "mirage_vmaf_sha256": (overall.get("mirage_vmaf") or {}).get("sha256"),
        "git_head": (overall.get("git") or {}).get("head_start"),
        "ffmpeg_version": (overall.get("environment") or {}).get("ffmpeg_version"),
        "pairs": pairs,
    }


def shift(base, run):
    """Per-frame shift run - base over the frames both lists have, or None
    when either side has no scores."""
    if base is None or run is None:
        return None
    n = min(len(base), len(run))
    d = [run[i] - base[i] for i in range(n)]
    size = [abs(x) for x in d]
    top = max(range(n), key=lambda i: (size[i], -i)) if n else None
    mean = lambda v: math.fsum(v) / len(v) if v else None  # noqa: E731
    return {"compared": n, "base_frames": len(base), "run_frames": len(run),
            "max_abs": size[top] if n else None, "max_abs_index": top,
            "mean_signed": mean(d), "mean_abs": mean(size),
            "mean_base": mean(base[:n]), "mean_run": mean(run[:n])}


def compare(base, runs):
    """The comparison report of loaded runs, ready for json.dumps."""
    everyone = [base] + runs
    warnings = []
    for key, what in (("mirage_vmaf_sha256", "mirage_vmaf builds"), ("git_head", "commits"),
                      ("ffmpeg_version", "FFmpeg versions")):
        values = {r[key] for r in everyone}
        if len(values) > 1:
            warnings.append(f"the runs come from different {what}: " +
                            ", ".join(f"{r['label']} {r[key]}" for r in everyone))
    differ = [k for k in PARAMETERS if len({json.dumps(r["parameters"][k]) for r in everyone}) > 1]
    for k in ("frames", "variant", "threshold"):
        if k in differ:
            warnings.append(f"the runs have different {k} parameters, so they did not score "
                            "the same frames the same way")
    pairs = []
    for name in base["pairs"]:
        b = base["pairs"][name]
        entry = {"pair": name, "base": {"self_check": b["self_check"],
                                        "pooled_mean": b["pooled_mean"],
                                        "settings": b["settings"]}, "runs": {}}
        for r in runs:
            o = r["pairs"].get(name)
            if o is None:
                warnings.append(f"{r['label']} has no pair {name}")
                continue
            entry["runs"][r["label"]] = {
                "self_check": o["self_check"], "pooled_mean": o["pooled_mean"],
                "settings": o["settings"],
                "mirage": shift(b["mirage"], o["mirage"]),
                "ffmpeg": shift(b["ffmpeg"], o["ffmpeg"]),
            }
            for side in ("mirage", "ffmpeg"):
                s = entry["runs"][r["label"]][side]
                if s and s["base_frames"] != s["run_frames"]:
                    warnings.append(f"{name}, {side}: {r['label']} scored {s['run_frames']} "
                                    f"frames, {base['label']} {s['base_frames']}")
        pairs.append(entry)
    for r in runs:
        for name in r["pairs"]:
            if name not in base["pairs"]:
                warnings.append(f"{r['label']} has a pair the base run lacks: {name}")
    strip = lambda r: {k: v for k, v in r.items() if k != "pairs"}  # noqa: E731
    return {"report_version": REPORT_VERSION, "tool": "eval/consistency/compare_runs.py",
            "base": strip(base), "runs": [strip(r) for r in runs],
            "parameters_that_differ": differ, "warnings": warnings, "pairs": pairs}


def _param_text(v):
    if isinstance(v, dict):
        return f"{v.get('path')} (sha256 {v.get('sha256')})"
    return "-" if v is None else str(v)


def markdown(c):
    """The Markdown report of compare()."""
    base = c["base"]
    everyone = [base] + c["runs"]
    lines = [f"# Score shifts against the {base['label']} run", "",
             "Each run below is an `eval/consistency/run_delta.sh` run over the same pairs; a "
             "shift is that run's score minus the base run's, per frame, over the frames both "
             "scored. Mirage's side are `mirage_vmaf`'s reports (full precision), FFmpeg's its "
             "libvmaf logs (6 decimals). Made by `eval/consistency/compare_runs.py`.", "",
             "| run | directory | status (Mirage vs FFmpeg) | " +
             " | ".join(c["parameters_that_differ"] or ["parameters"]) + " |",
             "|---|---|---|" + "---|" * max(1, len(c["parameters_that_differ"]))]
    for r in everyone:
        cells = [_param_text(r["parameters"][k]) for k in c["parameters_that_differ"]] or \
                ["the same as the base run's"]
        lines.append(f"| {r['label']}{' (base)' if r is base else ''} | `{r['dir']}` | "
                     f"{r['status']} | " + " | ".join(vd.cell(x) for x in cells) + " |")
    same = [k for k in PARAMETERS if k not in c["parameters_that_differ"]]
    lines += ["", "Parameters all runs share: " +
              ", ".join(f"{k} {_param_text(base['parameters'][k])}" for k in same) + ".",
              f"mirage_vmaf: sha256 {base['mirage_vmaf_sha256']}"
              + ("" if len({r['mirage_vmaf_sha256'] for r in everyone}) == 1
                 else " in the base run; others differ (see the warnings)")
              + f"; commit {base['git_head']}"
              + ("" if len({r['git_head'] for r in everyone}) == 1 else " in the base run")
              + "."]

    lines += ["", "## Mirage against FFmpeg in each run", "",
              "From each run's own `delta.json` (\\|delta\\| = \\|Mirage - FFmpeg\\| per frame).",
              "", "| pair | run | status | max \\|delta\\| | frames over the threshold |",
              "|---|---|---|---|---|"]
    for p in c["pairs"]:
        rows = [(base["label"], p["base"]["self_check"])] + \
               [(label, v["self_check"]) for label, v in p["runs"].items()]
        for label, sc in rows:
            lines.append(f"| {p['pair']} | {label} | {sc['status']} | {vd.num(sc['max_abs'])} "
                         f"| {vd.num(sc['over_threshold'])} |")

    for side, title in (("mirage", "Mirage's scores"), ("ffmpeg", "FFmpeg's scores")):
        lines += ["", f"## How far {title} move from the {base['label']} run's", "",
                  "| pair | run | frames compared | max \\|shift\\| (index) | mean shift "
                  "| mean \\|shift\\| | pooled mean, base | pooled mean, run | pooled mean shift |",
                  "|---|---|---|---|---|---|---|---|---|"]
        for p in c["pairs"]:
            pb = p["base"]["pooled_mean"][side]
            for label, v in p["runs"].items():
                s = v[side]
                pr = v["pooled_mean"][side]
                if s is None:
                    lines.append(f"| {p['pair']} | {label} | - | - | - | - | {vd.num(pb)} "
                                 f"| {vd.num(pr)} | - |")
                    continue
                pd = pr - pb if pr is not None and pb is not None else None
                lines.append(f"| {p['pair']} | {label} | {s['compared']} "
                             f"| {vd.num(s['max_abs'])} ({s['max_abs_index']}) "
                             f"| {vd.num(s['mean_signed'])} | {vd.num(s['mean_abs'])} "
                             f"| {vd.num(pb)} | {vd.num(pr)} | {vd.num(pd)} |")
    if c["warnings"]:
        lines += ["", "Warnings:"] + [f"- {vd.cell(w)}" for w in c["warnings"]]
    return "\n".join(lines) + "\n"


def main(argv=None):
    args = parse_args(argv)
    out_dir = os.path.dirname(os.path.abspath(args.out))
    try:
        loaded = [load_run(label, path, os.path.relpath(os.path.abspath(path), out_dir))
                  for label, path in [args.base] + args.run]
    except (InputError, OSError, KeyError, TypeError) as e:
        print(f"compare_runs: {e}", file=sys.stderr)
        return 1
    report = compare(loaded[0], loaded[1:])
    with open(args.out + ".json", "w", encoding="utf-8") as f:
        f.write(vd.to_json(report))
    with open(args.out + ".md", "w", encoding="utf-8") as f:
        f.write(markdown(report))
    print(f"compare_runs: wrote {args.out}.json and {args.out}.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
