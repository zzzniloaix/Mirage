"""Unit tests of eval/ladder/pool_check.py, with a stand-in for mirage_vmaf that writes a
ladder report of its own; /usr/bin/time -l is macOS's.

python3 -I -B -m unittest discover -s eval/ladder/tests -v
"""

import contextlib
import io
import json
import lzma
import os
import shlex
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(HERE))
import pool_check  # noqa: E402

# The stand-in: writes REPORT to --out with the pool fields of its options. Environment:
# FAKE_DIFF_JOBS, the --jobs value whose first pair scores its first frame 1e-9 higher;
# FAKE_STATUS_JOBS, the one whose run is cancelled; FAKE_PEAK, the report's peak resident
# memory; FAKE_NO_LIMIT, a report without the memory limit.
FAKE = r'''
import json, os, sys
args = sys.argv[1:]
opt = {args[i]: args[i + 1] for i in range(0, len(args) - 1, 2)}
jobs = opt.get("--jobs", "11")
rep = json.loads(os.environ["FAKE_REPORT"])
if os.environ.get("FAKE_DIFF_JOBS") == jobs:
    rep["pairs"][0]["frames"][0]["vmaf"] += 1e-9
if os.environ.get("FAKE_STATUS_JOBS") == jobs:
    rep["status"] = "cancelled"
limit = None
if "--memory-limit" in opt and not os.environ.get("FAKE_NO_LIMIT"):
    v = opt["--memory-limit"]
    limit = int(v[:-1]) << {"K": 10, "M": 20, "G": 30, "T": 40}[v[-1]]
rep["pool"] = {"jobs": int(jobs), "memory_limit": limit, "max_running": 1,
               "peak_resident": int(os.environ.get("FAKE_PEAK", "5000")), "warnings": []}
with open(opt["--out"], "w") as f:
    json.dump(rep, f)
print("mirage_vmaf: report written to " + opt["--out"])
'''

REPORT = {
    "status": "complete", "ladder": [{"uri": "v9.m3u8"}, {"uri": "v2.m3u8"}],
    "reference": {"uri": "v9.m3u8"}, "skipped": [], "not_scored": [],
    "pairs": [{"index": 0, "dis": {"url": "v2.m3u8"},
               "frames": [{"index": 0, "ref_pts": 0.0, "dis_pts": 0.0, "vmaf": 91.5},
                          {"index": 1, "ref_pts": 0.04, "dis_pts": 0.04, "vmaf": 92.25}],
               "pooled": {"mean": 91.875}}],
}


