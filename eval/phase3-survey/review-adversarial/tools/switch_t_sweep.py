"""switch_t_sweep.py: whether a switch alone becomes a drop under Q22 (b) (detection on the
session's curve with its own trailing baseline) as T changes, which design.md's Q22 says "flips
with small changes of T". The detector, the plans and W = 50, N = 12 are those of
<out>/design/tools/detection_semantics.py (imported from there unchanged); only T is swept, and
for each T the session curve's drops are printed. Read-only. Python 3 standard library only;
for the adversarial review of the Phase 3 design, not repository code.

usage: python3 -I -B switch_t_sweep.py <detection_semantics.py> <report.json> <layout.json>
"""

import importlib.util
import json
import statistics
import sys


def main(argv):
    spec = importlib.util.spec_from_file_location("ds", argv[1])
    ds = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ds)
    report = json.load(open(argv[2]))
    layout = json.load(open(argv[3]))
    segs = layout["segments"]
    curves, by_entry = {}, {}
    for p in report["pairs"]:
        name = p["dis"]["url"]
        curves[name] = {f["ref_index"]: f["vmaf"] for f in p["frames"]}
        for e in p["entries"]:
            by_entry[e] = name
    for name, pts in curves.items():
        print(f"{name}: median of all scored frames {statistics.median(pts.values()):.3f}")
    for plan_name, spans in ds.PLANS.items():
        played = []
        for entry, s0, s1 in spans:
            for s in segs[s0:s1 + 1]:
                for f in range(s["first_frame"], s["first_frame"] + s["frames"]):
                    played.append((f, by_entry[entry]))
        session = [(f, curves[name][f]) for f, name in played]
        print(f"\nplan {plan_name}: {spans}; W = {ds.W}, N = {ds.N}")
        for t in (3.0, 4.0, 5.0, 5.5, 6.0, 6.5, 7.0, 8.0):
            ds.T = t
            drops = ds.detect(session)
            text = "; ".join(f"frames {session[a][0]}-{session[b][0]}, baseline {bl:.3f}, "
                             f"min {mn:.3f}" for a, b, bl, mn, _ in drops) or "no drop"
            print(f"  T = {t:g}: {text}")


if __name__ == "__main__":
    main(sys.argv)
