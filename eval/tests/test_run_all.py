"""Unit tests of eval/run_all.py: its steps against the committed runs, the
comparisons, and a whole run with stand-ins for mirage_vmaf, ffmpeg and
ffprobe (eval/consistency/tests/fake_tools.py), without network.

python3 -I -B -m unittest discover -s eval/tests -v
"""

import contextlib
import io
import json
import lzma
import os
import shlex
import shutil
import stat
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
EVAL = os.path.dirname(HERE)
sys.dont_write_bytecode = True
sys.path.insert(0, EVAL)
import run_all as ra  # noqa: E402

FAKE_TOOLS = os.path.join(EVAL, "consistency", "tests", "fake_tools.py")
RECORDED_ONLY = {"--jobs", "--ffmpeg", "--ffprobe", "--timeout"}


def recorded_options(path):
    """The options of a committed run.cmd.txt, without those run_all.py sets
    from its own arguments."""
    with open(path, encoding="utf-8") as f:
        line = [x for x in f if x.startswith("$ ")][-1]
    argv = shlex.split(line[2:])
    assert argv[:3] == ["eval/consistency/run_delta.sh", "<mirage_vmaf>", "<out_dir>"], argv
    out, rest = [], argv[3:]
    while rest:
        a = rest.pop(0)
        if a in RECORDED_ONLY:
            rest.pop(0)
        elif a.startswith("--") and rest and not rest[0].startswith("--"):
            out += [a, rest.pop(0)]
        else:
            out.append(a)
    return out


def as_options(argv):
    """Options as {name: value}, flags as {name: True}."""
    out, rest = {}, list(argv)
    while rest:
        a = rest.pop(0)
        out[a] = rest.pop(0) if rest and not rest[0].startswith("--") else True
    return out


def write_json(path, data, compress=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    text = json.dumps(data)
    if compress:
        with lzma.open(path + ".xz", "wt", encoding="utf-8") as f:
            f.write(text)
    else:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)


def delta_run(root, statuses, scores=(1.0, 2.0, 3.0), compress=False):
    """A run_delta.sh output directory with one pair per status."""
    write_json(os.path.join(root, "delta.json"), {
        "status": "PASS" if all(s == "PASS" for s in statuses.values()) else "FAIL",
        "pairs": [{"pair": p, "status": s} for p, s in statuses.items()]})
    for p in statuses:
        write_json(os.path.join(root, p, "mirage.json"),
                   {"frames": [{"index": i, "vmaf": v} for i, v in enumerate(scores)]},
                   compress)
        write_json(os.path.join(root, p, "ffmpeg.json"),
                   {"fps": 9.9, "frames": [{"frameNum": i, "metrics": {"vmaf": round(v, 6),
                                                                       "adm2": 0.5}}
                                           for i, v in enumerate(scores)]})