@unittest.skipUnless(sys.platform == "darwin", "pool_check.py runs macOS's /usr/bin/time -l")
class PoolCheck(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        d = self.tmp.name
        with open(os.path.join(d, "fake.py"), "w") as f:
            f.write(FAKE)
        self.prog = os.path.join(d, "mirage_vmaf")
        with open(self.prog, "w") as f:
            f.write(f"#!/bin/sh\nexec {shlex.quote(sys.executable)} -I "
                    f"{shlex.quote(os.path.join(d, 'fake.py'))} \"$@\"\n")
        os.chmod(self.prog, 0o755)
        self.ladder = os.path.join(d, "ladder", "master.m3u8")
        os.makedirs(os.path.dirname(self.ladder))
        with open(self.ladder, "w") as f:
            f.write("#EXTM3U\n")
        self.env = {"FAKE_REPORT": json.dumps(REPORT)}

    def check(self, *extra, **env):
        out = os.path.join(self.tmp.name, "out")
        with mock.patch.dict(os.environ, self.env | env), \
                contextlib.redirect_stdout(io.StringIO()):
            code = pool_check.main([self.prog, out, "--ladder", self.ladder, *extra])
        with open(os.path.join(out, "check.json")) as f:
            summary = json.load(f)
        with open(os.path.join(out, "check.md")) as f:
            md = f.read()
        return code, summary, md, out

    def test_the_same_pairs_at_every_size_pass(self):
        code, s, md, out = self.check("--jobs", "1,2,0")
        self.assertEqual(code, 0)
        self.assertEqual(s["verdict"], "PASS")
        self.assertEqual(s["problems"], [])
        self.assertEqual([r["jobs_given"] for r in s["runs"]], [1, 2, 0])
        self.assertTrue(md.startswith("# The ladder at several pool sizes: PASS"))
        with open(os.path.join(out, "cmd.txt")) as f:
            cmd = f.read()
        self.assertIn("<mirage_vmaf> --ladder <ladder> --frames 300 --out "
                      "<out_dir>/jobs-1/report.json --jobs 1", cmd)
        self.assertNotIn(self.tmp.name, cmd)
        with lzma.open(os.path.join(out, "jobs-default", "report.json.xz")) as f:
            self.assertEqual(json.load(f)["pool"]["jobs"], 11)
        with open(os.path.join(out, "SHA256SUMS")) as f:
            self.assertIn("  check.md", f.read())

    def test_a_changed_score_fails(self):
        code, s, md, _ = self.check("--jobs", "1,2", FAKE_DIFF_JOBS="2")
        self.assertEqual(code, 1)
        self.assertEqual(s["problems"], ["pair 0 differs at jobs 2"])
        self.assertIn("| 0 | `v2.m3u8` | 2 | NO |", md)

    def test_an_incomplete_run_fails(self):
        code, s, _, _ = self.check("--jobs", "1,2", FAKE_STATUS_JOBS="2")
        self.assertEqual(code, 1)
        self.assertIn("jobs 2: the run is cancelled", s["problems"])

    def test_a_peak_under_the_memory_limit_passes(self):
        code, s, md, _ = self.check("--jobs", "1,0", "--memory-limit", "1G")
        self.assertEqual(code, 0, s["problems"])
        self.assertEqual(s["memory_limit_given"], "1G")
        self.assertEqual([r["pool"]["memory_limit"] for r in s["runs"]], [1 << 30] * 2)
        self.assertIn("| 1073741824 | 5000 |", md)

    def test_a_peak_over_the_memory_limit_fails(self):
        # The report's peak is over the limit, and so is the stand-in's own resident memory.
        code, s, _, _ = self.check("--jobs", "1", "--memory-limit", "4K", FAKE_PEAK="5000")
        self.assertEqual(code, 1)
        self.assertIn("jobs 1: the report's peak resident memory, 5000, is over the memory "
                      "limit, 4096", s["problems"])
        self.assertTrue(any(p.startswith("jobs 1: /usr/bin/time -l's maximum resident set "
                                         "size, ") for p in s["problems"]))

    def test_a_report_without_the_memory_limit_fails(self):
        code, s, _, _ = self.check("--jobs", "1", "--memory-limit", "1G", FAKE_NO_LIMIT="1")
        self.assertEqual(code, 1)
        self.assertEqual(s["problems"], ["jobs 1: the report gives no memory limit"])

    def test_frames_0_scores_every_frame(self):
        code, s, md, out = self.check("--jobs", "0", "--frames", "0")
        self.assertEqual(code, 0, s["problems"])
        with open(os.path.join(out, "cmd.txt")) as f:
            cmd = f.read()
        self.assertIn("<mirage_vmaf> --ladder <ladder> --out <out_dir>/jobs-default/report.json\n", cmd)
        self.assertNotIn("--frames", cmd)
        self.assertIn("every frame pair of each pair", md)

    def test_without_a_memory_limit_the_column_is_empty(self):
        code, s, md, _ = self.check("--jobs", "1", FAKE_PEAK="999999999999")
        self.assertEqual(code, 0, s["problems"])
        self.assertIn("| 1 | 0 | complete | 1 | 1 | - | 999999999999 |", md)


if __name__ == "__main__":
    unittest.main()
