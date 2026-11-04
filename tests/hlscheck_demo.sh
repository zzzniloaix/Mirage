#!/bin/sh
# A small HLS package whose master playlist declares what its media does not
# hold, one mistake of each kind the conformance check (src/HlsConform.h)
# looks for, and mirage_hlscheck's report on it. The media is generated
# (FFmpeg test patterns), so the package is the same on every machine but
# for the encoders' exact bit rates.
#
#   v1  H.264 640x360 25 fps: BANDWIDTH and AVERAGE-BANDWIDTH below what the
#       segments carry, CODECS Main profile for a High profile stream,
#       FRAME-RATE 30.000
#   v2  the same picture cut every 1.5 s whatever the frame (split_by_time)
#       under EXT-X-INDEPENDENT-SEGMENTS: segments that start inside a GOP
#   v3  HEVC Main 10 with PQ transfer (HDR10) in fMP4: no VIDEO-RANGE, a
#       CODECS level below the stream's, a wrong RESOLUTION
#   a1  stereo AAC declared CHANNELS="6"
#
# Usage: tests/hlscheck_demo.sh [mirage_hlscheck] [output dir]
# Requires ffmpeg on PATH with libx264, libx265 and the native AAC encoder.
# Leaves the package in the output dir (default: a new temporary dir), with
# report.txt and report.json; serve it over HTTP to see the inspector's
# Conformance tab on it, e.g. python3 -m http.server --directory <dir>.

set -eu

BIN=${1:-build/macos-debug/mirage_hlscheck}
OUT=${2:-$(mktemp -d "${TMPDIR:-/tmp}/hlscheck_demo.XXXXXX")}
if [ ! -x "$BIN" ]; then
    echo "error: $BIN not found — build first: cmake --build --preset macos-debug" >&2
    exit 2
fi
command -v ffmpeg >/dev/null || { echo "error: ffmpeg is not on PATH" >&2; exit 2; }
mkdir -p "$OUT/v1" "$OUT/v2" "$OUT/v3" "$OUT/a1"

ff() { ffmpeg -hide_banner -loglevel error -y "$@"; }
SRC="testsrc2=size=640x360:rate=25:duration=12"
X264="-c:v libx264 -threads 1 -profile:v high -level 3.0 -g 50 -keyint_min 50 -sc_threshold 0 -b:v 800k -maxrate 1200k -bufsize 1200k"

# shellcheck disable=SC2086
ff -f lavfi -i "$SRC" $X264 -f hls -hls_time 2 -hls_playlist_type vod \
   -hls_segment_filename "$OUT/v1/s%d.ts" "$OUT/v1/index.m3u8"
# shellcheck disable=SC2086
ff -f lavfi -i "$SRC" $X264 -f hls -hls_time 1.5 -hls_flags split_by_time -hls_playlist_type vod \
   -hls_segment_filename "$OUT/v2/s%d.ts" "$OUT/v2/index.m3u8"
ff -f lavfi -i "testsrc2=size=1280x720:rate=25:duration=12" -pix_fmt yuv420p10le \
   -c:v libx265 -x265-params "colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:keyint=50:min-keyint=50:scenecut=0:log-level=error" \
   -tag:v hvc1 -b:v 2M -f hls -hls_segment_type fmp4 -hls_time 2 -hls_playlist_type vod \
   -hls_segment_filename "$OUT/v3/s%d.m4s" "$OUT/v3/index.m3u8"
ff -f lavfi -i "sine=frequency=1000:sample_rate=48000:duration=12" -ac 2 -c:a aac -b:a 128k \
   -f hls -hls_time 2 -hls_playlist_type vod -hls_segment_filename "$OUT/a1/s%d.ts" "$OUT/a1/index.m3u8"

cat > "$OUT/master.m3u8" <<'EOF'
#EXTM3U
#EXT-X-VERSION:7
#EXT-X-INDEPENDENT-SEGMENTS
#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aac",NAME="English",LANGUAGE="en",DEFAULT=YES,CHANNELS="6",URI="a1/index.m3u8"
#EXT-X-STREAM-INF:BANDWIDTH=600000,AVERAGE-BANDWIDTH=500000,CODECS="avc1.4d401e,mp4a.40.2",RESOLUTION=640x360,FRAME-RATE=30.000,AUDIO="aac"
v1/index.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=2000000,CODECS="avc1.64001e,mp4a.40.2",RESOLUTION=640x360,FRAME-RATE=25.000,AUDIO="aac"
v2/index.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=4000000,CODECS="hvc1.2.4.L90.B0,mp4a.40.2",RESOLUTION=1920x1080,FRAME-RATE=25.000,AUDIO="aac"
v3/index.m3u8
EOF

# Paths in the report relative to the package.
BIN_ABS=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")
set +e
(cd "$OUT" && "$BIN_ABS" --segments all --json report.json master.m3u8 > report.txt)
rc=$?
set -e
cat "$OUT/report.txt"
echo
echo "package and reports: $OUT (mirage_hlscheck exit status $rc)"
