#!/bin/bash
# pipeline.sh <repo> <out> <scratch> <homebrew prefix>
#
# Every run behind design-library.md, in order, each recorded by tools/run.sh
# under design-library/runs/. Reads the sources survey's log probe
# (<out>/probe/logprobe.cpp, not changed) and the D-069 prototype's ladders
# and reports (<out>/testgen/, not changed); builds the probe into
# design-library/bin/ and deletes it at the end, since a binary carries local
# paths. Writes nothing outside design-library/.
set -u
export REPO="$1" OUT="$2" SCRATCH="$3" HOMEBREW="$4"
lib="$OUT/design-library"
run="$lib/tools/run.sh"
tg="$OUT/testgen"
mkdir -p "$lib/bin"
export PKG_CONFIG_PATH="$HOMEBREW/opt/ffmpeg-full/lib/pkgconfig"

# The log probe of the sources survey, built as its build.sh builds it but
# without debug information.
"$run" build-logprobe "$lib" sh -c 'clang++ -std=c++20 -O1 -Wall -Wextra -I"$REPO/src" -I"$REPO/tests" -o bin/logprobe "$OUT/probe/logprobe.cpp" $(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale libswresample) && echo built'

# The analyzer's reads of the ad ladder's reference (v360) and low rung
# (v180), and of the degraded ladder's degraded rung (v270), through FFmpeg's
# hls demuxer as DecodeCtx::open() reads an input.
"$run" logprobe-ad-v360 "$tg/media/ad-x264-a" "$lib/bin/logprobe" file v360/index.m3u8
"$run" logprobe-ad-v180 "$tg/media/ad-x264-a" "$lib/bin/logprobe" file v180/index.m3u8
"$run" logprobe-ad-origin1-v360 "$tg/media/ad-x264-origin1" "$lib/bin/logprobe" file v360/index.m3u8
"$run" logprobe-degraded-v270 "$tg/media/degraded-x264-a" "$lib/bin/logprobe" file v270/index.m3u8

# Packets to segments by byte position and by PTS, against the layout.
for r in ad-v360:ad-x264-a/v360 ad-v180:ad-x264-a/v180 ad-origin1-v360:ad-x264-origin1/v360 \
         degraded-v270:degraded-x264-a/v270; do
    name="${r%%:*}"; rung="${r#*:}"
    "$run" "map-by-pos-$name" "$lib/runs" python3 -I "$lib/tools/map_by_pos.py" \
        "logprobe-$name.txt" "$tg/media/$rung/index.m3u8" "$tg/media/${rung%/*}/layout.json"
done

# The four time axes and the interval query, on the intervals the D-069
# prototype found (testgen.md 3.1 and 3.2; testgen/logs/15, 46, 73), in exact
# ticks and, for two of them, in doubles.
for mode in exact float; do
    flag=""; sfx=""
    if [ "$mode" = float ]; then flag="--float"; sfx="-float"; fi
    "$run" "axis-ad$sfx" "$tg" python3 -I "$lib/tools/axis_probe.py" $flag reports/ad-x264.json \
        reports/ad-x264-a.layout.json media/ad-x264-a/v360/index.m3u8 v180/index.m3u8 75:124
    "$run" "axis-degraded$sfx" "$tg" python3 -I "$lib/tools/axis_probe.py" $flag \
        reports/degraded-x264.json reports/degraded-x264-a.layout.json \
        media/degraded-x264-a/v360/index.m3u8 v270/index.m3u8 100:124
done
"$run" axis-ad-origin1 "$tg" python3 -I "$lib/tools/axis_probe.py" reports/ad-x264-origin1.json \
    reports/ad-x264-origin1.layout.json media/ad-x264-origin1/v360/index.m3u8 v180/index.m3u8 75:124

rm -rf "$lib/bin"
