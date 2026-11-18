#!/bin/sh
# Every step of testgen.md, in order, each run through run.py, which records
# its command, its full stdout and stderr, its exit status and wall time in
# logs/<step>.txt. Needs a new or empty media/ and reports/ next to this
# script, Homebrew's ffmpeg-full, and the Phase 2 program, given as the first
# argument (its SHA-256 is recorded first).
#
# usage: sh run_all.sh <mirage_vmaf> <debug mirage_vmaf>
# The second program is the debug build of the Phase 3 branch, to time
# scoring as a debug build of the tests would and compare its scores.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
V=$1
VD=$2
cd "$here"
for d in media reports logs; do
    if [ -d "$d" ] && [ -n "$(ls -A "$d")" ]; then echo "run_all.sh: $d/ is not empty" >&2; exit 2; fi
    mkdir -p "$d"
done

r() { python3 -I run.py "$@" > /dev/null 2>&1; }          # a step that must succeed
x() { if python3 -I run.py "$@" > /dev/null 2>&1; then echo "run_all.sh: $1 was to fail" >&2; exit 1; fi; }

r 00_mirage_vmaf_sha256 -- shasum -a 256 "$V"
r 01_mirage_vmaf_help -- "$V" --help
r 02_build -- sh build.sh
r 03_build_O0 -- sh build_O0.sh
r 04_encoders -- ./testgen encoders
r 05_machine -- sh -c 'sysctl -n machdep.cpu.brand_string hw.physicalcpu hw.memsize; sw_vers -productVersion; uptime | sed "s/.*load/load/"'

# The degraded-segment ladder, libx264: twice, compared; read; scored against
# the top rung and against a lossless mezzanine.
r 10_degraded_x264_gen_a -- ./testgen degraded media/degraded-x264-a --codec libx264
r 11_degraded_x264_gen_b -- ./testgen degraded media/degraded-x264-b --codec libx264
r 12_degraded_x264_sha256 -- python3 -I tree_sha256.py media/degraded-x264-a media/degraded-x264-b
r 13_degraded_x264_probe_v270 -- ./testgen probe media/degraded-x264-a/v270/index.m3u8
r 14_degraded_x264_score --cwd media/degraded-x264-a -- "$V" --ladder master.m3u8 --out ../../reports/degraded-x264.json
r 15_degraded_x264_tables -- python3 -I tables.py media/degraded-x264-a/layout.json reports/degraded-x264.json
r 16_mezzanine_degraded_gen -- ./testgen mezzanine media/mezzanine/degraded.mkv --kind degraded
r 17_degraded_x264_score_mezzanine --cwd media/degraded-x264-a -- "$V" --ladder master.m3u8 --mezzanine ../mezzanine/degraded.mkv --out ../../reports/degraded-x264-mezzanine.json
r 18_degraded_x264_tables_mezzanine -- python3 -I tables.py media/degraded-x264-a/layout.json reports/degraded-x264-mezzanine.json

# The same with FFmpeg's MPEG-2 encoder.
r 20_degraded_mpeg2_gen_a -- ./testgen degraded media/degraded-mpeg2-a --codec mpeg2video
r 21_degraded_mpeg2_gen_b -- ./testgen degraded media/degraded-mpeg2-b --codec mpeg2video
r 22_degraded_mpeg2_sha256 -- python3 -I tree_sha256.py media/degraded-mpeg2-a media/degraded-mpeg2-b
r 23_degraded_mpeg2_probe_v270 -- ./testgen probe media/degraded-mpeg2-a/v270/index.m3u8
r 24_degraded_mpeg2_score --cwd media/degraded-mpeg2-a -- "$V" --ladder master.m3u8 --out ../../reports/degraded-mpeg2.json
r 25_degraded_mpeg2_tables -- python3 -I tables.py media/degraded-mpeg2-a/layout.json reports/degraded-mpeg2.json

