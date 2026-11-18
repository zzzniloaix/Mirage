"""Per-segment tables of a mirage_vmaf --ladder report of a testgen ladder.

usage: python3 -I tables.py <layout.json> <report.json> [--threshold T] [--window W] [--min-frames N]

layout.json is what testgen wrote next to the ladder's master playlist: each segment's position
in the playlist, its first frame and number of frames, its period (main or ad), the PTS of its
first frame and each rung's quantizer. The report is mirage_vmaf's, with each pair's frames
(index, ref_index, dis_index, ref_pts, dis_pts, vmaf).

For each pair of the report it prints, as Markdown:
  - the pair's alignment: frames paired and failed, discontinuities, each input's discontinuity
    sequences and the libvmaf contexts;
  - a table of the segments of the playlist: the reference frames each covers, the pairs whose
    reference frame is in it, their mean and minimum VMAF, and the rung's quantizer there;
  - where the scores are below the pair's median by more than T points (6 unless given): each run
    of such frames, its first and last reference frame and where those are in their segments;
  - a prototype of the work order's automatic detection (§3.2): a frame is low when its score is
    more than T points below the median of the last W scores before it that weren't low (50 unless
    given; no frame is tested before W/2 such scores exist), and each run of more than N low frames
    (12 unless given) is a drop; its first and last reference frame and their segments.
Frames are placed in segments by the reference frame's position in its input (ref_index), which
the layout's first frames give; each pair's reference PTS is checked against the layout's.
"""

import argparse
import json
import statistics
import sys


def args_of(argv):
    p = argparse.ArgumentParser(prog="tables.py")
    p.add_argument("layout")
    p.add_argument("report")
    p.add_argument("--threshold", type=float, default=6.0)
    p.add_argument("--window", type=int, default=50)
    p.add_argument("--min-frames", type=int, default=12)
    return p.parse_args(argv)


def segment_of(layout, ref_index):
    for s in layout["segments"]:
        if s["first_frame"] <= ref_index < s["first_frame"] + s["frames"]:
            return s
    return None


def where(layout, ref_index):
    s = segment_of(layout, ref_index)
    if s is None:
        return f"frame {ref_index} (in no segment)"
    off = ref_index - s["first_frame"]
    return (f"frame {ref_index}: segment {s['index']} ({s['file']}, {s['period']}), "
            f"its frame {off} of {s['frames']}")


def runs(flags):
    out, start = [], None
    for i, f in enumerate(flags + [False]):
        if f and start is None:
            start = i
        elif not f and start is not None:
            out.append((start, i - 1))
            start = None
    return out


def detect(scores, threshold, window, min_frames):
    """The trailing-median prototype: returns (low flags, the baseline at each frame or None)."""
    low, base, kept = [], [], []
    for v in scores:
        b = statistics.median(kept[-window:]) if len(kept) >= window // 2 else None
        is_low = b is not None and v < b - threshold
        low.append(is_low)
        base.append(b)
        if not is_low:
            kept.append(v)
    drops = [(a, z) for a, z in runs(low) if z - a + 1 > min_frames]
    return low, base, drops


def rung_of(layout, url):
    for r in layout["rungs"]:
        if url.startswith(r["name"] + "/"):
            return r["name"]
    return None


def main(argv):
    a = args_of(argv)
    with open(a.layout) as f:
        layout = json.load(f)
    with open(a.report) as f:
        report = json.load(f)
    fps = layout["fps"]
    print(f"# {a.report}: {layout['kind']} ladder, codec {layout['codec']}, source {layout['source']}, "
          f"reference mode {report['reference_mode']}, reference {report['reference']['url']}, "
          f"status {report['status']}")
    print()
    for pair in report["pairs"]:
        dis_url = pair["dis"]["url"]
        rung = rung_of(layout, dis_url)
        frames = pair["frames"]
        al = pair["alignment"]
        print(f"## Pair {pair['index']}: {dis_url} (rung {rung}) against {pair['ref']['url']}")
        print()
        print(f"- frames paired {al['paired']}, failed {al['failed']}, failure rate "
              f"{al['failure_rate']}, discontinuities {al['discontinuities']}, "
              f"after_discontinuity {al['after_discontinuity']}, unmatched_reference "
              f"{al['unmatched_reference']}, extra_distorted {al['extra_distorted']}")
        print(f"- reference sequences {json.dumps(al['reference']['sequences'])}")
        print(f"- distorted sequences {json.dumps(al['distorted']['sequences'])}")
        print(f"- libvmaf contexts {json.dumps(al['vmaf_contexts'])}")
        print(f"- pooled mean {pair['pooled']['mean']:.3f}, min {pair['pooled']['min']:.3f}")
        # The reference PTS of each pair against the layout's.
        mismatched = 0
        for fr in frames:
            s = segment_of(layout, fr["ref_index"])
            if s is None:
                mismatched += 1
                continue
            want = s["pts_first_s"] + (fr["ref_index"] - s["first_frame"]) / fps
            if abs(fr["ref_pts"] - want) > 0.5 / fps:
                mismatched += 1
        print(f"- pairs whose reference PTS isn't the layout's for their reference frame: {mismatched}")
        print()
        print("| segment | file | period | reference frames | pairs | mean VMAF | min VMAF | "
              "quantizer | tags before it |")
        print("|---|---|---|---|---|---|---|---|---|")
        for s in layout["segments"]:
            lo, hi = s["first_frame"], s["first_frame"] + s["frames"] - 1
            v = [fr["vmaf"] for fr in frames if lo <= fr["ref_index"] <= hi]
            mean = f"{statistics.fmean(v):.3f}" if v else "-"
            mn = f"{min(v):.3f}" if v else "-"
            tags = (["EXT-X-DISCONTINUITY"] if s["discontinuity"] else []) + s["tags"]
            print(f"| {s['index']} | {s['file']} | {s['period']} | {lo}-{hi} | {len(v)} | {mean} | "
                  f"{mn} | {s['q'][rung] if rung else '-'} | {', '.join(tags) or '-'} |")
        print()
        scores = [fr["vmaf"] for fr in frames]
        ref_index = [fr["ref_index"] for fr in frames]
        med = statistics.median(scores)
        below = [v < med - a.threshold for v in scores]
        print(f"Scores more than {a.threshold:g} points below the pair's median ({med:.3f}): "
              f"{sum(below)} frames")
        for x, y in runs(below):
            print(f"- pairs {x}-{y} ({y - x + 1} frames): first {where(layout, ref_index[x])}; "
                  f"last {where(layout, ref_index[y])}")
        low, base, drops = detect(scores, a.threshold, a.window, a.min_frames)
        print()
        print(f"Trailing-median prototype (threshold {a.threshold:g}, window {a.window}, more than "
              f"{a.min_frames} frames): {sum(low)} low frames, {len(drops)} drops")
        for x, y in runs(low):
            kind = "drop" if (x, y) in drops else "too short"
            print(f"- pairs {x}-{y} ({y - x + 1} frames, {kind}): first {where(layout, ref_index[x])}; "
                  f"last {where(layout, ref_index[y])}; baseline at the first {base[x]:.3f}, "
                  f"lowest score {min(scores[x:y + 1]):.3f}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
