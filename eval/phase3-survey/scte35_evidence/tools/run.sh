#!/bin/sh
# Runs a command from the evidence directory and writes runs/<name>.txt: the
# command, the date, the tools' SHA-256s and its full output, with the
# evidence directory's absolute path written <out>/scte35_evidence and the
# scratch directory's <scratch>, so that the record holds no local path.
# usage: sh tools/run.sh <name> <command...>
set -u
name=$1; shift
ev=$(cd "$(dirname "$0")/.." && pwd)
scratch=$(cd "$ev/../../.." && pwd)
log="$ev/runs/$name.txt"
cd "$ev"
{
  echo "command (cwd <out>/scte35_evidence): $*"
  echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  for t in tools/*.py tools/*.sh; do echo "tool sha256: $(shasum -a 256 "$t")"; done
  echo "output (stdout and stderr):"
  "$@" 2>&1
  echo "exit=$?"
} | sed -e "s#$ev#<out>/scte35_evidence#g" -e "s#$scratch#<scratch>#g" > "$log"
cat "$log"
