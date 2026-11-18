"""One line for each mirage_vmaf --ladder report given: its status and reference mode, the pool's
size and the most pairs scored at once, the peak resident memory, and each pair's scoring time
(ended_s - started_s) with its frames scored.

usage: python3 -I report_summary.py <report.json>...   (files ending in .layout.json are skipped)
"""

import json
import sys


def main(argv):
    for path in argv:
        if path.endswith(".layout.json"):
            continue
        with open(path) as f:
            r = json.load(f)
        pool = r["pool"]
        pairs = ", ".join(f"{p['dis']['url']} {p['ended_s'] - p['started_s']:.3f} s "
                          f"({p['frames_scored']} frames)" for p in r["pairs"])
        print(f"{path}: {r['status']}, {r['reference_mode']}, jobs {pool['jobs']}, max_running "
              f"{pool['max_running']}, peak_resident {pool['peak_resident']} bytes; {pairs}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
