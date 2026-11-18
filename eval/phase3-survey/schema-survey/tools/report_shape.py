"""The shape of one JSON report: every key path, the JSON types its values have,
how often it occurs, and one example of a short value.

usage: python3 -I report_shape.py REPORT   (REPORT: .json or .json.xz)

Paths are written as in the report: a list's entries as [], so that
pairs[].frames[].vmaf is the "vmaf" key of every frame of every pair. Every
value is visited. Types are JSON's, with integer and number apart, because
the report writes a double without a fraction as an integer literal
(std::format "{}", src/JsonText.cpp). Also prints each object's keys in the
order the report gives them, for the top level, a pair and a frame, and the
size of each list. Strings longer than 60 characters are cut. Python 3
standard library only.
"""

import collections
import hashlib
import json
import lzma
import os
import sys


def jtype(v):
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "boolean"
    if isinstance(v, int):
        return "integer"
    if isinstance(v, float):
        return "number"
    if isinstance(v, str):
        return "string"
    if isinstance(v, list):
        return "array"
    return "object"


def walk(v, path, stats):
    s = stats[path]
    s["types"][jtype(v)] += 1
    s["count"] += 1
    if isinstance(v, dict):
        if s["order"] is None:
            s["order"] = list(v.keys())
        elif s["order"] != list(v.keys()):
            s["orders_differ"] = True
        for k, x in v.items():
            walk(x, f"{path}.{k}" if path else k, stats)
    elif isinstance(v, list):
        s["lengths"].append(len(v))
        for x in v:
            walk(x, f"{path}[]", stats)
    elif s["example"] is None:
        text = json.dumps(v)
        s["example"] = text if len(text) <= 60 else text[:57] + "..."


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    path = argv[1]
    opener = lzma.open if path.endswith(".xz") else open
    with opener(path, "rt", encoding="utf-8") as f:
        text = f.read()
    with open(path, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    doc = json.loads(text)
    print(f"file: {path}")
    print(f"  bytes as committed: {os.path.getsize(path)}; JSON text: {len(text.encode())} bytes; "
          f"lines: {text.count(chr(10))}; sha256 of the file: {digest}")
    stats = collections.defaultdict(lambda: {"types": collections.Counter(), "count": 0,
                                             "example": None, "order": None,
                                             "orders_differ": False, "lengths": []})
    walk(doc, "", stats)
    print("\nkey order:")
    for p in ("", "pairs[]", "pairs[].frames[]", "frames[]", "pairs[].alignment", "alignment",
              "pool", "ladder", "ladder.entries[]", "not_scored[]"):
        if p in stats and stats[p]["order"] is not None:
            same = "" if not stats[p]["orders_differ"] else " (NOT the same in every entry)"
            print(f"  {p or '(top level)'} ({len(stats[p]['order'])} keys): "
                  f"{', '.join(stats[p]['order'])}{same}")
    print("\npaths (types with counts; occurrences; list sizes; an example):")
    for p, s in stats.items():
        if not p:
            continue
        types = ", ".join(f"{t} {n}" for t, n in sorted(s["types"].items()))
        line = f"  {p}: {types}"
        if s["lengths"]:
            ls = s["lengths"]
            line += f"; list sizes {min(ls)} to {max(ls)}" if min(ls) != max(ls) \
                else f"; list size {ls[0]}" + (f" (in each of {len(ls)})" if len(ls) > 1 else "")
        if s["example"] is not None:
            line += f"; e.g. {s['example']}"
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
