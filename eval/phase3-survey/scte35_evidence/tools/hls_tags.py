#!/usr/bin/env python3
"""Which tags a fetched playlist carries: master or media, VOD or live
(EXT-X-PLAYLIST-TYPE, EXT-X-ENDLIST), encryption (EXT-X-KEY METHOD), and
the count of each SCTE-35 related tag, with the first line of each.
usage: python3 -I hls_tags.py <label> <playlist> [...] (label, file pairs)"""
import re
import sys

TAGS = ["EXT-X-CUE-OUT", "EXT-X-CUE-OUT-CONT", "EXT-X-CUE-IN", "EXT-OATCLS-SCTE35", "EXT-X-DATERANGE",
        "EXT-X-SCTE35", "EXT-X-SPLICEPOINT-SCTE35", "EXT-X-ASSET", "EXT-X-CUE", "EXT-X-DISCONTINUITY",
        "EXT-X-PROGRAM-DATE-TIME", "EXT-X-KEY"]
args = sys.argv[1:]
for label, f in zip(args[0::2], args[1::2]):
    text = open(f, encoding="utf-8", errors="replace").read()
    lines = [l.strip() for l in text.split("\n")]
    kind = "master" if any(l.startswith("#EXT-X-STREAM-INF") for l in lines) else "media"
    ptype = next((l.split(":", 1)[1] for l in lines if l.startswith("#EXT-X-PLAYLIST-TYPE:")), None)
    endlist = any(l == "#EXT-X-ENDLIST" for l in lines)
    segs = sum(1 for l in lines if l.startswith("#EXTINF"))
    print(f"== {label}")
    print(f"   {kind} playlist, {len(text)} bytes, {segs} EXTINF, EXT-X-PLAYLIST-TYPE {ptype}, "
          f"EXT-X-ENDLIST {'yes' if endlist else 'no'} -> "
          + ("VOD" if (endlist and ptype in (None, 'VOD', 'EVENT')) else ("master" if kind == "master" else "live (no EXT-X-ENDLIST)")))
    for t in TAGS:
        hits = [l for l in lines if re.match(r"#" + re.escape(t) + r"(:|$)", l)]
        if hits:
            ex = re.sub(r"eyJ[A-Za-z0-9_=-]+", "<b64>", hits[0])
            print(f"   {t}: {len(hits)} (first: {ex[:160]})")
    methods = sorted({m.group(1) for l in lines for m in [re.search(r"METHOD=([A-Z0-9-]+)", l)] if l.startswith("#EXT-X-KEY") and m})
    if methods:
        print(f"   EXT-X-KEY methods: {methods}")
    other = sorted({l.split(":")[0] for l in lines if l.startswith("#") and not l.startswith("##")
                    and l.split(":")[0][1:] not in TAGS and l.split(":")[0] not in
                    ("#EXTM3U", "#EXTINF", "#EXT-X-VERSION", "#EXT-X-TARGETDURATION", "#EXT-X-MEDIA-SEQUENCE",
                     "#EXT-X-PLAYLIST-TYPE", "#EXT-X-ENDLIST", "#EXT-X-INDEPENDENT-SEGMENTS", "#EXT-X-STREAM-INF",
                     "#EXT-X-MEDIA", "#EXT-X-I-FRAME-STREAM-INF", "#EXT-X-MAP", "#EXT-X-DISCONTINUITY-SEQUENCE")})
    if other:
        print(f"   other tags: {other}")
