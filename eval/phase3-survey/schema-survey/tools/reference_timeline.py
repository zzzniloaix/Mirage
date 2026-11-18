"""How the pairs of a ladder report sit on the reference's timeline: which
reference frames each pair scored, so that pairs can be compared frame by
frame (work order section 3.1 has variants compared on the
reference variant's timeline).

usage: python3 -I reference_timeline.py REPORT   (a ladder report, .json or .json.xz)

For each pair: its frames, its alignment's grid and frame rates, its off-grid
reference frames, the steps between consecutive ref_index values, the first and
last ref_pts, whether ref_pts rises at every frame, and whether every ref_index
it scored is one of those of the pair with the most frames. Also whether a
reference frame has the same ref_pts in every pair that scored it. Python 3
standard library only.
"""

import collections
import json
import lzma
import sys


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    path = argv[1]
    opener = lzma.open if path.endswith(".xz") else open
    with opener(path, "rt", encoding="utf-8") as f:
        doc = json.load(f)
    pairs = doc.get("pairs") or []
    biggest = max(pairs, key=lambda p: len(p.get("frames") or []))
    big_refs = {f["ref_index"] for f in biggest["frames"]}
    pts_of = collections.defaultdict(set)
    print(f"report: {path}")
    print(f"reference: {(doc.get('reference') or {}).get('url')}; the pair with the most frames: "
          f"index {biggest.get('index')}")
    for p in pairs:
        fr = p.get("frames") or []
        al = p.get("alignment") or {}
        refs = [f["ref_index"] for f in fr]
        steps = collections.Counter(b - a for a, b in zip(refs, refs[1:]))
        pts = [f["ref_pts"] for f in fr]
        rising = all(b > a for a, b in zip(pts, pts[1:]))
        for f in fr:
            pts_of[f["ref_index"]].add(f["ref_pts"])
        print(f"pair {p.get('index')} ({(p.get('dis') or {}).get('url')}): {len(fr)} frames; "
              f"grid {al.get('grid')}; frame rates reference {(al.get('reference') or {}).get('frame_rate')}, "
              f"distorted {(al.get('distorted') or {}).get('frame_rate')}; off_grid_reference "
              f"{al.get('off_grid_reference')}; ref_index steps {dict(sorted(steps.items()))}; "
              f"ref_pts {pts[0] if pts else None} to {pts[-1] if pts else None}, "
              f"rising at every frame: {rising}; every ref_index among the biggest pair's: "
              f"{set(refs) <= big_refs}")
    same = all(len(v) == 1 for v in pts_of.values())
    print(f"reference frames scored by any pair: {len(pts_of)}; each with one ref_pts in every "
          f"pair that scored it: {same}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
