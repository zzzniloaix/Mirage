"""Sizes of the prototype's timeline documents (attribute.py's JSON, indent 1) and what they hold:
bytes, events by source, attributions. For the Phase 3 design (design-evaluation.md); Python 3
standard library only.

usage: python3 -I -B doc_sizes.py <dir> <name>...
"""
import collections
import json
import os
import sys


def main(argv):
    d = argv[0]
    print("document\tbytes\tevents\tby source\tattributions\tframes in the ladder report's pairs")
    for name in argv[1:]:
        p = os.path.join(d, name)
        with open(p) as f:
            doc = json.load(f)
        by = collections.Counter(e["source"] for e in doc["timeline"])
        print(f"{name}\t{os.path.getsize(p)}\t{len(doc['timeline'])}\t"
              f"{', '.join(f'{k} {v}' for k, v in sorted(by.items()))}\t{len(doc['attributions'])}\t"
              f"(see the report)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
