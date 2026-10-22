"""Per-rung summary of a Mirage VMAF JSON report (the existing write_json format):
frame count, the pooled values Mirage wrote, and the per-frame distribution."""
import json
import statistics as st
import sys

rep = json.load(open(sys.argv[1]))
for r in rep['results']:
    pf = r.get('per_frame') or []
    if not pf:
        print(f"{r['label']!r}: no scores; error={r.get('error')!r}")
        continue
    print(f"{r['label']!r}: frames={len(pf)} written mean={r['vmaf_mean']} min={r['vmaf_min']} p5={r['vmaf_p5']}"
          f" | recomputed mean={st.mean(pf):.4f} median={st.median(pf):.4f} max={max(pf):.4f}"
          f" first_frame={pf[0]:.4f} frames_above_5={sum(v > 5 for v in pf)} resolution_field={r['resolution']!r}")
