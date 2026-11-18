#!/bin/bash
# quote_each.sh <checker> <reference> <file>...
#
# Runs the quote checker on each file named, one file at a time, so that the
# record gives every file's own count of prose runs and runs of names. The
# checker prints counts, files and lines only, never the shared words. Exit
# status 1 if any file has a prose run. For the third review of the merged
# Phase 3 design (review-3); not repository code.
set -u
checker="$1"; shift
reference="$1"; shift
rc=0
for f in "$@"; do
  echo "== $f"
  python3 -I "$checker" "$reference" "$f" || rc=1
done
exit $rc
