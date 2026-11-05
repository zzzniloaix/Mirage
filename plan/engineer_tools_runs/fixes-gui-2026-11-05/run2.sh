#!/bin/sh
# The player fixes' scripted checks, second part (record2): what the first run of gui3_runs.sh did not make
# or could not show with the screen locked, for the runs without audio. The ten --no-hw runs of the bars,
# which a clash of the script's variables skipped, with the comparisons against the first run's HW captures;
# the frame steps, one capture per run; the scrub preview inside a GOP of the MPEG-TS and of an MP4.
SP=${SP:?the scratch folder: merge-wt (this worktree, built), trash}
cd "$SP/merge-wt" || exit 9
O=$SP/gui3b
G=$SP/gui3                      # the first run: its HW captures and its clips
[ -e "$O" ] && mv "$O" "$SP/trash/gui3b-$(date +%m%d-%H%M%S)"
mkdir -p "$O/home" "$O/colour"
B=build/macos-debug
CL=plan/engineer_tools_runs/investigate-2026-11-03/render
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
  run sh -c "cd '$G/clips' && shasum -a 256 tagswitch.ts h264.mp4"

  # ── Section 3: the bars with --no-hw, against the first run's HW captures.
  for c in bars_sdr10.mp4 bars_pq.mp4 bars8_hevc.mp4 bars_prores422hq.mov bars_prores4444.mov \
           bars_prores4444a.mov bars_hevc422.mp4 bars_hevc444.mp4 bars_h264_422.mp4 bars_hevc444_8.mp4; do
    n=${c%.*}
    player "colour/${n}_sw" 90 --no-hw --snapshot "$O/colour/${n}_sw.png" 10 "$CL/clips/$c"
  done
  for c in bars_sdr10 bars_pq bars8_hevc bars_prores422hq bars_prores4444 bars_prores4444a \
           bars_hevc422 bars_hevc444 bars_h264_422 bars_hevc444_8; do
    run python3 -I "$CL/bars_at.py" "$O/colour/${c}_sw.png"
    run "$B/snapshot_diff" "$G/colour/${c}_hw.png" "$O/colour/${c}_sw.png"
  done

  # ── Section 2: a frame step forward, and one back, each with one capture after it.
  player steps_fwd 60 --window 1600x1000 --ui-overlays P --speed-seq 'pause@1,fwd@2,snap@3' --ui-snapshot "$O/steps_fwd.png" 1 "$G/clips/tagswitch.ts"
  run grep -E 'speed-seq|step landed|ui-snapshot' "$O/steps_fwd.log"
  player steps_back 60 --window 1600x1000 --ui-overlays P --speed-seq 'pause@1.6,back@2,snap@6' --ui-snapshot "$O/steps_back.png" 1 "$G/clips/tagswitch.ts"
  run grep -E 'speed-seq|step landed|ui-snapshot|ScrubDecoder' "$O/steps_back.log"

  # ── The scrub preview (ScrubDecoder): inside the MPEG-TS's first GOP and its second (keyframes at
  # positions 0 and 3), and inside an MP4, whose index gives the keyframe.
  player scrub_gop1 60 --window 1280x720 --speed-seq 'scrub:1.2@3,snap@5' --ui-snapshot "$O/scrub_gop1.png" 1 "$G/clips/tagswitch.ts"
  run grep -E 'speed-seq|ui-snapshot|ScrubDecoder' "$O/scrub_gop1.log"
  player scrub_gop2 60 --window 1280x720 --speed-seq 'scrub:4.2@3,snap@5' --ui-snapshot "$O/scrub_gop2.png" 1 "$G/clips/tagswitch.ts"
  run grep -E 'speed-seq|ui-snapshot|ScrubDecoder' "$O/scrub_gop2.log"
  player scrub_mp4 60 --window 1280x720 --speed-seq 'scrub:3.2@3,snap@5' --ui-snapshot "$O/scrub_mp4.png" 1 "$G/clips/h264.mp4"
  run grep -E 'speed-seq|ui-snapshot|ScrubDecoder' "$O/scrub_mp4.log"

  run git status --porcelain
  run uptime
} > "$O/record_raw.txt" 2>&1
touch "$O/DONE"
