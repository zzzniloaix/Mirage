"""Unit tests of eval/consistency/compare_runs.py, on run directories written here.

python3 -I -B -m unittest discover -s eval/consistency/tests -v
"""

import contextlib
import io
import json
import lzma
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(HERE))
import compare_runs as cr  # noqa: E402


def write_run(root, name, scaler, mirage, ffmpeg, sha="m1", head="c1", frames=3, xz=False):
    """A run_delta.py output directory with one pair, "p", and these scores."""
    d = os.path.join(root, name)
    os.makedirs(os.path.join(d, "p"))
    overall = {
        "status": "PASS",
        "parameters": {"variant": "plain", "frames": frames, "threshold": 0.01,
                       "model": "vmaf_v0.6.1", "scaler": scaler, "threads": 4,
                       "model_file": None},
        "mirage_vmaf": {"sha256": sha}, "git": {"head_start": head},
        "environment": {"ffmpeg_version": "ffmpeg version 8.1.1"},
        "pairs": [{"pair": "p", "status": "PASS", "max_abs": 0.0, "over_threshold": 0,
                   "frames": {"mirage": len(mirage), "ffmpeg": len(ffmpeg)}}],
    }
    with open(os.path.join(d, "delta.json"), "w") as f:
        json.dump(overall, f)
    rep = {"model": "vmaf_v0.6.1", "model_source": "built-in", "model_sha256": None,
           "scaler": scaler, "pooled": {"mean": sum(mirage) / len(mirage)},
           "frames": [{"index": i, "vmaf": v} for i, v in enumerate(mirage)]}
    log = {"frames": [{"frameNum": i, "metrics": {"vmaf": v}} for i, v in enumerate(ffmpeg)],
           "pooled_metrics": {"vmaf": {"mean": sum(ffmpeg) / len(ffmpeg)}}}
    with open(os.path.join(d, "p", "mirage.json"), "w") as f:
        json.dump(rep, f)
    if xz:
        with lzma.open(os.path.join(d, "p", "ffmpeg.json.xz"), "wt") as f:
            json.dump(log, f)
    else:
        with open(os.path.join(d, "p", "ffmpeg.json"), "w") as f:
            json.dump(log, f)
    return d


class Shift(unittest.TestCase):
    def test_numbers(self):
        s = cr.shift([10.0, 20.0, 30.0, 40.0], [10.5, 19.0, 30.0, 41.0])
        self.assertEqual((s["compared"], s["max_abs"], s["max_abs_index"]), (4, 1.0, 1))
        self.assertEqual(s["mean_signed"], 0.125)
        self.assertEqual(s["mean_abs"], 0.625)
        self.assertEqual((s["mean_base"], s["mean_run"]), (25.0, 25.125))

    def test_unequal_lengths_missing_and_empty(self):
        s = cr.shift([1.0, 2.0, 3.0], [1.0, 2.5])
        self.assertEqual((s["compared"], s["base_frames"], s["run_frames"]), (2, 3, 2))
        self.assertIsNone(cr.shift(None, [1.0]))
        e = cr.shift([], [])
        self.assertEqual((e["compared"], e["max_abs"], e["mean_abs"]), (0, None, None))


class CompareRuns(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = self.tmp.name

    def run_main(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            rc = cr.main(list(argv))
        return rc, out.getvalue()

    def test_runs_against_the_base_run(self):
        write_run(self.root, "bilinear", "bilinear", [50.0, 60.0, 70.0], [50.0, 60.0, 70.0])
        write_run(self.root, "bicubic", "bicubic", [51.0, 60.0, 72.0], [51.0, 60.0, 72.0],
                  xz=True)
        out = os.path.join(self.root, "scaler_effect")
        rc, text = self.run_main("--base", f"bilinear={self.root}/bilinear",
                                 "--run", f"bicubic={self.root}/bicubic", "--out", out)
        self.assertEqual(rc, 0, text)
        with open(out + ".json") as f:
            c = json.load(f)
        self.assertEqual(c["parameters_that_differ"], ["scaler"])
        self.assertEqual(c["warnings"], [])
        self.assertEqual((c["base"]["dir"], c["runs"][0]["dir"]), ("bilinear", "bicubic"))
        p = c["pairs"][0]
        self.assertEqual(p["pair"], "p")
        for side in ("mirage", "ffmpeg"):
            s = p["runs"]["bicubic"][side]
            self.assertEqual((s["compared"], s["max_abs"], s["max_abs_index"]), (3, 2.0, 2))
            self.assertEqual(s["mean_signed"], 1.0)
        self.assertEqual(p["runs"]["bicubic"]["settings"]["scaler"], "bicubic")
        with open(out + ".md") as f:
            md = f.read()
        self.assertTrue(md.startswith("# Score shifts against the bilinear run\n"))
        self.assertIn("| run | directory | status (Mirage vs FFmpeg) | scaler |", md)
        self.assertIn("| bicubic | `bicubic` | PASS | bicubic |", md)
        self.assertIn("| p | bicubic | 3 | 2.000000 (2) | 1.000000 | 1.000000 | 60.000000 "
                      "| 61.000000 | 1.000000 |", md)
        self.assertIn("## How far FFmpeg's scores move from the bilinear run's", md)
        self.assertNotIn(self.root, md)
        self.assertNotIn(self.root, json.dumps(c))

    def test_warnings(self):
        write_run(self.root, "a", "bilinear", [1.0, 2.0, 3.0], [1.0, 2.0, 3.0])
        write_run(self.root, "b", "bicubic", [1.0, 2.0], [1.0, 2.0, 3.0], sha="m2", head="c2",
                  frames=2)
        c = cr.compare(cr.load_run("a", f"{self.root}/a", "a"),
                       [cr.load_run("b", f"{self.root}/b", "b")])
        text = "\n".join(c["warnings"])
        self.assertIn("different mirage_vmaf builds: a m1, b m2", text)
        self.assertIn("different commits", text)
        self.assertIn("different frames parameters", text)
        self.assertIn("p, mirage: b scored 2 frames, a 3", text)
        self.assertIn("Warnings:", cr.markdown(c))

    def test_missing_inputs(self):
        write_run(self.root, "a", "bilinear", [1.0], [1.0])
        rc, text = self.run_main("--base", f"a={self.root}/a", "--run", f"b={self.root}/none",
                                 "--out", os.path.join(self.root, "x"))
        self.assertEqual(rc, 1)
        self.assertIn("delta.json", text)
        # A pair whose mirage_vmaf left no report is compared on FFmpeg's side only.
        write_run(self.root, "b", "bicubic", [1.0], [2.0])
        os.remove(os.path.join(self.root, "b", "p", "mirage.json"))
        c = cr.compare(cr.load_run("a", f"{self.root}/a", "a"),
                       [cr.load_run("b", f"{self.root}/b", "b")])
        self.assertIsNone(c["pairs"][0]["runs"]["b"]["mirage"])
        self.assertEqual(c["pairs"][0]["runs"]["b"]["ffmpeg"]["max_abs"], 1.0)
        self.assertIn("| p | b | - | - | - | - |", cr.markdown(c))

    def test_labels_must_differ(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            cr.parse_args(["--base", "a=x", "--run", "a=y", "--out", "z"])
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            cr.parse_args(["--base", "a", "--run", "b=y", "--out", "z"])


if __name__ == "__main__":
    unittest.main()
