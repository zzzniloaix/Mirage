"""Tests of eval/consistency/ladder_check.py: the comparison of a ladder
report with the reports of its pairs scored one by one, and whole runs with
mirage_vmaf's stand-in (tests/fake_tools.py), so no network and no real
scoring.

python3 -I -B -m unittest discover -s eval/consistency/tests -v
"""

import contextlib
import copy
import hashlib
import io
import json
import lzma
import os
import stat
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(HERE))
import ladder_check as lc  # noqa: E402
import vmafdelta as vd  # noqa: E402

# mirage_vmaf's stand-in as the current program: every option and report key.
CURRENT = {f"FAKE_MIRAGE_HAS_{x}": "1"
           for x in ("SCALER", "MODEL", "D022", "D027", "D028", "LADDER")}

TOP = "fake:1920x1080:bt709"
LOW = "fake:640x360:smpte170m"
MID = "fake:960x540:bt709"


def master(*entries):
    """A master playlist of (attributes, URI) entries."""
    return "#EXTM3U\n" + "".join(f"#EXT-X-STREAM-INF:{a}\n{uri}\n" for a, uri in entries)


LADDER = master(("BANDWIDTH=300,RESOLUTION=640x360", LOW),
                ("BANDWIDTH=900,RESOLUTION=1920x1080", TOP),
                ("BANDWIDTH=500,RESOLUTION=960x540,AUDIO=\"a\"", MID),
                ("BANDWIDTH=550,RESOLUTION=960x540,AUDIO=\"b\"", MID),
                ("BANDWIDTH=40,CODECS=\"mp4a.40.2\"", "fake:0x0:none"))


def pair(index, entries, ref, dis, scores):
    return {"index": index, "entries": entries, "scored_as": {"width": 1920},
            "frames_scored": len(scores), "pooled": {"mean": sum(scores) / len(scores)},
            "ref": {"url": ref}, "dis": {"url": dis},
            "frames": [{"index": k, "vmaf": v} for k, v in enumerate(scores)]}


RUN = {"tool": "mirage_vmaf", "model": "vmaf_v0.6.1", "scaler": "bicubic", "n_threads": 4,
       "frame_limit": 3}


def ladder_report(*pairs, entries=3, skipped=()):
    return {**RUN, "reference_mode": "top-rung",
            "ladder": {"entries": [{"index": i} for i in range(entries)]},
            "reference": {"url": "r", "entries": [0]},
            "skipped": [{"entry": i, "reason": "no video"} for i in skipped],
            "pairs": list(pairs)}


def alone(p):
    """The report of a ladder's pair scored on its own: a copy of its values."""
    return copy.deepcopy({**RUN, "reference_mode": "explicit-pair",
                          **{k: v for k, v in p.items() if k not in ("index", "entries")}})


