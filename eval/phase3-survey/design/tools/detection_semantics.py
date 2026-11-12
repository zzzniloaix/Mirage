"""detection_semantics.py: two questions of the merged Phase 3 design (design.md), computed on
testgen's ad ladder report (Phase 2's mirage_vmaf, top-rung mode, reference v360) and its layout.

1. Frames that start a libvmaf context (D-037). Where the ad's drop starts, and whether the frame
   the EXT-X-CUE-OUT precedes lies in the drop's half-open interval of reference frames, when the
   first pair of every libvmaf context is
     A  treated like any other point,
     B  left out of the curve (it is never low and never in the baseline),
     C  left out of the baseline only (it can be low, but never enters the baseline).
2. A session's drops (D-068), for two scripted plans of master entries and segment ranges:
     (b) detection on the session's curve, with its own trailing baseline;
     (c) each pair's drops (rule A above) clipped to the spans that played its variant.

The detector is the prototype of testgen's tables.py and design-evaluation's sweep.py: a point is
low when its score is more than T below the median of the last W scores before it that weren't
low, tested once W/2 such scores exist; a drop is a run of more than N consecutive low points.
T = 6, W = 50, N = 12 are the prototype's parameters (design-evaluation run 10), not defaults.

Python 3 standard library only. For the design's runs; not repository code.

usage: python3 -I -B detection_semantics.py <report.json> <layout.json>
"""

import json
import statistics
import sys

T, W, N = 6.0, 50, 12

PLANS = {
    # name: [(master entry, first segment, last segment)], in play order
    "ad-switch (design-evaluation run 10's plan)": [(1, 0, 2), (2, 3, 4), (1, 5, 7)],
    "switch-down (entry 1, then entry 2 from segment 5)": [(1, 0, 4), (2, 5, 7)],
}


