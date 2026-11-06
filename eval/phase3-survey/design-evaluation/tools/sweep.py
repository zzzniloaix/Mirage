"""Sweeps prototypes of §3.2's automatic drop detection over the testgen ladder reports, to see
for which parameters each case's known drop is found and each control finds none. It informs the
acceptance tests' own parameters; it is not a measurement for the tool's defaults (synthetic
content, one source picture, 200 frames per pair).

usage: python3 -I -B sweep.py <testgen reports dir>

The cases: each pair of the testgen reports listed in CASES, with its ground truth from the
ladder's layout.json: the reference frames (ref_index) of the segments whose quantizer for the
pair's rung is above the ladder's normal one; a pair with no such segment is a control.

Detectors (a frame is "low" when its score is more than T points below the baseline b; a drop is
a run of more than N consecutive low frames, the work order's "for more than N frames"):
  median_nonlow  b = median of the last W scores before the frame that weren't low; no frame is
                 tested before W/2 such scores exist (testgen's tables.py prototype)
  mean_nonlow    the same with the mean
  median_all     b = median of the last W scores before the frame, low or not; tested from
                 W/2 scores on
Outcome of a case: "exact" when the drops are exactly the ground truth's run; for a target,
"hit" when one drop overlaps the truth and none lies outside it, "miss" otherwise; for a
control, "ok" with no drop, "fp" otherwise. Python 3 standard library only; for the Phase 3
design (design-evaluation.md).
"""

import json
import os
import statistics
import sys

# report file, layout file, mode
CASES = [
    ("degraded-x264.json", "degraded-x264-a.layout.json"),
    ("degraded-mpeg2.json", "degraded-mpeg2-a.layout.json"),
    ("degraded-x264-mezzanine.json", "degraded-x264-a.layout.json"),
    ("ad-x264.json", "ad-x264-a.layout.json"),
    ("ad-mpeg2.json", "ad-mpeg2-a.layout.json"),
    ("ad-x264-origin1.json", "ad-x264-origin1.layout.json"),
    ("ad-x264-nodisc.json", "ad-x264-nodisc.layout.json"),
    ("ad-x264-busy.json", "ad-x264-busy.layout.json"),
    ("ad-x264-control.json", "ad-x264-control.layout.json"),
    ("ad-x264-busy-control.json", "ad-x264-busy-control.layout.json"),
]
# The two acceptance cases of D-069 as testgen's defaults make them, with their controls.
ACCEPTANCE = {("degraded-x264.json", "v270"), ("degraded-x264.json", "v180"),
              ("ad-x264.json", "v180"), ("ad-x264.json", "v270")}

THRESHOLDS = [3, 4, 5, 6, 7, 8, 10, 12, 15, 20, 25, 30, 40, 50]
WINDOWS = [10, 25, 50, 100]
MIN_FRAMES = [0, 1, 2, 3, 4, 5, 8, 12, 16, 20, 24, 30, 40, 49]
DETECTORS = ["median_nonlow", "mean_nonlow", "median_all"]


def truth_of(layout, rung):
    normal = layout.get("degraded", layout.get("ad"))["q_normal"]
    frames = set()
    for s in layout["segments"]:
        if s["q"].get(rung, normal) > normal:
            frames.update(range(s["first_frame"], s["first_frame"] + s["frames"]))
    return frames


