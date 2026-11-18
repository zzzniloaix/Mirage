#!/usr/bin/env python3
"""A reference decoder of SCTE 35 splice_info_section() for this survey.

Written by the agent from the syntax tables of ANSI/SCTE 35 2023r1
(Table 5 splice_info_section, 9 splice_schedule, 10 splice_insert,
11 time_signal, 13 private_command, 14 splice_time, 15 break_duration,
17 splice_descriptor, 18 avail_descriptor, 19 DTMF_descriptor,
20 segmentation_descriptor, 27 time_descriptor, 28 audio_descriptor) and
the CRC_32 of ISO/IEC 13818-1 (CRC-32/MPEG-2: polynomial 0x04C11DB7,
initial value 0xFFFFFFFF, no reflection, no final XOR; the section's CRC
over all its bytes, CRC_32 included, is 0).

It exists to check that the published vectors were copied exactly (CRC)
and that the layout written in scte35.md decodes them to what their
sources say. It is not the design of Phase 3's decoder.

Encrypted sections (encrypted_packet = 1) are not decrypted: the fields
from splice_command_type on are reported as encrypted (the work order
puts encrypted streams out of scope).

usage: python3 -I scte35_decode.py <hex-or-base64> [...]
"""
import base64
import binascii
import json
import re
import sys

TICKS = 90000
WRAP = 1 << 33

COMMAND_NAMES = {0x00: "splice_null", 0x04: "splice_schedule", 0x05: "splice_insert",
                 0x06: "time_signal", 0x07: "bandwidth_reservation", 0xFF: "private_command"}

DESCRIPTOR_NAMES = {0x00: "avail_descriptor", 0x01: "DTMF_descriptor", 0x02: "segmentation_descriptor",
                    0x03: "time_descriptor", 0x04: "audio_descriptor"}

# Table 23 of SCTE 35 2023r1.
SEGMENTATION_TYPES = {
    0x00: "Not Indicated", 0x01: "Content Identification", 0x02: "Private",
    0x10: "Program Start", 0x11: "Program End", 0x12: "Program Early Termination",
    0x13: "Program Breakaway", 0x14: "Program Resumption", 0x15: "Program Runover Planned",
    0x16: "Program Runover Unplanned", 0x17: "Program Overlap Start", 0x18: "Program Blackout Override",
    0x19: "Program Join", 0x1A: "Program Immediate Resumption",
    0x20: "Chapter Start", 0x21: "Chapter End", 0x22: "Break Start", 0x23: "Break End",
    0x24: "Opening Credit Start (deprecated)", 0x25: "Opening Credit End (deprecated)",
    0x26: "Closing Credit Start (deprecated)", 0x27: "Closing Credit End (deprecated)",
    0x30: "Provider Advertisement Start", 0x31: "Provider Advertisement End",
    0x32: "Distributor Advertisement Start", 0x33: "Distributor Advertisement End",
    0x34: "Provider Placement Opportunity Start", 0x35: "Provider Placement Opportunity End",
    0x36: "Distributor Placement Opportunity Start", 0x37: "Distributor Placement Opportunity End",
    0x38: "Provider Overlay Placement Opportunity Start", 0x39: "Provider Overlay Placement Opportunity End",
    0x3A: "Distributor Overlay Placement Opportunity Start", 0x3B: "Distributor Overlay Placement Opportunity End",
    0x3C: "Provider Promo Start", 0x3D: "Provider Promo End",
    0x3E: "Distributor Promo Start", 0x3F: "Distributor Promo End",
    0x40: "Unscheduled Event Start", 0x41: "Unscheduled Event End",
    0x42: "Alternate Content Opportunity Start", 0x43: "Alternate Content Opportunity End",
    0x44: "Provider Ad Block Start", 0x45: "Provider Ad Block End",
    0x46: "Distributor Ad Block Start", 0x47: "Distributor Ad Block End",
    0x50: "Network Start", 0x51: "Network End",
}

# Table 20: the types after which sub_segment_num and sub_segments_expected
# may follow (their presence is told by descriptor_length, 10.3.3.1).
SUB_SEGMENT_TYPES = {0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x44, 0x46}

# Table 22: the names of segmentation_upid_type values.
UPID_TYPES = {0x00: "Not Used", 0x01: "User Defined (deprecated)", 0x02: "ISCI (deprecated)", 0x03: "Ad-ID",
              0x04: "UMID", 0x05: "ISAN (deprecated)", 0x06: "ISAN", 0x07: "TID", 0x08: "TI", 0x09: "ADI",
              0x0A: "EIDR", 0x0B: "ATSC Content Identifier", 0x0C: "MPU()", 0x0D: "MID()",
              0x0E: "ADS Information", 0x0F: "URI", 0x10: "UUID", 0x11: "SCR"}


