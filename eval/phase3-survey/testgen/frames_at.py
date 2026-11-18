"""Prints given frame pairs of one pair of a mirage_vmaf --ladder report, as the report has them.

usage: python3 -I frames_at.py <report.json> <pair index> <first:last>...
"""

import json
import sys


def main(argv):
    with open(argv[0]) as f:
        r = json.load(f)
    p = r["pairs"][int(argv[1])]
    print(f"pair {p['index']}: {p['dis']['url']} against {p['ref']['url']}")
    for span in argv[2:]:
        a, z = (int(x) for x in span.split(":"))
        for fr in p["frames"][a:z + 1]:
            print(json.dumps(fr))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
