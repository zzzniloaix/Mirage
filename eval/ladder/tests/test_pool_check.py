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
import shutil
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
# memory; FAKE_NO_LIMIT, a report without the memory limit; FAKE_LIMIT, the limit the report
# gives instead of the one asked for; FAKE_STDERR, what it writes to stderr; FAKE_EXIT, its
# exit status.
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
    limit = int(os.environ.get("FAKE_LIMIT", limit))
rep["pool"] = {"jobs": int(jobs), "memory_limit": limit, "baseline": 1000 if limit else None,
               "max_running": 1,
               "pair_memory": [{"pair": 1, "running": 1, "resident": 5000, "measure": 4000},
                               {"pair": 2, "running": 1, "resident": 6000, "measure": 5000}]
               if limit else [],
               "peak_resident": int(os.environ.get("FAKE_PEAK", "5000")), "warnings": []}
with open(opt["--out"], "w") as f:
    json.dump(rep, f)
print("mirage_vmaf: report written to " + opt["--out"])
sys.stderr.write(os.environ.get("FAKE_STDERR", ""))
sys.exit(int(os.environ.get("FAKE_EXIT", "0")))
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
        # Each admission's measure, and the memory before the run (D-058).
        self.assertIn("| pair 1: 4000; pair 2: 5000 | 1073741824 | 1000 | 5000 |", md)

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

    def test_a_run_that_exits_non_zero_fails(self):
        # A complete report, and the exit status of a run that went wrong after it.
        code, s, _, _ = self.check("--jobs", "1,2", FAKE_EXIT="3")
        self.assertEqual(code, 1)
        self.assertEqual(s["problems"], ["jobs 1: mirage_vmaf exited with status 3",
                                        "jobs 2: mirage_vmaf exited with status 3"])

    def test_the_peak_is_checked_against_the_limit_given(self):
        # The report gives another limit than the one given, and the peak is under it.
        code, s, _, _ = self.check("--jobs", "1", "--memory-limit", "1M",
                                   FAKE_LIMIT=str(2 << 30), FAKE_PEAK=str(3 << 20))
        self.assertEqual(code, 1)
        self.assertEqual(s["memory_limit_bytes"], 1 << 20)
        self.assertIn(f"jobs 1: the report gives the memory limit {2 << 30}, not the "
                      f"{1 << 20} given", s["problems"])
        self.assertIn(f"jobs 1: the report's peak resident memory, {3 << 20}, is over the "
                      f"memory limit, {1 << 20}", s["problems"])

    def test_a_size_mirage_vmaf_wouldnt_read_is_refused(self):
        for size in ("1.5G", "0", "1g", "1 G", "1GiB", ""):
            with self.subTest(size=size), contextlib.redirect_stderr(io.StringIO()):
                out = os.path.join(self.tmp.name, "refused")
                self.assertEqual(pool_check.main([self.prog, out, "--ladder", self.ladder,
                                                  "--memory-limit", size]), 2)
                self.assertFalse(os.path.exists(os.path.join(out, "check.json")))

    def test_the_pass_line_names_every_criterion(self):
        _, _, md, out = self.check("--jobs", "1,2", "--memory-limit", "1G")
        self.assertIn("- PASS when every run exits 0 with a complete report, each pair's frames "
                      "(indices, timestamps, scores at full precision) and pooled values are "
                      "the same in every run, and with --memory-limit 1G, 1073741824 bytes, "
                      "every report gives that limit and neither its peak resident memory nor "
                      "/usr/bin/time -l's maximum resident set size is over it.\n", md)
        shutil.rmtree(out)
        _, _, md, _ = self.check("--jobs", "1,2")
        self.assertIn("are the same in every run.\n", md)

    def test_time_l_is_split_off_at_its_last_real_line(self):
        # The program's own stderr says "real" before /usr/bin/time -l's lines.
        code, s, _, out = self.check("--jobs", "1",
                                     FAKE_STDERR="mirage_vmaf: warning: real 1.0 user 2.0\n")
        self.assertEqual(code, 0, s["problems"])
        with open(os.path.join(out, "jobs-1", "stderr.txt")) as f:
            self.assertEqual(f.read(), "mirage_vmaf: warning: real 1.0 user 2.0\n")
        with open(os.path.join(out, "jobs-1", "time.txt")) as f:
            first = f.readline()
        self.assertRegex(first, r"^\s*[\d.]+ real\s+[\d.]+ user\s+[\d.]+ sys\n$")

    def test_one_run_writes_a_valid_table(self):
        code, s, md, _ = self.check("--jobs", "1")
        self.assertEqual(code, 0, s["problems"])
        self.assertIn("\n| pair | distorted | frames |\n|---|---|---|\n| 0 | `v2.m3u8` | 2 |\n", md)

    def test_without_a_memory_limit_the_column_is_empty(self):
        code, s, md, _ = self.check("--jobs", "1", FAKE_PEAK="999999999999")
        self.assertEqual(code, 0, s["problems"])
        self.assertIn("| 1 | 0 | complete | 1 | 1 | - | - | - | 999999999999 |", md)


if __name__ == "__main__":
    unittest.main()
