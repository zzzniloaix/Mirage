#!/bin/sh
# Whether the Unified demo's renditions carry the same cue lines: prints each
# playlist's DATERANGE / CUE-OUT / CUE-IN lines and their SHA-256 together.
for f in downloads/stream-unified-avod-k8s-video4017000/body downloads/stream-unified-avod-k8s-video608000/body downloads/stream-unified-avod-k8s-audio130000/body; do
  lines=$(grep -E '^#EXT-X-(DATERANGE|CUE-OUT|CUE-IN)' "$f")
  echo "$f: $(printf '%s' "$lines" | shasum -a 256 | cut -d' ' -f1)"
  printf '%s\n' "$lines"
done
