#!/usr/bin/env python3
"""Where each SCTE-35 cue of a media playlist falls, by each rule a source
gives: the segment the tag applies to (the next URI line, RFC 8216bis
§4.4.4), that segment's playlist time (the sum of the EXTINF before it),
its EXT-X-PROGRAM-DATE-TIME (explicit, or carried forward when no
discontinuity is between), START-DATE mapped through the PDT of the
segment holding it (as hls.js does, src/loader/date-range.ts startTime),
and the splice time inside the SCTE-35 payload (pts_adjustment applied,
mod 2^33), with tools/scte35_decode.py.

usage: python3 -I cue_positions.py <label> <file> [first_line last_line]
"""
import base64
import datetime as dt
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scte35_decode as sd  # noqa: E402

CUE_TAGS = ("EXT-X-CUE-OUT-CONT", "EXT-X-CUE-OUT", "EXT-X-CUE-IN", "EXT-OATCLS-SCTE35", "EXT-X-DATERANGE",
            "EXT-X-SPLICEPOINT-SCTE35", "EXT-X-SCTE35", "EXT-X-ASSET", "EXT-X-CUE")


def parse_date(s):
    s = s.strip().strip('"')
    s = s.replace("Z", "+00:00")
    m = re.match(r"(.*T\d\d:\d\d:\d\d)(\.\d+)?(.*)$", s)
    if not m:
        return None
    frac = (m.group(2) or ".0")[:7]
    try:
        return dt.datetime.fromisoformat(m.group(1) + frac + (m.group(3) or "+00:00"))
    except ValueError:
        return None


def payload(tag, value):
    """(encoding, string) of the SCTE-35 message a tag carries, or None."""
    if tag in ("EXT-OATCLS-SCTE35", "EXT-X-SPLICEPOINT-SCTE35"):
        return value.strip()
    if tag == "EXT-X-CUE-OUT-CONT":
        m = re.search(r"SCTE35=([A-Za-z0-9+/=]+)", value)
        return m.group(1) if m else None
    if tag == "EXT-X-DATERANGE":
        m = re.search(r"SCTE35-(?:OUT|IN|CMD)=?(0[xX][0-9A-Fa-f]+)", value)
        return m.group(1) if m else None
    if tag == "EXT-X-SCTE35":
        m = re.search(r'CUE="([^"]+)"', value)
        return m.group(1) if m else None
    return None


def decode(s):
    try:
        if s[:2].lower() == "0x":
            data = bytes.fromhex(s[2:])
        else:
            data = base64.b64decode(s, validate=True)
        d = sd.decode(data)
    except Exception as e:  # noqa: BLE001
        return f"not decoded ({e})"
    parts = [d.get("splice_command_name", "?"), "crc ok" if d["crc_32_valid"] else "CRC BAD"]
    c = d.get("command", {})
    if d.get("splice_command_type") == 5:
        parts.append("out" if c.get("out_of_network_indicator") else "in")
        if c.get("splice_immediate_flag"):
            parts.append("immediate")
        if "break_duration" in c:
            parts.append(f"break_duration {sd.seconds(c['break_duration']['duration'])} s")
    for x in d.get("descriptors", []):
        if x.get("name") == "segmentation_descriptor":
            parts.append(f"type 0x{x.get('segmentation_type_id', 0):02x}")
            if "segmentation_duration" in x:
                parts.append(f"segmentation_duration {sd.seconds(x['segmentation_duration'])} s")
    t = sd.times(d)
    if t:
        parts.append(f"splice PTS {t[0]['adjusted']} = {t[0]['adjusted_seconds']} s"
                     + (f" (pts_time {t[0]['pts_time']} + pts_adjustment {d['pts_adjustment']})" if d["pts_adjustment"] else ""))
    else:
        parts.append("no splice PTS in the message")
    return ", ".join(parts)


def splice_seconds(s):
    try:
        data = bytes.fromhex(s[2:]) if s[:2].lower() == "0x" else base64.b64decode(s, validate=True)
        t = sd.times(sd.decode(data))
    except Exception:  # noqa: BLE001
        return None
    return t[0]["adjusted_seconds"] if t else None