class StepsAgainstTheCommittedRuns(unittest.TestCase):
    def test_every_delta_run_has_the_options_of_its_run_cmd(self):
        fake = mock.Mock(out="/x", args=mock.Mock(model_dir=ra.DEFAULT_MODEL_DIR))
        for s in ra.STEPS:
            for run in s.runs:
                path = os.path.join(EVAL, s.committed, run.name, "run.cmd.txt")
                with self.subTest(step=s.name, run=run.name):
                    expected = as_options(recorded_options(path))
                    mine = [o.replace("{clips}", "<tmp>/clips")
                            .replace("{format_clips}", "<tmp>/format_clips") for o in run.options]
                    self.assertEqual(as_options(ra.expand(mine, fake)), expected)

    def test_every_committed_measurement_has_a_step(self):
        covered = {os.path.join(s.committed, run.name) for s in ra.STEPS for run in s.runs}
        found = set()
        for dirpath, _, names in os.walk(os.path.join(EVAL, "consistency")):
            if "run.cmd.txt" in names:
                found.add(os.path.relpath(dirpath, EVAL))
        self.assertTrue(found)
        self.assertEqual(found - covered, set(), "committed runs that run_all.py misses")
        self.assertEqual(covered - found, set(), "steps without a committed run")
        records = {s.committed for s in ra.STEPS if s.kind == "ffmpeg-record"}
        for name in os.listdir(os.path.join(EVAL, "consistency")):
            d = os.path.join("consistency", name)
            if os.path.isfile(os.path.join(EVAL, d, "checks.txt")) and \
                    not any(n.startswith(d + os.sep) for n in found):
                self.assertIn(d, records)
        for name in os.listdir(os.path.join(EVAL, "pooling")):
            if os.path.isfile(os.path.join(EVAL, "pooling", name, "pooling.json")):
                self.assertIn(f"pooling/{name}", {s.committed for s in ra.STEPS})

    def test_the_committed_outputs_of_every_step_exist(self):
        for s in ra.STEPS:
            with self.subTest(step=s.name):
                self.assertTrue(os.path.exists(os.path.join(EVAL, s.committed)))

    def test_ffmpeg_record_commands_are_those_of_checks_txt(self):
        include = os.path.join(os.path.dirname(os.path.dirname(ra.DEFAULT_FFMPEG)), "include")
        for name, commands in ra.FFMPEG_RECORDS.items():
            with open(os.path.join(EVAL, "consistency", name, "checks.txt")) as f:
                text = f.read()
            for n, template in commands.items():
                with self.subTest(record=name, check=n):
                    block = text.split(f"\n# {n}.", 1)[1].split("\n# ", 1)[0]
                    committed = [x[2:] for x in block.splitlines() if x.startswith("$ ")][0]
                    self.assertEqual(template.format(ffmpeg=shlex.quote(ra.DEFAULT_FFMPEG),
                                                     include=shlex.quote(include)), committed)

    def test_steps_needed_are_added(self):
        names = lambda **kw: [s.name for s in ra.selected_steps(**kw)]  # noqa: E731
        self.assertEqual(names(only=["pooling"], skip_before=False),
                         ["diag-after-300", "pooling"])
        self.assertEqual(names(only=["color-cases-after"], skip_before=False),
                         ["clips", "color-cases-after"])
        self.assertEqual(names(only=["format-cases-after"], skip_before=False),
                         ["format-clips", "format-cases-after"])
        self.assertEqual(names(only=["format-cases-d022"], skip_before=False),
                         ["format-clips", "format-cases-d022"])
        self.assertNotIn("format-cases-before", names(only=None, skip_before=True))
        self.assertEqual(names(only=None, skip_before=False), list(ra.STEP_NAMES))
        self.assertNotIn("diag-before-300", names(only=None, skip_before=True))
        self.assertNotIn("color-cases-before", names(only=None, skip_before=True))
        self.assertEqual(names(only=["pooling-full"], skip_before=False),
                         ["acceptance-full", "pooling-full"])
        rest = names(only=None, skip_before=False, skip_full=True)
        self.assertEqual(rest, [n for n in ra.STEP_NAMES if n not in ra.FULL_LENGTH])
        self.assertIn("pooling", rest)

    def test_each_delta_step_has_the_program_of_its_build_txt(self):
        # The commit a committed run's build.txt names, for the programs built
        # from a fixed commit; the head's runs name the commit they were made at.
        programs = {name: (option, commit) for name, option, commit, _ in ra.PROGRAMS}
        for s in ra.STEPS:
            if s.kind != "delta":
                continue
            with self.subTest(step=s.name):
                self.assertIn(s.binary, programs)
                commit = programs[s.binary][1]
                with open(os.path.join(EVAL, s.committed, "build.txt"), encoding="utf-8") as f:
                    text = f.read()
                if commit:
                    self.assertIn(commit[:7], text)   # some name it by its short hash
        self.assertEqual([s.name for s in ra.STEPS if s.binary == "before-d022"],
                         ["format-cases-after"])
        self.assertEqual(ra.parse_args(["out", "--mirage-vmaf-before-d022", "p"])
                         .mirage_vmaf_before_d022, "p")

    def test_clips_steps_and_their_sets(self):
        clips = {s.name: s for s in ra.STEPS if s.kind == "clips"}
        self.assertEqual(set(clips), {"clips", "format-clips"})
        self.assertEqual(clips["clips"].options, ())
        self.assertEqual(clips["format-clips"].options, ("--set", "formats"))
        fake = mock.Mock(out="/o", args=mock.Mock(model_dir="/m"))
        self.assertEqual(ra.expand(["{clips}/pairs.txt", "{format_clips}/pairs.txt"], fake),
                         ["/o/consistency/clips/pairs.txt",
                          "/o/consistency/format_clips/pairs.txt"])

    def test_a_full_length_run_has_no_frame_limit(self):
        self.assertNotIn("--frames", ra.delta_options(None))
        self.assertEqual(ra.delta_options(300)[:2], ["--frames", "300"])

    def test_pooling_steps_read_the_reruns_of_the_steps_they_need(self):
        outs = {s.name: s.out for s in ra.STEPS}
        for s in ra.STEPS:
            if s.kind != "pooling":
                continue
            with self.subTest(step=s.name):
                reruns = [a.split("{out}/", 1)[1] for a in ra.POOLING_SOURCES[s.name]
                          if a.startswith("{out}/")]
                self.assertTrue(reruns)
                for rel in reruns:
                    self.assertTrue(any(rel.startswith(outs[n] + "/") for n in s.needs), rel)

    def test_only_rejects_unknown_steps(self):
        with mock.patch("sys.stderr"):
            self.assertRaises(SystemExit, ra.parse_args, ["out", "--only", "nonsense"])
        self.assertEqual(ra.parse_args(["out", "--only", "clips,pooling"]).only,
                         ["clips", "pooling"])
        self.assertTrue(ra.parse_args(["out", "--skip-full"]).skip_full)


