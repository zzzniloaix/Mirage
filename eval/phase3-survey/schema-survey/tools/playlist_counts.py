"""The segments and tags of committed HLS media playlists: how many events
per-segment sources (HTTP timing, playlist tags) would have for them.

usage: python3 -I playlist_counts.py PLAYLIST...

For each playlist: its #EXTINF lines (segments) and the sum of their
durations, its EXT-X-TARGETDURATION, and how many times each tag (a line
starting with "#EXT", up to ':' or the end of the line) occurs. Then, for all
of them, which tags any of them has. Python 3 standard library only.
"""

import collections
import sys
from decimal import Decimal


def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    every = collections.Counter()
    for path in argv[1:]:
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = [x.strip() for x in f]
        tags = collections.Counter()
        total = Decimal(0)
        segments = 0
        target = None
        for line in lines:
            if not line.startswith("#EXT"):
                continue
            name = line.split(":", 1)[0]
            tags[name] += 1
            if name == "#EXTINF":
                segments += 1
                total += Decimal(line.split(":", 1)[1].split(",", 1)[0])
            elif name == "#EXT-X-TARGETDURATION":
                target = line.split(":", 1)[1]
        every.update(tags.keys())
        print(f"{path}: {segments} segments, {total} s in all, target duration {target}")
        print("  tags: " + ", ".join(f"{t} {n}" for t, n in sorted(tags.items())))
    print("\ntags in any of them (playlists that have it): "
          + ", ".join(f"{t} ({n})" for t, n in sorted(every.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
