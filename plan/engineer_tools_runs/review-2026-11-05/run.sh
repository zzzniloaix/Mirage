#!/bin/sh
# The checks behind D-093 and D-094 (plan/engineer_tools_player_fixes.md, sections 1 and 2), in the form
# of eval/test_runs/: each command, then its output and exit status.
# usage: run.sh <out dir>  (the clips and the probe are made there; run from the repository's root)
#   D-093: H.264 in MPEG-TS, open GOPs of 2 s with every SEI taken out, so that FFmpeg's parser flags
#          only the first frame as a keyframe; with one and with three references (x264's default).
#          corrupt_count.c seeks each to 9.2 s and counts the frames its decoder makes, per decoder flag.
#   D-094: FFmpeg's MPEG-TS of AAC and H.264: where each stream starts, the container's start and duration.
O=${1:?usage: run.sh <out dir>}
FF=/opt/homebrew/opt/ffmpeg-full/bin
mkdir -p "$O" || exit 9
run() { printf '$ %s\n' "$*"; "$@" 2>&1; printf '[exit status %s]\n\n' "$?"; }
run git rev-parse HEAD
run sw_vers
run sh -c "'$FF/ffmpeg' -hide_banner -version | head -1"

for ref in 1 3; do
  run "$FF/ffmpeg" -nostdin -v error -y -f lavfi -i testsrc2=size=320x180:rate=25 -t 30 -c:v libx264 -preset medium \
      -pix_fmt yuv420p -x264-params keyint=50:min-keyint=50:open-gop=1:scenecut=0:ref=$ref \
      -bsf:v filter_units=remove_types=6 -f mpegts "$O/nosei_ref$ref.ts"
  run sh -c "'$FF/ffprobe' -v error -select_streams v -show_entries packet=flags -of csv=p=0 '$O/nosei_ref$ref.ts' | grep -c K"
  run sh -c "'$FF/ffprobe' -v error -select_streams v -show_entries frame=pict_type -of csv=p=0 '$O/nosei_ref$ref.ts' | grep -c I"
done
run sh -c "cc -O1 -o '$O/corrupt_count' plan/engineer_tools_runs/review-2026-11-05/corrupt_count.c \$(PKG_CONFIG_PATH=/opt/homebrew/opt/ffmpeg-full/lib/pkgconfig pkg-config --cflags --libs libavformat libavcodec libavutil)"
for ref in 1 3; do
  for fl in 0 corrupt showall; do
    run "$O/corrupt_count" "$O/nosei_ref$ref.ts" 9.2 $fl
  done
done

run "$FF/ffmpeg" -nostdin -v error -y -f lavfi -i testsrc2=size=320x180:rate=25 -f lavfi -i sine=frequency=1000:sample_rate=48000 \
    -t 6 -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -f mpegts "$O/av.ts"
run "$FF/ffprobe" -v error -show_entries format=start_time,duration:stream=index,codec_type,start_time,duration -of compact "$O/av.ts"
run sh -c "cd '$O' && shasum -a 256 nosei_ref1.ts nosei_ref3.ts av.ts"
