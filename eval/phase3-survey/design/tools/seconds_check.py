"""seconds_check.py: the check of the seconds of Q02 (c) and (d) as revision 2 of the merged Phase 3
design (design.md §4.2) specifies it, computed on testgen's ladder reports. Each report's generator
layout stands in for the reference's segment map: on these ladders the demux pass's map gave each
segment exactly the layout's frames (design-evaluation run 04).

For each report, with its layout or with '-' for a reference that has no segment map:
  revision 1's check, for comparison: each step between consecutive reference frames against one
     frame duration, flagged beyond half a frame unless the later frame is the first frame of a
     segment that an EXT-X-DISCONTINUITY precedes (design/tools/axis_check.py's second check);
  revision 2 (1): (d)'s seconds (ref_index x one frame) against (c)'s (the segment's playlist time,
     the sum of the EXTINF before it, plus the frame's PTS offset from the segment's first PTS) at
     every scored reference frame;
  revision 2 (2): each frame's PTS against the PTS of the first scored frame of its playlist
     discontinuity sequence (a new one at every segment that an EXT-X-DISCONTINUITY precedes) plus
     its distance in frames times one frame duration;
  without a segment map: (2) on the report's reference sequences (alignment.reference.sequences)
     instead, which is the computation of the first version of this design's check;
  the second review's suggestion, for comparison: (d)'s seconds against (c)'s at the first frame of
     each segment only (review-2/tools/rate_change_check.py's alternative).
Each check prints its runs of consecutive scored frames beyond half a frame (the aligner's
tolerance, src/VMAFAlign.h:23-25), each with its first and last frame and its largest difference,
signed: (d) minus (c) for (1), the PTS minus the expected PTS for (2). The largest difference of
(d)'s seconds from (c)'s follows.

Simulations, on copies in memory of the first report and its layout, each labelled so; none is a
stream:
  --drop K...        reference frame K missing: the later frames' ref_index lowered by one, and
                     K's segment one frame shorter in the map; once per K;
  --retime A B R...  reference frames A to B at R frames per second: frame A keeps its PTS, each
                     later one comes 1/R s after the one before, the frames after B keep theirs,
                     and the EXTINF of every segment within A to B becomes its frames / R, so that
                     the playlist time follows the media; once per R;
  --extinf S F       the EXTINF of segment S lengthened by F frame durations: the playlist time of
                     the later segments moves, the frames don't.
Under --retime and --extinf the report's frame rate, sequences and scores stay as they are.

Exact arithmetic (fractions of seconds, ticks of the reference's time base). Read-only: reads the
files named, writes nothing. Python 3 standard library only; for the merged design's runs, not
repository code.

usage: python3 -I -B seconds_check.py <report.json> <layout.json or -> [<report> <layout or ->]...
                                      [--drop K...] [--retime A B R...] [--extinf S F]
"""

import copy
import json
import sys
from fractions import Fraction


def frac(x):
    return Fraction(str(x))


def reference(report):
    """{ref_index: PTS seconds} over every pair, checked to agree; time base, rate, sequences."""
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


def runs(flags):
    """[(frame, difference)] of flagged frames, in frame order, as runs of consecutive frames."""
    out = []
    for i, d in flags:
        if out and i == out[-1]["last"] + 1:
            r = out[-1]
            r["last"], r["n"] = i, r["n"] + 1
            if abs(d) > abs(r["worst"]):
                r["worst"], r["at"] = d, i
        else:
            out.append({"first": i, "last": i, "n": 1, "worst": d, "at": i})
    return out


def text(rs):
    if not rs:
        return "0 runs (none)"
    parts = [f"frames {r['first']}-{r['last']} ({r['n']} frames), largest {float(r['worst']):+.3f} s "
             f"at frame {r['at']}" for r in rs]
    return f"{len(rs)} run{'' if len(rs) == 1 else 's'} (" + "; ".join(parts) + ")"


