#!/bin/sh
# usage: run.sh <repo> <dir> <commit>: eval/run_all.sh <dir>/out --jobs 4, every step, from a new
# clean worktree of <commit> (D-063); the load average every 600 s in <dir>/load.log; writes
# <dir>/DONE when it ends.
REPO=$1; D=$2; C=$3
LOG="$D/run.log"
git -C "$REPO" worktree add --detach "$D/wt" "$C" > "$LOG" 2>&1 || { echo "worktree add failed" >> "$LOG"; touch "$D/DONE"; exit 1; }
mkdir -p "$D/tmp"
cd "$D/wt" || { touch "$D/DONE"; exit 9; }
{
  echo "start $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "HEAD $(git rev-parse HEAD)"
  echo "status at the start: $(git status --porcelain | wc -l | tr -d ' ') changed paths"
  uptime
  sw_vers
  pmset -g batt | head -2
} >> "$LOG" 2>&1
( while [ ! -f "$D/DONE" ]; do echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) $(uptime)"; sleep 600; done ) > "$D/load.log" 2>&1 &
TMPDIR="$D/tmp" caffeinate -i -s eval/run_all.sh "$D/out" --jobs 4 >> "$LOG" 2>&1
echo "exit $?" >> "$LOG"
{
  echo "end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "status at the end: $(git status --porcelain | wc -l | tr -d ' ') changed paths"
  uptime
  pmset -g batt | head -2
} >> "$LOG" 2>&1
touch "$D/DONE"
