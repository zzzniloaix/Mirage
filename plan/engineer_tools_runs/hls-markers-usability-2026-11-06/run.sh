#!/bin/sh
# The checks of plan/hls_markers_usability.md, in the form of eval/test_runs/: each command, then its output
# and exit status, with uptime before each window.
# usage: SCR=<scratch folder> run.sh — from the root of a worktree of the branch, both builds made. Each
# window runs from $SCR with relative paths, so no local path shows in its snapshot.
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

  # The inputs: the demo package; then the names package, the encrypted copy and the MPD (make_inputs.py).
  run sh tests/hlsmarkers_demo.sh "$B/mirage_hlscheck" "$SCR/demo"
  run python3 -I "$RD/make_inputs.py" "$SCR" "$SCR/demo"

  # Section 1: the check with nothing playing (no media argument), then the test script that does the same.
  check cli_demo demo/master.m3u8
  player launcher_demo 120 --window 1600x1000 --ui-net "markers_only=1,marker_tags=EXT-X-CUE-MARKER,marker_open=0,conform_out=tab_demo.json,conform_html=tab_demo.html,hls=demo/master.m3u8" --ui-snapshot "$SCR/launcher_demo.png" 30
  run cmp "$SCR/cli_demo.json" "$SCR/tab_demo.json"
  run grep -E 'ui-snapshot|conform_out|conformance:' "$SCR/launcher_demo.log"
  run sh tests/hlsmarkers_gui.sh build/macos-debug "$SCR/guitest"
  # The inspector's other tabs on the launcher, nothing playing.
  for t in network manifest alignment; do
    player "launcher_$t" 60 --window 1600x1000 --ui-net "tab=$t" --ui-snapshot "$SCR/launcher_$t.png" 30
  done
  # A master the player cannot open (SAMPLE-AES, a made-up key) and an MPD: the launcher's error, "Check it".
  player enc_fail 60 --window 1600x1000 --ui-snapshot "$SCR/enc_fail.png" 30 enc/master.m3u8
  run grep -E 'avformat_open_input|ui-snapshot' "$SCR/enc_fail.log"
  player mpd_fail 60 --window 1600x1000 --ui-snapshot "$SCR/mpd_fail.png" 30 mpd/manifest.mpd
  run grep -E 'avformat_open_input|ui-snapshot' "$SCR/mpd_fail.log"
  # That master checked from the launcher, as "Check it" does (NetInspector::check_url, as hls= is).
  check cli_enc enc/master.m3u8
  player launcher_enc 120 --window 1600x1000 --ui-net "markers_only=1,marker_tags=EXT-X-CUE-MARKER,conform_out=tab_enc.json,hls=enc/master.m3u8" --ui-snapshot "$SCR/launcher_enc.png" 30
  run cmp "$SCR/cli_enc.json" "$SCR/tab_enc.json"

  # Sections 2 to 4: the names package. marker_open=0 and 1; the default width and a wide inspector; the
  # missing tag an error (D-102). The page, as --html writes it.
  run sh -c "cd '$SCR' && '$B/mirage_hlscheck' --markers-only --marker-tag EXT-X-CUE-MARKER --json cli_names.json --html cli_names.html names/master.m3u8"
  player names_b1 120 --window 1600x1000 --ui-net "markers_only=1,marker_tags=EXT-X-CUE-MARKER,marker_open=0,conform_out=tab_names.json,hls=names/master.m3u8" --ui-snapshot "$SCR/names_b1.png" 30
  run cmp "$SCR/cli_names.json" "$SCR/tab_names.json"
  player names_b2 120 --window 1600x1000 --ui-net "markers_only=1,marker_tags=EXT-X-CUE-MARKER,marker_open=1,hls=names/master.m3u8" --ui-snapshot "$SCR/names_b2.png" 30
  player names_b2_wide 120 --window 1600x1000 --ui-net "width=900,markers_only=1,marker_tags=EXT-X-CUE-MARKER,marker_open=1,hls=names/master.m3u8" --ui-snapshot "$SCR/names_b2_wide.png" 30
  # The page's script, parsed by JavaScriptCore without running it, and the data it reads.
  run python3 -I "$RD/page_script.py" "$SCR/cli_names.html" "$SCR/page_check.js"
  run /System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc "$SCR/page_check.js"
  run grep -o -E '"missing": \[[0-9, ]*\], "missing_severity": \[[^]]*\]' "$SCR/cli_names.html"

  # The session's way in, as before: the demo master playing, conform=1 (the 2026-11-05 record's run).
  run sh -c "cd '$SCR' && '$B/mirage_hlscheck' --marker-tag EXT-X-CUE-MARKER --json cli_playing.json demo/master.m3u8 > cli_playing.txt"
  player tab_playing 120 --window 1600x1000 --ui-net "tab=conformance,conform=1,width=760,marker_tags=EXT-X-CUE-MARKER,marker_open=0,conform_out=tab_playing.json" --ui-snapshot "$SCR/tab_playing.png" 300 demo/master.m3u8
  run cmp "$SCR/cli_playing.json" "$SCR/tab_playing.json"

  run git status --porcelain
  run uptime
} > "$SCR/record_raw.txt" 2>&1
touch "$SCR/DONE"
