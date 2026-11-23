"""axis_check.py: the check of Q02 (d)'s seconds as revision 1 of the merged Phase 3 design
(design.md §4.2) specifies it, computed on testgen's ladder reports. Each report's generator
layout stands in for the reference's segment map: on these ladders the demux pass's map gave each
segment exactly the layout's frames (design-evaluation run 04, e.g. v360/ad_000.ts frames 75-99).

For each report and layout:
  1. the round-1 wording of the check: each scored reference frame's PTS against
     first_pts + (ref_index - first_index) x one frame within its report sequence
     (alignment.reference.sequences), flagged beyond half a frame (the aligner's tolerance,
     src/VMAFAlign.h:23-25);
  2. revision 1, with the segment map: each step between consecutive reference frames, flagged
     when it differs from one frame by more than half a frame, unless the later frame is the first
     frame of a segment that an EXT-X-DISCONTINUITY precedes (a jump the playlist signals);
  3. revision 1, without a segment map: the steps within one report sequence that differ from one
     frame by more than half a frame, listed as facts, with no claim about the seconds;
  4. (d)'s seconds (ref_index x one frame) against (c)'s (the segment's playlist time, the sum of
     the EXTINF before it, plus the frame's PTS offset from the segment's first PTS): the largest
     difference over the scored reference frames.
With --drop K, the same four are then computed on a copy of the first report from which reference
frame K is removed: the later frames' ref_index lowered by one, and K's segment one frame shorter
in the map, as a reference that lacks one frame would be read. That case is simulated, not a
stream.

Exact arithmetic on ticks of the reference's time base. Read-only: reads the files named, writes
nothing. Python 3 standard library only; for the merged design's runs, not repository code.

usage: python3 -I -B axis_check.py <report.json> <layout.json> [<report.json> <layout.json>]...
                                   [--drop K]
"""

import copy
import json
import sys
from fractions import Fraction


def ticks(seconds, tb):
    return round(Fraction(str(seconds)) / tb)


def count(n, word):
    return f"{n} {word}" + ("" if n == 1 else "s")


def ranges(frames):
    """[3, 4, 5, 9] as '3-5, 9'."""
    out, start, prev = [], None, None
    for f in frames:
        if start is None:
            start = prev = f
        elif f == prev + 1:
            prev = f
        else:
            out.append(f"{start}-{prev}" if prev != start else f"{start}")
            start = prev = f
    if start is not None:
        out.append(f"{start}-{prev}" if prev != start else f"{start}")
    return ", ".join(out) or "none"


def reference_frames(report):
    """{ref_index: ref_pts seconds} over every pair, checked to agree; and the reference's clock."""
    pts, tb, rate, seqs = {}, None, None, None
    for p in report["pairs"]:
        num, den = (int(x) for x in p["ref"]["time_base"].split("/"))
        fn, fd = (int(x) for x in p["alignment"]["reference"]["frame_rate"].split("/"))
        tb, rate = Fraction(num, den), Fraction(fn, fd)
        seqs = p["alignment"]["reference"]["sequences"]
        for f in p["frames"]:
            old = pts.setdefault(f["ref_index"], f["ref_pts"])
            if old != f["ref_pts"]:
                raise SystemExit(f"reference frame {f['ref_index']}: two PTS {old}, {f['ref_pts']}")
    return pts, tb, rate, seqs