def grid_flags(order, pts, starts, first_pts, frame_ticks, tb):
    """(2): PTS against the frame grid from the first scored frame of each sequence."""
    flags, seq_first = [], None
    starts = sorted(starts)
    for i in order:
        s = max(x for x in starts if x <= i)
        if seq_first is None or seq_first[0] != s:
            base = first_pts.get(s)
            seq_first = (s, i if base is None else s, pts[i] if base is None else base)
        _, f0, p0 = seq_first
        expected = p0 + (i - f0) * frame_ticks
        if abs(pts[i] - expected) > frame_ticks / 2:
            flags.append((i, (pts[i] - expected) * tb))
    return flags


def check(name, report, layout):
    pts_s, tb, rate, seqs = reference(report)
    frame_s = 1 / rate
    frame_ticks = frame_s / tb
    pts = {i: round(frac(s) / tb) for i, s in pts_s.items()}
    order = sorted(pts)
    head = (f"== {name}: {len(order)} scored reference frames; frame rate {rate}, time base {tb}; "
            f"one frame {frame_ticks} ticks; report sequences (first_index, first_pts, frames): "
            + ", ".join(f"({q['first_index']}, {q['first_pts']}, {q['frames']})" for q in seqs))
    report_starts = [q["first_index"] for q in seqs]
    report_first = {q["first_index"]: round(frac(q["first_pts"]) / tb) for q in seqs}
    if layout is None:
        print(head + "; no segment map")
        print("  revision 2 without a segment map, (2) on the report's sequences: "
              + text(runs(grid_flags(order, pts, report_starts, report_first, frame_ticks, tb))))
        return
    segs = layout["segments"]
    seg_of = {}
    for s in segs:
        for i in range(s["first_frame"], s["first_frame"] + s["frames"]):
            seg_of[i] = s
    disc = [s["first_frame"] for s in segs if s["discontinuity"]]
    print(head + "; segments after an EXT-X-DISCONTINUITY start at reference frames "
          + (", ".join(str(x) for x in disc) or "none"))

    # revision 1's check, for comparison
    steps = []
    for a, b in zip(order, order[1:]):
        if b != a + 1:
            continue
        step = pts[b] - pts[a]
        if abs(step - frame_ticks) <= frame_ticks / 2:
            continue
        signalled = b in seg_of and seg_of[b]["first_frame"] == b and seg_of[b]["discontinuity"]
        if not signalled:
            steps.append(f"frame {b}, step {float(step * tb):+.3f} s")
    print(f"  revision 1's check (each step against one frame, with the segment map): "
          f"{len(steps)} step{'' if len(steps) == 1 else 's'} flagged ({'; '.join(steps) or 'none'})")

    # revision 2 (1): (d) against (c) at every scored reference frame
    flags1, worst, at = [], Fraction(0), None
    for i in order:
        s = seg_of[i]
        seg_pts = [pts[j] for j in range(s["first_frame"], s["first_frame"] + s["frames"]) if j in pts]
        c = frac(s["position_s"]) + (pts[i] - min(seg_pts)) * tb
        d = i * frame_s
        if abs(d - c) > frame_s / 2:
            flags1.append((i, d - c))
        if abs(d - c) > worst:
            worst, at = abs(d - c), i
    print("  revision 2 (1), (d) against (c) at every scored reference frame: " + text(runs(flags1)))
    starts_flagged = [f"frame {i}: {float(d):+.3f} s" for i, d in flags1 if seg_of[i]["first_frame"] == i]
    print(f"  the second review's suggestion, (d) against (c) at each segment's first frame only: "
          f"{len(starts_flagged)} flagged ({'; '.join(starts_flagged) or 'none'})")

    # revision 2 (2): PTS against the frame grid of each playlist discontinuity sequence
    starts = [0] + disc
    print("  revision 2 (2), PTS against the frame grid of each playlist discontinuity sequence: "
          + text(runs(grid_flags(order, pts, starts, {}, frame_ticks, tb))))
    print("  revision 2 without a segment map, (2) on the report's sequences: "
          + text(runs(grid_flags(order, pts, report_starts, report_first, frame_ticks, tb))))
    print(f"  largest difference of (d)'s seconds from (c)'s: {float(worst):.3f} s"
          + (f" (first at reference frame {at})" if at is not None else ""))


