"""Per-frame comparison of Mirage's scores (report.vmaf.json, entry `label`)
with FFmpeg's libvmaf filter log (JSON) for the same pair, over the frames the
FFmpeg run scored (it starts at the first frame, as Mirage does)."""
import json
import statistics as st
import sys

report, label, ff_log = sys.argv[1], sys.argv[2], sys.argv[3]
m = next(r for r in json.load(open(report))['results'] if r['label'] == label)['per_frame']
f = json.load(open(ff_log))
fv = [fr['metrics']['vmaf'] for fr in f['frames']]
n = min(len(fv), len(m))
d = [abs(m[i] - fv[i]) for i in range(n)]
print(f"{label!r} vs {ff_log.split('/')[-1]}: frames compared={n}")
print(f"  mirage mean={st.mean(m[:n]):.4f}  ffmpeg mean={st.mean(fv[:n]):.4f}  ffmpeg pooled mean={f['pooled_metrics']['vmaf']['mean']:.4f}")
print(f"  |delta| per frame: max={max(d):.4f} mean={st.mean(d):.4f} frames over 0.01: {sum(x > 0.01 for x in d)}")
print(f"  signed mean (mirage - ffmpeg)={st.mean(m[i] - fv[i] for i in range(n)):.4f}")
