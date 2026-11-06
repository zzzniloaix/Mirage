"""Copies an HLS stream directory and damages one of its MPEG-TS segments in
the copy: in every 4th 188-byte TS packet of the segment's second half, the
48 bytes after the 4-byte TS header (adaptation field or payload) are
overwritten with 0xFF. The 4-byte headers, and so the packets' PIDs and
continuity counters, are left as they were.

    python3 -I corrupt_segment.py <source dir> <copy dir> <segment file name>
"""
import os
import shutil
import sys

src, dst, name = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(dst, exist_ok=True)
for f in sorted(os.listdir(src)):
    shutil.copyfile(os.path.join(src, f), os.path.join(dst, f))
path = os.path.join(dst, name)
data = bytearray(open(path, "rb").read())
packets = len(data) // 188
damaged = 0
for k in range(packets // 2, packets, 4):
    at = k * 188
    if data[at] != 0x47:
        continue
    data[at + 4:at + 52] = b"\xff" * 48
    damaged += 1
open(path, "wb").write(bytes(data))
print(f"copied {len(os.listdir(dst))} files; {name}: {packets} TS packets, {damaged} damaged")
