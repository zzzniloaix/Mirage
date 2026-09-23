#!/usr/bin/env bash
# pathb_snapshot_regression.sh — visual regression check for the HW zero-copy
# render path against the SW rendering path.
#
# Generates a small synthetic H.264 test pattern via ffmpeg, plays it through
# mirage in both --no-hw (SW) and default (HW) modes with --snapshot, then
# pixel-diffs the resulting PNGs. Exits nonzero if the diff exceeds the
# allowed budget of visible-diff pixels.
#
# Requires: ffmpeg on PATH (already a build dependency), a display (mirage
# creates a GLFW window). Not runnable headless — this is a developer
# reproducer, not a CI test.
#
# Usage:  ./tests/pathb_snapshot_regression.sh  [build_dir]
#         Default build_dir = build/macos-debug

set -eu

BUILD_DIR="${1:-build/macos-debug}"
MIRAGE="$BUILD_DIR/mirage"
DIFF="$BUILD_DIR/snapshot_diff"

if [[ ! -x "$MIRAGE" ]]; then
    echo "error: mirage not found at $MIRAGE — build first: cmake --build --preset macos-debug" >&2
    exit 2
fi
if [[ ! -x "$DIFF" ]]; then
    echo "error: snapshot_diff not found at $DIFF — build first" >&2
    exit 2
fi

WORK=$(mktemp -d -t pathb.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

CLIP="$WORK/pattern.mp4"
HW="$WORK/hw.png"
SW="$WORK/sw.png"

# Saturated single-color clip: cheap to encode/decode, no hard edges (so no
# spurious interpolation diffs), yet still exercises Y + Cb + Cr non-neutrally
# — a wrong YUV→RGB matrix (e.g. the factor-of-2 chroma bug we caught with
# the YuvMatrix unit tests) would produce very different output between paths.
# Color 0x80c040 = medium yellow-green, which lands mid-range in all 3
# channels and requires the full inverse matrix to reproduce.
echo "── generating synthetic solid-color H.264 clip ──"
ffmpeg -y -f lavfi -i "color=c=0x80c040:s=640x480:rate=30" -t 3 \
    -c:v libx264 -pix_fmt yuv420p "$CLIP" 2>/dev/null

echo "── HW snapshot ──"
"$MIRAGE" --snapshot "$HW" 30 "$CLIP" > "$WORK/hw.log" 2>&1
grep -E "HW zero-copy|snapshot: wrote" "$WORK/hw.log" || {
    echo "error: mirage HW run did not produce expected log lines" >&2
    cat "$WORK/hw.log" >&2
    exit 1
}

echo "── SW snapshot (--no-hw) ──"
"$MIRAGE" --no-hw --snapshot "$SW" 30 "$CLIP" > "$WORK/sw.log" 2>&1
grep -E "snapshot: wrote" "$WORK/sw.log" || {
    echo "error: mirage SW run did not produce a snapshot" >&2
    cat "$WORK/sw.log" >&2
    exit 1
}

echo "── pixel diff ──"
# On a solid-color clip, HW and SW should agree to within ~2/255 (measured
# empirically). Budget=100 leaves headroom for rounding noise from window
# scaling. Real regressions (like the missing 2× chroma-coefficient bug we
# caught earlier) would tint every pixel and blow this budget by orders
# of magnitude.
"$DIFF" "$HW" "$SW" 100
