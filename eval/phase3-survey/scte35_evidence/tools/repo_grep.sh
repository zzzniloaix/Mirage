#!/bin/sh
# What the repository has for SCTE-35 today (read only). usage: sh tools/repo_grep.sh <repo>
R=$1
cd "$R" || exit 1
echo "git: $(git rev-parse --abbrev-ref HEAD) $(git rev-parse --short HEAD)"
echo "-- grep -rn -i -E 'scte|splice|oatcls|daterange|cue-out|cue-in|CueOut|CueIn' src (excluding nothing)"
grep -rn -i -E 'scte|splice|oatcls|daterange|cue-out|cue-in|CueOut|CueIn' src
echo "-- grep -rn -i -E 'scte|splice|oatcls|daterange|cue' tests"
grep -rn -i -E 'scte|splice|oatcls|daterange|cue' tests
echo "-- the tags HlsAlign's parse_media_timeline reads"
grep -n -E 'tag == "#EXT' src/HlsAlign.cpp
