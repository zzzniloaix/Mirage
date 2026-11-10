#!/bin/sh
# The package tests/hlscheck_demo.sh makes, with break markers added to its
# playlists, and mirage_hlscheck's marker report on it (src/HlsMarkers.h).
# Every date and ID is made up; EXT-X-CUE-MARKER stands for any packager's
# own cue tag.
#
#   v1, v2  EXT-X-DATERANGE ID="break-1", an interstitial at 6.000 s, on a
#           segment boundary; ID="chapter-1" at 2.000 s, not a splice point
#   v3      the same interstitial at 6.300 s: 300 ms off a boundary
#   a1      #EXT-X-CUE-MARKER:ID="break-1" one segment early, before the
#           segment starting at 4.011 s
#
# Every playlist is dated from 2024-03-01T12:00:00Z by one
# EXT-X-PROGRAM-DATE-TIME.
#
# Usage: tests/hlsmarkers_demo.sh [mirage_hlscheck] [output dir]
# Requires what tests/hlscheck_demo.sh does, and python3. Leaves the package
# in the output dir with markers.txt, markers.json and markers.html (the
# report as --html writes it), and the full check's report.txt and
# report.json from tests/hlscheck_demo.sh.

set -eu

BIN=${1:-build/macos-debug/mirage_hlscheck}
OUT=${2:-$(mktemp -d "${TMPDIR:-/tmp}/hlsmarkers_demo.XXXXXX")}
HERE=$(cd "$(dirname "$0")" && pwd)
sh "$HERE/hlscheck_demo.sh" "$BIN" "$OUT" > /dev/null 2>&1
command -v python3 >/dev/null || { echo "error: python3 is not on PATH" >&2; exit 2; }

python3 - "$OUT" <<'EOF'
import sys
out = sys.argv[1]
PDT = "#EXT-X-PROGRAM-DATE-TIME:2024-03-01T12:00:00.000Z\n"
def edit(path, fn):
    with open(path) as f:
        text = f.read()
    with open(path, "w") as f:
        f.write(fn(text))
def dated(t):
    return t.replace("#EXT-X-PLAYLIST-TYPE:VOD\n", "#EXT-X-PLAYLIST-TYPE:VOD\n" + PDT, 1)
for v, first, start in (("v1", "s0.ts", "12:00:06.000"), ("v2", "s0.ts", "12:00:06.000"),
                        ("v3", "s0.m4s", "12:00:06.300")):
    def video(t, first=first, start=start):
        t = dated(t)
        t = t.replace("#EXTINF", '#EXT-X-DATERANGE:ID="chapter-1",START-DATE="2024-03-01T12:00:02.000Z"\n#EXTINF', 1)
        return t.replace(first + "\n", first + '\n#EXT-X-DATERANGE:ID="break-1",CLASS="com.apple.hls.interstitial",'
                         'START-DATE="2024-03-01T%sZ",DURATION=15.0\n' % start, 1)
    edit("%s/%s/index.m3u8" % (out, v), video)
edit(out + "/a1/index.m3u8", lambda t: dated(t).replace("s1.ts\n", 's1.ts\n#EXT-X-CUE-MARKER:ID="break-1"\n', 1))
EOF

BIN_ABS=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")
set +e
(cd "$OUT" && "$BIN_ABS" --markers-only --marker-tag EXT-X-CUE-MARKER --json markers.json --html markers.html \
    master.m3u8 > markers.txt)
rc=$?
set -e
cat "$OUT/markers.txt"
echo
echo "package and reports: $OUT (mirage_hlscheck exit status $rc)"
