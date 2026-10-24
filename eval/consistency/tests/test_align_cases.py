"""Unit tests of eval/consistency/align_cases.py; no clip is made (the
programs are replaced by a stand-in for run()).

python3 -I -B -m unittest discover -s eval/consistency/tests -v
"""

import contextlib
import io
import os
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(HERE))
import align_cases as ac  # noqa: E402
import vmafdelta as vd  # noqa: E402


class Cases(unittest.TestCase):
    def test_the_cases(self):
        self.assertEqual(len({c.label for c in ac.CASES}), len(ac.CASES))
        for c in ac.CASES:
            self.assertEqual((c.ref.size, c.dis.size), (ac.REF_SIZE, ac.DIS_SIZE), c.label)
            self.assertTrue(c.ref.name.startswith("ref") and c.dis.name.startswith("dis"))
        # The six kinds the section 1.4 work names, and the controls.
        kinds = {c.label.split("_", 1)[1] for c in ac.CASES}
        self.assertEqual(kinds, {"aligned", "dropped_distorted", "dropped_reference",
                                 "duplicated_timestamp", "half_rate", "later_start",
                                 "later_start_audio", "restart", "ms_vs_90khz"})
        clips = ac.all_clips()
        self.assertEqual(len({x.name for x in clips}), len(clips))
        # The full clips come first: the others' pictures are checked against them.
        self.assertEqual(clips[:4], list(ac.FULL.values()))

    def test_frames_of_the_clips(self):
        self.assertEqual(ac.DIS_DROP5.frames, tuple((k, k) for k in (0, 1, 2, 3, 4, 6, 7, 8, 9)))
        self.assertEqual(ac.DIS_DUP5.frames[5:7], ((5, 5), (5, 5)))
        self.assertEqual(len(ac.DIS_DUP5.frames), 11)
        self.assertEqual(ac.DIS_HALF.frames, ((0, 0), (2, 2), (4, 4), (6, 6), (8, 8)))
        self.assertEqual(ac.DIS_FROM3.frames[0], (3, 3))
        self.assertTrue(ac.DIS_FROM3_AUDIO.audio)
        self.assertEqual(ac.DIS_TS_RESTART5.frames[4:6], ((4, 4), (5, 0)))
        self.assertEqual(len(ac.DIS_TS_RESTART5.parts), 2)

    def test_pairs_file_reads_back(self):
        text = ac.pairs_text(ac.CASES, "/clips", "Title.")
        pairs = vd.parse_pairs(text)
        self.assertEqual([p.label for p in pairs], [c.label for c in ac.CASES])
        self.assertEqual(pairs[1].dis, "/clips/dis_drop5.mkv")
        self.assertEqual(pairs[7], vd.Pair("a07_restart", "/clips/ref_ts.ts",
                                           "/clips/dis_ts_restart5.ts"))
        self.assertTrue(text.startswith("# Title.\n"))


class Commands(unittest.TestCase):
    def test_a_plain_clip(self):
        [argv] = ac.make_commands("ff", ac.DIS, "/c/dis.mkv")
        self.assertEqual(argv, ["ff", "-hide_banner", "-nostdin", "-v", "error", "-f", "lavfi",
                                "-i", "testsrc2=size=1920x1080:rate=30000/1001", "-vf",
                                "trim=end_frame=10,scale=640:360:flags=bicubic,format=yuv420p,"
                                "setparams=colorspace=bt709:range=tv", "-c:v", "rawvideo",
                                "-f", "matroska", "/c/dis.mkv"])
        [ref] = ac.make_commands("ff", ac.REF_DROP5, "/c/r.mkv")
        self.assertIn("trim=end_frame=10,format=yuv420p,setparams=colorspace=bt709:range=tv,"
                      "select=not(eq(n\\,5))", ref)   # the reference keeps its size

    def test_the_duplicate_keeps_both_frames(self):
        [argv] = ac.make_commands("ff", ac.DIS_DUP5, "/c/d.mkv")
        self.assertIn(",loop=loop=1:size=1:start=6,setpts=(N-gt(N\\,5))/(FRAME_RATE*TB)",
                      argv[argv.index("-vf") + 1])
        self.assertEqual(argv[argv.index("-fps_mode") + 1], "passthrough")

    def test_audio_from_0(self):
        [argv] = ac.make_commands("ff", ac.DIS_FROM3_AUDIO, "/c/a.mkv")
        self.assertIn("anullsrc=r=48000:cl=mono", argv)
        graph = argv[argv.index("-filter_complex") + 1]
        self.assertTrue(graph.endswith(",select=gte(n\\,3)[v];[1:a]atrim=end_sample=16016[a]"))
        self.assertEqual(argv[argv.index("-c:a") + 1], "pcm_s16le")
        self.assertNotIn("-vf", argv)

    def test_a_clip_in_two_parts(self):
        one, two = ac.make_commands("ff", ac.DIS_TS_RESTART5, "/c/x.ts")
        self.assertEqual((one[-1], two[-1]), ("/c/x.ts.part1", "/c/x.ts.part2"))
        self.assertTrue(one[one.index("-vf") + 1].startswith("trim=end_frame=5,scale="))
        self.assertTrue(two[two.index("-vf") + 1].startswith(
            "trim=start_frame=5:end_frame=10,setpts=PTS-STARTPTS,scale="))
        self.assertNotIn("-mpegts_flags", one)
        self.assertEqual(two[two.index("-mpegts_flags") + 1], "+initial_discontinuity")
        self.assertEqual(one[one.index("-c:v"):-1], ["-c:v", "mpeg2video", "-q:v", "2", "-g", "1",
                                                     "-bf", "0", "-f", "mpegts"])

    def test_reading_the_records(self):
        self.assertEqual(ac.parse_times("129003,\n132006,\n\nN/A\n"), [129003, 132006, None])
        text = ("#format: frame checksums\n#tb 0: 1001/30000\n"
                "0,          0,          0,        1,   345600, 8dffcfb8\n"
                "0,          1,          1,        1,   345600, f20ff0b5\n")
        self.assertEqual(ac.parse_md5s(text), ["8dffcfb8", "f20ff0b5"])