def detect(points, no_baseline=frozenset()):
    """points: [(ref_index, score)] in order. Returns [(i0, i1, baseline, min, mean)] by position."""
    low, base, kept = [], [], []
    for ref, v in points:
        pool = kept[-W:]
        if len(pool) >= max(1, W // 2):
            b = statistics.median(pool)
            is_low = v < b - T
        else:
            b, is_low = None, False
        low.append(is_low)
        base.append(b)
        if not is_low and ref not in no_baseline:
            kept.append(v)
    drops, start = [], None
    for i, flag in enumerate(low + [False]):
        if flag and start is None:
            start = i
        elif not flag and start is not None:
            if i - start > N:
                vals = [points[j][1] for j in range(start, i)]
                drops.append((start, i - 1, base[start], min(vals), statistics.fmean(vals)))
            start = None
    return drops


def where(frame, first, last):
    if first <= frame <= last:
        return "inside"
    if frame == last + 1:
        return "at the end (not inside)"
    return "before" if frame < first else "after"


def main(argv):
    report = json.load(open(argv[1]))
    layout = json.load(open(argv[2]))
    segs = layout["segments"]
    cue_out = next(s["first_frame"] for s in segs
                   if any(t.startswith("#EXT-X-CUE-OUT:") for t in s["tags"]))
    cue_in = next(s["first_frame"] for s in segs if "#EXT-X-CUE-IN" in s["tags"])
    print(f"report {argv[1]}; layout {argv[2]}; detector T = {T:g}, W = {W}, N = {N} "
          "(the prototype's parameters)")
    print(f"EXT-X-CUE-OUT precedes reference frame {cue_out}; EXT-X-CUE-IN precedes {cue_in}")

    curves, contexts, by_entry = {}, {}, {}
    for p in report["pairs"]:
        name = p["dis"]["url"]
        to_ref = {f["index"]: f["ref_index"] for f in p["frames"]}
        curves[name] = [(f["ref_index"], f["vmaf"]) for f in p["frames"]]
        contexts[name] = frozenset(to_ref[c["first_index"]]
                                   for c in p["alignment"]["vmaf_contexts"])
        for e in p["entries"]:
            by_entry[e] = name

    print("\n## 1. First pairs of libvmaf contexts")
    pair_drops = {}
    for name, pts in curves.items():
        ctx = contexts[name]
        print(f"{name}: context-first pairs at reference frames {sorted(ctx)}")
        variants = {
            "A (no special case)": (pts, frozenset()),
            "B (left out of the curve)": ([q for q in pts if q[0] not in ctx], frozenset()),
            "C (left out of the baseline only)": (pts, ctx),
        }
        for label, (series, nb) in variants.items():
            drops = detect(series, nb)
            if label.startswith("A"):
                pair_drops[name] = [(series[a][0], series[b][0], bl, mn, me)
                                    for a, b, bl, mn, me in drops]
            if not drops:
                print(f"  {label}: no drop")
            for a, b, bl, mn, me in drops:
                f0, f1 = series[a][0], series[b][0]
                print(f"  {label}: drop at reference frames {f0}-{f1}, half-open [{f0}, {f1 + 1}); "
                      f"baseline {bl:.3f}, min {mn:.3f}, mean {me:.3f}, magnitude {bl - mn:.3f}; "
                      f"cue-out frame {cue_out}: {where(cue_out, f0, f1)}; "
                      f"cue-in frame {cue_in}: {where(cue_in, f0, f1)}")

    print("\n## 2. A session's drops")
    score_at = {name: dict(pts) for name, pts in curves.items()}
    for plan_name, spans in PLANS.items():
        print(f"\nplan {plan_name}: {spans}")
        played = []   # (ref frame, pair name)
        for entry, s0, s1 in spans:
            name = by_entry[entry]
            for s in segs[s0:s1 + 1]:
                for f in range(s["first_frame"], s["first_frame"] + s["frames"]):
                    played.append((f, name))
        switches = [played[i][0] for i in range(1, len(played)) if played[i][1] != played[i - 1][1]]
        print(f"  switches at reference frames {switches}")
        session = [(f, score_at[name][f]) for f, name in played]
        drops = detect(session)
        if not drops:
            print("  (b) session curve, own baseline: no drop")
        for a, b, bl, mn, me in drops:
            f0, f1 = session[a][0], session[b][0]
            inside = [s for s in switches if f0 <= s <= f1]
            print(f"  (b) session curve, own baseline: drop at reference frames {f0}-{f1}; "
                  f"baseline {bl:.3f}, min {mn:.3f}, mean {me:.3f}, magnitude {bl - mn:.3f}; "
                  f"switches in it {inside}")
        clipped = []
        for name in sorted(set(n for _, n in played)):
            frames = sorted(f for f, n in played if n == name)
            fs = set(frames)
            for f0, f1, bl, _mn, _me in pair_drops.get(name, []):
                run = [f for f in range(f0, f1 + 1) if f in fs]
                if not run:
                    continue
                # split into consecutive pieces
                pieces, cur = [], [run[0]]
                for f in run[1:]:
                    if f == cur[-1] + 1:
                        cur.append(f)
                    else:
                        pieces.append(cur)
                        cur = [f]
                pieces.append(cur)
                for piece in pieces:
                    vals = [score_at[name][f] for f in piece]
                    clipped.append((piece[0], piece[-1], name, bl, min(vals), statistics.fmean(vals)))
        if not clipped:
            print("  (c) pair drops clipped to the played spans: no drop")
        for f0, f1, name, bl, mn, me in sorted(clipped):
            inside = [s for s in switches if f0 <= s <= f1]
            print(f"  (c) pair drops clipped to the played spans: {name} at reference frames "
                  f"{f0}-{f1}; the pair's baseline {bl:.3f}, min {mn:.3f}, mean {me:.3f}, "
                  f"magnitude {bl - mn:.3f}; switches in it {inside}")


if __name__ == "__main__":
    main(sys.argv)
