#!/bin/sh
# join_check.py on the five testgen ladders the design uses.
# usage: sh join_all.sh <testgen reports dir> <segmap dir>
set -eu
here=$(cd "$(dirname "$0")" && pwd)
r="$1"; s="$2"
python3 -I -B "$here/join_check.py" "$r/degraded-x264.json" "$s" degraded-x264-a
python3 -I -B "$here/join_check.py" "$r/ad-x264.json" "$s" ad-x264-a
python3 -I -B "$here/join_check.py" "$r/degraded-mpeg2.json" "$s" degraded-mpeg2-a
python3 -I -B "$here/join_check.py" "$r/ad-mpeg2.json" "$s" ad-mpeg2-a
python3 -I -B "$here/join_check.py" "$r/ad-x264-origin1.json" "$s" ad-x264-origin1
