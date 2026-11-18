#!/bin/sh
# The design's session-fetch run: serves <root> with slow_server.py (one path answered <delay> s
# late), fetches the scripted session's files in plan order with the fetch prototype, writes its
# JSON to <out.json>, and stops the server. Files and segments of the session "ad-switch" on the
# testgen ad ladder: entry 1 (v270) for segments 0-2, entry 2 (v180) for segments 3-4 (the ad),
# entry 1 for segments 5-7; each media playlist is fetched before its first segment.
# usage: sh session_fetch.sh <fetch binary> <root> <slow path> <delay s> <out.json>
set -eu
fetch="$1"; root="$2"; slow="$3"; delay="$4"; out="$5"
here=$(cd "$(dirname "$0")" && pwd)
work=$(dirname "$out")
portf="$work/server.port"
rm -f "$portf"
python3 -I -B "$here/slow_server.py" "$root" "$portf" "$slow" "$delay" 2>"$work/server.log" &
pid=$!
trap 'kill $pid 2>/dev/null || true' EXIT
i=0
while [ ! -s "$portf" ]; do i=$((i+1)); [ $i -gt 100 ] && { echo "server did not start"; exit 1; }; sleep 0.05; done
port=$(cat "$portf")
base="http://127.0.0.1:$port"
plan="$work/session-ad-switch.tsv"
{
  printf 'master\t%s/master.m3u8\n' "$base"
  printf 'playlist v270\t%s/v270/index.m3u8\n' "$base"
  for s in 000 001 002; do printf 'v270 main_%s\t%s/v270/main_%s.ts\n' "$s" "$base" "$s"; done
  printf 'playlist v180\t%s/v180/index.m3u8\n' "$base"
  for s in 000 001; do printf 'v180 ad_%s\t%s/v180/ad_%s.ts\n' "$s" "$base" "$s"; done
  for s in 005 006 007; do printf 'v270 main_%s\t%s/v270/main_%s.ts\n' "$s" "$base" "$s"; done
} > "$plan"
echo "slow path: $slow, delay before its response head: $delay s"
"$fetch" "$plan" "$out"
kill $pid 2>/dev/null || true
wait $pid 2>/dev/null || true
echo "--- server log ---"
sed -e "s/127\.0\.0\.1 - - \[[^]]*\] //" "$work/server.log"
rm -f "$portf" "$work/server.log"
