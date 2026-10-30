#!/bin/sh
# Section 5 consistency check: Mirage's VMAF scores against FFmpeg's libvmaf
# filter. See eval/consistency/README.md, or run it with --help.
#
# usage: eval/consistency/run_delta.sh <mirage_vmaf> <out_dir> [--frames N] [--jobs J]
#                                      [--pairs <file>] [--diagnostic-relabel] [...]
exec python3 -I -B "$(dirname "$0")/run_delta.py" "$@"
