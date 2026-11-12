"""sweep_grid.py: for each grid over the four acceptance curves in 05_detection_sweep.txt, lists the
cells that are not E (every target exact, every control without a drop).
usage: python3 -I -B sweep_grid.py <05_detection_sweep.txt>"""
import re, sys
lines = open(sys.argv[1], encoding="utf-8").read().split("\n")[6:200]
cur = None
for l in lines:
    m = re.match(r"(\w+), W = (\d+): rows T, columns N = (.*)", l)
    if m:
        cur = f"{m.group(1)} W={m.group(2)}"; cols = m.group(3).split(); continue
    m = re.match(r"\s+T\s+(\d+):\s+(.*)", l)
    if m and cur and cur.startswith("median_nonlow"):
        bad = [f"N={n}:{c}" for n, c in zip(cols, m.group(2).split()) if c != "E"]
        print(f"{cur} T={m.group(1)}: not E: {' '.join(bad) or 'none'}")
