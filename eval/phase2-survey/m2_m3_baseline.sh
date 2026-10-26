#!/bin/sh
# M2 and M3 of docs/PHASE2_SURVEY.md: one pair's peak memory and wall time, and the ladder's,
# scored one pair after another by a mirage_vmaf from before the pool, on the local copy of
# Apple's advanced sample (eval/streams/local-advanced-2026-10-26/), the first 300 frame pairs
# of each pair, as D-049's acceptance runs score it.
#
# usage: eval/phase2-survey/m2_m3_baseline.sh <mirage_vmaf> <copy_dir> <out_dir>
#
# Writes the record to stdout: each command, then its output and /usr/bin/time -l's lines,
# with <mirage_vmaf>, <copy_dir> and <out_dir> in place of paths; the reports go to <out_dir>.
P=$1; C=$2; O=$3
[ $# -eq 3 ] || { echo "usage: $0 <mirage_vmaf> <copy_dir> <out_dir>" >&2; exit 2; }
mkdir -p "$O" || exit 2
shown() { sed -e "s|$O|<out_dir>|g" -e "s|$C|<copy_dir>|g" -e "s|$P|<mirage_vmaf>|g"; }
run() {
    echo "\$ /usr/bin/time -l $*" | shown
    /usr/bin/time -l "$@" 2>&1 | grep -vE "^\[(INFO|WARN)\]" | shown
    echo
}
echo "# M2 and M3 of docs/PHASE2_SURVEY.md, made by eval/phase2-survey/m2_m3_baseline.sh: each command, then its output and /usr/bin/time -l's lines."
echo
echo "\$ shasum -a 256 <mirage_vmaf>"
shasum -a 256 "$P" | shown
echo
echo "## M2: one pair, the top rung against a 1080p and against a 270p playlist"
echo
run "$P" --ref "$C/v9/prog_index.m3u8" --dis "$C/v8/prog_index.m3u8" --frames 300 --out "$O/m2_v9_v8.json"
run "$P" --ref "$C/v9/prog_index.m3u8" --dis "$C/v2/prog_index.m3u8" --frames 300 --out "$O/m2_v9_v2.json"
echo "## M3: the ladder, one pair after another"
echo
run "$P" --ladder "$C/master.m3u8" --frames 300 --out "$O/m3_ladder.json"