def main():
    label, f = sys.argv[1], sys.argv[2]
    lines = open(f, encoding="utf-8", errors="replace").read().split("\n")
    first = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    last = int(sys.argv[4]) if len(sys.argv) > 4 else len(lines)
    segs, cues, pending = [], [], []
    extinf, pdt, disc, t = None, None, False, 0.0
    playlist_type, endlist, tsmap = None, False, None
    for n in range(first, last + 1):
        raw = lines[n - 1].strip()
        if not raw:
            continue
        if raw.startswith("#"):
            body = raw[1:].strip()
            name, _, value = body.partition(":")
            name = name.strip()
            if name == "EXTINF":
                extinf = float(value.split(",")[0])
            elif name == "EXT-X-PROGRAM-DATE-TIME":
                pdt = parse_date(value)
            elif name == "EXT-X-DISCONTINUITY":
                disc = True
            elif name == "EXT-X-PLAYLIST-TYPE":
                playlist_type = value
            elif name == "EXT-X-ENDLIST":
                endlist = True
            elif name == "USP-X-TIMESTAMP-MAP":
                tsmap = value
            elif name.split(" ")[0] in CUE_TAGS:
                pending.append({"line": n, "tag": name.split(" ")[0], "value": value, "text": raw})
            continue
        if extinf is None:
            print(f"   warning: line {n}: a URI line without EXTINF, skipped (not counted as a segment)")
            continue
        seg_pdt = pdt
        if seg_pdt is None and segs and not disc and segs[-1]["pdt"] is not None:
            seg_pdt = segs[-1]["pdt"] + dt.timedelta(seconds=segs[-1]["dur"])
        seg = {"i": len(segs), "start": t, "dur": extinf, "pdt": seg_pdt, "line": n, "disc": disc}
        segs.append(seg)
        for c in pending:
            c["seg"] = seg
        cues.extend(pending)
        pending = []
        t += extinf
        extinf, pdt, disc = None, None, False
    for c in pending:
        c["seg"] = None
    cues.extend(pending)
    print(f"== {label}: lines {first}-{last} of {f}")
    print(f"   segments {len(segs)}, playlist duration {round(t, 6)} s, EXT-X-PLAYLIST-TYPE {playlist_type}, "
          f"EXT-X-ENDLIST {'yes' if endlist else 'no'}" + (f", USP-X-TIMESTAMP-MAP {tsmap}" if tsmap else ""))
    for c in cues:
        s = c["seg"]
        where = (f"applies to segment #{s['i']} (URI line {s['line']}): playlist time {round(s['start'], 6)} s"
                 + (f", PDT {s['pdt'].isoformat()}" if s["pdt"] else ", no PDT")) if s else "after the last segment"
        print(f" line {c['line']}: {c['text'][:150]}")
        print(f"    position: {where}")
        if c["tag"] == "EXT-X-DATERANGE":
            m = re.search(r'START-DATE="?([^",]+)"?', c["value"])
            sdate = parse_date(m.group(1)) if m else None
            if sdate:
                holder = [x for x in segs if x["pdt"] and x["pdt"] <= sdate < x["pdt"] + dt.timedelta(seconds=x["dur"])]
                if holder:
                    h = holder[-1]
                    mt = h["start"] + (sdate - h["pdt"]).total_seconds()
                    print(f"    START-DATE {sdate.isoformat()}: inside segment #{h['i']}, playlist time {round(mt, 6)} s"
                          + (f" ({round(mt - s['start'], 6):+} s from the tag's position)" if s else ""))
                else:
                    with_pdt = [x for x in segs if x["pdt"]]
                    if with_pdt:
                        h = with_pdt[0] if sdate < with_pdt[0]["pdt"] else with_pdt[-1]
                        mt = h["start"] + (sdate - h["pdt"]).total_seconds()
                        print(f"    START-DATE {sdate.isoformat()}: in no segment's PDT range; extrapolated from segment #{h['i']}: "
                              f"playlist time {round(mt, 6)} s" + (f" ({round(mt - s['start'], 6):+} s from the tag's position)" if s else ""))
                    else:
                        print(f"    START-DATE {sdate.isoformat()}: the playlist has no PDT to map it")
        p = payload(c["tag"], c["value"])
        if p:
            print(f"    payload: {decode(p)}")
            if c["tag"] == "EXT-X-DATERANGE":
                m = re.search(r'START-DATE="?([^",]+)"?', c["value"])
                sdate = parse_date(m.group(1)) if m else None
                pts = splice_seconds(p)
                if sdate is not None and pts is not None:
                    print(f"    START-DATE minus the splice PTS: {round(sdate.timestamp() - pts, 6)} s")
                m = re.search(r'END-DATE="?([^",]+)"?', c["value"])
                edate = parse_date(m.group(1)) if m else None
                if edate is not None and pts is not None:
                    print(f"    END-DATE minus the splice PTS: {round(edate.timestamp() - pts, 6)} s")


if __name__ == "__main__":
    main()
