# Print the Y/Cb/Cr codes at the middle of each 64-px bar of the first frame of
# a raw planar file: 16-bit LE samples (yuv420p10le / yuv422p10le / yuv444p1Xle)
# by default, 8-bit samples (yuv420p / yuv422p / yuv444p) with bits = 8.
# Usage: read_bars.py <in.yuv> <420|422|444> [shift] [bits]
#        (shift: >> applied, e.g. 6 for p010-style words; bits: 16 (default) or 8)
import struct
import sys

path, sub = sys.argv[1], sys.argv[2]
shift = int(sys.argv[3]) if len(sys.argv) > 3 else 0
bps = 1 if (len(sys.argv) > 4 and sys.argv[4] == "8") else 2   # bytes per sample
W, H = 448, 128
cw = {"420": W // 2, "422": W // 2, "444": W}[sub]
ch = {"420": H // 2, "422": H, "444": H}[sub]
data = open(path, "rb").read(bps * (W * H + 2 * cw * ch))
def at(off, w, x, y):
    i = off + bps * (y * w + x)
    v = data[i] if bps == 1 else struct.unpack_from("<H", data, i)[0]
    return v >> shift
yo, uo = 0, bps * W * H
vo = uo + bps * cw * ch
ys, us, vs = [], [], []
for b in range(7):
    x = 64 * b + 32
    ys.append(at(yo, W, x, H // 2))
    us.append(at(uo, cw, x * cw // W, ch // 2))
    vs.append(at(vo, cw, x * cw // W, ch // 2))
print("Y ", ys)
print("Cb", us)
print("Cr", vs)