# The existing tests' ramp as the source, for comparison: its measured
# BANDWIDTHs don't follow the rungs, so the ladder is refused, then written
# with the existing tests' nominal BANDWIDTHs.
x 30_ramp_gen_measured_bandwidth -- ./testgen degraded media/ramp-refused --codec libx264 --source ramp
r 31_ramp_gen -- ./testgen degraded media/degraded-x264-ramp --codec libx264 --source ramp --nominal-bandwidth
r 32_ramp_score --cwd media/degraded-x264-ramp -- "$V" --ladder master.m3u8 --out ../../reports/degraded-x264-ramp.json
r 33_ramp_tables -- python3 -I tables.py media/degraded-x264-ramp/layout.json reports/degraded-x264-ramp.json

# The ad-boundary ladder, libx264: the calm ad, its timestamps from 10 s, the
# discontinuity indicator on each new sequence's first packets.
r 40_ad_x264_gen_a -- ./testgen ad media/ad-x264-a --codec libx264
r 41_ad_x264_gen_b -- ./testgen ad media/ad-x264-b --codec libx264
r 42_ad_x264_sha256 -- python3 -I tree_sha256.py media/ad-x264-a media/ad-x264-b
r 43_ad_x264_probe_v180 -- ./testgen probe media/ad-x264-a/v180/index.m3u8
r 44_ad_x264_probe_v360 -- ./testgen probe media/ad-x264-a/v360/index.m3u8
r 45_ad_x264_score --cwd media/ad-x264-a -- "$V" --ladder master.m3u8 --out ../../reports/ad-x264.json
r 46_ad_x264_tables -- python3 -I tables.py media/ad-x264-a/layout.json reports/ad-x264.json
r 46b_ad_x264_frames_at_splices -- python3 -I frames_at.py reports/ad-x264.json 1 0:1 73:77 123:127
# A control: the same ladder with v180's ad at the normal quantizer, what the
# change of content alone does to v180's scores.
r 47_ad_x264_control_gen -- ./testgen ad media/ad-x264-control --codec libx264 --q-low 22
r 48_ad_x264_control_score --cwd media/ad-x264-control -- "$V" --ladder master.m3u8 --out ../../reports/ad-x264-control.json
r 49_ad_x264_control_tables -- python3 -I tables.py media/ad-x264-control/layout.json reports/ad-x264-control.json

# The same with FFmpeg's MPEG-2 encoder.
r 50_ad_mpeg2_gen_a -- ./testgen ad media/ad-mpeg2-a --codec mpeg2video
r 51_ad_mpeg2_gen_b -- ./testgen ad media/ad-mpeg2-b --codec mpeg2video
r 52_ad_mpeg2_sha256 -- python3 -I tree_sha256.py media/ad-mpeg2-a media/ad-mpeg2-b
r 53_ad_mpeg2_probe_v180 -- ./testgen probe media/ad-mpeg2-a/v180/index.m3u8
r 54_ad_mpeg2_score --cwd media/ad-mpeg2-a -- "$V" --ladder master.m3u8 --out ../../reports/ad-mpeg2.json
r 55_ad_mpeg2_tables -- python3 -I tables.py media/ad-mpeg2-a/layout.json reports/ad-mpeg2.json

# The busy ad, and its control.
r 56_ad_x264_busy_gen -- ./testgen ad media/ad-x264-busy --codec libx264 --ad-content busy
r 57_ad_x264_busy_score --cwd media/ad-x264-busy -- "$V" --ladder master.m3u8 --out ../../reports/ad-x264-busy.json
r 58_ad_x264_busy_tables -- python3 -I tables.py media/ad-x264-busy/layout.json reports/ad-x264-busy.json
r 59a_ad_x264_busy_control_gen -- ./testgen ad media/ad-x264-busy-control --codec libx264 --ad-content busy --q-low 22
r 59b_ad_x264_busy_control_score --cwd media/ad-x264-busy-control -- "$V" --ladder master.m3u8 --out ../../reports/ad-x264-busy-control.json
r 59c_ad_x264_busy_control_tables -- python3 -I tables.py media/ad-x264-busy-control/layout.json reports/ad-x264-busy-control.json

