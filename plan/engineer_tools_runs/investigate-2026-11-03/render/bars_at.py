# Read the 7 bar centres of a make_bars.py clip from a PNG (e.g. a mirage
# --snapshot of the video viewport, which is the clip scaled to the window).
# Usage: python3 -I bars_at.py <snapshot.png> [ffmpeg_bin_dir]
import json
import subprocess
import sys

png = sys.argv[1]
bindir = sys.argv[2] if len(sys.argv) > 2 else "/opt/homebrew/opt/ffmpeg-full/bin"
info = json.loads(subprocess.run([f"{bindir}/ffprobe", "-v", "error", "-select_streams", "v:0",
                                  "-show_entries", "stream=width,height", "-of", "json", png],
                                 check=True, capture_output=True).stdout)
w, h = info["streams"][0]["width"], info["streams"][0]["height"]
rgb = subprocess.run([f"{bindir}/ffmpeg", "-v", "error", "-i", png, "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                     check=True, capture_output=True).stdout
y = h // 2
out = []
for b in range(7):
    x = int((64 * b + 32) / 448 * w)
    i = 3 * (y * w + x)
    out.append(tuple(rgb[i:i + 3]))
print(f"{png} ({w}x{h}):", " ".join(f"{r},{g},{b}" for r, g, b in out))
