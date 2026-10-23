"""End-to-end tests of eval/consistency/run_delta.py with stand-ins for
mirage_vmaf, ffmpeg and ffprobe (tests/fake_tools.py), so no network and no
real scoring.

python3 -I -B -m unittest discover -s eval/consistency/tests -v
"""

import contextlib
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
import run_delta  # noqa: E402

# Inputs of the stand-ins: fake:<width>x<height>:<color_space>[:<option>...]
REF = "fake:1920x1080:bt709"
SAME = "fake:960x540:bt709"            # same tags as REF: FFmpeg converts nothing
OTHER = "fake:640x360:smpte170m"       # other color_space: FFmpeg's stand-in converts it


class RunDelta(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = self.tmp.name
        self.bin = os.path.join(self.dir, "bin")
        os.mkdir(self.bin)
        for tool in ("mirage_vmaf", "ffmpeg", "ffprobe"):
            path = os.path.join(self.bin, tool)
            with open(path, "w") as f:
                f.write(f'#!/bin/sh\nexec "{sys.executable}" -I '
                        f'"{os.path.join(HERE, "fake_tools.py")}" {tool} "$@"\n')
            os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
        self.out = os.path.join(self.dir, "out")

    def pairs(self, *rows):
        path = os.path.join(self.dir, "pairs.txt")
        with open(path, "w") as f:
            f.write("# test pairs\n" + "".join(f"{label} {ref} {dis}\n"
                                               for label, ref, dis in rows))
        return path

    def run_tool(self, pairs, *extra, env=None):
        argv = [os.path.join(self.bin, "mirage_vmaf"), self.out, "--pairs", pairs,
                "--ffmpeg", os.path.join(self.bin, "ffmpeg"), *extra]
        stdout, stderr = io.StringIO(), io.StringIO()
        with mock.patch.dict(os.environ, env or {}), contextlib.redirect_stdout(stdout), \
                contextlib.redirect_stderr(stderr):
            rc = run_delta.main(argv)
        return rc, stdout.getvalue(), stderr.getvalue()

    def read(self, rel):
        with open(os.path.join(self.out, rel), encoding="utf-8") as f:
            return f.read()

    def report(self, rel="delta.json"):
        return json.loads(self.read(rel))

    def check_files(self):
        """SHA256SUMS covers every file and holds, and nothing names the temp dir."""
        sums = {}
        for line in self.read("SHA256SUMS").splitlines():
            digest, rel = line.split("  ", 1)
            sums[rel] = digest
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
        self.assertFalse([r for r in found if r.endswith(".log")], "uncompressed logs left")
        return found

    def test_plain_run(self):
        rc, out, err = self.run_tool(self.pairs(("same", REF, SAME), ("other", REF, OTHER)),
                                     "--frames", "4", "--jobs", "2")
        self.assertEqual(rc, 1, err)
        files = self.check_files()
        for rel in ("run.cmd.txt", "pairs.txt", "environment.txt", "delta.md", "delta.json",
                    "same/cmd.txt", "same/ffprobe_ref.json", "same/ffprobe_dis.json",
                    "same/mirage.json", "same/mirage.stdout.txt", "same/mirage.stderr.log.xz",
                    "same/ffmpeg.json", "same/ffmpeg.stderr.log.xz",
                    "same/ffmpeg_colorspace_lines.txt", "same/delta.md", "same/delta.json"):
            self.assertIn(rel, files)
        o = self.report()
        self.assertEqual((o["status"], o["pairs_failed"]), ("FAIL", ["other"]))
        self.assertEqual(o["parameters"]["frames"], 4)
        same, other = self.report("same/delta.json"), self.report("other/delta.json")
        self.assertEqual(same["status"], "PASS", same["fail_reasons"])
        self.assertEqual(same["frames"], {"mirage": 4, "ffmpeg": 4, "compared": 4,
                                          "counts_match": True})
        self.assertEqual(same["ffmpeg_color"]["conversions"], [])
        self.assertEqual(other["status"], "FAIL")
        self.assertEqual(other["fail_reasons"], ["4 of 4 frames have |delta| > 0.01"])
        self.assertEqual(other["ffmpeg_color"]["conversions"],
                         ["Parsed_scale_1: color_space smpte170m -> bt709"])
        self.assertIsNone(other["relabel"])

        cmd = self.read("other/cmd.txt")
        self.assertIn(f"$ <mirage_vmaf> --ref {REF} --dis {OTHER} --out mirage.json --frames 4 "
                      "> mirage.stdout.txt 2> mirage.stderr.log", cmd)
        self.assertIn("$ <ffmpeg> -hide_banner -nostdin -nostats -v verbose -i "
                      f"{OTHER} -i {REF} -an -lavfi '[0:v]trim=end_frame=4,scale=1920:1080:"
                      "flags=bilinear,format=yuv420p[d];[1:v]trim=end_frame=4,format=yuv420p[r];"
                      "[d][r]libvmaf=model=version=vmaf_v0.6.1:n_threads=4:log_fmt=json:"
                      "log_path=ffmpeg.json' -f null - > ffmpeg.stdout.txt 2> ffmpeg.stderr.log",
                      cmd)
        self.assertIn("$ eval/consistency/run_delta.sh <mirage_vmaf> <out_dir> --frames 4 "
                      "--jobs 2 --pairs <tmp>", self.read("run.cmd.txt"))
        self.assertIn("csp:smpte170m range:tv sar:0/1 -> w:1920 h:1080",
                      self.read("other/ffmpeg_colorspace_lines.txt"))
        env = self.read("environment.txt")
        self.assertIn("<ffmpeg> -hide_banner -version\nffmpeg version fake", env)
        self.assertIn("--help, first line: Usage: mirage_vmaf --ref", env)
        with open(os.path.join(self.out, "other/mirage.stderr.log.xz"), "rb") as f:
            self.assertEqual(lzma.decompress(f.read()), b"fake mirage_vmaf: a warning\n")
        self.assertIn("**Overall: FAIL**: 1 of 2 pairs fail.", self.read("delta.md"))
        self.assertIn("other: FAIL", out)

    def test_relabel_variant(self):
        rc, _, err = self.run_tool(self.pairs(("same", REF, SAME), ("other", REF, OTHER)),
                                   "--frames", "3", "--diagnostic-relabel")
        self.assertEqual(rc, 0, err)
        self.check_files()
        o = self.report()
        self.assertEqual((o["status"], o["variant"]), ("PASS", "diagnostic-relabel"))
        same, other = self.report("same/delta.json"), self.report("other/delta.json")
        self.assertIsNone(same["relabel"])
        self.assertEqual(other["relabel"], "setparams=colorspace=bt709")
        self.assertEqual(other["ffmpeg_color"]["conversions"], [])
        self.assertIn("[0:v]trim=end_frame=3,setparams=colorspace=bt709,scale=1920:1080:",
                      self.read("other/cmd.txt"))
        self.assertNotIn("setparams", self.read("same/cmd.txt"))
        self.assertIn("Variant: DIAGNOSTIC, not the section 5 comparison", self.read("delta.md"))

    def test_every_frame_and_a_count_mismatch(self):
        rc, _, _ = self.run_tool(self.pairs(("extra", REF, SAME + ":ffmpeg_extra_frame")))
        self.assertEqual(rc, 1)
        r = self.report("extra/delta.json")
        self.assertEqual(r["frames"]["mirage"], 10)   # the stand-ins' length
        self.assertEqual(r["frames"]["ffmpeg"], 11)
        self.assertTrue(r["fail_reasons"][0].startswith("FRAME COUNT MISMATCH"))
        self.assertNotIn("trim", self.read("extra/cmd.txt"))
        self.assertNotIn("--frames", self.read("extra/cmd.txt"))
        self.assertIn("| extra | FAIL | 10 / 11 **MISMATCH** |", self.read("delta.md"))

    def test_small_offsets_and_the_threshold(self):
        pairs = self.pairs(("off", REF, SAME + ":mirage_offset=0.02"))
        rc, _, _ = self.run_tool(pairs, "--frames", "2")
        self.assertEqual(rc, 1)
        self.assertEqual(self.report("off/delta.json")["delta"]["over_threshold"], 2)
        self.out = os.path.join(self.dir, "out2")
        rc, _, _ = self.run_tool(pairs, "--frames", "2", "--threshold", "0.05")
        self.assertEqual(rc, 0)

    def test_scaler_is_checked_and_forwarded(self):
        pairs = self.pairs(("same", REF, SAME))
        rc, _, _ = self.run_tool(pairs, "--frames", "2", "--scaler", "bicubic")
        self.assertEqual(rc, 1)
        self.assertIn("SETTINGS MISMATCH: mirage_vmaf's report has scaler 'bilinear', "
                      "FFmpeg's run 'bicubic'", self.report("same/delta.json")["fail_reasons"])
        # A mirage_vmaf whose --help lists --scaler is given the scaler.
        self.out = os.path.join(self.dir, "out2")
        rc, _, err = self.run_tool(pairs, "--frames", "2", "--scaler", "bicubic",
                                   env={"FAKE_MIRAGE_HAS_SCALER": "1"})
        self.assertEqual(rc, 0, err)
        self.assertIn("--frames 2 --scaler bicubic >", self.read("same/cmd.txt"))

    def test_model_file_is_forwarded_and_checked(self):
        model = os.path.join(self.dir, "models", "my_model.json")
        os.mkdir(os.path.dirname(model))
        with open(model, "w") as f:
            f.write('{"model_dict": {}}\n')
        with open(model, "rb") as f:
            sha = hashlib.sha256(f.read()).hexdigest()
        # The outputs show the temporary directory, where the file is, as <tmp>.
        tmp = os.path.realpath(os.environ.get("TMPDIR") or "/tmp")
        shown = "<tmp>/" + os.path.relpath(os.path.realpath(model), tmp)
        pairs = self.pairs(("same", REF, SAME))
        rc, _, err = self.run_tool(pairs, "--frames", "2", "--model-file", model,
                                   env={"FAKE_MIRAGE_HAS_MODEL": "1"})
        self.assertEqual(rc, 0, err)
        self.check_files()
        cmd = self.read("same/cmd.txt")
        self.assertIn(f"libvmaf=model=path={shown}:n_threads=4", cmd)
        self.assertIn(f"--frames 2 --model-file {shown} >", cmd)
        o = self.report()
        self.assertEqual(o["parameters"]["model_file"], {"path": shown, "sha256": sha})
        self.assertIsNone(o["parameters"]["model"])
        self.assertIn(f"--model-file {shown} --threads 4", self.read("run.cmd.txt"))
        r = self.report("same/delta.json")
        self.assertEqual((r["mirage_vmaf"]["model_source"], r["mirage_vmaf"]["model_sha256"]),
                         ("file", sha))
        self.assertEqual(r["mirage_vmaf"]["reference_mode"], "explicit-pair")
        # A mirage_vmaf without --model-file keeps its built-in model: a mismatch.
        self.out = os.path.join(self.dir, "out2")
        rc, _, _ = self.run_tool(pairs, "--frames", "2", "--model-file", model)
        self.assertEqual(rc, 1)
        self.assertIn("SETTINGS MISMATCH: mirage_vmaf's report has model_source None, "
                      "FFmpeg's run 'file'", self.report("same/delta.json")["fail_reasons"])

    def test_model_options_are_checked(self):
        pairs = self.pairs(("same", REF, SAME))
        with self.assertRaises(SystemExit) as cm, \
                contextlib.redirect_stderr(io.StringIO()):
            self.run_tool(pairs, "--model", "vmaf_v0.6.1", "--model-file", "x.json")
        self.assertEqual(cm.exception.code, 2)
        rc, _, err = self.run_tool(pairs, "--model-file", os.path.join(self.dir, "a:b.json"))
        self.assertEqual(rc, 2)
        self.assertIn("a character with a meaning in a filter graph", err)
        rc, _, err = self.run_tool(pairs, "--model-file", os.path.join(self.dir, "missing.json"))
        self.assertEqual(rc, 2)
        self.assertIn("missing.json", err)
        # The default model, given to both sides when mirage_vmaf takes --model.
        rc, _, err = self.run_tool(pairs, "--frames", "2", env={"FAKE_MIRAGE_HAS_MODEL": "1"})
        self.assertEqual(rc, 0, err)
        self.assertIn("--frames 2 --model vmaf_v0.6.1 >", self.read("same/cmd.txt"))
        self.assertIn("libvmaf 3.0.0 at run time (its libvmaf logs), package unknown.",
                      self.read("delta.md"))

    def test_large_json_outputs_are_compressed(self):
        with mock.patch.object(run_delta, "COMPRESS_JSON_ABOVE", 200):
            rc, _, err = self.run_tool(self.pairs(("same", REF, SAME)), "--frames", "5")
        self.assertEqual(rc, 0, err)
        files = self.check_files()
        for name in ("mirage.json", "ffmpeg.json"):
            self.assertNotIn(f"same/{name}", files)
            with open(os.path.join(self.out, "same", name + ".xz"), "rb") as f:
                data = json.loads(lzma.decompress(f.read()))
            self.assertEqual(len(data["frames"]), 5)
        self.assertIn("same/ffprobe_ref.json", files)   # not a scoring output: as it is
        self.assertEqual(self.report("same/delta.json")["frames"]["compared"], 5)

    def test_failing_mirage_vmaf(self):
        rc, _, _ = self.run_tool(self.pairs(("bad", REF, SAME + ":mirage_fails")), "--frames", "2")
        self.assertEqual(rc, 1)
        r = self.report("bad/delta.json")
        self.assertEqual(r["fail_reasons"], ["mirage_vmaf exited with status 1"])
        self.check_files()

    def test_out_dir_must_be_new_or_empty(self):
        os.mkdir(self.out)
        open(os.path.join(self.out, "x"), "w").close()
        rc, _, err = self.run_tool(self.pairs(("same", REF, SAME)))
        self.assertEqual(rc, 2)
        self.assertIn("is not an empty directory", err)

    def test_bad_pairs_file(self):
        rc, _, err = self.run_tool(self.pairs(("a/b", REF, SAME)))
        self.assertEqual(rc, 2)
        self.assertIn("may only use", err)


if __name__ == "__main__":
    unittest.main()
