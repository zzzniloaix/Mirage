#!/usr/bin/env python3
"""Checks every vector of vectors_src.json with tools/scte35_decode.py and
writes the deliverable scte35_vectors.json: the same records, each with an
`agent_check` (the CRC_32, whether its encodings give the same bytes,
whether the decode agrees with what its source states) and an
`agent_decode` summary. Both come from this run, not from the sources.

usage (cwd <out>/scte35_evidence): python3 -I tools/check_vectors.py vectors_src.json ../scte35_vectors.json
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
import scte35_decode as sd  # noqa: E402

SRC, OUT = sys.argv[1], sys.argv[2]
doc = json.load(open(SRC, encoding="utf-8"))
by_id = {v["id"]: v for v in doc["vectors"]}


def decode_value(v, enc):
    if enc == "hex":
        t = v[2:] if v[:2].lower() == "0x" else v
        if len(t) % 2:
            raise sd.DecodeError("odd number of hex digits")
        try:
            return bytes.fromhex(t)
        except ValueError as e:
            raise sd.DecodeError(f"not hex: {e}")
    import base64
    import binascii
    try:
        return base64.b64decode(v, validate=True)
    except (binascii.Error, ValueError) as e:
        raise sd.DecodeError(f"not base64: {e}")


def get(d, dotted):
    cur = d
    for part in dotted.split("."):
        if not isinstance(cur, dict) or part not in cur:
            return None
        cur = cur[part]
    return cur


class Cmp:
    def __init__(self):
        self.checked = 0
        self.mismatch = []

    def eq(self, what, got, want):
        self.checked += 1
        if got != want:
            self.mismatch.append(f"{what}: decoded {got!r}, source {want!r}")


def hexint(s):
    return int(s, 16)


def check_listing(c, d, lines):
    """Compare with the SCTE 35 sample listing (§14)."""
    descs = [x for x in d.get("descriptors", [])]
    di = -1
    for l in lines:
        m = re.match(r"Section Length = (\d+)$", l)
        if m:
            c.eq("section_length", d["section_length"], int(m.group(1)))
        m = re.match(r"Protocol Version = (\d+)$", l)
        if m:
            c.eq("protocol_version", d["protocol_version"], int(m.group(1)))
        m = re.match(r"PTS Adjustment = (0x[0-9a-fA-F]+)$", l)
        if m:
            c.eq("pts_adjustment", d["pts_adjustment"], hexint(m.group(1)))
        m = re.match(r"Tier = (0x[0-9a-fA-F]+)$", l)
        if m:
            c.eq("tier", hexint(d["tier"]), hexint(m.group(1)))
        m = re.match(r"Splice Command Length = (0x[0-9a-fA-F]+)$", l)
        if m:
            c.eq("splice_command_length", d["splice_command_length"], hexint(m.group(1)))
        if l == "Time Signal":
            c.eq("splice_command_type", d["splice_command_type"], 6)
        if l == "Splice Insert":
            c.eq("splice_command_type", d["splice_command_type"], 5)
        m = re.match(r"Time = (0x[0-9a-fA-F]+) - ([\d.]+)$", l)
        if m:
            c.eq("time_signal pts_time", get(d, "command.pts_time"), hexint(m.group(1)))
            c.eq("time_signal pts_time (s)", "%.6f" % (get(d, "command.pts_time") / 90000), m.group(2))
        m = re.match(r"Descriptor Loop Length = (\d+)$", l)
        if m:
            c.eq("descriptor_loop_length", d["descriptor_loop_length"], int(m.group(1)))
        m = re.match(r"(Segmentation|Avail) Descriptor - Length=(\d+)$", l)
        if m:
            di += 1
            c.eq(f"descriptor[{di}].descriptor_length", descs[di]["descriptor_length"] if di < len(descs) else None, int(m.group(2)))
        dd = descs[di] if 0 <= di < len(descs) else {}
        m = re.match(r"Segmentation Event ID = (0x[0-9a-fA-F]+)$", l)
        if m:
            c.eq(f"descriptor[{di}].segmentation_event_id", dd.get("segmentation_event_id"), hexint(m.group(1)))
        if l == "Segmentation Event Cancel Indicator NOT set":
            c.eq(f"descriptor[{di}].segmentation_event_cancel_indicator", dd.get("segmentation_event_cancel_indicator"), 0)
        for name, key in (("Delivery Not Restricted flag", "delivery_not_restricted_flag"),
                          ("Web Delivery Allowed flag", "web_delivery_allowed_flag"),
                          ("No Regional Blackout flag", "no_regional_blackout_flag"),
                          ("Archive Allowed flag", "archive_allowed_flag"),
                          ("Device Restrictions", "device_restrictions")):
            m = re.match(re.escape(name) + r" = (\d+)$", l)
            if m:
                c.eq(f"descriptor[{di}].{key}", dd.get(key), int(m.group(1)))
        if l == "Program Segmentation flag SET":
            c.eq(f"descriptor[{di}].program_segmentation_flag", dd.get("program_segmentation_flag"), 1)
        m = re.match(r"Segmentation Duration = (0x[0-9a-fA-F]+) = ([\d.]+) seconds$", l)
        if m:
            c.eq(f"descriptor[{di}].segmentation_duration", dd.get("segmentation_duration"), hexint(m.group(1)))
        m = re.match(r"UPID Type = Turner Identifier length = (\d+)$", l)
        if m:
            c.eq(f"descriptor[{di}].segmentation_upid_type", dd.get("segmentation_upid_type"), 0x08)
            c.eq(f"descriptor[{di}].segmentation_upid_length", dd.get("segmentation_upid_length"), int(m.group(1)))
        m = re.match(r"Turner Identifier = (0x[0-9a-fA-F]+)$", l)
        if m:
            c.eq(f"descriptor[{di}].segmentation_upid", int(dd.get("segmentation_upid", "0") or "0", 16), hexint(m.group(1)))
        m = re.match(r"Type = (.+)$", l)
        if m:
            want = m.group(1)
            got = dd.get("segmentation_type_name", "")
            c.checked += 1
            if not got.endswith(want) and want not in got:
                c.mismatch.append(f"descriptor[{di}].segmentation_type: decoded {got!r}, source {want!r}")
        m = re.match(r"Segment num = (\d+) Segments Expected = (\d+)$", l)
        if m:
            c.eq(f"descriptor[{di}].segment_num", dd.get("segment_num"), int(m.group(1)))
            c.eq(f"descriptor[{di}].segments_expected", dd.get("segments_expected"), int(m.group(2)))
        m = re.match(r"CRC32 = (0x[0-9a-fA-F]+)$", l)
        if m:
            c.eq("crc_32", hexint(d["crc_32"]), hexint(m.group(1)))
        m = re.match(r"Splice Event ID = (0x[0-9a-fA-F]+)$", l)
        if m:
            c.eq("splice_event_id", get(d, "command.splice_event_id"), hexint(m.group(1)))
        m = re.match(r"Flags OON=(\d) Prog=(\d) Duration=(\d) Immediate=(\d)$", l)
        if m:
            for k, key in enumerate(("out_of_network_indicator", "program_splice_flag", "duration_flag", "splice_immediate_flag")):
                c.eq(key, get(d, "command." + key), int(m.group(k + 1)))
        m = re.match(r"Splice time = (0x[0-9a-fA-F]+) - ([\d.]+)$", l)
        if m:
            c.eq("splice_insert pts_time", get(d, "command.splice_time.pts_time"), hexint(m.group(1)))
        if l == "Auto Return":
            c.eq("break_duration.auto_return", get(d, "command.break_duration.auto_return"), 1)
        m = re.match(r"break duration = (0x[0-9a-fA-F]+) = ([\d.]+) seconds$", l)
        if m:
            c.eq("break_duration.duration", get(d, "command.break_duration.duration"), hexint(m.group(1)))
            c.eq("break_duration.duration (s)", "%.6f" % (get(d, "command.break_duration.duration") / 90000), m.group(2))
        for name, key in (("Unique Program ID", "unique_program_id"), ("Avail Num", "avail_num"), ("Avails Expected", "avails_expected")):
            m = re.match(re.escape(name) + r" = (\d+)$", l)
            if m:
                c.eq(key, get(d, "command." + key), int(m.group(1)))
        m = re.match(r"Avail Descriptor = (0x[0-9a-fA-F]+) - (\d+)$", l)
        if m:
            c.eq(f"descriptor[{di}].provider_avail_id", dd.get("provider_avail_id"), hexint(m.group(1)))


def check_threefive(c, d, sdec):
    j = sdec["json"]
    info, cmd = j["info_section"], j["command"]
    c.eq("section_length", d["section_length"], info["section_length"])
    c.eq("pts_adjustment (s)", round(d["pts_adjustment"] / 90000, 6), info["pts_adjustment"])
    c.eq("tier", hexint(d["tier"]), hexint(info["tier"]))
    c.eq("splice_command_length", d["splice_command_length"], info["splice_command_length"])
    c.eq("splice_command_type", d["splice_command_type"], info["splice_command_type"])
    c.eq("descriptor_loop_length", d["descriptor_loop_length"], info["descriptor_loop_length"])
    c.eq("crc_32", hexint(d["crc_32"]), hexint(info["crc"]))
    dc = d["command"]
    st = dc.get("splice_time", dc) if d["splice_command_type"] == 5 else dc
    if "time_specified_flag" in cmd:
        c.eq("time_specified_flag", bool(st.get("time_specified_flag")), cmd["time_specified_flag"])
    if "pts_time" in cmd:
        c.eq("pts_time (s)", round(st["pts_time"] / 90000, 6), cmd["pts_time"])
    for key in ("splice_event_id", "splice_event_cancel_indicator", "out_of_network_indicator", "program_splice_flag",
                "duration_flag", "splice_immediate_flag", "event_id_compliance_flag", "unique_program_id", "avail_num",
                "avails_expected"):
        if key in cmd:
            c.eq(key, dc.get(key), int(cmd[key]) if isinstance(cmd[key], bool) else cmd[key])
    if "break_duration" in cmd:
        c.eq("break_duration (s)", round(dc["break_duration"]["duration"] / 90000, 6), cmd["break_duration"])
        c.eq("break_auto_return", bool(dc["break_duration"]["auto_return"]), cmd["break_auto_return"])
    if "xml_break_duration_ticks" in sdec:
        c.eq("break_duration (ticks, xml)", dc["break_duration"]["duration"], int(sdec["xml_break_duration_ticks"]))
    if "xml_ptsAdjustment_ticks" in sdec:
        c.eq("pts_adjustment (ticks, xml)", d["pts_adjustment"], int(sdec["xml_ptsAdjustment_ticks"]))
        c.eq("pts_time (ticks, xml)", st["pts_time"], int(sdec["xml_ptsTime_ticks"]))


def check_expected(c, d, exp):
    for key in ("splice_command_type", "sap_type", "pts_adjustment", "splice_command_length"):
        if key in exp:
            c.eq(key, d.get(key), exp[key])
    if "tier" in exp:
        c.eq("tier", hexint(d["tier"]), hexint(exp["tier"]))
    for key, want in exp.get("command", {}).items():
        c.eq("command." + key, get(d["command"], key), want)
    if "segmentation_type_ids" in exp:
        got = [x.get("segmentation_type_id") for x in d.get("descriptors", []) if x.get("name") == "segmentation_descriptor"]
        c.eq("segmentation_type_ids", got, exp["segmentation_type_ids"])
    if "descriptors" in exp:
        got = d.get("descriptors", [])
        c.eq("number of descriptors", len(got), len(exp["descriptors"]))
        for i, (g, w) in enumerate(zip(got, exp["descriptors"])):
            for key, want in w.items():
                if key == "upid_ti_decimal":
                    c.eq(f"descriptor[{i}].TI as decimal", str(int(g.get("segmentation_upid", "0") or "0", 16)), want)
                elif key == "mid_types":
                    c.eq(f"descriptor[{i}].MID types", [x["segmentation_upid_type"] for x in g.get("mid", [])], want)
                elif key == "mid_adi_text":
                    t = [x.get("segmentation_upid_text") for x in g.get("mid", []) if x["segmentation_upid_type"] == 0x09]
                    c.eq(f"descriptor[{i}].MID ADI text", t[0] if t else None, want)
                elif key == "identifier_int":
                    c.eq(f"descriptor[{i}].identifier", hexint(g.get("identifier", "0x0")), want)
                elif key == "audio_channels":
                    for k, (gc, wc) in enumerate(zip(g.get("audio_channels", []), want)):
                        for kk, vv in wc.items():
                            c.eq(f"descriptor[{i}].audio_channels[{k}].{kk}", gc.get(kk), vv)
                else:
                    c.eq(f"descriptor[{i}].{key}", g.get(key), want)


def summary(d):
    s = {"splice_command": d.get("splice_command_name"), "pts_adjustment": d.get("pts_adjustment"),
         "tier": d.get("tier"), "crc_32": d.get("crc_32")}
    c = d.get("command", {})
    if d.get("splice_command_type") == 5:
        for k in ("splice_event_id", "splice_event_cancel_indicator", "out_of_network_indicator", "splice_immediate_flag"):
            if k in c:
                s[k] = c[k]
        if "splice_time" in c:
            s["pts_time"] = c["splice_time"].get("pts_time")
        if "break_duration" in c:
            s["break_duration"] = c["break_duration"]["duration"]
            s["auto_return"] = c["break_duration"]["auto_return"]
    if d.get("splice_command_type") == 6:
        s["pts_time"] = c.get("pts_time")
    segs = []
    for x in d.get("descriptors", []):
        if x.get("name") == "segmentation_descriptor":
            e = {"segmentation_event_id": x.get("segmentation_event_id"),
                 "segmentation_type_id": x.get("segmentation_type_id"),
                 "segmentation_type_name": x.get("segmentation_type_name")}
            if "segmentation_duration" in x:
                e["segmentation_duration"] = x["segmentation_duration"]
            if x.get("segmentation_event_cancel_indicator"):
                e["cancel"] = 1
            segs.append(e)
        else:
            segs.append({"descriptor": x.get("name"), "tag": x.get("splice_descriptor_tag")})
    if segs:
        s["descriptors"] = segs
    t = sd.times(d)
    if t:
        s["splice_pts_adjusted"] = t[0]["adjusted"]
    return s


report = []
n_ok = n_crc_bad = n_err = n_mis = 0
for v in doc["vectors"]:
    chk = {"by": "tools/check_vectors.py with tools/scte35_decode.py (runs/check_vectors.txt)"}
    line = [v["id"]]
    try:
        data = decode_value(v["value"], v["encoding"])
        d = sd.decode(data)
        chk["decoded"] = True
        chk["crc_32_valid"] = d["crc_32_valid"]
        if not d["crc_32_valid"]:
            chk["crc_32_note"] = f"the CRC_32 field is {d['crc_32']}; the CRC of the preceding bytes is {d['crc_32_computed']}"
        for k in ("splice_command_length_mismatch", "bytes_after_section", "alignment_stuffing_bytes"):
            if k in d:
                chk[k] = d[k]
        unparsed = [x for x in d.get("descriptors", []) if "unparsed_bytes_in_descriptor" in x or "error" in x]
        if unparsed:
            chk["descriptor_notes"] = [{k: x[k] for k in ("splice_descriptor_tag", "unparsed_bytes_in_descriptor", "error") if k in x}
                                       for x in unparsed]
        v["agent_decode"] = summary(d)
    except sd.DecodeError as e:
        d = None
        chk["decoded"] = False
        chk["error"] = str(e)
    agree = []
    for a in ([] if "source_expects_error" in v else v.get("also_published_as", [])):
        try:
            other = decode_value(a["value"], a["encoding"])
            agree.append(other == decode_value(v["value"], v["encoding"]))
        except sd.DecodeError as e:
            agree.append(f"error: {e}")
    if agree:
        chk["encodings_agree"] = all(x is True for x in agree)
    c = Cmp()
    exp = v.get("expected")
    if d is not None and exp:
        if exp.get("from") == "scte35-2023r1 listing":
            check_listing(c, d, v["source_decoded"]["lines"])
        elif exp.get("from", "").startswith("same value as "):
            ref = by_id[exp["from"].split("same value as ")[1].split(" ")[0]]
            c.eq("base64 identical to the standard's", v["value"], ref["value"])
            mine = [a["value"].upper() for a in v.get("also_published_as", [])]
            theirs = [a["value"].upper() for a in ref.get("also_published_as", [])]
            c.eq("hex identical to the standard's", mine, theirs)
        elif exp.get("from") == "threefive json":
            check_threefive(c, d, v["source_decoded"])
        else:
            check_expected(c, d, exp)
    if "source_expects_error" in v:
        want = v["source_expects_error"]
        got_err = d is None or not d["crc_32_valid"]
        c.eq(f"the source expects {want}", "an error" if got_err else "no error", "an error")
    chk["compared_fields"] = c.checked
    if c.mismatch:
        chk["mismatches"] = c.mismatch
    v["agent_check"] = chk
    if not chk.get("decoded"):
        n_err += 1
    elif not chk["crc_32_valid"]:
        n_crc_bad += 1
    if c.mismatch:
        n_mis += 1
    if chk.get("decoded") and chk["crc_32_valid"] and not c.mismatch:
        n_ok += 1
    flags = []
    flags.append("decoded" if chk.get("decoded") else "NOT DECODED: " + chk.get("error", ""))
    if chk.get("decoded"):
        flags.append("crc ok" if chk["crc_32_valid"] else "CRC BAD (" + chk["crc_32_note"] + ")")
    if "encodings_agree" in chk:
        flags.append("encodings agree" if chk["encodings_agree"] else "ENCODINGS DIFFER")
    flags.append(f"{c.checked} fields compared")
    if c.mismatch:
        flags.append("MISMATCHES: " + "; ".join(c.mismatch))
    if "source_expects_error" in v:
        flags.append("source expects " + v["source_expects_error"])
    for k in ("splice_command_length_mismatch", "bytes_after_section", "alignment_stuffing_bytes", "descriptor_notes"):
        if k in chk:
            flags.append(f"{k}={chk[k]}")
    if v.get("agent_decode"):
        flags.append("decode: " + json.dumps(v["agent_decode"], ensure_ascii=False))
    report.append(" | ".join(line + flags))

doc["agent_check_summary"] = {
    "vectors": len(doc["vectors"]),
    "decoded_crc_ok_no_mismatch": n_ok,
    "decoded_crc_bad": n_crc_bad,
    "not_decoded": n_err,
    "with_mismatches": n_mis,
    "record": "scte35_evidence/runs/check_vectors.txt",
}
with open(OUT, "w", encoding="utf-8") as fh:
    json.dump(doc, fh, indent=1, ensure_ascii=False)
for r in report:
    print(r)
print(json.dumps(doc["agent_check_summary"]))
