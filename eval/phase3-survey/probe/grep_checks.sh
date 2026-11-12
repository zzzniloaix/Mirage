#!/bin/sh
# The text searches sources.md states results of: each pattern, where it was
# searched, and every match (none: "(no match)"). Needs REPO and OUT.
set -u
check() {
  what="$1"; shift
  echo "== $what"
  echo "   grep $*"
  out="$(grep "$@" 2>&1)"
  if [ -n "$out" ]; then echo "$out"; else echo "(no match)"; fi
}
check "automatic switching or bandwidth estimation in the code" \
  -rniE '\babr\b|adaptive bitrate|bandwidth estimat|throughput estimat' "$REPO/src" "$REPO/tools"
check "request_video_switch: its definition and its callers" \
  -rn 'request_video_switch(' "$REPO/src" "$REPO/tools"
check "AV_CODEC_FLAG_COPY_OPAQUE or opaque_ref in the code and tests" \
  -rnE 'COPY_OPAQUE|opaque_ref' "$REPO/src" "$REPO/tools" "$REPO/tests"
check "AVPacket::pos read in the code and tests" \
  -rnE 'pkt->pos|packet->pos|p->pos' "$REPO/src" "$REPO/tools" "$REPO/tests"
check "CUE-OUT, CUE-IN or OATCLS in RFC 8216" \
  -nE 'CUE-OUT|CUE-IN|OATCLS' "$OUT/rfc/rfc8216.txt"
check "CUE-OUT forms in the repository's tests" \
  -rn 'EXT-X-CUE-OUT' "$REPO/tests"
