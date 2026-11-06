#!/bin/sh
# Reruns the commands of runs 07, 08 and 10 with the tools as they are now, into a scratch copy of
# the out directory, and compares each stdout with the one its record holds (the records' local
# paths are placeholders, so the reruns' output goes through the same replacement first: run.py).
# usage: sh repro_check.sh <design-evaluation dir>
set -eu
d="$1"
tmp="$d/out-repro"
rm -rf "$tmp"; mkdir -p "$tmp"
cp -R "$d/out/session" "$tmp/session"
cp -R "$d/out/segmap" "$tmp/segmap"
extract() { sed -n '/^--- stdout ---$/,/^--- stderr ---$/p' "$1" | sed '1d;$d'; }
for spec in "07_attribute_seconds_as_doubles::" "08_attribute_ticks:--ticks:" "10_attribute_final:--ticks:--segments --own-variants"; do
  name=${spec%%:*}; rest=${spec#*:}; ticks=${rest%%:*}; extra=${rest#*:}
  TICKS="$ticks" EXTRA="$extra" sh "$d/tools/attribute_all.sh" "$d/../testgen" "$tmp/segmap" "$tmp" > "$tmp/$name.stdout"
  python3 -I -B "$d/tools/run.py" _scratch_repro -- cat "$tmp/$name.stdout" > /dev/null
  extract "$d/runs/_scratch_repro.txt" > "$tmp/$name.now"
  extract "$d/runs/$name.txt" > "$tmp/$name.recorded"
  if cmp -s "$tmp/$name.now" "$tmp/$name.recorded"; then echo "$name: stdout reproduced"; else echo "$name: DIFFERS"; diff "$tmp/$name.recorded" "$tmp/$name.now" | head -20; fi
done
rm -f "$d/runs/_scratch_repro.txt"
rm -rf "$tmp"
