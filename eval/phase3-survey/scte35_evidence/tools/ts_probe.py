#!/usr/bin/env python3
"""What an MPEG-TS segment carries, for this survey (no FFmpeg binary):
its PAT and PMT (stream types, the program's registration descriptors),
the first PTS of each PES stream, the packet count of each PID, and every
SCTE-35 splice_info_section (table_id 0xFC) on a stream of type 0x86,
decoded with tools/scte35_decode.py.

usage: python3 -I ts_probe.py <segment.ts> [...]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scte35_decode as sd  # noqa: E402


def pts_of(b):
    return (((b[0] >> 1) & 0x07) << 30) | (b[1] << 22) | ((b[2] >> 1) << 15) | (b[3] << 7) | (b[4] >> 1)


def probe(path):
    data = open(path, "rb").read()
    print(f"== {os.path.basename(os.path.dirname(path))}/{os.path.basename(path)}: {len(data)} bytes, "
          f"{len(data) // 188} packets, remainder {len(data) % 188}")
    counts, pmt_pids, streams, first_pts, sections, prog_regs = {}, set(), {}, {}, {}, []
    buf = {}
    for off in range(0, len(data) - 187, 188):
        p = data[off:off + 188]
        if p[0] != 0x47:
            print(f"   sync byte lost at offset {off}")
            break
        pusi = (p[1] >> 6) & 1
        pid = ((p[1] & 0x1F) << 8) | p[2]
        afc = (p[3] >> 4) & 3
        counts[pid] = counts.get(pid, 0) + 1
        i = 4
        if afc in (2, 3):
            i += 1 + p[4]
        if afc in (0, 2) or i >= 188:
            continue
        pay = p[i:]
        if pid == 0 or pid in pmt_pids or streams.get(pid) == 0x86:
            if pusi:
                ptr = pay[0]
                buf[pid] = bytearray(pay[1 + ptr:])
            elif pid in buf:
                buf[pid] += pay
            else:
                continue
            sec = buf[pid]
            if len(sec) < 3:
                continue
            ln = 3 + (((sec[1] & 0x0F) << 8) | sec[2])
            if len(sec) < ln:
                continue
            sec = bytes(sec[:ln])
            del buf[pid]
            tid = sec[0]
            if pid == 0 and tid == 0x00:
                for k in range(8, ln - 4, 4):
                    prog = (sec[k] << 8) | sec[k + 1]
                    ppid = ((sec[k + 2] & 0x1F) << 8) | sec[k + 3]
                    if prog:
                        pmt_pids.add(ppid)
            elif pid in pmt_pids and tid == 0x02:
                pil = ((sec[10] & 0x0F) << 8) | sec[11]
                k = 12
                while k < 12 + pil:
                    if sec[k] == 0x05:
                        prog_regs.append(sec[k + 2:k + 6].decode("latin-1"))
                    k += 2 + sec[k + 1]
                k = 12 + pil
                while k < ln - 4:
                    st = sec[k]
                    epid = ((sec[k + 1] & 0x1F) << 8) | sec[k + 2]
                    eil = ((sec[k + 3] & 0x0F) << 8) | sec[k + 4]
                    streams[epid] = st
                    k += 5 + eil
            elif streams.get(pid) == 0x86 and tid == 0xFC:
                sections.setdefault(pid, []).append(sec)
        elif pusi and pid not in first_pts and len(pay) >= 14 and pay[0:3] == b"\x00\x00\x01":
            flags = pay[7]
            if flags & 0x80:
                first_pts[pid] = pts_of(pay[9:14])
    print(f"   program registration descriptors: {prog_regs or 'none'}")
    for pid in sorted(streams):
        line = f"   PID {pid} (0x{pid:04x}): stream_type 0x{streams[pid]:02x}, {counts.get(pid, 0)} packets"
        if pid in first_pts:
            line += f", first PTS {first_pts[pid]} = {round(first_pts[pid] / 90000, 6)} s"
        print(line)
    print(f"   PIDs seen: {sorted(counts)}")
    if not any(v == 0x86 for v in streams.values()):
        print("   no stream of type 0x86 (SCTE-35) in the PMT")
    for pid, secs in sections.items():
        for s in secs:
            d = sd.decode(s)
            print(f"   SCTE-35 section on PID {pid}: {s.hex()} -> {d.get('splice_command_name')}, crc "
                  f"{'ok' if d['crc_32_valid'] else 'BAD'}, times {sd.times(d)}")


if __name__ == "__main__":
    for a in sys.argv[1:]:
        probe(a)