class DecodeError(Exception):
    pass


def crc32_mpeg2(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return crc


class Bits:
    def __init__(self, data, start=0, end=None):
        self.d = data
        self.pos = start * 8
        self.end = (len(data) if end is None else end) * 8

    def left_bits(self):
        return self.end - self.pos

    def read(self, n):
        if n > self.left_bits():
            raise DecodeError(f"read of {n} bits at bit {self.pos} past the end (bit {self.end})")
        v = 0
        for _ in range(n):
            byte = self.d[self.pos >> 3]
            v = (v << 1) | ((byte >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def bytes(self, n):
        if self.pos & 7:
            raise DecodeError("unaligned byte read")
        if n * 8 > self.left_bits():
            raise DecodeError(f"read of {n} bytes at byte {self.pos >> 3} past the end")
        b = self.d[self.pos >> 3:(self.pos >> 3) + n]
        self.pos += n * 8
        return bytes(b)

    def byte_pos(self):
        return self.pos >> 3


def to_bytes(text):
    """Bytes of a published vector: hex with or without 0x, or base64."""
    t = text.strip()
    if re.fullmatch(r"(0[xX])?[0-9A-Fa-f]+", t) and (t[:2].lower() == "0x" or len(t) % 2 == 0 and not re.search(r"[g-zG-Z+/=]", t)):
        h = t[2:] if t[:2].lower() == "0x" else t
        if len(h) % 2:
            raise DecodeError("odd number of hex digits")
        return bytes.fromhex(h), "hex"
    try:
        return base64.b64decode(t, validate=True), "base64"
    except (binascii.Error, ValueError) as e:
        raise DecodeError(f"neither hex nor base64: {e}")


def seconds(ticks):
    return round(ticks / TICKS, 6)


def splice_time(r):
    out = {"time_specified_flag": r.read(1)}
    if out["time_specified_flag"]:
        r.read(6)
        out["pts_time"] = r.read(33)
    else:
        r.read(7)
    return out


def break_duration(r):
    auto_return = r.read(1)
    r.read(6)
    return {"auto_return": auto_return, "duration": r.read(33)}


def cmd_splice_insert(r):
    c = {"splice_event_id": r.read(32), "splice_event_cancel_indicator": r.read(1)}
    r.read(7)
    if c["splice_event_cancel_indicator"]:
        return c
    c["out_of_network_indicator"] = r.read(1)
    c["program_splice_flag"] = r.read(1)
    c["duration_flag"] = r.read(1)
    c["splice_immediate_flag"] = r.read(1)
    c["event_id_compliance_flag"] = r.read(1)
    r.read(3)
    if c["program_splice_flag"] and not c["splice_immediate_flag"]:
        c["splice_time"] = splice_time(r)
    if not c["program_splice_flag"]:
        n = r.read(8)
        comps = []
        for _ in range(n):
            comp = {"component_tag": r.read(8)}
            if not c["splice_immediate_flag"]:
                comp["splice_time"] = splice_time(r)
            comps.append(comp)
        c["components"] = comps
    if c["duration_flag"]:
        c["break_duration"] = break_duration(r)
    c["unique_program_id"] = r.read(16)
    c["avail_num"] = r.read(8)
    c["avails_expected"] = r.read(8)
    return c


def cmd_splice_schedule(r):
    events = []
    for _ in range(r.read(8)):
        e = {"splice_event_id": r.read(32), "splice_event_cancel_indicator": r.read(1),
             "event_id_compliance_flag": r.read(1)}
        r.read(6)
        if not e["splice_event_cancel_indicator"]:
            e["out_of_network_indicator"] = r.read(1)
            e["program_splice_flag"] = r.read(1)
            e["duration_flag"] = r.read(1)
            r.read(5)
            if e["program_splice_flag"]:
                e["utc_splice_time"] = r.read(32)
            else:
                e["components"] = [{"component_tag": r.read(8), "utc_splice_time": r.read(32)}
                                   for _ in range(r.read(8))]
            if e["duration_flag"]:
                e["break_duration"] = break_duration(r)
            e["unique_program_id"] = r.read(16)
            e["avail_num"] = r.read(8)
            e["avails_expected"] = r.read(8)
        events.append(e)
    return {"events": events}


def decode_command(ctype, r, length):
    if ctype == 0x00 or ctype == 0x07:
        return {}
    if ctype == 0x05:
        return cmd_splice_insert(r)
    if ctype == 0x06:
        return splice_time(r)
    if ctype == 0x04:
        return cmd_splice_schedule(r)
    if ctype == 0xFF:
        ident = r.read(32)
        n = (length - 4) if length is not None else 0
        return {"identifier": ident, "private_bytes": r.bytes(n).hex()}
    raise DecodeError(f"reserved splice_command_type 0x{ctype:02x}")


def upid_text(t, b):
    if t in (0x03, 0x07, 0x09, 0x0E, 0x0F, 0x11, 0x02, 0x01):
        try:
            return b.decode("ascii")
        except UnicodeDecodeError:
            return None
    return None


def seg_descriptor(r, end_byte):
    d = {"segmentation_event_id": r.read(32), "segmentation_event_cancel_indicator": r.read(1),
         "segmentation_event_id_compliance_indicator": r.read(1)}
    r.read(6)
    if d["segmentation_event_cancel_indicator"]:
        return d
    d["program_segmentation_flag"] = r.read(1)
    d["segmentation_duration_flag"] = r.read(1)
    d["delivery_not_restricted_flag"] = r.read(1)
    if not d["delivery_not_restricted_flag"]:
        d["web_delivery_allowed_flag"] = r.read(1)
        d["no_regional_blackout_flag"] = r.read(1)
        d["archive_allowed_flag"] = r.read(1)
        d["device_restrictions"] = r.read(2)
    else:
        r.read(5)
    if not d["program_segmentation_flag"]:
        comps = []
        for _ in range(r.read(8)):
            tag = r.read(8)
            r.read(7)
            comps.append({"component_tag": tag, "pts_offset": r.read(33)})
        d["components"] = comps
    if d["segmentation_duration_flag"]:
        d["segmentation_duration"] = r.read(40)
    d["segmentation_upid_type"] = r.read(8)
    d["segmentation_upid_type_name"] = UPID_TYPES.get(d["segmentation_upid_type"], "Reserved")
    n = r.read(8)
    d["segmentation_upid_length"] = n
    raw = r.bytes(n)
    d["segmentation_upid"] = raw.hex()
    txt = upid_text(d["segmentation_upid_type"], raw)
    if txt is not None:
        d["segmentation_upid_text"] = txt
    if d["segmentation_upid_type"] == 0x0D:
        # MID(): a loop of (segmentation_upid_type, length, segmentation_upid), Table 25.
        mid, i = [], 0
        while i + 2 <= len(raw):
            t, ln = raw[i], raw[i + 1]
            item = {"segmentation_upid_type": t, "length": ln, "segmentation_upid": raw[i + 2:i + 2 + ln].hex()}
            tx = upid_text(t, raw[i + 2:i + 2 + ln])
            if tx is not None:
                item["segmentation_upid_text"] = tx
            mid.append(item)
            i += 2 + ln
        d["mid"] = mid
    if d["segmentation_upid_type"] == 0x0C and n >= 4:
        d["mpu_format_identifier"] = int.from_bytes(raw[:4], "big")
    d["segmentation_type_id"] = r.read(8)
    d["segmentation_type_name"] = SEGMENTATION_TYPES.get(d["segmentation_type_id"], "Reserved")
    d["segment_num"] = r.read(8)
    d["segments_expected"] = r.read(8)
    if d["segmentation_type_id"] in SUB_SEGMENT_TYPES and r.byte_pos() + 2 <= end_byte:
        d["sub_segment_num"] = r.read(8)
        d["sub_segments_expected"] = r.read(8)
    return d


def decode_descriptor(data, pos, end):
    if pos + 2 > end:
        raise DecodeError(f"descriptor header at byte {pos} past the loop end {end}")
    tag, length = data[pos], data[pos + 1]
    body_end = pos + 2 + length
    if body_end > end:
        raise DecodeError(f"descriptor at byte {pos} (tag 0x{tag:02x}, length {length}) overruns the loop end {end}")
    d = {"splice_descriptor_tag": tag, "descriptor_length": length}
    if length < 4:
        d["error"] = "descriptor_length < 4: no identifier"
        return d, body_end
    ident = int.from_bytes(data[pos + 2:pos + 6], "big")
    d["identifier"] = f"0x{ident:08x}"
    r = Bits(data, pos + 6, body_end)
    if ident != 0x43554549:  # not "CUEI": private, skipped by length
        d["name"] = "private descriptor"
        d["private_bytes"] = data[pos + 6:body_end].hex()
        return d, body_end
    d["name"] = DESCRIPTOR_NAMES.get(tag, "reserved descriptor")
    try:
        if tag == 0x00:
            d["provider_avail_id"] = r.read(32)
        elif tag == 0x01:
            d["preroll"] = r.read(8)
            cnt = r.read(3)
            r.read(5)
            d["dtmf_chars"] = r.bytes(cnt).decode("ascii", "replace")
        elif tag == 0x02:
            d.update(seg_descriptor(r, body_end))
        elif tag == 0x03:
            d["TAI_seconds"] = r.read(48)
            d["TAI_ns"] = r.read(32)
            d["UTC_offset"] = r.read(16)
        elif tag == 0x04:
            n = r.read(4)
            r.read(4)
            chans = []
            for _ in range(n):
                chans.append({"component_tag": r.read(8), "ISO_code": r.bytes(3).decode("ascii", "replace"),
                              "Bit_Stream_Mode": r.read(3), "Num_Channels": r.read(4), "Full_Srvc_Audio": r.read(1)})
            d["audio_channels"] = chans
        else:
            d["private_bytes"] = data[pos + 6:body_end].hex()
    except DecodeError as e:
        d["error"] = str(e)
    left = body_end - r.byte_pos()
    if left > 0 and "error" not in d and tag in DESCRIPTOR_NAMES:
        d["unparsed_bytes_in_descriptor"] = left
    return d, body_end


def decode(data):
    """Decoded fields of one splice_info_section (a dict); raises DecodeError."""
    if len(data) < 3:
        raise DecodeError("shorter than a section header")
    out = {"bytes": len(data)}
    r = Bits(data)
    out["table_id"] = f"0x{r.read(8):02x}"
    out["section_syntax_indicator"] = r.read(1)
    out["private_indicator"] = r.read(1)
    out["sap_type"] = r.read(2)
    out["section_length"] = r.read(12)
    total = 3 + out["section_length"]
    out["section_bytes_expected"] = total
    if total > len(data):
        raise DecodeError(f"section_length {out['section_length']} needs {total} bytes, the data has {len(data)}")
    if total < len(data):
        out["bytes_after_section"] = len(data) - total
    sec = data[:total]
    crc_field = int.from_bytes(sec[-4:], "big")
    out["crc_32"] = f"0x{crc_field:08x}"
    out["crc_32_valid"] = crc32_mpeg2(sec) == 0
    out["crc_32_computed"] = f"0x{crc32_mpeg2(sec[:-4]):08x}"
    r = Bits(sec, 3, total - 4)
    out["protocol_version"] = r.read(8)
    out["encrypted_packet"] = r.read(1)
    out["encryption_algorithm"] = r.read(6)
    out["pts_adjustment"] = r.read(33)
    out["cw_index"] = r.read(8)
    out["tier"] = f"0x{r.read(12):03x}"
    out["splice_command_length"] = r.read(12)
    if out["encrypted_packet"]:
        out["encrypted"] = "fields from splice_command_type on are encrypted; not decoded"
        return out
    ctype = r.read(8)
    out["splice_command_type"] = ctype
    out["splice_command_name"] = COMMAND_NAMES.get(ctype, "reserved")
    cmd_start = r.byte_pos()
    length = None if out["splice_command_length"] == 0xFFF else out["splice_command_length"]
    out["command"] = decode_command(ctype, r, length)
    used = r.byte_pos() - cmd_start + (1 if r.pos & 7 else 0)
    if r.pos & 7:
        raise DecodeError("splice command does not end on a byte")
    if length is not None and used != length:
        out["splice_command_length_mismatch"] = f"command parsed as {used} bytes, splice_command_length says {length}"
        r = Bits(sec, cmd_start + length, total - 4)
    dll = r.read(16)
    out["descriptor_loop_length"] = dll
    pos = r.byte_pos()
    end = pos + dll
    if end > total - 4:
        raise DecodeError(f"descriptor_loop_length {dll} overruns the section (loop ends at byte {end}, CRC at {total - 4})")
    descs = []
    while pos < end:
        d, pos = decode_descriptor(sec, pos, end)
        descs.append(d)
    out["descriptors"] = descs
    stuffing = (total - 4) - end
    if stuffing:
        out["alignment_stuffing_bytes"] = stuffing
    return out


def times(out):
    """The splice times of a decoded section, pts_adjustment applied (mod 2^33)."""
    adj = out.get("pts_adjustment", 0)
    res = []
    c = out.get("command", {})
    st = c.get("splice_time") if out.get("splice_command_type") == 0x05 else (c if out.get("splice_command_type") == 0x06 else None)
    if st and st.get("time_specified_flag"):
        p = st["pts_time"]
        res.append({"pts_time": p, "adjusted": (p + adj) % WRAP, "adjusted_seconds": seconds((p + adj) % WRAP)})
    return res


def main(argv):
    for a in argv:
        try:
            data, enc = to_bytes(a)
            d = decode(data)
            d["input_encoding"] = enc
            d["splice_times"] = times(d)
            print(json.dumps(d, indent=1))
        except DecodeError as e:
            print(json.dumps({"input": a, "error": str(e)}))


if __name__ == "__main__":
    main(sys.argv[1:])
