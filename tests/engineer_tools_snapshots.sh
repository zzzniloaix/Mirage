#!/usr/bin/env bash
# engineer_tools_snapshots.sh — the scripted checks of the engineer-tools
# panels (plan/engineer_tools_status.md), as a reproducer.
#
# Generates the synthetic clips the checks used (x264 SDR with B-frames,
# x265 tagged HDR10 with mastering-display / light-level SEI, an MPEG-TS
# whose HEVC stream switches from BT.709 to PQ, a 5.1 PCM clip with set
# channel levels), opens each in mirage with panels staged by the QA flags,
# and writes a window snapshot per case. For the loudness cases it also
# prints FFmpeg's own ebur128 reading of the same file, to compare with the
# card in the snapshot. Nothing is compared automatically: look at the PNGs.
#
# Requires: ffmpeg on PATH with libx264 and libx265, a display (mirage opens
# a GLFW window; --qa-no-focus keeps it from taking the keyboard).
#
# Usage:  ./tests/engineer_tools_snapshots.sh  [build_dir]  [out_dir]
#         Defaults: build/macos-debug, a new temporary directory (kept).

set -eu

BUILD_DIR="${1:-build/macos-debug}"
OUT="${2:-$(mktemp -d -t engineer_tools.XXXXXX)}"
if [[ ! -x "$BUILD_DIR/mirage" ]]; then
    echo "error: mirage not found at $BUILD_DIR/mirage — build first: cmake --build --preset macos-debug" >&2
    exit 2
fi
MIRAGE="$(cd "$BUILD_DIR" && pwd)/mirage"   # absolute: the clips are made in $OUT
mkdir -p "$OUT/home"
OUT="$(cd "$OUT" && pwd)"
cd "$OUT"

echo "── clips ──"
ffmpeg -v error -y -f lavfi -i testsrc2=size=1280x720:rate=25 -f lavfi -i sine=frequency=1000:sample_rate=48000 \
    -t 8 -c:v libx264 -bf 3 -g 50 -pix_fmt yuv420p -colorspace bt709 -c:a aac -ac 2 x264_sdr.mp4
ffmpeg -v error -y -f lavfi -i testsrc2=size=1280x720:rate=25 -t 4 -pix_fmt yuv420p10le -c:v libx265 \
    -x265-params "log-level=error:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:range=limited:master-display=G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,50):max-cll=1000,400:hdr10=1:repeat-headers=1" \
    -tag:v hvc1 x265_hdr10.mp4
ffmpeg -v error -y -f lavfi -i testsrc2=size=1280x720:rate=25 -t 3 -pix_fmt yuv420p10le -c:v libx265 \
    -x265-params "log-level=error:colorprim=bt709:transfer=bt709:colormatrix=bt709:repeat-headers=1" part_sdr.ts
ffmpeg -v error -y -f lavfi -i testsrc2=size=1280x720:rate=25 -ss 3 -t 3 -pix_fmt yuv420p10le -c:v libx265 \
    -x265-params "log-level=error:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:master-display=G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,50):max-cll=1000,400:repeat-headers=1" part_pq.ts
printf "file 'part_sdr.ts'\nfile 'part_pq.ts'\n" > tagswitch.txt
ffmpeg -v error -y -f concat -safe 0 -i tagswitch.txt -c copy tagswitch.ts
# 5.1: fronts 1 kHz at -23 dBFS, centre -30, LFE 50 Hz at -6 (not counted), surrounds -33.
ffmpeg -v error -y -f lavfi -i testsrc2=size=1280x720:rate=25 \
    -f lavfi -i "aevalsrc=exprs=0.0708*sin(2*PI*1000*t)|0.0708*sin(2*PI*1000*t)|0.0316*sin(2*PI*1000*t)|0.5*sin(2*PI*50*t)|0.0224*sin(2*PI*1000*t)|0.0224*sin(2*PI*1000*t):c=5.1(side):s=48000" \
    -t 20 -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a pcm_s24le surround51.mkv

# One scripted window: name, clip, mirage arguments...
snap() {
    local name=$1 clip=$2; shift 2
    HOME="$OUT/home" perl -e 'alarm 60; exec @ARGV' "$MIRAGE" --qa-no-focus --window 1600x1000 "$@" \
        --ui-snapshot "$OUT/$name.png" "${SNAP_FRAME:-60}" "$clip" > "$OUT/$name.log" 2>&1 || true
    if grep -q "ui-snapshot: wrote" "$OUT/$name.log"; then echo "  $name.png"; else echo "  $name: no snapshot (see $name.log)"; fi
}

echo "── snapshots ──"
snap frame_inspector_sdr x264_sdr.mp4    --ui-overlays P --ui-sub-at 2.0
snap frame_inspector_hdr x265_hdr10.mp4  --ui-overlays P --ui-sub-at 2.0
SNAP_FRAME=270 snap frame_inspector_tag_switch tagswitch.ts --ui-overlays P
snap scopes_waveform_hdr x265_hdr10.mp4  --ui-overlays SX --scope-mode waveform    --ui-probe 0.08,0.3 --ui-sub-at 2.0
snap scopes_vectorscope  x264_sdr.mp4    --ui-overlays SX --scope-mode vectorscope --ui-probe 0.08,0.3 --ui-sub-at 2.0
snap scopes_parade       x264_sdr.mp4    --ui-overlays S  --scope-mode parade      --ui-sub-at 2.0
snap scopes_histogram    x265_hdr10.mp4  --ui-overlays S  --scope-mode histogram   --ui-sub-at 2.0
snap frame_sizes         x264_sdr.mp4    --ui-overlays B  --ui-sub-at 3.0
SNAP_FRAME=420 snap loudness_51  surround51.mkv --ui-overlays L
SNAP_FRAME=380 snap loudness_aac x264_sdr.mp4   --ui-overlays L

echo "── FFmpeg's ebur128 on the loudness clips (compare with the cards) ──"
for f in surround51.mkv x264_sdr.mp4; do
    echo "  $f:"
    ffmpeg -hide_banner -nostats -i "$f" -map 0:a -af ebur128=peak=true -f null - 2>&1 \
        | grep -E '^ +(I|LRA|Peak):' | sed 's/^/    /'
done
echo "Output: $OUT"
