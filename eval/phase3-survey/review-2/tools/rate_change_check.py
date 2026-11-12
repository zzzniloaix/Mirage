"""rate_change_check.py: applies the merged Phase 3 design's check of Q02 (d)'s seconds
(design.md §4.2, as design/tools/axis_check.py implements it) to simulated changes of frame rate
in the reference, for the review (review-2). The simulation takes testgen's ad ladder report and
layout and re-times the reference's ad, frames 75 to 124, at another rate R: frame 75 keeps its
PTS, each later ad frame comes 1/R s after the one before it; the frames after the ad keep their
PTS, as the main content's own timestamps do; the playlist's EXTINF of the two ad segments becomes
25/R s each, so that its segment start times follow the media (the layout's position_s, which
axis_check.py reads as (c)'s clock). The report's frame rate (25/1), sequences and every pair's
scores stay as they are. These are simulations on copies in memory, not streams.

For each R it prints what design/tools/axis_check.py's check() prints (the round-1 wording, the
revised check with and without the segment map, and (d)'s seconds against (c)'s), and then one
check the review proposes as an alternative: (d)'s seconds against (c)'s at the first frame of
every segment, flagged beyond half a frame.

Read-only: reads the files named, writes nothing. Python 3 standard library only; not repository
code.

usage: python3 -I -B rate_change_check.py <axis_check.py> <report.json> <layout.json> R...
"""
import copy
import importlib.util
import json
import sys
from fractions import Fraction

AD_FIRST, AD_LAST, AD_SEGMENTS = 75, 124, (3, 4)


def load_module(path):
    spec = importlib.util.spec_from_file_location("axis_check", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def simulate(report, layout, rate):
    r, lay = copy.deepcopy(report), copy.deepcopy(layout)
    step = Fraction(1, rate)
    for p in r["pairs"]:
        first = None
        for f in p["frames"]:
            if f["ref_index"] == AD_FIRST:
                first = Fraction(str(f["ref_pts"]))
        for f in p["frames"]:
            i = f["ref_index"]
            if AD_FIRST < i <= AD_LAST:
                f["ref_pts"] = float(first + (i - AD_FIRST) * step)
    pos = Fraction(0)
    for s in lay["segments"]:
        s["position_s"] = float(pos)
        pos += Fraction(s["frames"], rate) if s["index"] in AD_SEGMENTS else Fraction(s["frames"], 25)
    return r, lay


def segment_start_check(mod, report, layout):
    pts_s, tb, rate, _ = mod.reference_frames(report)
    pts = {i: mod.ticks(s, tb) for i, s in pts_s.items()}
    half = Fraction(1, 2) / rate
    flagged = []
    for s in layout["segments"]:
        i = s["first_frame"]
        if i not in pts:
            continue
        c = Fraction(str(s["position_s"]))
        d = i / rate
        if abs(c - d) > half:
            flagged.append(f"frame {i}: {float(d - c):+.3f} s")
    print(f"  alternative (the review's): (d) against (c) at each segment's first frame, beyond "
          f"half a frame: {len(flagged)} flagged ({'; '.join(flagged) or 'none'})")


def main(argv):
    if len(argv) < 5:
        print(__doc__)
        return 2
    mod = load_module(argv[1])
    report = json.load(open(argv[2], encoding="utf-8"))
    layout = json.load(open(argv[3], encoding="utf-8"))
    for rate in (int(x) for x in argv[4:]):
        r, lay = simulate(report, layout, rate)
        mod.check(f"{argv[2]}, reference frames {AD_FIRST}-{AD_LAST} re-timed at {rate} fps "
                  f"(simulated; the report's frame rate stays 25/1)", r, lay)
        segment_start_check(mod, r, lay)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