def low_flags(scores, detector, window, threshold):
    low, kept, seen = [], [], []
    for v in scores:
        pool = (kept if detector != "median_all" else seen)[-window:]
        if len(pool) >= max(1, window // 2):
            b = statistics.fmean(pool) if detector == "mean_nonlow" else statistics.median(pool)
            is_low = v < b - threshold
        else:
            is_low = False
        low.append(is_low)
        seen.append(v)
        if not is_low:
            kept.append(v)
    return low


def runs(flags):
    out, start = [], None
    for i, f in enumerate(list(flags) + [False]):
        if f and start is None:
            start = i
        elif not f and start is not None:
            out.append((start, i - 1))
            start = None
    return out


def outcome(drops_ref, truth):
    if not truth:
        return "ok" if not drops_ref else "fp"
    lo, hi = min(truth), max(truth)
    if drops_ref == [(lo, hi)]:
        return "exact"
    overlap = [d for d in drops_ref if d[1] >= lo and d[0] <= hi]
    outside = [d for d in drops_ref if d[1] < lo or d[0] > hi]
    return "hit" if overlap and not outside else "miss"


def main(argv):
    rdir = argv[0]
    curves = []   # (report, rung, ref_index list, scores, truth)
    for rep, lay in CASES:
        with open(os.path.join(rdir, rep)) as f:
            report = json.load(f)
        with open(os.path.join(rdir, lay)) as f:
            layout = json.load(f)
        for pair in report["pairs"]:
            rung = pair["dis"]["url"].split("/")[0]
            frames = sorted(pair["frames"], key=lambda fr: fr["index"])
            curves.append((rep, rung, [fr["ref_index"] for fr in frames],
                           [fr["vmaf"] for fr in frames], truth_of(layout, rung)))
    print(f"{len(curves)} curves: " + ", ".join(
        f"{r}:{g}({'target ' + str(min(t)) + '-' + str(max(t)) if t else 'control'})"
        for r, g, _, _, t in curves))
    results = {}
    for det in DETECTORS:
        for w in WINDOWS:
            for t in THRESHOLDS:
                lows = [low_flags(sc, det, w, t) for _, _, _, sc, _ in curves]
                for n in MIN_FRAMES:
                    res = []
                    for (rep, rung, ri, sc, truth), low in zip(curves, lows):
                        drops = [(ri[a], ri[z]) for a, z in runs(low) if z - a + 1 > n]
                        res.append(outcome(drops, truth))
                    results[(det, w, t, n)] = res
    acc_idx = [i for i, (r, g, _, _, _) in enumerate(curves) if (r, g) in ACCEPTANCE]

    def mark(res, idx):
        sel = [res[i] for i in idx]
        if all(x in ("exact", "ok") for x in sel):
            return "E"
        if all(x in ("exact", "hit", "ok") for x in sel):
            return "h"
        return "."

    for label, idx in (("the four acceptance curves (degraded-x264 v270, v180; ad-x264 v180, v270)",
                        acc_idx), ("all curves", list(range(len(curves))))):
        print()
        print(f"## Grids over {label}: E = every target exact and every control without a drop; "
              f"h = every target hit (not all exact) and every control without a drop; . = some "
              f"target missed or some control has a drop")
        for det in DETECTORS:
            for w in WINDOWS:
                print()
                print(f"{det}, W = {w}: rows T, columns N = " + " ".join(f"{n:>2}" for n in MIN_FRAMES))
                for t in THRESHOLDS:
                    print(f"  T {t:>2}: " + " ".join(f"{mark(results[(det, w, t, n)], idx):>2}"
                                                    for n in MIN_FRAMES))
    print()
    print("## Each curve at the work order's suggested T = 6, median_nonlow, W = 50")
    for n in MIN_FRAMES:
        res = results[("median_nonlow", 50, 6, n)]
        print(f"N = {n:>2}: " + ", ".join(f"{r}:{g}={o}" for (r, g, _, _, _), o in zip(curves, res)))
    print()
    print("## The drops found at T = 6, median_nonlow, W = 50, N = 0 (every run of low frames)")
    for (rep, rung, ri, sc, truth) in curves:
        low = low_flags(sc, "median_nonlow", 50, 6)
        rr = [(ri[a], ri[z], z - a + 1) for a, z in runs(low)]
        print(f"{rep}:{rung}: " + (", ".join(f"{a}-{z} ({k})" for a, z, k in rr) or "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
