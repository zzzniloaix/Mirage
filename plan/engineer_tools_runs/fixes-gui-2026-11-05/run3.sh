#!/bin/sh
# The player fixes' scripted checks, third part (record3): the runs whose playback needs audio output,
# which the first run made with the screen locked (CoreAudio then plays nothing, and video, which follows
# the audio clock, hardly moves). Refuses to run while the screen is locked. A control first: the source
# clip played plainly, its audio and video clocks side by side.
SP=${SP:?the scratch folder: merge-wt (this worktree, built), trash}
cd "$SP/merge-wt" || exit 9
if ioreg -n Root -d1 -a | plutil -extract IOConsoleUsers xml1 -o - - | grep -A1 CGSSessionScreenIsLocked | grep -q true; then
  echo "the screen is locked: not running"; exit 3
fi
O=$SP/gui3c
G=$SP/gui3                      # the first run's clips
[ -e "$O" ] && mv "$O" "$SP/trash/gui3c-$(date +%m%d-%H%M%S)"
mkdir -p "$O/home"
B=build/macos-debug
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
  run sh -c "cd '$G/clips' && shasum -a 256 av_src.mp4 av_ac4.mov av_evc.mov r_only_av.mp4 nosei.ts"

  # Control: audio plays, and the clocks move together.
  player control 60 --av-report "$O/control.json" 5 "$G/clips/av_src.mp4"
  run cat "$O/control.json"

  # Section 4.2: a switch to a track this FFmpeg has no decoder for.
  player ac4_tracks 60 --qa-switch 'a#2@2' --speed-seq 'panel:T@2.5,snap@3' --ui-snapshot "$O/ac4_tracks.png" 0 \
      --av-report "$O/ac4.json" 6 "$G/clips/av_ac4.mov"
  run cat "$O/ac4.json"
  run grep -E 'switch|No decoder|no decoder|ui-snapshot|av-report|loading-indicator' "$O/ac4_tracks.log"
  player ac4_report 60 --qa-switch 'a#2@2' --av-report "$O/ac4_report.json" 8 "$G/clips/av_ac4.mov"
  run cat "$O/ac4_report.json"
  player evc_tracks 60 --qa-switch 'v#2@2' --speed-seq 'panel:T@2.5,snap@3' --ui-snapshot "$O/evc_tracks.png" 0 \
      --av-report "$O/evc.json" 6 "$G/clips/av_evc.mov"
  run cat "$O/evc.json"
  run grep -E 'switch|No decoder|no decoder|ui-snapshot|av-report|loading-indicator' "$O/evc_tracks.log"
  player evc_seek 60 --qa-switch 'v#2@2' --speed-seq 'seek:1@4' --av-report "$O/evc_seek.json" 8 "$G/clips/av_evc.mov"
  run cat "$O/evc_seek.json"
  run grep -E 'switch|seek|loading-indicator|no data' "$O/evc_seek.log"

  # Section 4.4: the W strip of a clip whose left channel is silent.
  player r_only_W 60 --ui-overlays W --ui-snapshot "$O/r_only_W.png" 120 "$G/clips/r_only_av.mp4"

  # D-093: the seek 50 s into the stream without flagged keyframes, timed with its audio playing.
  for m in hw sw; do
    f=""; [ $m = sw ] && f=--no-hw
    player "seek_nosei_$m" 180 --window 1280x720 $f --ui-sub-at 50 --ui-snapshot "$O/seek_nosei_$m.png" 30 "$G/clips/nosei.ts"
    run grep -E 'ui-sub-at|Demuxer: no keyframe|first frame after seek|ui-snapshot' "$O/seek_nosei_$m.log"
  done

  run git status --porcelain
  run uptime
} > "$O/record_raw.txt" 2>&1
touch "$O/DONE"