# Without the discontinuity indicator: continuity counters that start again
# unmarked at each EXT-X-DISCONTINUITY.
r 60_ad_x264_nodisc_gen -- ./testgen ad media/ad-x264-nodisc --codec libx264 --no-ts-discontinuity
r 61_ad_x264_nodisc_probe_v180 -- ./testgen probe media/ad-x264-nodisc/v180/index.m3u8
r 62_ad_x264_nodisc_score --cwd media/ad-x264-nodisc -- "$V" --ladder master.m3u8 --out ../../reports/ad-x264-nodisc.json
r 63_ad_x264_nodisc_tables -- python3 -I tables.py media/ad-x264-nodisc/layout.json reports/ad-x264-nodisc.json
r 64_ad_x264_nodisc_compare -- python3 -I compare_reports.py reports/ad-x264.json reports/ad-x264-nodisc.json
r 65_ad_x264_nodisc_count_warnings -- sh -c 'grep -c "Packet corrupt" logs/62_ad_x264_nodisc_score.txt; grep -c "Packet corrupt" logs/45_ad_x264_score.txt || true'

# The ad's timestamps from 1 s: a forward jump of more than 10 s after it.
r 70_ad_x264_origin1_gen -- ./testgen ad media/ad-x264-origin1 --codec libx264 --ad-origin 1
r 71_ad_x264_origin1_probe_v180 -- ./testgen probe media/ad-x264-origin1/v180/index.m3u8
r 72_ad_x264_origin1_score --cwd media/ad-x264-origin1 -- "$V" --ladder master.m3u8 --out ../../reports/ad-x264-origin1.json
r 73_ad_x264_origin1_tables -- python3 -I tables.py media/ad-x264-origin1/layout.json reports/ad-x264-origin1.json
# From 0 s: x264's first DTS is before 0, and the muxer shifts the sequence.
r 74_ad_x264_origin0_gen -- ./testgen ad media/ad-x264-origin0 --codec libx264 --ad-origin 0
r 75_ad_x264_origin0_probe_v180 -- ./testgen probe media/ad-x264-origin0/v180/index.m3u8

# The ad ladder against one file of the stitched programme, whose timestamps
# go on through the ad.
r 80_mezzanine_ad_gen -- ./testgen mezzanine media/mezzanine/ad.mkv --kind ad
r 81_ad_x264_score_mezzanine --cwd media/ad-x264-a -- "$V" --ladder master.m3u8 --mezzanine ../mezzanine/ad.mkv --out ../../reports/ad-x264-mezzanine.json
r 82_ad_x264_tables_mezzanine -- python3 -I tables.py media/ad-x264-a/layout.json reports/ad-x264-mezzanine.json

# The generator built without optimization, as the debug preset builds tests.
r 90_degraded_x264_gen_O0 -- ./testgen-O0 degraded media/degraded-x264-O0 --codec libx264
r 91_degraded_x264_sha256_O0 -- python3 -I tree_sha256.py media/degraded-x264-a media/degraded-x264-O0

# Each report's pool and the scoring time of each pair.
r 95_report_summary -- sh -c 'python3 -I report_summary.py reports/*.json'

# The debug build's program on two ladders: its time, and its scores against
# the release program's.
r 96_debug_mirage_vmaf_sha256 -- shasum -a 256 "$VD"
r 97_debug_score_degraded_x264 --cwd media/degraded-x264-a -- "$VD" --ladder master.m3u8 --out ../../reports/debug-degraded-x264.json
r 98_debug_score_ad_x264 --cwd media/ad-x264-a -- "$VD" --ladder master.m3u8 --out ../../reports/debug-ad-x264.json
r 99a_debug_compare_degraded -- python3 -I compare_reports.py reports/degraded-x264.json reports/debug-degraded-x264.json
r 99b_debug_compare_ad -- python3 -I compare_reports.py reports/ad-x264.json reports/debug-ad-x264.json

# The layouts next to the reports they describe.
for l in degraded-x264-a degraded-mpeg2-a degraded-x264-ramp ad-x264-a ad-x264-control ad-x264-busy ad-x264-busy-control ad-mpeg2-a ad-x264-nodisc ad-x264-origin1; do
    r "99_copy_layout_$l" -- cp "media/$l/layout.json" "reports/$l.layout.json"
done
echo "run_all.sh: every step done"
