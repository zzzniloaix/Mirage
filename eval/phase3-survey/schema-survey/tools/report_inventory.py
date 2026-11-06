"""Inventory of the JSON reports of Mirage's scorers committed in the repository.

usage: python3 -I report_inventory.py   (run in the repository's top directory)

Reads every committed file named mirage.json, mirage.json.xz, report.json.xz or
report.vmaf.json (git ls-files), and classifies each by what wrote it:

  pair     mirage_vmaf's report of one pair (vmafreport::to_json): "frames" at the top
  ladder   mirage_vmaf's ladder report (vmafreport::ladder_to_json): "pairs" and "ladder"
  player   the player's write_json (VMAFAnalyzer::write_json): "results" at the top
  other    anything else

For each report it notes which keys of today's format it has, so that the
committed reports group by the format they were written in (there is no version
field to say so). Prints one line per format group with its count and its files'
directories, then the totals of the files' sizes. Python 3 standard library only.
"""

import collections
import json
import lzma
import os
import subprocess
import sys

NAMES = ("mirage.json", "mirage.json.xz", "report.json.xz", "report.vmaf.json")


def committed():
    out = subprocess.run(["git", "ls-files", "-z"], capture_output=True, check=True).stdout
    return sorted(p for p in out.decode().split("\0") if p and os.path.basename(p) in NAMES)


def load(path):
    opener = lzma.open if path.endswith(".xz") else open
    with opener(path, "rt", encoding="utf-8") as f:
        text = f.read()
    return json.loads(text), len(text.encode("utf-8"))


def kind(doc):
    if not isinstance(doc, dict):
        return "other"
    if "results" in doc:
        return "player"
    if "pairs" in doc and "ladder" in doc:
        return "ladder"
    if "frames" in doc:
        return "pair"
    return "other"


def features(doc, k):
    """The keys of today's format a report has, as short labels."""
    pairs = doc.get("pairs") if k == "ladder" else [doc] if k == "pair" else []
    p0 = pairs[0] if pairs else {}
    f0 = (p0.get("frames") or [{}])[0] if isinstance(p0, dict) else {}
    al = p0.get("alignment") if isinstance(p0.get("alignment"), dict) else None
    pooled = p0.get("pooled") if isinstance(p0.get("pooled"), dict) else {}
    out = []
    for key in ("reference_mode", "ffmpeg_libraries", "config_file", "model_sha256",
                "frame_limit", "status", "pool"):
        if key in doc:
            out.append(key)
    for key in ("scored_as", "dis_conversion", "ref_conversion", "warnings", "alignment"):
        if key in p0:
            out.append(key)
    if al is not None:
        if isinstance(al.get("reference"), dict) and "sequences" in al["reference"]:
            out.append("alignment.sequences")
        if "vmaf_contexts" in al:
            out.append("alignment.vmaf_contexts")
    if "p1" in pooled:
        out.append("pooled.p1")
    if "min_without_context_first_frames" in pooled:
        out.append("pooled.min_without_context_first_frames")
    for key in ("ref_index", "ref_pts"):
        if key in f0:
            out.append(f"frames[].{key}")
    if k == "ladder":
        pool = doc.get("pool") or {}
        if isinstance(pool.get("pair_memory"), list):
            out.append("pool.pair_memory[]")
        if "not_scored" in doc:
            out.append("not_scored")
    return tuple(out)


def main():
    groups = collections.OrderedDict()
    sizes = collections.Counter()
    counts = collections.Counter()
    frames_total = collections.Counter()
    for path in committed():
        try:
            doc, raw = load(path)
        except (OSError, ValueError, lzma.LZMAError) as e:
            print(f"unreadable: {path}: {e}")
            continue
        k = kind(doc)
        f = features(doc, k)
        groups.setdefault((k, f), []).append(path)
        counts[k] += 1
        sizes[(k, "file")] += os.path.getsize(path)
        sizes[(k, "json")] += raw
        if k == "pair":
            frames_total[k] += len(doc.get("frames") or [])
        elif k == "ladder":
            frames_total[k] += sum(len(p.get("frames") or []) for p in doc.get("pairs") or [])
        elif k == "player":
            frames_total[k] += sum(len(r.get("per_frame") or []) for r in doc.get("results") or [])
    print("Committed reports, by what wrote them and the keys of today's format they have.")
    print("(A group is one combination; its directories are listed with their file counts.)\n")
    for n, ((k, f), paths) in enumerate(groups.items(), 1):
        dirs = collections.Counter()
        for p in paths:
            parts = p.split("/")
            dirs["/".join(parts[:3])] += 1
        print(f"group {n}: {k}, {len(paths)} files")
        print("  has: " + (", ".join(f) if f else "(none of them)"))
        print("  under: " + "; ".join(f"{d} ({c})" for d, c in sorted(dirs.items())))
    print()
    for k in ("pair", "ladder", "player", "other"):
        if counts[k]:
            print(f"{k}: {counts[k]} files, {sizes[(k, 'file')]} bytes as committed, "
                  f"{sizes[(k, 'json')]} bytes of JSON text, {frames_total[k]} per-frame scores")
    return 0


if __name__ == "__main__":
    sys.exit(main())
