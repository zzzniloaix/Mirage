#!/bin/sh
# The prototype attribution (attribute.py) on the testgen ladders: the two acceptance cases with
# their controls, their MPEG-2 and ad-origin-1 variants, and the scripted session "ad-switch".
# The prototype's parameters: T = 6 (the work order's suggestion), W = 50 and N = 12 (testgen's
# prototype values, inside the region run 05 measured), not defaults.
# With TICKS=--ticks in the environment the report's PTS are rounded to the time base; EXTRA
# passes more options (--segments --own-variants).
# usage: sh attribute_all.sh <testgen dir> <segmap dir> <out dir>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
tg="$1"; sm="$2"; out="$3"
p="--threshold 6 --window 50 --min-frames 12 ${TICKS:-} ${EXTRA:-}"
a() { python3 -I -B "$here/attribute.py" "$@"; echo; }
a ladder "$tg/media/degraded-x264-a" "$tg/reports/degraded-x264.json" "$sm" degraded-x264-a "$out/degraded-x264-v270.json" $p --curve v270
a ladder "$tg/media/degraded-x264-a" "$tg/reports/degraded-x264.json" "$sm" degraded-x264-a "$out/degraded-x264-v180.json" $p --curve v180
a ladder "$tg/media/ad-x264-a" "$tg/reports/ad-x264.json" "$sm" ad-x264-a "$out/ad-x264-v180.json" $p --curve v180
a ladder "$tg/media/ad-x264-a" "$tg/reports/ad-x264.json" "$sm" ad-x264-a "$out/ad-x264-v270.json" $p --curve v270
a ladder "$tg/media/degraded-mpeg2-a" "$tg/reports/degraded-mpeg2.json" "$sm" degraded-mpeg2-a "$out/degraded-mpeg2-v270.json" $p --curve v270
a ladder "$tg/media/ad-mpeg2-a" "$tg/reports/ad-mpeg2.json" "$sm" ad-mpeg2-a "$out/ad-mpeg2-v180.json" $p --curve v180
a ladder "$tg/media/ad-x264-origin1" "$tg/reports/ad-x264-origin1.json" "$sm" ad-x264-origin1 "$out/ad-x264-origin1-v180.json" $p --curve v180
a session "$tg/media/ad-x264-a" "$tg/reports/ad-x264.json" "$sm" ad-x264-a "$out/ad-x264-session-ad-switch.json" $p --plan "$out/session/ad-switch.plan" --fetch "$out/session/ad-switch-fetch.json"
