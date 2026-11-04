#!/bin/sh
# usage: sidedata_check.sh <dir>: encodes a 1 s test clip per codec in <dir> and lists, for its
# first four frames, the side data FFmpeg's software decoders export (plan/engineer_tools_ideas.md).
# FF: the directory of ffmpeg and ffprobe (default: Homebrew's ffmpeg-full).
cd "$1" || exit 9
FF=${FF:-/opt/homebrew/opt/ffmpeg-full/bin}
enc() { n=$1; shift; "$FF/ffmpeg" -v error -y -f lavfi -i testsrc2=size=320x240:rate=25 -t 1 -pix_fmt yuv420p "$@" "$n.mkv" || echo "encode $n failed"; }
enc h264  -c:v libx264 -bf 2
enc mpeg2 -c:v mpeg2video -bf 2
enc hevc  -c:v libx265 -x265-params log-level=error
enc vp9   -c:v libvpx-vp9 -deadline realtime
enc av1   -c:v libsvtav1 -preset 12
for n in h264 mpeg2 hevc vp9 av1; do
  printf '%-6s ' "$n"
  "$FF/ffprobe" -v error -export_side_data venc_params -flags2 +export_mvs -select_streams v \
    -show_entries frame=pict_type:frame_side_data=side_data_type -read_intervals %+#4 -of compact=nk=1 "$n.mkv" 2>&1 | tr '\n' ' '
  echo
done
