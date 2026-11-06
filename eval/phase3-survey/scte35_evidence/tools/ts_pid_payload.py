#!/usr/bin/env python3
"""The PES payloads of one PID of an MPEG-TS file, with the ID3 frames in
them listed (frame id, size, the owner or description string, and the
first bytes after it in hex).
usage: python3 -I ts_pid_payload.py <segment.ts> <pid>"""
import sys

data = open(sys.argv[1], "rb").read()
want = int(sys.argv[2])
pes, cur = [], None
for off in range(0, len(data) - 187, 188):
    p = data[off:off + 188]
    pid = ((p[1] & 0x1F) << 8) | p[2]
    if pid != want:
        continue
    afc = (p[3] >> 4) & 3
    i = 4 + (1 + p[4] if afc in (2, 3) else 0)
    if afc in (0, 2):
        continue
    if (p[1] >> 6) & 1:
        if cur is not None:
            pes.append(cur)
        cur = bytearray()
    if cur is not None:
        cur += p[i:]
if cur is not None:
    pes.append(cur)
for k, b in enumerate(pes):
    hl = b[8]
    flags = b[7]
    pts = None
    if flags & 0x80:
        q = b[9:14]
        pts = (((q[0] >> 1) & 7) << 30) | (q[1] << 22) | ((q[2] >> 1) << 15) | (q[3] << 7) | (q[4] >> 1)
    body = bytes(b[9 + hl:])
    print(f"PES {k}: {len(b)} bytes, PTS {pts}, payload starts {body[:3]!r}")
    if body[:3] == b"ID3":
        size = (body[6] << 21) | (body[7] << 14) | (body[8] << 7) | body[9]
        j = 10
        while j + 10 <= 10 + size and body[j:j + 4] != b"\x00\x00\x00\x00":
            fid = body[j:j + 4].decode("latin-1")
            fs = int.from_bytes(body[j + 4:j + 8], "big")
            fr = body[j + 10:j + 10 + fs]
            nul = fr.find(b"\x00", 1 if fid[0] == "T" else 0)
            owner = fr[:nul].decode("latin-1", "replace") if nul >= 0 else ""
            rest = fr[nul + 1:] if nul >= 0 else fr
            print(f"   frame {fid} size {fs}: owner/description {owner!r}, then {rest[:64].hex()}")
            j += 10 + fs
