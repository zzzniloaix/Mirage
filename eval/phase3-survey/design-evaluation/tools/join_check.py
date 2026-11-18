"""Checks the design's frame -> segment join on the testgen ladders: each frame of a mirage_vmaf
ladder report is placed in a segment of its input by its position (ref_index or dis_index)
against the presentation order the demux pass gives (each segment's video packets by PTS, the
segments in the order they were read), and the PTS the report gives the frame is compared with
the PTS of the packet at that position.

usage: python3 -I -B join_check.py <report.json> <segmap dir> <ladder name>

<segmap dir>/<ladder>-<rung>.json are segmap's outputs (tools/segmap.cpp); a rung is the
directory part of a playlist URL ("v270/index.m3u8" -> v270). For each pair, for its reference
and its distorted input, prints: frames placed, frames whose PTS agrees with the packet's (to the
tick of the stream's time base), frames whose PTS is found in more than one segment of the input
(where a PTS alone can't name the segment), and the segments' frame ranges. Python 3 standard
library only; for the Phase 3 design (design-evaluation.md).
"""

import json
import os
import sys
from fractions import Fraction


def load_segmap(path):
    with open(path) as f:
        m = json.load(f)
    num, den = m["time_base"]
    order, counts = [], {}
    nseg = 1 + max(p["segment_by_pos"] for p in m["packets"])
    for s in range(nseg):
        pts = sorted(p["pts"] for p in m["packets"] if p["segment_by_pos"] == s)
        counts[s] = len(pts)
        order += [(s, t) for t in pts]
    urls = [f["url"] for f in m["files"][1:] if f["open_result"] >= 0]
    by_pts = {}
    for s, t in order:
        by_pts.setdefault(t, set()).add(s)
    return {"tb": Fraction(num, den), "order": order, "counts": counts, "urls": urls, "by_pts": by_pts}


def ticks(seconds, tb):
    return round(Fraction(seconds) / tb)


def check(frames, key_index, key_pts, sm):
    placed = agree = ambiguous = 0
    seg_ranges = {}
    for fr in frames:
        i = fr[key_index]
        if i is None or i >= len(sm["order"]):
            continue
        placed += 1
        s, t = sm["order"][i]
        if fr[key_pts] is not None and ticks(fr[key_pts], sm["tb"]) == t:
            agree += 1
        if len(sm["by_pts"].get(t, ())) > 1:
            ambiguous += 1
        lo, hi = seg_ranges.get(s, (i, i))
        seg_ranges[s] = (min(lo, i), max(hi, i))
    return placed, agree, ambiguous, seg_ranges


def main(argv):
    report_path, segdir, ladder = argv
    with open(report_path) as f:
        report = json.load(f)
    rung = lambda url: url.split("/")[0]
    ref = load_segmap(os.path.join(segdir, f"{ladder}-{rung(report['reference']['url'])}.json"))
    print(f"# {ladder}: report {os.path.basename(report_path)}, reference {report['reference']['url']}")
    for pair in report["pairs"]:
        dis = load_segmap(os.path.join(segdir, f"{ladder}-{rung(pair['dis']['url'])}.json"))
        for side, sm, ki, kp in (("reference", ref, "ref_index", "ref_pts"),
                                 ("distorted", dis, "dis_index", "dis_pts")):
            placed, agree, amb, ranges = check(pair["frames"], ki, kp, sm)
            print(f"pair {pair['index']} ({pair['dis']['url']}), {side}: {len(pair['frames'])} frames, "
                  f"{placed} placed, {agree} with the packet's PTS, {amb} whose PTS is in more than one "
                  f"segment of the input")
            print("  segments: " + ", ".join(
                f"{s} {sm['urls'][s]} frames {lo}-{hi}" for s, (lo, hi) in sorted(ranges.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
