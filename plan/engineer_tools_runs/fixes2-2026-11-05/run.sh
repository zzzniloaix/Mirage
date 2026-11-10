#!/bin/sh
# The checks of plan/engineer_tools_player_fixes.md section 5 (the fixes D-095 lets the agent make), in the
# form of eval/test_runs/: each command, then its output and exit status, with uptime before each window.
# usage: SP=<scratch folder> run.sh — SP holds merge-wt (this worktree, built), trash, and gui3/clips
# (the clips plan/engineer_tools_runs/fixes-gui-2026-11-05/run.sh made).
SP=${SP:?the scratch folder: merge-wt (this worktree, built), trash, gui3/clips}
cd "$SP/merge-wt" || exit 9
O=$SP/fixes2
G=$SP/gui3/clips
[ -e "$O" ] && mv "$O" "$SP/trash/fixes2-$(date +%m%d-%H%M%S)"
mkdir -p "$O/home"
B=build/macos-debug
FF=/opt/homebrew/opt/ffmpeg-full/bin
CL=plan/engineer_tools_runs/investigate-2026-11-03/render/clips
run() { printf '$ %s\n' "$*"; "$@" 2>&1; printf '[exit status %s]\n\n' "$?"; }
player() {   # name alarm-secs mirage-args... : one window, its log in <name>.log
  pn=$1; ps=$2; shift 2
  run uptime
  printf '$ HOME=%s/home %s --qa-no-focus %s > %s.log\n' "$O" "$B/mirage" "$*" "$pn"
  HOME="$O/home" caffeinate -i perl -e 'alarm shift; exec @ARGV' "$ps" "$B/mirage" --qa-no-focus "$@" > "$O/$pn.log" 2>&1
  printf '[exit status %s]\n\n' "$?"
}
{
  run git rev-parse HEAD
  run git status --porcelain
  run sw_vers
  run sh -c "ioreg -n Root -d1 -a | plutil -extract IOConsoleUsers xml1 -o - - | grep -A1 CGSSessionScreenIsLocked"
  run cmake --build --preset macos-debug
  run sh -c "cd '$G' && shasum -a 256 tagswitch.ts h264.mp4 h264_noise.mp4 av_evc.mov"

  # 5.2: bar 2's luma codes over every row of each clip's first frame, decoded in software and with
  # VideoToolbox (FFmpeg's hwdownload of its buffer).
  for c in bars_h264_422.mp4:yuv422p:nv16 bars_hevc444_8.mp4:yuv444p:nv24 bars8_hevc.mp4:yuv420p:nv12 \
           bars_hevc422.mp4:yuv422p10le:p210; do
    n=$(echo "$c" | cut -d: -f1); sw=$(echo "$c" | cut -d: -f2); hwf=$(echo "$c" | cut -d: -f3)
    run "$FF/ffmpeg" -nostdin -v error -y -i "$CL/$n" -frames:v 1 -f rawvideo -pix_fmt "$sw" "$O/${n%.*}.sw.raw"
    run "$FF/ffmpeg" -nostdin -v error -y -hwaccel videotoolbox -hwaccel_output_format videotoolbox_vld -i "$CL/$n" \
        -vf "hwdownload,format=$hwf" -frames:v 1 -f rawvideo "$O/${n%.*}.vt.raw"
    case $sw in *10le) run python3 -I plan/engineer_tools_runs/fixes2-2026-11-05/bar2_codes.py "$O/${n%.*}.sw.raw" 2 0 ;;
                    *) run python3 -I plan/engineer_tools_runs/fixes2-2026-11-05/bar2_codes.py "$O/${n%.*}.sw.raw" 1 0 ;; esac
    case $hwf in p210) run python3 -I plan/engineer_tools_runs/fixes2-2026-11-05/bar2_codes.py "$O/${n%.*}.vt.raw" 2 6 ;;
                    *) run python3 -I plan/engineer_tools_runs/fixes2-2026-11-05/bar2_codes.py "$O/${n%.*}.vt.raw" 1 0 ;; esac
  done

  # 5.1: the scrub preview and the step back in the MPEG-TS (keyframes at PTS 1.48 and 4.48), and in an MP4.
  player scrub_gop1 60 --window 1280x720 --speed-seq 'scrub:1.2@3,snap@5' --ui-snapshot "$O/scrub_gop1.png" 1 "$G/tagswitch.ts"
  run grep -E 'speed-seq|ui-snapshot|ScrubDecoder: (stopped|no keyframe)' "$O/scrub_gop1.log"
  player scrub_gop2 60 --window 1280x720 --speed-seq 'scrub:4.2@3,snap@5' --ui-snapshot "$O/scrub_gop2.png" 1 "$G/tagswitch.ts"
  run grep -E 'speed-seq|ui-snapshot|ScrubDecoder: (stopped|no keyframe)' "$O/scrub_gop2.log"
  player scrub_end 60 --window 1280x720 --speed-seq 'scrub:6@7,snap@9' --ui-snapshot "$O/scrub_end.png" 1 "$G/tagswitch.ts"
  run grep -E 'speed-seq|ui-snapshot|ScrubDecoder: (stopped|no keyframe)' "$O/scrub_end.log"
  player steps_back 60 --window 1600x1000 --ui-overlays P --speed-seq 'pause@1.6,back@2,snap@6' --ui-snapshot "$O/steps_back.png" 1 "$G/tagswitch.ts"
  run grep -E 'speed-seq|step landed|ui-snapshot|ScrubDecoder: stopped' "$O/steps_back.log"
  player scrub_mp4 60 --window 1280x720 --speed-seq 'scrub:3.2@3,snap@5' --ui-snapshot "$O/scrub_mp4.png" 1 "$G/h264.mp4"
  run grep -E 'speed-seq|ui-snapshot|ScrubDecoder: stopped' "$O/scrub_mp4.log"

  # 5.3: the I panel's Decode line with VideoToolbox's errors, and an HDR clip's I panel.
  player noise_I 60 --ui-overlays I --ui-snapshot "$O/noise_I.png" 60 "$G/h264_noise.mp4"
  player pq_I 60 --ui-overlays I --ui-snapshot "$O/pq_I.png" 30 "$CL/bars_pq.mp4"

  # 5.4: the badges after a switch to the video track with no decoder.
  player evc_badges 60 --qa-switch 'v#2@2' --speed-seq 'snap@5' --ui-snapshot "$O/evc_badges.png" 0 "$G/av_evc.mov"
  run grep -E 'switch: no decoder|ui-snapshot' "$O/evc_badges.log"

  run git status --porcelain
  run uptime
} > "$O/record_raw.txt" 2>&1
touch "$O/DONE"