def extinf_of(layout, rate):
    """Each segment's EXTINF, from the differences of the layout's playlist times."""
    segs = layout["segments"]
    out = []
    for k, s in enumerate(segs):
        if k + 1 < len(segs):
            out.append(frac(segs[k + 1]["position_s"]) - frac(s["position_s"]))
        else:
            out.append(Fraction(s["frames"]) / rate)
    return out


def set_extinf(layout, extinf):
    pos = frac(layout["segments"][0]["position_s"])
    for s, e in zip(layout["segments"], extinf):
        s["position_s"] = str(pos)
        pos += e


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


def retime(report, layout, a, b, new_rate):
    r, lay = copy.deepcopy(report), copy.deepcopy(layout)
    _, _, rate, _ = reference(report)
    step = 1 / Fraction(new_rate)
    for p in r["pairs"]:
        base = next((frac(f["ref_pts"]) for f in p["frames"] if f["ref_index"] == a), None)
        if base is None:
            raise SystemExit(f"reference frame {a} not scored in pair {p.get('index')}")
        for f in p["frames"]:
            if a < f["ref_index"] <= b:
                f["ref_pts"] = str(base + (f["ref_index"] - a) * step)
    extinf = extinf_of(lay, rate)
    for k, s in enumerate(lay["segments"]):
        if a <= s["first_frame"] and s["first_frame"] + s["frames"] - 1 <= b:
            extinf[k] = Fraction(s["frames"]) * step
    set_extinf(lay, extinf)
    return r, lay


def lengthen_extinf(report, layout, seg, frames):
    lay = copy.deepcopy(layout)
    _, _, rate, _ = reference(report)
    extinf = extinf_of(lay, rate)
    extinf[seg] += Fraction(frames) / rate
    set_extinf(lay, extinf)
    return report, lay


def main(argv):
    args = argv[1:]
    drops = retimes = extinf = None

    def values(flag):
        i = args.index(flag)
        j = i + 1
        while j < len(args) and not args[j].startswith("--"):
            j += 1
        vals = [int(x) for x in args[i + 1:j]]
        del args[i:j]
        return vals

    if "--drop" in args:
        drops = values("--drop")
        if not drops:
            print(__doc__)
            return 2
    if "--extinf" in args:
        i = args.index("--extinf")
        extinf = (int(args[i + 1]), int(args[i + 2]))
        del args[i:i + 3]
    if "--retime" in args:
        vals = values("--retime")
        if len(vals) < 3:
            print(__doc__)
            return 2
        retimes = (vals[0], vals[1], vals[2:])
    if not args or len(args) % 2:
        print(__doc__)
        return 2
    loaded = []
    for k in range(0, len(args), 2):
        rep = json.load(open(args[k], encoding="utf-8"))
        lay = None if args[k + 1] == "-" else json.load(open(args[k + 1], encoding="utf-8"))
        loaded.append((args[k], args[k + 1], rep, lay))
    for name, lname, rep, lay in loaded:
        check(f"{name} with {'no segment map' if lay is None else lname}", rep, lay)
    name, _, rep, lay = loaded[0]
    if lay is None and (drops or retimes or extinf):
        raise SystemExit("the simulations need the first report's layout")
    for k in drops or []:
        r, l2 = drop_frame(rep, lay, k)
        check(f"{name}, reference frame {k} removed (simulated)", r, l2)
    if retimes:
        a, b, rates = retimes
        for rr in rates:
            r, l2 = retime(rep, lay, a, b, rr)
            check(f"{name}, reference frames {a}-{b} at {rr} fps (frame {a} keeps its PTS), the "
                  f"EXTINF of the segments within them following the media (simulated; the report's "
                  f"frame rate stays as it is)", r, l2)
    if extinf:
        seg, n = extinf
        r, l2 = lengthen_extinf(rep, lay, seg, n)
        check(f"{name}, the EXTINF of segment {seg} lengthened by {n} frame duration"
              f"{'' if n == 1 else 's'} (simulated; the frames unchanged)", r, l2)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