class Checks(unittest.TestCase):
    def test_expected_times(self):
        # Matroska: each slot's time in milliseconds, as its muxer rounds it.
        self.assertEqual([ac.ms(s) for s in range(10)],
                         [0, 33, 67, 100, 133, 167, 200, 234, 267, 300])
        self.assertEqual(ac.expected_times(ac.DIS_FROM3, [])[:2], [100, 133])
        # MPEG-TS: 3003 ticks a slot from the clip's first frame.
        self.assertEqual(ac.expected_times(ac.DIS_TS_RESTART5, [129003])[3:7],
                         [129003 + 3 * 3003, 129003 + 4 * 3003, 129003, 129003 + 3003])

    def test_frame_problems(self):
        pictures = [f"p{k}" for k in range(10)]
        md5s = [f"p{p}" for p, _ in ac.DIS_DROP5.frames]
        times = ac.expected_times(ac.DIS_DROP5, [])
        self.assertEqual(ac.frame_problems(ac.DIS_DROP5, times, md5s, pictures), [])
        wrong = md5s[:5] + ["p5"] + md5s[6:]
        self.assertEqual(ac.frame_problems(ac.DIS_DROP5, times, wrong, pictures),
                         ["dis_drop5: frame 5 is not picture 6"])
        late = times[:2] + [68] + times[3:]
        self.assertEqual(ac.frame_problems(ac.DIS_DROP5, late, md5s, pictures),
                         ["dis_drop5: frame 2 has pts 68, not 67 (slot 2)"])
        self.assertEqual(ac.frame_problems(ac.DIS_DROP5, times, md5s[:-1], pictures),
                         ["dis_drop5: 8 frames (MD5s) and 9 timestamps, not 9"])


class Main(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = os.path.join(self.tmp.name, "clips")

    def fake_run(self, wrong_picture=None):
        """A stand-in for align_cases.run: writes the file a command makes, and
        reads back each clip as intended, with MD5 "<container>-<width>-<picture>"
        (another picture for frame 0 of the clip named `wrong_picture`)."""
        clips = {ac.file_name(x): x for x in ac.all_clips()}

        def run(argv):
            if "lavfi" in argv:
                with open(argv[-1], "wb") as f:
                    f.write(b"part")
                return ""
            path = argv[-1] if argv[-1] != "-" else argv[argv.index("-i") + 1]
            x = clips[os.path.basename(path)]
            if "framemd5" in argv:
                pics = [p for p, _ in x.frames]
                if x.name == wrong_picture:
                    pics[0] = 9
                return "#format: frame checksums\n" + "".join(
                    f"0, {k}, {k}, 1, 1, {x.container}-{x.size[0]}-{p}\n"
                    for k, p in enumerate(pics))
            if "frame=pts" in argv:
                return "".join(f"{t}\n" for t in ac.expected_times(x, [129003]))
            return f"start_time=0.000000\nwidth={x.size[0]}\n"
        return run

    def main(self, **kw):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(ac, "run", self.fake_run(**kw)), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = ac.main([self.dir, "--ffmpeg", "/opt/ff/ffmpeg"])
        return rc, out.getvalue(), err.getvalue()

    def test_writes_clips_pairs_and_record(self):
        rc, out, err = self.main()
        self.assertEqual(rc, 0, err)
        names = sorted(os.listdir(self.dir))
        self.assertEqual(sorted(n for n in names if n.endswith((".mkv", ".ts"))),
                         sorted(ac.file_name(x) for x in ac.all_clips()))
        self.assertFalse([n for n in names if ".part" in n])   # the parts are joined
        with open(os.path.join(self.dir, "dis_ts_restart5.ts"), "rb") as f:
            self.assertEqual(f.read(), b"partpart")
        with open(os.path.join(self.dir, "pairs.txt")) as f:
            self.assertEqual([p.label for p in vd.parse_pairs(f.read())],
                             [c.label for c in ac.CASES])
        with open(os.path.join(self.dir, "clips.txt")) as f:
            record = f.read()
        self.assertNotIn(self.dir, record)
        self.assertTrue(record.startswith("# The clips of eval/consistency/align_cases.py, made "
                                          "in <clips_dir>"))
        self.assertIn("$ cat <clips_dir>/dis_ts_restart5.ts.part1 <clips_dir>/dis_ts_restart5.ts"
                      ".part2 > <clips_dir>/dis_ts_restart5.ts", record)
        self.assertIn("frames as checked (picture@slot): 0@0 1@1 2@2 3@3 4@4 5@5 5@5 6@6", record)
        self.assertIn("$ <ffprobe> -v error -select_streams v:0 -show_entries frame=pts", record)
        self.assertIn(f"{len(ac.all_clips())} clips, {len(ac.CASES)} cases", out)

    def test_a_wrong_picture_fails(self):
        rc, _, err = self.main(wrong_picture="dis_half")
        self.assertEqual(rc, 1)
        self.assertIn("dis_half: frame 0 is not picture 0", err)

    def test_needs_an_empty_directory(self):
        os.makedirs(self.dir)
        with open(os.path.join(self.dir, "x"), "w"):
            pass
        rc, _, err = self.main()
        self.assertEqual(rc, 2)
        self.assertIn("not an empty directory", err)


if __name__ == "__main__":
    unittest.main()
