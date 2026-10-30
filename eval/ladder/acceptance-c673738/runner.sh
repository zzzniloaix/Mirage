#!/bin/sh
# usage: run.sh <repo> <dir> <commit> <wait_marker>: once <wait_marker> exists, builds mirage_vmaf
# (macos-release) in a new clean worktree of <commit>, makes the local copy of Apple's advanced
# sample and checks it against the committed SHA256SUMS, and runs §2's three acceptance checks
# with eval/ladder/pool_check.py: pool sizes at 300 frames, the 1 GiB limit at 300 frames, and
# the 1 GiB limit over every frame from Apple's CDN. Writes <dir>/DONE at the end.
REPO=$1; D=$2; C=$3; WAITM=$4
CDN=https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/master.m3u8
while [ ! -f "$WAITM" ]; do sleep 30; done
LOG="$D/run.log"
batt() { pmset -g batt | head -2 | sed 's/(id=[0-9]*)//'; }
redact() { sed -e "s|$D/wt|<worktree>|g" -e "s|$D|<scratch>|g" -e "s|$HOME|<home>|g"; }
git -C "$REPO" worktree add --detach "$D/wt" "$C" > "$LOG" 2>&1 || { echo "worktree add failed" >> "$LOG"; touch "$D/DONE"; exit 1; }
cd "$D/wt" || { touch "$D/DONE"; exit 9; }
{ echo "HEAD $(git rev-parse HEAD)"; date -u +%Y-%m-%dT%H:%M:%SZ; uptime; sw_vers; batt; } >> "$LOG" 2>&1
{ echo '$ git rev-parse HEAD'; git rev-parse HEAD; echo '$ git status --porcelain'; git status --porcelain; } > "$D/build.txt"
{ echo '$ cmake --preset macos-release'; cmake --preset macos-release 2>&1; echo "(exit status $?)"; } | redact > "$D/configure.log"
{ echo '$ cmake --build --preset macos-release --target mirage_vmaf'; cmake --build --preset macos-release --target mirage_vmaf 2>&1; echo "(exit status $?)"; } | redact > "$D/build.log"
{ echo '$ shasum -a 256 build/macos-release/mirage_vmaf'; shasum -a 256 build/macos-release/mirage_vmaf | sed 's|build/macos-release/mirage_vmaf|<mirage_vmaf>|'; } >> "$D/build.txt"
echo "built $(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$LOG"
mkdir -p "$D/tmp"
python3 -I -B eval/streams/local_copy.py "$D/copy" "$D/copy-record" --segments 2 > "$D/copy.log" 2>&1
echo "copy exit $? $(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$LOG"
( cd "$D/copy" && shasum -a 256 -c "$D/wt/eval/streams/local-advanced-2026-10-26/SHA256SUMS" ) > "$D/copy_check.txt" 2>&1
echo "copy check exit $? ($(grep -c ': OK$' "$D/copy_check.txt") OK)" >> "$LOG"
P=build/macos-release/mirage_vmaf
for run in local-300 local-300-mem1g cdn-full; do
  case $run in
    local-300)       set -- --ladder "$D/copy/master.m3u8" --frames 300 ;;
    local-300-mem1g) set -- --ladder "$D/copy/master.m3u8" --frames 300 --jobs 1,0 --memory-limit 1G ;;
    cdn-full)        set -- --ladder "$CDN" --frames 0 --jobs 0 --memory-limit 1G ;;
  esac
  echo "$run start $(date -u +%Y-%m-%dT%H:%M:%SZ) $(uptime)" >> "$LOG"
  TMPDIR="$D/tmp" caffeinate -i -s python3 -I -B eval/ladder/pool_check.py "$P" "$D/$run" "$@" >> "$LOG" 2>&1
  echo "$run exit $? $(date -u +%Y-%m-%dT%H:%M:%SZ) $(uptime)" >> "$LOG"
done
{ date -u +%Y-%m-%dT%H:%M:%SZ; batt; echo "status at the end: $(git status --porcelain | wc -l | tr -d ' ') changed paths"; } >> "$LOG" 2>&1
touch "$D/DONE"
