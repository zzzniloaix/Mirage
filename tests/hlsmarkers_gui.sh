#!/bin/sh
# The Conformance tab with nothing playing, as a test
# (plan/hls_markers_usability.md, section 1). The player gets no media, only
# --ui-net "…,hls=<master>", and checks the package tests/hlsmarkers_demo.sh
# makes from its launch screen. Passes when:
#   - the player exits with status 0 after writing its window snapshot;
#   - the tab's JSON report is mirage_hlscheck --markers-only's, byte for
#     byte, on the same path.
#
# Usage: tests/hlsmarkers_gui.sh [build dir] [output dir]
#        Defaults: build/macos-debug, a new temporary directory (kept).
# Requires what tests/hlsmarkers_demo.sh does, and a display: mirage opens a
# window, and --qa-no-focus keeps it from taking the keyboard. The output dir
# keeps the package, both reports, the snapshot and the player's log. Paths
# are relative to it, so the snapshot shows none of the machine's.

set -eu

BUILD=${1:-build/macos-debug}
OUT=${2:-$(mktemp -d "${TMPDIR:-/tmp}/hlsmarkers_gui.XXXXXX")}
if [ ! -x "$BUILD/mirage" ] || [ ! -x "$BUILD/mirage_hlscheck" ]; then
    echo "error: mirage and mirage_hlscheck not found in $BUILD; build first: cmake --build --preset macos-debug" >&2
    exit 2
fi
BUILD=$(cd "$BUILD" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT/home"
OUT=$(cd "$OUT" && pwd)
sh "$HERE/hlsmarkers_demo.sh" "$BUILD/mirage_hlscheck" "$OUT/pkg" > "$OUT/demo.txt" 2>&1
cd "$OUT"

set +e
"$BUILD/mirage_hlscheck" --markers-only --marker-tag EXT-X-CUE-MARKER --json cli.json pkg/master.m3u8 > cli.txt 2>&1
HOME="$OUT/home" "$BUILD/mirage" --qa-no-focus --window 1600x1000 \
    --ui-net "markers_only=1,marker_tags=EXT-X-CUE-MARKER,marker_open=0,conform_out=tab.json,hls=pkg/master.m3u8" \
    --ui-snapshot launcher.png 30 > player.log 2>&1
rc=$?
set -e

fail=0
[ "$rc" -eq 0 ] || { echo "FAIL: mirage exited with status $rc (see player.log)"; fail=1; }
[ -s launcher.png ] || { echo "FAIL: no window snapshot"; fail=1; }
if [ ! -s tab.json ]; then
    echo "FAIL: the tab wrote no JSON report"; fail=1
elif ! cmp -s cli.json tab.json; then
    echo "FAIL: the tab's JSON report is not mirage_hlscheck's"; fail=1
fi
[ "$fail" -eq 0 ] && echo "PASS: checked from the launch screen; the tab's report is mirage_hlscheck's"
echo "output: $OUT"
exit "$fail"
