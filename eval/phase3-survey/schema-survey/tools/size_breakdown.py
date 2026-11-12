"""Where the bytes of committed ladder reports go, and what one candidate
timeline encoding would add to them.

usage: python3 -I size_breakdown.py REPORT...   (each a ladder report, .json or .json.xz)

For each report:
  - its size as committed, the size of its JSON text, and the text compressed
    again here with lzma.compress() at its default preset (what
    eval/ladder/pool_check.py does when it commits a report);
  - its pairs and their frames; the per-frame lines (one line per frame, as
    vmafreport::ladder_to_json writes them: src/VMAFReport.cpp, pair_tail), their
    bytes and their share of the text; the text without them, plain and
    compressed;
  - the per-frame scores written as integer literals (a double without a
    fraction, std::format "{}") and their distinct values;
  - candidate C1, which is not a design: each per-frame score repeated once more
    as an entry of a work order section 7 style timeline list,
      {"pts": <ref_pts>, "wallclock": null, "source": "vmaf", "type": "frame",
       "payload": {"pair": <pair index>, "vmaf": <vmaf>}}
    one per line, indented as the report's frames are, with the number tokens
    copied from the report's own text; its bytes alone, plain and compressed,
    and those of the report with it appended.
Python 3 standard library only.
"""

import json
import lzma
import os
import re
import sys

FRAME_LINE = re.compile(
    r'^(?P<ind>[ ]*)\{"index": (?P<index>\d+), "ref_index": (?P<ref_index>[^,]+), '
    r'"dis_index": (?P<dis_index>[^,]+), "ref_pts": (?P<ref_pts>[^,]+), '
    r'"dis_pts": (?P<dis_pts>[^,]+), "vmaf": (?P<vmaf>[^}]+)\},?$')


def xz(b):
    return len(lzma.compress(b))


def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    for path in argv[1:]:
        opener = lzma.open if path.endswith(".xz") else open
        with opener(path, "rt", encoding="utf-8") as f:
            text = f.read()
        raw = text.encode("utf-8")
        doc = json.loads(text)
        pairs = doc.get("pairs") or []
        per_pair = [len(p.get("frames") or []) for p in pairs]
        lines = text.split("\n")
        frame_lines, other_lines = [], []
        c1 = []
        pair_of_line = []
        # The frame lines come pair by pair, in the order of "pairs".
        k, seen_in_pair = 0, 0
        for line in lines:
            m = FRAME_LINE.match(line)
            if m:
                while k < len(per_pair) and seen_in_pair >= per_pair[k]:
                    k, seen_in_pair = k + 1, 0
                frame_lines.append(line)
                pair_index = pairs[k].get("index") if k < len(pairs) else None
                pair_of_line.append(pair_index)
                seen_in_pair += 1
                entry = (f'{m["ind"]}{{"pts": {m["ref_pts"]}, "wallclock": null, '
                         f'"source": "vmaf", "type": "frame", "payload": '
                         f'{{"pair": {pair_index}, "vmaf": {m["vmaf"]}}}}}')
                c1.append(entry)
            else:
                other_lines.append(line)
        frame_bytes = sum(len(x.encode()) + 1 for x in frame_lines)   # each with its newline
        rest = "\n".join(other_lines).encode()
        c1_text = ("[\n" + ",\n".join(c1) + "\n]\n").encode()
        ints = [f["vmaf"] for p in pairs for f in p.get("frames") or []
                if isinstance(f.get("vmaf"), int) and not isinstance(f.get("vmaf"), bool)]
        print(f"report: {path}")
        print(f"  as committed: {os.path.getsize(path)} bytes; JSON text: {len(raw)} bytes; "
              f"recompressed here: {xz(raw)} bytes")
        print(f"  status: {doc.get('status')}; frame_limit: {doc.get('frame_limit')}; "
              f"ladder entries: {len((doc.get('ladder') or {}).get('entries') or [])}; "
              f"pairs: {len(pairs)}; frames per pair: {per_pair}; frames in all: {sum(per_pair)}")
        print(f"  per-frame lines: {len(frame_lines)} (the frames in all: "
              f"{'the same' if len(frame_lines) == sum(per_pair) else 'NOT the same'}), "
              f"{frame_bytes} bytes, {100.0 * frame_bytes / len(raw):.2f}% of the text; "
              f"bytes per line (newline included) from "
              f"{min(len(x.encode()) + 1 for x in frame_lines)} to "
              f"{max(len(x.encode()) + 1 for x in frame_lines)}, "
              f"{frame_bytes / len(frame_lines):.2f} on average")
        print(f"  the text without the per-frame lines: {len(rest)} bytes, compressed "
              f"{xz(rest)} bytes")
        print(f"  per-frame scores written as integers: {len(ints)}, values: {sorted(set(ints))}")
        print(f"  candidate C1 (each per-frame score once more as a timeline entry): "
              f"{len(c1)} entries, {len(c1_text)} bytes, compressed alone {xz(c1_text)} bytes; "
              f"the report with C1 appended: {len(raw) + len(c1_text)} bytes, compressed "
              f"{xz(raw + c1_text)} bytes")
        if c1:
            print(f"  C1's first entry: {c1[0].strip()}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