class Compare(unittest.TestCase):
    def setUp(self):
        self.a = pair(0, [1], "r", "a", [10.0, 11.0, 12.0])
        self.b = pair(1, [2], "r", "b", [20.0, 21.0, 22.0])
        self.pairs = [vd.Pair("pa", "r", "a"), vd.Pair("pb", "r", "b")]

    def test_the_same_pairs_pass(self):
        r = lc.compare(ladder_report(self.a, self.b), {"pa": alone(self.a), "pb": alone(self.b)},
                       self.pairs)
        self.assertEqual(r["status"], "PASS", r["problems"])
        self.assertEqual([(x["pair"], x["status"], x["ladder_index"], x["entries"])
                          for x in r["pairs"]], [("pa", "SAME", 0, [1]), ("pb", "SAME", 1, [2])])
        self.assertEqual(r["pairs"][0]["frames"], {"ladder": 3, "alone": 3})
        self.assertTrue(r["entries_accounted_once"])
        self.assertEqual(r["extra_pairs"], [])

    def test_a_score_or_a_setting_that_differs(self):
        own = alone(self.a)
        own["frames"][1]["vmaf"] = 11.25
        r = lc.compare(ladder_report(self.a, self.b), {"pa": own, "pb": alone(self.b)},
                       self.pairs)
        self.assertEqual(r["status"], "FAIL")
        x = r["pairs"][0]
        self.assertEqual((x["status"], x["differing"]), ("DIFFERS", ["frames"]))
        self.assertEqual((x["differing_frames"], x["largest_score_difference"]), (1, 0.25))
        self.assertEqual(r["problems"], ["pa: the ladder's pair differs in frames"])

        other = alone(self.b)
        other["scaler"] = "lanczos"
        del other["pooled"]
        r = lc.compare(ladder_report(self.a, self.b), {"pa": alone(self.a), "pb": other},
                       self.pairs)
        self.assertEqual(r["pairs"][1]["differing"], ["pooled", "run's scaler"])

        # The pair scored alone has to be an explicit pair's report.
        own = alone(self.a)
        own["reference_mode"] = "top-rung"
        r = lc.compare(ladder_report(self.a, self.b), {"pa": own, "pb": alone(self.b)},
                       self.pairs)
        self.assertEqual(r["pairs"][0]["differing"], ["run's reference_mode"])

    def test_pairs_missing_twice_or_extra(self):
        r = lc.compare(ladder_report(self.a), {"pa": alone(self.a), "pb": alone(self.b)},
                       self.pairs)
        self.assertEqual(r["pairs"][1]["status"], "MISSING")
        self.assertIn("pb: the ladder didn't score this pair", r["problems"])

        c = pair(2, [0], "r", "c", [1.0])
        r = lc.compare(ladder_report(self.a, self.b, c, entries=3),
                       {"pa": alone(self.a), "pb": alone(self.b)}, self.pairs)
        self.assertEqual(r["status"], "FAIL")
        self.assertEqual(r["extra_pairs"], [{"index": 2, "dis": "c", "entries": [0]}])

        twice = pair(1, [2], "r", "a", [10.0, 11.0, 12.0])
        r = lc.compare(ladder_report(self.a, twice), {"pa": alone(self.a), "pb": alone(self.b)},
                       self.pairs)
        self.assertEqual(r["pairs"][0]["status"], "TWICE")

        r = lc.compare(None, {"pa": alone(self.a), "pb": None}, self.pairs)
        self.assertEqual(r["status"], "FAIL")
        self.assertEqual(r["problems"], ["the ladder run left no report"])
        self.assertEqual([x["status"] for x in r["pairs"]], ["NO LADDER REPORT"] * 2)

        r = lc.compare(ladder_report(self.a, self.b), {"pa": alone(self.a), "pb": None},
                       self.pairs)
        self.assertEqual(r["pairs"][1]["status"], "NO REPORT")

    def test_every_entry_once(self):
        self.assertEqual(lc.accounting(ladder_report(self.a, self.b)), [])
        self.assertEqual(lc.accounting(ladder_report(self.a, self.b, entries=4)),
                         ["entry 3 is neither the reference's, a pair's nor skipped"])
        self.assertEqual(lc.accounting(ladder_report(self.a, self.b, skipped=[1])),
                         ["entry 1 is pair 0 and skipped"])
        self.assertEqual(lc.accounting(ladder_report(self.a, self.b, entries=2)),
                         ["entry 2 is listed but the master has no such entry"])
        r = lc.compare(ladder_report(self.a, self.b, skipped=[1]),
                       {"pa": alone(self.a), "pb": alone(self.b)}, self.pairs)
        self.assertEqual(r["status"], "FAIL")
        self.assertFalse(r["entries_accounted_once"])


