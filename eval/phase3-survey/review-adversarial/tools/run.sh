#!/bin/bash
# run.sh <name> <working directory> <command> [args...]
#
# The adversarial review's runner: runs the command in the working directory
# and writes one record, review-adversarial/runs/<name>.txt, with the working
# directory, the exact command, start and end (UTC), everything the command
# wrote to stdout and stderr in order, and its exit status. Local absolute
# paths become <out>, <repo> and <scratch>; nothing else is changed. Needs
# OUT, REPO and SCRATCH in the environment. Not repository code.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
base="$(dirname "$here")"
name="$1"; shift
dir="$1"; shift
mkdir -p "$base/runs"
raw="$base/runs/$name.raw"
start="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
( cd "$dir" && "$@" ) > "$raw" 2>&1
status=$?
end="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
sanitize() {
  # OUT and REPO lie inside SCRATCH, so they are replaced first; macOS gives
  # /tmp also under its /private prefix, so both spellings are replaced.
  sed \
    -e "s#/private${OUT#/private}#<out>#g"   -e "s#${OUT#/private}#<out>#g" \
    -e "s#/private${REPO#/private}#<repo>#g" -e "s#${REPO#/private}#<repo>#g" \
    -e "s#/private${SCRATCH#/private}#<scratch>#g" -e "s#${SCRATCH#/private}#<scratch>#g"
}
{
  printf '# cwd: %s\n' "$dir"
  printf '# command:'
  for a in "$@"; do printf " '%s'" "$a"; done
  printf '\n# started: %s, ended: %s (UTC)\n' "$start" "$end"
  printf '# stdout and stderr, in order:\n'
  cat "$raw"
  printf '# exit status: %s\n' "$status"
} | sanitize > "$base/runs/$name.txt"
rm -f "$raw"
echo "$name: exit $status, $(wc -l < "$base/runs/$name.txt") lines"