def check(name, report, layout):
    pts_s, tb, rate, seqs = reference_frames(report)
    frame = 1 / rate / tb                                  # one frame, in ticks
    half = frame / 2
    pts = {i: ticks(s, tb) for i, s in pts_s.items()}
    order = sorted(pts)
    segs = layout["segments"]
    seg_of = {}
    for s in segs:
        for i in range(s["first_frame"], s["first_frame"] + s["frames"]):
            seg_of[i] = s
    print(f"== {name}: {len(order)} scored reference frames; frame rate {rate}, time base {tb}; "
          f"one frame {frame} ticks; report sequences (first_index, first_pts, frames): "
          + ", ".join(f"({q['first_index']}, {q['first_pts']}, {q['frames']})" for q in seqs)
          + "; segments after an EXT-X-DISCONTINUITY start at reference frames "
          + (", ".join(str(s["first_frame"]) for s in segs if s["discontinuity"]) or "none"))

    # 1. the round-1 wording
    flagged = []
    for i in order:
        q = max((q for q in seqs if q["first_index"] <= i), key=lambda q: q["first_index"])
        expected = ticks(q["first_pts"], tb) + (i - q["first_index"]) * frame
        if abs(pts[i] - expected) > half:
            flagged.append(i)
    print(f"  1. round-1 wording (PTS against the position in its report sequence): "
          f"{count(len(flagged), 'frame')} flagged ({ranges(flagged)})")

    # 2. and 3. revision 1
    seq_first = {q["first_index"] for q in seqs}
    with_map, without_map = [], []
    for a, b in zip(order, order[1:]):
        if b != a + 1:
            continue                                      # not consecutive frames of the reference
        step = pts[b] - pts[a]
        if abs(step - frame) <= half:
            continue
        signalled = b in seg_of and seg_of[b]["first_frame"] == b and seg_of[b]["discontinuity"]
        if not signalled:
            with_map.append((b, step))
        if b not in seq_first:
            without_map.append((b, step))
    text = lambda xs: "; ".join(f"frame {b}, step {float(st * tb):+.3f} s" for b, st in xs) or "none"
    print(f"  2. revision 1, with the segment map: {count(len(with_map), 'step')} flagged "
          f"({text(with_map)})")
    print(f"  3. revision 1, without a segment map: {count(len(without_map), 'step')} within a "
          f"report sequence listed ({text(without_map)})")

    # 4. (d) against (c)
    worst, at = Fraction(0), None
    for i in order:
        s = seg_of[i]
        first_pts = min(pts[j] for j in range(s["first_frame"], s["first_frame"] + s["frames"])
                        if j in pts)
        c = Fraction(str(s["position_s"])) + (pts[i] - first_pts) * tb
        d = i / rate
        if abs(c - d) > worst:
            worst, at = abs(c - d), i
    print(f"  4. (d)'s seconds against (c)'s: largest difference {float(worst):.3f} s"
          + (f" (first at reference frame {at})" if at is not None else ""))


def drop_frame(report, layout, k):
    r, lay = copy.deepcopy(report), copy.deepcopy(layout)
    for p in r["pairs"]:
        p["frames"] = [f for f in p["frames"] if f["ref_index"] != k]
        for f in p["frames"]:
            if f["ref_index"] > k:
                f["ref_index"] -= 1
        for q in p["alignment"]["reference"]["sequences"]:
            if q["first_index"] > k:
                q["first_index"] -= 1
            elif q["first_index"] <= k < q["first_index"] + q["frames"]:
                q["frames"] -= 1
    for s in lay["segments"]:
        if s["first_frame"] > k:
            s["first_frame"] -= 1
        elif s["first_frame"] <= k < s["first_frame"] + s["frames"]:
            s["frames"] -= 1
    return r, lay


def main(argv):
    drop = None
    if "--drop" in argv:
        i = argv.index("--drop")
        drop = int(argv[i + 1])
        del argv[i:i + 2]
    files = argv[1:]
    if not files or len(files) % 2:
        print(__doc__)
        return 2
    loaded = [(files[i], json.load(open(files[i])), json.load(open(files[i + 1])))
              for i in range(0, len(files), 2)]
    for name, report, layout in loaded:
        check(f"{name} with {files[files.index(name) + 1]}", report, layout)
    if drop is not None:
        name, report, layout = loaded[0]
        r, lay = drop_frame(report, layout, drop)
        check(f"{name}, reference frame {drop} removed (simulated)", r, lay)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
