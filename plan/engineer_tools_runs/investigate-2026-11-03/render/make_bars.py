# Raw planar frames with vertical luma bars and neutral chroma, for
# exact-code checks. Usage: make_bars.py <out.yuv> <420|422|444> [frames] [bits]
# Bars (64 px wide each), then a 7th bar with chroma (a colour, to see chroma):
#   bits 10 (default): 16-bit LE samples. Y = 64, 300, 509, 723, 940, 1019;
#                      Cb = Cr = 512; 7th bar Y = 509, Cb = 300, Cr = 700.
#   bits 8:            8-bit samples. Y = 16, 75, 126, 180, 235, 254;
#                      Cb = Cr = 128; 7th bar Y = 126, Cb = 75, Cr = 175.
#   bits 10to8:        the 10-bit bars >> 2, as 8-bit samples
#                      (Y = 16, 75, 127, 180, 235, 254; 7th bar 127 / 75 / 175).
import struct
import sys

out, sub = sys.argv[1], sys.argv[2]
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 25
bits = sys.argv[4] if len(sys.argv) > 4 else "10"
W, H = 448, 128
BARS = [64, 300, 509, 723, 940, 1019, 509]
CB = [512] * 6 + [300]
CR = [512] * 6 + [700]
if bits == "8":
    BARS = [16, 75, 126, 180, 235, 254, 126]
    CB = [128] * 6 + [75]
    CR = [128] * 6 + [175]
elif bits == "10to8":
    BARS, CB, CR = ([v >> 2 for v in t] for t in (BARS, CB, CR))
elif bits != "10":
    sys.exit(f"bits must be 10, 8 or 10to8, not {bits}")
cw = {"420": W // 2, "422": W // 2, "444": W}[sub]
ch = {"420": H // 2, "422": H, "444": H}[sub]
bar_c = 64 * cw // W   # chroma width of one bar

def plane(w, h, values, bar_w):
    if bits == "10":
        row = b"".join(struct.pack("<H", values[x // bar_w]) for x in range(w))
    else:
        row = bytes(values[x // bar_w] for x in range(w))
    return row * h

y = plane(W, H, BARS, 64)
u = plane(cw, ch, CB, bar_c)
v = plane(cw, ch, CR, bar_c)
with open(out, "wb") as f:
    for _ in range(frames):
        f.write(y + u + v)
print(f"{out}: {frames} frames {W}x{H} yuv{sub}p{'10le' if bits == '10' else ''} ({bits})")
