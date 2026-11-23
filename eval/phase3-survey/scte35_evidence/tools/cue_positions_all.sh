#!/bin/sh
# The example playlists cue_positions.py is run on (paths relative to the
# evidence directory; line ranges cut a document's example out of it).
set -u
p() { python3 -I tools/cue_positions.py "$@"; echo; }
p "threefive-2024 examples/hls/scte35.m3u8 (MIT)" downloads/threefive-old-examples_hls_scte35.m3u8/body
p "threefive-2024 examples/hls/simple-scte35.m3u8 (MIT)" downloads/threefive-old-examples_hls_simple-scte35.m3u8/body
p "Unified Streaming Remix AVOD demo, 4017000 b/s video playlist (fetched)" downloads/stream-unified-avod-k8s-video4017000/body
p "AWS MediaPackage v1 example manifest" downloads/aws-mediapackage-v1-scte.md/body 120 153
p "Unified Origin live example (docs)" derived/unified-live-scte-35.txt 1522 1541
p "Wowza DATERANGE example" derived/wowza-scte-35.txt 1791 1820
p "Wowza EXT-X-CUE example" derived/wowza-scte-35.txt 1830 1870
p "broadpeak.io DATERANGE example" derived/broadpeak-dai-scte35.txt 424 440
p "broadpeak.io EXT-OATCLS-SCTE35 example" derived/broadpeak-dai-scte35.txt 450 465
p "broadpeak.io EXT-X-CUE-OUT example" derived/broadpeak-dai-scte35.txt 502 523
