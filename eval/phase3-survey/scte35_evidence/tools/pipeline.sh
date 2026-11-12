#!/bin/sh
# Every step after the fetches, in order, each recorded in runs/<step>.txt by tools/run.sh.
# usage (cwd <out>/scte35_evidence): sh tools/pipeline.sh <repo> <ffmpeg-full prefix>
set -eu
R=$1; F=$2
sh tools/run.sh derive_all sh tools/derive_all.sh > /dev/null
sh tools/run.sh build_vectors python3 -I tools/build_vectors.py . vectors_src.json > /dev/null
sh tools/run.sh check_vectors python3 -I tools/check_vectors.py vectors_src.json ../scte35_vectors.json > /dev/null
sh tools/run.sh cue_positions sh tools/cue_positions_all.sh > /dev/null
sh tools/run.sh ts_probe-unified-avod python3 -I tools/ts_probe.py downloads/stream-unified-avod-k8s-video4017000-seg1/body downloads/stream-unified-avod-k8s-video4017000-seg6/body > /dev/null
sh tools/run.sh ts_pid35-unified-avod-seg6 python3 -I tools/ts_pid_payload.py downloads/stream-unified-avod-k8s-video4017000-seg6/body 35 > /dev/null
sh tools/run.sh hls_tags-streams python3 -I tools/hls_tags.py \
 "Unified Remix AVOD demo master" downloads/stream-unified-avod-k8s-master/body \
 "Unified Remix AVOD demo video=4017000" downloads/stream-unified-avod-k8s-video4017000/body \
 "Unified Remix AVOD demo video=608000" downloads/stream-unified-avod-k8s-video608000/body \
 "Unified Remix AVOD demo audio=130000" downloads/stream-unified-avod-k8s-audio130000/body \
 "Unified live scte35.isml master" downloads/stream-unified-live-scte35-master/body \
 "Unified live scte35.isml media" downloads/stream-unified-live-scte35-media/body \
 "Unified live scte35-no-splicing.isml master" downloads/stream-unified-live-scte35-nosplice-master/body \
 "Unified live scte35-no-splicing.isml media" downloads/stream-unified-live-scte35-nosplice-media/body \
 "Mux test stream dai-discontinuity-deltatre" downloads/stream-mux-dai-deltatre-master/body \
 "threefive examples/hls/scte35.m3u8 (repository file)" downloads/threefive-old-examples_hls_scte35.m3u8/body > /dev/null
sh tools/run.sh same_daterange sh tools/same_daterange.sh > /dev/null
sh tools/run.sh decode-medialive-oatcls python3 -I tools/scte35_decode.py "<vector aws-medialive:126:EXT-OATCLS-SCTE35>" > /dev/null
sh tools/run.sh decode-scte35go-unused-subsegments python3 -I tools/scte35_decode.py "<vector scte35-go:Unused Subsegments Included>" > /dev/null
sh tools/run.sh build_parser_probe sh tools/build_parser_probe.sh "$R" "$F" > /dev/null
sed -i '' "s#$F#<ffmpeg-full 8.1.1 prefix>#g" runs/build_parser_probe.txt
sh tools/run.sh parser_probe build/parser_probe downloads/threefive-old-examples_hls_scte35.m3u8/body \
 derived/playlists/wowza-cue-example.m3u8 derived/playlists/google-video-stitcher-eabn-example.m3u8 \
 downloads/stream-unified-avod-k8s-video4017000/body downloads/stream-mux-dai-deltatre-master/body > /dev/null
rm -rf build
sh tools/run.sh repo_grep sh tools/repo_grep.sh "$R" > /dev/null
echo "pipeline done"
