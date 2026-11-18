#!/bin/bash
# run.sh <name> <working directory> <command> [args...]
#
# Runs the command in the working directory and saves, in $OUT/runs/<name>/:
#   cmd.txt     the working directory and the exact command, as run
#   output.txt  everything it wrote to stdout and stderr, in order
#   exit.txt    its exit status, and when it started and ended (UTC)
# Both through probe/sanitize.sh: the local absolute paths become <repo>,
# <out>, <scratch>, <homebrew>; nothing else is changed. Needs REPO, OUT,
# SCRATCH and HOMEBREW in the environment.
set -u
name="$1"; shift
dir="$1"; shift
here="$(cd "$(dirname "$0")" && pwd)"
run="$OUT/runs/$name"
mkdir -p "$run"
{
  printf 'cwd: %s\n' "$dir"
  printf 'command:'
  for a in "$@"; do printf " '%s'" "$a"; done
  printf '\n'
} | "$here/sanitize.sh" > "$run/cmd.txt"
start="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
( cd "$dir" && "$@" ) > "$run/output.raw" 2>&1
status=$?
end="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
"$here/sanitize.sh" < "$run/output.raw" > "$run/output.txt"
rm -f "$run/output.raw"
printf 'exit status: %s\nstarted: %s\nended: %s\n' "$status" "$start" "$end" > "$run/exit.txt"
echo "$name: exit $status, $(wc -l < "$run/output.txt") lines"
