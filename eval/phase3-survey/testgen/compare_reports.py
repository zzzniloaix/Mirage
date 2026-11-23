"""Compares the pairs of two mirage_vmaf --ladder reports: for each pair, whether its frames
(every index, position, PTS and score, at full precision), its alignment, its pooled values and
its warnings are the same in both.

usage: python3 -I compare_reports.py <report_a.json> <report_b.json>
"""

import json
import sys


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    with open(argv[0]) as f:
        a = json.load(f)
    with open(argv[1]) as f:
        b = json.load(f)
    if len(a["pairs"]) != len(b["pairs"]):
        print(f"{len(a['pairs'])} pairs against {len(b['pairs'])}")
        return 1
    same = True
    for pa, pb in zip(a["pairs"], b["pairs"]):
        fields = {k: pa.get(k) == pb.get(k) for k in ("frames", "alignment", "pooled", "warnings")}
        same &= all(fields.values()) and pa["dis"]["url"] == pb["dis"]["url"]
        print(f"pair {pa['index']} {pa['dis']['url']} / {pb['dis']['url']}: "
              + ", ".join(f"{k} {'same' if v else 'DIFFERENT'}" for k, v in fields.items()))
    print("the pairs are the same" if same else "the pairs differ")
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
