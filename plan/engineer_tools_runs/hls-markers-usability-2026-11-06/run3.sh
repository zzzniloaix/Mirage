#!/bin/sh
# The checks of plan/hls_markers_usability.md section 3, fix 3, in the form of eval/test_runs/: each command,
# then its output and exit status, with uptime before each window.
# usage: SCR=<scratch folder> run3.sh — as run.sh: from the root of a worktree of the branch, both builds
# made; each window runs from $SCR with relative paths.
SCR=${SCR:?a scratch folder}
REPO=$(pwd)
RD=plan/engineer_tools_runs/hls-markers-usability-2026-11-06
B=$REPO/build/macos-debug
mkdir -p "$SCR/home"
run() { printf '$ %s\n' "$*"; "$@" 2>&1; printf '[exit status %s]\n\n' "$?"; }
player() {   # name alarm-secs mirage-args... : one window from $SCR, its log in <name>.log
  pn=$1; ps=$2; shift 2
  run uptime
  printf '$ (cd %s && HOME=%s/home %s --qa-no-focus %s > %s.log)\n' "$SCR" "$SCR" "$B/mirage" "$*" "$pn"
  (cd "$SCR" && HOME="$SCR/home" perl -e 'alarm shift; exec @ARGV' "$ps" "$B/mirage" --qa-no-focus "$@" > "$SCR/$pn.log" 2>&1)
  printf '[exit status %s]\n\n' "$?"
}
check() {   # name master: mirage_hlscheck --markers-only on the master (relative to SCR), its JSON in <name>.json
  run sh -c "cd '$SCR' && '$B/mirage_hlscheck' --markers-only --marker-tag EXT-X-CUE-MARKER --json $1.json $2 > $1.txt"
}
{
  run git rev-parse HEAD
  run git status --porcelain
  run sw_vers
  run sh -c "ioreg -n Root -d1 -a | plutil -extract IOConsoleUsers xml1 -o - - | grep -A1 CGSSessionScreenIsLocked"
  run cmake --build --preset macos-debug
  run cmake --build --preset macos-headless
  run sh tests/hlsmarkers_demo.sh "$B/mirage_hlscheck" "$SCR/demo"
  run python3 -I "$RD/make_inputs.py" "$SCR" "$SCR/demo"

  # The mixed package: thirty audio renditions in one group, three codec families, the language at the head.
  # Opening a card with marker_open= logs each of its rows as the card shows it: count them, and the alike.
  check cli_mixed mixed/master.m3u8
  player mixed_b1 120 --window 1600x1000 --ui-net "markers_only=1,marker_tags=EXT-X-CUE-MARKER,marker_open=0,conform_out=tab_mixed.json,hls=mixed/master.m3u8" --ui-snapshot "$SCR/mixed_b1.png" 30
  run cmp "$SCR/cli_mixed.json" "$SCR/tab_mixed.json"
  run grep -E 'card .* row:' "$SCR/mixed_b1.log"
  run sh -c "grep -E 'card .* row:' '$SCR/mixed_b1.log' | sed -E 's/.*row: //' | sort | uniq -d | wc -l"
  player mixed_b2 120 --window 1600x1000 --ui-net "markers_only=1,marker_tags=EXT-X-CUE-MARKER,marker_open=1,hls=mixed/master.m3u8" --ui-snapshot "$SCR/mixed_b2.png" 30
  run grep -E 'card .* row:' "$SCR/mixed_b2.log"
  run sh -c "grep -E 'card .* row:' '$SCR/mixed_b2.log' | sed -E 's/.*row: //' | sort | uniq -d | wc -l"
  # Fix 2's packages again: NAMEs that differ in their first token only, and NAMEs that share their head.
  player tails_b2 120 --window 1600x1000 --ui-net "markers_only=1,marker_tags=EXT-X-CUE-MARKER,marker_open=1,hls=tails/master.m3u8" --ui-snapshot "$SCR/tails_b2.png" 30
  run grep -E 'card .* row:' "$SCR/tails_b2.log"
  player names_b2 120 --window 1600x1000 --ui-net "markers_only=1,marker_tags=EXT-X-CUE-MARKER,marker_open=1,hls=names/master.m3u8" --ui-snapshot "$SCR/names_b2.png" 30
  run grep -E 'card .* row:' "$SCR/names_b2.log"
  # Section 1's test script, again.
  run sh tests/hlsmarkers_gui.sh build/macos-debug "$SCR/guitest"

  run git status --porcelain
  run uptime
} > "$SCR/record3_raw.txt" 2>&1
touch "$SCR/DONE3"