class WholeRun(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = self.tmp.name
        self.bin = os.path.join(self.dir, "bin")
        os.mkdir(self.bin)
        path = os.path.join(self.bin, "mirage_vmaf")
        with open(path, "w") as f:
            f.write(f'#!/bin/sh\nexec "{sys.executable}" -I '
                    f'"{os.path.join(HERE, "fake_tools.py")}" mirage_vmaf "$@"\n')
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
        self.runs = 0

    def write(self, name, text):
        path = os.path.join(self.dir, name)
        with open(path, "w") as f:
            f.write(text)
        return path

    def run_check(self, ladder_text, rows, *extra):
        """ladder_check.main() on the stand-in, into a new self.out; the temp
        directory shows as <tmp>."""
        self.runs += 1
        self.out = os.path.join(self.dir, f"out{self.runs}")
        argv = [os.path.join(self.bin, "mirage_vmaf"), self.out,
                "--ladder", self.write("master.m3u8", ladder_text),
                "--pairs", self.write("pairs.txt", "# test pairs\n" + "".join(
                    f"{label} {ref} {dis}\n" for label, ref, dis in rows)), *extra]
        stdout, stderr = io.StringIO(), io.StringIO()
        with mock.patch.dict(os.environ, dict(CURRENT, TMPDIR=self.dir)), \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            rc = lc.main(argv)
        return rc, stdout.getvalue(), stderr.getvalue()

    def report(self):
        with open(os.path.join(self.out, "check.json"), encoding="utf-8") as f:
            return json.load(f)

    def check_files(self):
        """SHA256SUMS covers every file and holds, and nothing names the temp dir."""
        with open(os.path.join(self.out, "SHA256SUMS"), encoding="utf-8") as f:
            sums = dict(reversed(line.split("  ", 1)) for line in f.read().splitlines())
        found = set()
        for dirpath, _, names in os.walk(self.out):
            for name in names:
                rel = os.path.relpath(os.path.join(dirpath, name), self.out)
                if rel == "SHA256SUMS":
                    continue
                found.add(rel)
                with open(os.path.join(self.out, rel), "rb") as f:
                    data = f.read()
                self.assertEqual(hashlib.sha256(data).hexdigest(), sums.get(rel), rel)
                if rel.endswith(".xz"):
                    data = lzma.decompress(data)
                for local in {self.dir, os.path.realpath(self.dir)}:
                    self.assertNotIn(local.encode(), data, rel)
        self.assertEqual(found, set(sums))
        return found

    def test_a_ladder_whose_pairs_are_the_same(self):
        rc, out, err = self.run_check(LADDER, [("low", TOP, LOW), ("mid", TOP, MID)],
                                      "--frames", "4", "--jobs", "2")
        self.assertEqual(rc, 0, out + err)
        files = self.check_files()
        for rel in ("run.cmd.txt", "pairs.txt", "environment.txt", "check.md", "check.json",
                    "ladder/cmd.txt", "ladder/mirage.json", "ladder/mirage.stdout.txt",
                    "ladder/mirage.stderr.log.xz", "low/mirage.json", "mid/cmd.txt"):
            self.assertIn(rel, files)
        r = self.report()
        self.assertEqual(r["status"], "PASS", r["problems"])
        self.assertEqual([(x["pair"], x["status"], x["entries"]) for x in r["pairs"]],
                         [("low", "SAME", [0]), ("mid", "SAME", [2, 3])])
        self.assertEqual(r["pairs"][0]["frames"], {"ladder": 4, "alone": 4})
        lad = r["ladder"]
        self.assertEqual(lad["reference_mode"], "top-rung")
        self.assertEqual(lad["reference"], {"url": TOP, "entries": [1]})
        self.assertEqual(lad["skipped"], [{"entry": 4, "reason": "no video"}])
        self.assertEqual([e["role"] for e in lad["entries"]],
                         ["pair 0", "reference", "pair 1", "pair 1", "skipped: no video"])
        self.assertEqual(r["params"]["settings"],
                         ["--frames", "4", "--scaler", "bicubic", "--model", "vmaf_v0.6.1"])
        with open(os.path.join(self.out, "run.cmd.txt"), encoding="utf-8") as f:
            cmd = f.read().splitlines()[-1]
        self.assertEqual(cmd, "$ python3 -I -B eval/consistency/ladder_check.py <mirage_vmaf> "
                              "<out_dir> --ladder <tmp>/master.m3u8 --pairs <tmp>/pairs.txt "
                              "--frames 4 --scaler bicubic --model vmaf_v0.6.1 --jobs 2 "
                              "--timeout 21600")
        with open(os.path.join(self.out, "ladder", "cmd.txt"), encoding="utf-8") as f:
            self.assertIn("$ <mirage_vmaf> --ladder <tmp>/master.m3u8 --out mirage.json "
                          "--frames 4 --scaler bicubic --model vmaf_v0.6.1 > mirage.stdout.txt",
                          f.read())
        with open(os.path.join(self.out, "check.md"), encoding="utf-8") as f:
            md = f.read()
        self.assertIn("**PASS**", md)
        self.assertIn("| mid | 1 | 2, 3 | 4 / 4 | SAME | - | 0 |", md)

    def test_mezzanine_mode(self):
        rc, out, err = self.run_check(LADDER, [("low", MID, LOW), ("top", MID, TOP),
                                               ("mid", MID, MID)], "--mezzanine", MID)
        self.assertEqual(rc, 0, out + err)
        r = self.report()
        self.assertEqual(r["ladder"]["reference_mode"], "mezzanine")
        self.assertEqual(r["ladder"]["reference"], {"url": MID, "entries": []})
        self.assertEqual([x["status"] for x in r["pairs"]], ["SAME"] * 3)
        self.assertEqual(r["params"]["mezzanine"], MID)

    def test_a_ladder_pair_that_scores_otherwise_fails(self):
        low = LOW + ":ladder_offset=0.5"
        rc, out, err = self.run_check(LADDER.replace(LOW, low), [("low", TOP, low),
                                                                 ("mid", TOP, MID)])
        self.assertEqual(rc, 1, out + err)
        r = self.report()
        self.assertEqual(r["status"], "FAIL")
        x = r["pairs"][0]
        self.assertEqual((x["status"], x["differing"]), ("DIFFERS", ["frames"]))
        self.assertEqual(x["differing_frames"], 10)
        self.assertEqual(x["largest_score_difference"], 0.5)
        self.assertEqual(r["pairs"][1]["status"], "SAME")

    def test_pairs_the_ladder_doesnt_have_and_a_failed_run(self):
        rc, _, _ = self.run_check(LADDER, [("low", TOP, LOW)])
        self.assertEqual(rc, 1)
        r = self.report()
        self.assertEqual(r["extra_pairs"][0]["dis"], MID)

        rc, _, _ = self.run_check(LADDER.replace(MID, MID + ":mirage_fails"),
                                  [("low", TOP, LOW)])
        self.assertEqual(rc, 1)
        r = self.report()
        self.assertIn("the ladder run left no report", r["problems"])
        self.assertIn("ladder: mirage_vmaf exited with status 1", r["problems"])
        self.assertIsNone(r["ladder"])

    def test_bad_arguments(self):
        self.out = os.path.join(self.dir, "out")
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(lc.main([os.path.join(self.bin, "mirage_vmaf"), self.out,
                                      "--pairs", os.path.join(self.dir, "none.txt")]), 2)
            with self.assertRaises(SystemExit):
                lc.main([os.path.join(self.bin, "mirage_vmaf"), self.out, "--model", "a",
                         "--model-file", "b"])


if __name__ == "__main__":
    unittest.main()
