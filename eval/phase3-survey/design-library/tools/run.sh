#!/bin/bash
# run.sh <name> <working directory> <command> [args...]
#
# Runs the command in the working directory and writes one record,
# design-library/runs/<name>.txt: the working directory, the exact command,
# when it started and ended (UTC), everything it wrote to stdout and stderr in
# order, and its exit status. The local absolute paths in the record become
# <repo>, <out>, <scratch>, <homebrew> and <home>; nothing else is changed.
# Needs REPO, OUT, SCRATCH and HOMEBREW in the environment (tools/pipeline.sh
# sets them from its arguments), so this file names no local path.
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
  # OUT and REPO lie inside SCRATCH: they are replaced first. macOS gives
  # /tmp also under its /private prefix: both spellings are replaced.
  sed \
    -e "s#/private${OUT#/private}#<out>#g"   -e "s#${OUT#/private}#<out>#g" \
    -e "s#/private${REPO#/private}#<repo>#g" -e "s#${REPO#/private}#<repo>#g" \
    -e "s#/private${SCRATCH#/private}#<scratch>#g" -e "s#${SCRATCH#/private}#<scratch>#g" \
    -e "s#${HOMEBREW}#<homebrew>#g" \
    -e "s#${HOME}#<home>#g"
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
