#!/bin/sh
# Reruns every committed Phase 1 measurement into a new directory and
# compares the outputs with the committed ones. See eval/README.md, or run
# it with --help.
#
# usage: eval/run_all.sh <out_dir> [--mirage-vmaf PROG] [--mirage-vmaf-before PROG]
#                        [--skip-before] [--only STEP[,STEP...]] [...]
exec python3 -I -B "$(dirname "$0")/run_all.py" "$@"
