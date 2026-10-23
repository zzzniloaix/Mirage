"""Unit tests of eval/pooling/pooling.py.

python3 -I -B -m unittest discover -s eval/pooling/tests -v
"""

import contextlib
import io
import json
import lzma
import math
import os
import statistics
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(HERE))
import pooling  # noqa: E402


class Percentiles(unittest.TestCase):
    ORDER = [float(v) for v in range(1, 11)]   # 1 .. 10

    def test_today_is_the_analyzers_p5_rule(self):
        # src/VMAFAnalyzer.cpp: sorted[sorted.size() * 5 / 100]
        order = [float(v) for v in range(200)]
        self.assertEqual(pooling.pct_today(order, 5), order[200 * 5 // 100])
        self.assertEqual(pooling.pct_today(self.ORDER, 50), 6.0)   # the upper median
        self.assertEqual(pooling.pct_today(self.ORDER, 1), 1.0)

    def test_nearest_rank(self):
        self.assertEqual(pooling.pct_nearest_rank(self.ORDER, 50), 5.0)
        self.assertEqual(pooling.pct_nearest_rank(self.ORDER, 25), 3.0)   # ceil(2.5) = 3rd
        self.assertEqual(pooling.pct_nearest_rank(self.ORDER, 1), 1.0)
        self.assertEqual(pooling.pct_nearest_rank([7.0], 5), 7.0)

    def test_linear_is_definition_7(self):
        self.assertEqual(pooling.pct_linear(self.ORDER, 50), 5.5)
        self.assertEqual(pooling.pct_linear(self.ORDER, 25), 3.25)
        self.assertEqual(pooling.pct_linear([7.0], 50), 7.0)
        # statistics.quantiles' 'inclusive' method is the same definition.
        data = [0.3, 9.1, 4.4, 2.2, 7.7, 5.0, 1.9, 8.8, 6.1, 3.3, 0.0, 4.9]
        cuts = statistics.quantiles(data, n=100, method="inclusive")
        for p in pooling.PERCENTILES:
            with self.subTest(p=p):
                self.assertAlmostEqual(pooling.pct_linear(sorted(data), p), cuts[p - 1], 12)


class Harmonic(unittest.TestCase):
    def test_forms(self):
        self.assertAlmostEqual(pooling.harmonic_plain([1.0, 2.0, 4.0]), 3 / 1.75, 12)
        self.assertAlmostEqual(pooling.harmonic_shifted([0.0, 1.0, 3.0]), 3 / 1.75 - 1, 12)
        self.assertIsNone(pooling.harmonic_plain([0.0, 5.0]))
        self.assertEqual(pooling.harmonic_shifted([0.0, 0.0]), 0.0)


class SeriesStats(unittest.TestCase):
    def test_min_and_the_first_frame(self):
        st = pooling.series_stats([10.0, 30.0, 20.0, 10.0, 40.0])
        self.assertEqual((st["min"], st["min_first_index"], st["frames_at_min"]), (10.0, 0, 2))
        self.assertEqual((st["first_frame"], st["min_without_first_frame"],
                          st["min_without_first_frame_index"]), (10.0, 10.0, 3))
        self.assertEqual(st["mean"], 22.0)
        self.assertEqual(st["percentiles"]["p50"]["today"], 20.0)
        self.assertEqual(st["percentiles"]["p50"]["largest_difference"], 0.0)
        self.assertEqual(st["harmonic_mean"]["frames_at_or_below_zero"], 0)
        one = pooling.series_stats([5.0])
        self.assertIsNone(one["min_without_first_frame"])
        with self.assertRaises(pooling.InputError):
            pooling.series_stats([])


def write_delta_run(root, xz=False):
    d = os.path.join(root, "run")
    os.makedirs(os.path.join(d, "p"))
    with open(os.path.join(d, "delta.json"), "w") as f:
        json.dump({"pairs": [{"pair": "p"}]}, f)
    m = [2.0, 4.0, 3.0]
    with open(os.path.join(d, "p", "mirage.json"), "w") as f:
        json.dump({"pooled": {"mean": 3.0, "min": 2.0},
                   "frames": [{"index": i, "vmaf": v} for i, v in enumerate(m)]}, f)
    log = {"frames": [{"frameNum": i, "metrics": {"vmaf": v, "integer_motion2": 0.0 if i == 0
                                                   else 1.5, "integer_motion": 0.0}}
                      for i, v in enumerate(m)],
           "pooled_metrics": {"vmaf": {"min": 2.0, "max": 4.0, "mean": 3.0,
                                       "harmonic_mean": round(3 / (1/3 + 1/5 + 1/4) - 1, 6)}}}
    opener = (lambda p: lzma.open(p + ".xz", "wt")) if xz else (lambda p: open(p, "w"))
    with opener(os.path.join(d, "p", "ffmpeg.json")) as f:
        json.dump(log, f)
    return d


class Reports(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = self.tmp.name

    def run_main(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            return pooling.main(list(argv)), out.getvalue()

    def test_gui_report_and_delta_run(self):
        gui = os.path.join(self.root, "report.vmaf.json")
        scores = [float(v) for v in range(20, 0, -1)]   # 20 .. 1
        with open(gui, "w") as f:
            json.dump({"results": [
                {"label": "a", "vmaf_mean": 10.5, "vmaf_min": 1.0,
                 "vmaf_p5": scores[::-1][20 * 5 // 100], "per_frame": scores},
                {"label": "0x0  0.0 Mbps", "error": "cannot open distorted: x"}]}, f)
        run = write_delta_run(self.root, xz=True)
        out = os.path.join(self.root, "pooling")
        rc, text = self.run_main("--gui-report", gui, "--delta-run", run, "--out", out)
        self.assertEqual(rc, 0, text)
        with open(out + ".json") as f:
            r = json.load(f)
        gui_src, delta_src = r["sources"]
        self.assertEqual(gui_src["skipped"], [{"label": "0x0  0.0 Mbps",
                                               "error": "cannot open distorted: x"}])
        a = gui_src["series"][0]
        self.assertEqual(a["stats"]["frames"], 20)
        self.assertTrue(a["checks"]["recorded_p5_equals_today_at_4_decimals"])
        self.assertTrue(a["checks"]["recorded_min_equals_min"])
        self.assertEqual(a["stats"]["min_first_index"], 19)
        labels = [s["label"] for s in delta_src["series"]]
        self.assertEqual(labels, ["p, Mirage", "p, FFmpeg"])
        ff = delta_src["series"][1]
        self.assertTrue(ff["checks"]["shifted_harmonic_mean_equals_libvmaf_at_6_decimals"])
        self.assertEqual(ff["motion"], {"frame0_integer_motion2": 0.0,
                                        "frame0_integer_motion": 0.0,
                                        "frames_with_integer_motion2_zero": 1})
        self.assertTrue(math.isclose(ff["stats"]["harmonic_mean"]["plain"],
                                     3 / (1/2 + 1/4 + 1/3)))
        with open(out + ".md") as f:
            md = f.read()
        self.assertTrue(md.startswith("# Pooling definitions on committed per-frame scores\n"))
        self.assertIn("| a | p5 |", md)
        self.assertIn("### Motion of the first frame (FFmpeg's libvmaf log)", md)
        self.assertIn("- skipped: '0x0  0.0 Mbps': cannot open distorted: x", md)
        self.assertNotIn(self.root, md)
        self.assertNotIn(self.root, json.dumps(r))

    def test_zero_scores_and_errors(self):
        gui = os.path.join(self.root, "zeros.json")
        with open(gui, "w") as f:
            json.dump({"results": [{"label": "z", "per_frame": [0.0, 1.0, 0.0]}]}, f)
        out = os.path.join(self.root, "z")
        rc, text = self.run_main("--gui-report", gui, "--out", out)
        self.assertEqual(rc, 0, text)
        with open(out + ".md") as f:
            self.assertIn("| undefined | 2 |", f.read())
        rc, text = self.run_main("--delta-run", os.path.join(self.root, "none"), "--out", out)
        self.assertEqual(rc, 1)
        self.assertIn("delta.json", text)
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            pooling.parse_args(["--out", out])


if __name__ == "__main__":
    unittest.main()
