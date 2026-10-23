#!/bin/bash
# finalize_baseline.sh <run-dir> <dest-dir>
# Copies one run_baseline.sh run into the repository: the player's stdout as
# is, its stderr (FFmpeg's log + time -l) xz-compressed, the JSON report the
# existing code wrote (found under the run's working directory), the run's
# timestamps, and SHA-256 sums of all of it.
run=$1; dest=$2
mkdir -p "$dest"
cp "$run/mirage.log" "$dest/mirage.log"
xz -9 -T1 -c "$run/time_and_stderr.log" > "$dest/time_and_stderr.log.xz"
for f in started_utc.txt report_seen_utc.txt ended_utc.txt rc.txt; do [ -f "$run/$f" ] && cp "$run/$f" "$dest/$f"; done
json=$(find "$run/cwd" -name '*.vmaf.json' -type f | head -1)
if [ -n "$json" ]; then
  cp "$json" "$dest/report.vmaf.json"
  echo "${json#$run/cwd/}" > "$dest/report_path_written_by_mirage.txt"
fi
# The rusage block time(1) printed at the end of stderr, uncompressed for reading.
awk '/real .* user .* sys/{f=1} f' "$run/time_and_stderr.log" > "$dest/time_l.txt"
(cd "$dest" && shasum -a 256 mirage.log time_and_stderr.log.xz time_l.txt *.txt report.vmaf.json 2>/dev/null | grep -v SHA256SUMS > SHA256SUMS)
ls -la "$dest"
