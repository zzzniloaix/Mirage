# The luma codes of bar 2 (columns 132-187 of 448) over every row (128) of a raw frame whose luma plane comes
# first: usage bar2_codes.py <raw> <bytes per sample: 1 or 2> <right shift, for MSB-aligned samples>.
# Prints each code and how many samples hold it.
import collections
import struct
import sys

W, H = 448, 128
path, size, shift = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
data = open(path, "rb").read()
luma = data[:W * H] if size == 1 else [v >> shift for v in struct.unpack("<%dH" % (W * H), data[:W * H * 2])]
counts = collections.Counter(luma[r * W + x] for r in range(H) for x in range(132, 188))
print(dict(sorted(counts.items())))
