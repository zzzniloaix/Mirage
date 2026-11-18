#!/bin/sh
# Fetches one URL into its own new directory, downloads/<name>/body, and logs
# the command, the date, curl's report and the body's SHA-256 to
# runs/fetch-<name>.txt. Paths in the log are relative to the evidence
# directory (<out>/scte35_evidence).
# usage: sh tools/fetch.sh <name> <url>
set -u
name=$1; url=$2
ev=$(cd "$(dirname "$0")/.." && pwd)
d="$ev/downloads/$name"
mkdir -p "$d"
log="$ev/runs/fetch-$name.txt"
fmt='http_code=%{http_code}\nurl_effective=%{url_effective}\nsize_download=%{size_download}\ncontent_type=%{content_type}\n'
{
  echo "command: curl -sS -L --max-time 90 -o downloads/$name/body -w '$fmt' '$url'"
  echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  ( cd "$d" && curl -sS -L --max-time 90 -o body -w "$fmt" "$url" 2>&1 ); echo "curl_exit=$?"
  if [ -f "$d/body" ]; then echo "sha256=$(shasum -a 256 "$d/body" | cut -d' ' -f1)"; fi
} > "$log"
cat "$log"
