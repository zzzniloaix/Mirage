#!/bin/sh
# Writes derived/: the text of the SCTE 35 PDF and of each HTML page read.
set -eu
python3 -I tools/pdftext.py downloads/scte35-go-docs_SCTE_35_2023r1.pdf/body derived/SCTE_35_2023r1.txt
for n in aws-medialive-sample-manifests-hls unified-live-scte-35 unified-faq-dai unified-demo-scte35 \
         unified-avod-scte35-mediatailor-page wowza-scte-35 broadpeak-dai-scte35 google-admanager-13049027 \
         gcloud-video-stitcher-ad-markers adobe-psdk-ios-live-linear; do
  printf '%s: ' "$n"; python3 -I tools/html2text.py "downloads/$n/body" "derived/$n.txt"
done
sed -n 1830,1870p derived/wowza-scte-35.txt | sed 's/^ //' > derived/playlists/wowza-cue-example.m3u8
sed -n 545,568p derived/gcloud-video-stitcher-ad-markers.txt > derived/playlists/google-video-stitcher-eabn-example.m3u8
for f in derived/*.txt derived/playlists/*; do echo "sha256 $(shasum -a 256 "$f")"; done