class Redaction(unittest.TestCase):
    def test_paths_and_their_cmake_variable_forms(self):
        with tempfile.TemporaryDirectory() as d:
            wt = os.path.join(d, "run-all", "head-worktree")
            r = ra.Redactor([(wt, "<worktree>"), (d, "<tmp>")])
            mangled = "".join(c if c.isalnum() else "_" for c in os.path.realpath(wt))
            text = (f"cd {wt}/build && ls {os.path.realpath(d)}/x\n"
                    f"-- Performing Test HAVE_FLAG__ffile_prefix_map_{mangled}_build__deps__")
            out, _ = r.text(text)
            self.assertEqual(out, "cd <worktree>/build && ls <tmp>/x\n"
                                  "-- Performing Test HAVE_FLAG__ffile_prefix_map_<worktree>"
                                  "_build__deps__")
            self.assertEqual(r.data(text.encode())[0], out.encode())
        # A one-component path only as itself, not as a word.
        r = ra.Redactor([("/tmp", "<tmp>")])
        self.assertEqual(r.text("a_tmp_b /tmp/c")[0], "a_tmp_b <tmp>/c")


class Comparisons(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.new = os.path.join(self.tmp.name, "new")
        self.old = os.path.join(self.tmp.name, "old")

    def test_identical_delta_runs(self):
        delta_run(self.new, {"a": "PASS", "b": "FAIL"})
        delta_run(self.old, {"a": "PASS", "b": "FAIL"}, compress=True)
        c = ra.compare_delta_run(self.new, self.old)
        self.assertTrue(c["identical"])
        self.assertEqual([p["pair"] for p in c["pairs"]], ["a", "b"])
        self.assertEqual(c["pairs"][0]["mirage"]["rerun"], 3)

    def test_a_changed_score_a_verdict_and_a_missing_pair(self):
        delta_run(self.old, {"a": "PASS", "b": "PASS"})
        delta_run(self.new, {"a": "PASS", "b": "PASS"}, scores=(1.0, 2.5, 3.0))
        c = ra.compare_delta_run(self.new, self.old)
        self.assertFalse(c["identical"])
        m = c["pairs"][0]["mirage"]
        self.assertEqual((m["differing"], m["largest_difference"]), (1, 0.5))
        self.assertEqual(c["pairs"][0]["ffmpeg"]["differing"], 1)

        shutil.rmtree(self.new)
        delta_run(self.new, {"a": "FAIL"})
        c = ra.compare_delta_run(self.new, self.old)
        self.assertFalse(c["pairs"][0]["identical"])     # the verdict differs
        self.assertFalse(c["pairs"][1]["identical"])     # the pair is missing

    def test_reports_both_sides_left_out_alike(self):
        delta_run(self.old, {"a": "FAIL"})
        delta_run(self.new, {"a": "FAIL"})
        for root in (self.old, self.new):
            os.remove(os.path.join(root, "a", "ffmpeg.json"))
        self.assertTrue(ra.compare_delta_run(self.new, self.old)["identical"])
        os.remove(os.path.join(self.new, "a", "mirage.json"))
        c = ra.compare_delta_run(self.new, self.old)
        self.assertFalse(c["identical"])
        self.assertEqual(c["pairs"][0]["mirage"]["committed"], "present")

    def test_effect_ignores_settings_only(self):
        pairs = [{"pair": "a", "base": {"settings": {"model": "/x/m.json"}, "pooled_mean": 1},
                  "runs": {"r": {"settings": {"model": "/x/m.json"}, "mirage": {"mean": 2}}}}]
        write_json(os.path.join(self.old, "e.json"),
                   {"pairs": pairs, "parameters_that_differ": ["model"]})
        moved = json.loads(json.dumps(pairs))
        moved[0]["base"]["settings"]["model"] = "/y/m.json"
        write_json(os.path.join(self.new, "e.json"),
                   {"pairs": moved, "parameters_that_differ": ["model"]})
        new, old = os.path.join(self.new, "e.json"), os.path.join(self.old, "e.json")
        self.assertTrue(ra.compare_effect(new, old)["identical"])
        moved[0]["runs"]["r"]["mirage"]["mean"] = 3
        write_json(new, {"pairs": moved, "parameters_that_differ": ["model"]})
        self.assertEqual(ra.compare_effect(new, old)["differing"], ["pairs"])

    def test_pooling_compares_series_not_inputs(self):
        src = {"kind": "delta-run", "label": "The run <a>", "inputs": [{"sha256": "1"}],
               "skipped": [], "series": [{"label": "x", "stats": {"mean": 1.5}}]}
        write_json(os.path.join(self.old, "p.json"), {"sources": [src]})
        other = dict(src, label="The run <b>", inputs=[{"sha256": "2"}])
        write_json(os.path.join(self.new, "p.json"), {"sources": [other]})
        new, old = os.path.join(self.new, "p.json"), os.path.join(self.old, "p.json")
        self.assertTrue(ra.compare_pooling(new, old)["identical"])
        write_json(new, {"sources": [dict(other, series=[{"label": "x",
                                                          "stats": {"mean": 1.6}}])]})
        self.assertEqual(ra.compare_pooling(new, old)["differing_sources"], [0])

    def test_streams(self):
        rep = {"streams": [{"master": {"file": "m", "sha256": "1"},
                            "playlists": [{"file": "p", "sha256": "2",
                                           "probe": {"stream": {"width": 640}}}]}]}
        write_json(os.path.join(self.old, "s.json"), rep)
        write_json(os.path.join(self.new, "s.json"), rep)
        new, old = os.path.join(self.new, "s.json"), os.path.join(self.old, "s.json")
        self.assertTrue(ra.compare_streams(new, old)["identical"])
        rep["streams"][0]["playlists"][0]["probe"]["stream"]["width"] = 641
        write_json(new, rep)
        self.assertEqual(ra.compare_streams(new, old)["differing"], ["p"])

    def test_ffmpeg_records_ignore_addresses(self):
        committed = ("# Head.\n\n# 1. git\n\n$ git rev-parse HEAD\nabc\n\n"
                     "# 2. version\n\n$ ffmpeg -version | head -n 1\nffmpeg version 8\n\n"
                     "# 3. errors\n\n$ ffmpeg ...\n[a] [enc @ 0x96ec501c0] x\nexit status 234\n")
        rerun = ("# The commands.\n\n# 2.\n\n$ <ffmpeg> -version | head -n 1\nffmpeg version 8"
                 "\n\n# 3.\n\n$ <ffmpeg> ...\n[a] [enc @ 0x1234abcd] x\nexit status 234\n")
        os.makedirs(self.old)
        os.makedirs(self.new)
        old, new = os.path.join(self.old, "checks.txt"), os.path.join(self.new, "record.txt")
        with open(old, "w") as f:
            f.write(committed)
        with open(new, "w") as f:
            f.write(rerun)
        self.assertEqual(ra.checks_blocks(committed)[1], "abc")
        self.assertTrue(ra.compare_ffmpeg_record(new, old, [2, 3])["identical"])
        with open(new, "w") as f:
            f.write(rerun.replace("234", "0"))
        self.assertEqual(ra.compare_ffmpeg_record(new, old, [2, 3])["differing"], [3])

    def test_committed_binary(self):
        os.makedirs(os.path.join(self.old, "x"))
        with open(os.path.join(self.old, "x", "build.txt"), "w") as f:
            f.write("$ shasum -a 256 build/macos-release/mirage_vmaf\n" + "ab" * 32 +
                    "  build/macos-release/mirage_vmaf\n")
        r = mock.Mock(args=mock.Mock(committed=self.old))
        self.assertEqual(ra.committed_binary(r, ra.step("x", "delta", committed="x")), "ab" * 32)
        self.assertIsNone(ra.committed_binary(r, ra.step("y", "delta", committed="y")))


class WholeRun(unittest.TestCase):
    """main() with one delta step over fake inputs, the stand-ins as programs."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = os.path.realpath(self.tmp.name)
        self.bin = os.path.join(self.dir, "bin")
        os.mkdir(self.bin)
        for tool in ("mirage_vmaf", "ffmpeg", "ffprobe"):
            path = os.path.join(self.bin, tool)
            with open(path, "w") as f:
                f.write(f'#!/bin/sh\nexec "{sys.executable}" -I "{FAKE_TOOLS}" {tool} "$@"\n')
            os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
        pairs = os.path.join(self.dir, "pairs.txt")
        with open(pairs, "w") as f:
            f.write("same fake:1920x1080:bt709 fake:960x540:bt709\n"
                    "other fake:1920x1080:bt709 fake:640x360:smpte170m\n")
        self.steps = (ra.step("diag-after-300", "delta", "head",
                              committed="consistency/diag-after-300",
                              runs=[ra.Run("plain", ra.delta_options(5, pairs=pairs))]),)

    def main(self, out, *extra):
        argv = [out, "--mirage-vmaf", os.path.join(self.bin, "mirage_vmaf"),
                "--ffmpeg", os.path.join(self.bin, "ffmpeg"), "--work-dir",
                os.path.join(self.dir, "work"), *extra]
        with mock.patch.object(ra, "STEPS", self.steps), \
                mock.patch.object(ra, "STEP_NAMES", ("diag-after-300",)), \
                contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            return ra.main(argv)

    def test_a_rerun_matches_its_own_outputs_and_finds_a_change(self):
        first = os.path.join(self.dir, "first")
        self.assertEqual(self.main(first, "--no-compare"), 0)
        self.assertTrue(os.path.isfile(os.path.join(first, "consistency", "diag-after-300",
                                                    "plain", "delta.json")))
        self.assertFalse(os.path.exists(os.path.join(first, "rerun_check.md")))
        with open(os.path.join(first, "run_all.json")) as f:
            run = json.load(f)
        self.assertEqual([c["exit_code"] for c in run["commands"]], [1])  # a pair fails
        self.assertTrue(run["commands"][0]["command"].startswith(
            "eval/consistency/run_delta.sh <tmp>/"), run["commands"][0]["command"])
        self.assertIn(" <out_dir>/consistency/diag-after-300/plain --frames 5 ",
                      run["commands"][0]["command"])
        self.assertNotIn(self.dir, json.dumps(run))

        committed = os.path.join(first)   # the first run serves as the committed outputs
        second = os.path.join(self.dir, "second")
        self.assertEqual(self.main(second, "--committed", committed), 0)
        with open(os.path.join(second, "rerun_check.json")) as f:
            check = json.load(f)
        self.assertTrue(check["identical"])
        pairs = check["steps"][0]["comparison"]["runs"]["plain"]["pairs"]
        self.assertEqual([(p["pair"], p["status"]["rerun"]) for p in pairs],
                         [("same", "PASS"), ("other", "FAIL")])
        with open(os.path.join(second, "rerun_check.md")) as f:
            self.assertIn("Everything compared is as committed.", f.read())

        # A committed score changed: the rerun no longer matches.
        report = os.path.join(committed, "consistency", "diag-after-300", "plain", "same",
                              "mirage.json")
        with open(report) as f:
            data = json.load(f)
        data["frames"][2]["vmaf"] += 0.25
        with open(report, "w") as f:
            json.dump(data, f)
        third = os.path.join(self.dir, "third")
        self.assertEqual(self.main(third, "--committed", committed), 1)
        with open(os.path.join(third, "rerun_check.md")) as f:
            md = f.read()
        self.assertIn("NOT everything is as committed", md)
        self.assertIn("1 differ, largest 0.250000", md)

    def test_out_dir_inside_the_repository_is_refused(self):
        with mock.patch("sys.stderr"):
            self.assertEqual(ra.main([os.path.join(EVAL, "no_such_dir")]), 2)
        self.assertFalse(os.path.exists(os.path.join(EVAL, "no_such_dir")))


if __name__ == "__main__":
    unittest.main()
