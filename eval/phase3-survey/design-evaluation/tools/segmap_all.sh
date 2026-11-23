#!/bin/sh
# Runs segmap on every rung of the testgen ladders the design uses, from each ladder's own
# directory (so that the playlists and segments are named relative to it), and writes the JSON
# of each to <out dir>/<ladder>-<rung>.json.
# usage: sh segmap_all.sh <segmap binary> <testgen media dir> <out dir>
set -eu
segmap="$1"; media="$2"; out="$3"
mkdir -p "$out"
for ladder in degraded-x264-a ad-x264-a degraded-mpeg2-a ad-mpeg2-a ad-x264-origin1; do
  for rung in v360 v270 v180; do
    echo "== $ladder $rung =="
    (cd "$media/$ladder" && "$segmap" "$rung/index.m3u8" "$out/$ladder-$rung.json")
  done
done
