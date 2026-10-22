#!/bin/bash
# run_baseline.sh <mirage-binary> <url> <outdir>
# Phase 0 baseline: the existing --vmaf ladder scoring, unchanged. Mirage writes
# its report to "<url>.vmaf.json" relative to the working directory, so the
# directories that path names are created first. Ends the player once the
# report line appears (the existing tool keeps playing after it).
bin=$1; url=$2; out=$3
S=${SCRATCH:?set SCRATCH to a scratch directory}
mkdir -p "$out/cwd" "$S/home-baseline"
rel="${url#*://}"; mkdir -p "$out/cwd/${url%%://*}:/$(dirname "$rel")"
cd "$out/cwd" || exit 1
date -u +%Y-%m-%dT%H:%M:%SZ > "$out/started_utc.txt"
# time(1) is the parent (its rusage report needs it to outlive the player);
# the 6 h alarm sits on the player itself.
HOME=$S/home-baseline /usr/bin/time -l perl -e 'alarm shift; exec @ARGV' 21600 "$bin" "$url" --vmaf --qa-no-focus --window 1280x720 > "$out/mirage.log" 2> "$out/time_and_stderr.log" &
pid=$!
echo "$pid" > "$out/pid.txt"
while kill -0 $pid 2>/dev/null; do
  if grep -q 'VMAF report written to' "$out/mirage.log" 2>/dev/null; then
    perl -e 'select(undef,undef,undef,2.0)'
    date -u +%Y-%m-%dT%H:%M:%SZ > "$out/report_seen_utc.txt"
    pkill -TERM -P $pid 2>/dev/null   # the player (time's child); time then reports
    break
  fi
  perl -e 'select(undef,undef,undef,15.0)'
done
wait $pid; echo "rc=$?" > "$out/rc.txt"
date -u +%Y-%m-%dT%H:%M:%SZ > "$out/ended_utc.txt"
