"""axis_d_check.py: the frame-rate check that design.md §4.2 and Q02 (d) give the axis, applied
to testgen's ladder reports: "comparing each frame's PTS with its position within its report
sequence, beyond half a frame" (the aligner's tolerance, half the reference's frame duration).
For each pair of each report: the reference's sequences (alignment.reference.sequences), and the
scored frames whose ref_pts differs from first_pts + (ref_index - first_index) / frame_rate by
more than half a frame, with the largest difference. Exact arithmetic on ticks of the
reference's time base (ref.time_base), the report's seconds rounded to ticks. Read-only. Python 3
standard library only; for the adversarial review of the Phase 3 design, not repository code.

usage: python3 -I -B axis_d_check.py <report.json>...
"""

import json
import sys
from fractions import Fraction


def ticks(seconds, tb):
    return round(Fraction(seconds) / tb)


def main(argv):
    for path in argv[1:]:
        rep = json.load(open(path))
        print(f"== {path}")
        for p in rep["pairs"]:
            num, den = (int(x) for x in p["ref"]["time_base"].split("/"))
            tb = Fraction(num, den)
            fr_n, fr_d = (int(x) for x in p["alignment"]["reference"]["frame_rate"].split("/"))
            frame = Fraction(fr_d, fr_n) / tb            # one frame, in ticks
            tol = frame / 2
            seqs = p["alignment"]["reference"]["sequences"]
            flagged, worst = [], Fraction(0)
            for f in p["frames"]:
                i = f["ref_index"]
                s = max((q for q in seqs if q["first_index"] <= i), key=lambda q: q["first_index"])
                expected = ticks(s["first_pts"], tb) + (i - s["first_index"]) * frame
                d = ticks(f["ref_pts"], tb) - expected
                if abs(d) > tol:
                    flagged.append(i)
                    worst = max(worst, abs(d))
            seq_text = ", ".join(f"({q['first_index']}, {q['first_pts']}, {q['frames']})" for q in seqs)
            span = f"{flagged[0]}-{flagged[-1]}" if flagged else "-"
            print(f"pair {p['index']} ({p['dis']['url']}): frame rate {fr_n}/{fr_d}, time base "
                  f"{num}/{den}; reference sequences (first_index, first_pts, frames): {seq_text}; "
                  f"{len(p['frames'])} scored frames, {len(flagged)} beyond half a frame "
                  f"(reference frames {span}), largest difference {float(worst * tb):.3f} s")


if __name__ == "__main__":
    main(sys.argv)
