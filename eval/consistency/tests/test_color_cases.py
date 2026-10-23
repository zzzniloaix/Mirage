"""Unit tests of eval/consistency/color_cases.py; no clip is made (the
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
import color_cases as cc  # noqa: E402
import vmafdelta as vd  # noqa: E402


class Cases(unittest.TestCase):
    def test_labels_and_clips(self):
        cases = cc.CASES + cc.REFUSED
        self.assertEqual(len({c.label for c in cases}), len(cases))
        for c in cases:
            self.assertEqual(c.ref.size, cc.REF_SIZE, c.label)
            self.assertEqual(c.dis.size, cc.DIS_SIZE, c.label)
            self.assertTrue(c.ref.name.startswith("ref_") and c.dis.name.startswith("dis_"))
        clips = cc.all_clips()
        self.assertEqual(len({x.name for x in clips}), len(clips))
        self.assertEqual(len({x.tags for x in clips if x.size == cc.DIS_SIZE}),
                         sum(1 for x in clips if x.size == cc.DIS_SIZE))
        # References first, then the distorted inputs.
        roles = [x.name[:3] for x in clips]
        self.assertEqual(roles, sorted(roles, key=lambda r: r != "ref"))

    def test_clip_names_follow_tags(self):
        x = cc.clip("dis", ["colorspace=bt709", "range=tv", "chroma_location=topleft"])
        self.assertEqual(x.name, "dis_bt709-tv-topleft")
        self.assertEqual(x.tags, (("colorspace", "bt709"), ("range", "tv"),
                                  ("chroma_location", "topleft")))

    def test_pairs_files_read_back(self):
        text = cc.pairs_text(cc.CASES, "/clips", "Title.")
        pairs = vd.parse_pairs(text)
        self.assertEqual([p.label for p in pairs], [c.label for c in cc.CASES])
        self.assertEqual(pairs[0].ref, "/clips/ref_bt709-tv.mkv")
        self.assertEqual(pairs[0].dis, "/clips/dis_smpte170m-tv.mkv")
        self.assertTrue(text.startswith("# Title.\n"))


class Commands(unittest.TestCase):
    def test_make_command(self):
        ref = cc.make_command("ffmpeg", cc.REF_709_TV, "/c/ref.mkv", frames=7)
        self.assertIn("testsrc2=size=1920x1080:rate=30000/1001", ref)
        self.assertEqual(ref[ref.index("-frames:v") + 1], "7")
        self.assertEqual(ref[ref.index("-vf") + 1],
                         "format=yuv420p,setparams=colorspace=bt709:range=tv")
        self.assertEqual(ref[-3:], ["-f", "matroska", "/c/ref.mkv"])
        self.assertEqual(ref[ref.index("-c:v") + 1], "rawvideo")
        dis = cc.make_command("ffmpeg", cc.DIS_709_TV_TFF, "/c/dis.mkv")
        self.assertEqual(dis[dis.index("-frames:v") + 1], str(cc.DEFAULT_FRAMES))
        self.assertEqual(dis[dis.index("-vf") + 1],
                         "scale=640:360:flags=bicubic,format=yuv420p,"
                         "setparams=colorspace=bt709:range=tv:field_mode=tff")

    def test_expected_tags(self):
        self.assertEqual(cc.expected_tags(cc.REF_UNTAGGED), {
            "width": "1920", "height": "1080", "pix_fmt": "yuv420p",
            "color_space": "unknown", "color_range": "unknown", "color_primaries": "unknown",
            "color_transfer": "unknown", "chroma_location": "unspecified",
            "field_order": "progressive"})
        tff = cc.expected_tags(cc.DIS_709_TV_TFF)
        self.assertEqual((tff["color_space"], tff["color_range"], tff["field_order"]),
                         ("bt709", "tv", "tb"))
        other = cc.expected_tags(cc.DIS_709_TV_470BG)
        self.assertEqual((other["color_primaries"], other["color_transfer"]),
                         ("bt470bg", "bt470bg"))
        self.assertEqual(cc.expected_tags(cc.DIS_709_TV_TOPLEFT)["chroma_location"], "topleft")

    def test_tag_problems(self):
        probed = cc.parse_probe("width=640\nheight=360\npix_fmt=yuv420p\ncolor_range=tv\n"
                                "color_space=bt709\ncolor_transfer=unknown\n"
                                "color_primaries=unknown\nchroma_location=unspecified\n"
                                "field_order=progressive\n")
        self.assertEqual(cc.tag_problems(cc.DIS_709_TV, probed), [])
        self.assertEqual(cc.tag_problems(cc.DIS_709_PC, probed),
                         ["dis_bt709-pc: color_range is 'tv', not 'pc'"])

    def test_shown(self):
        argv = ["/opt/ff/ffmpeg", "-i", "/c/x/dis.mkv", "-vf", "a=b,c", "out put"]
        self.assertEqual(cc.shown(argv, "/c/x", {"/opt/ff/ffmpeg": "<ffmpeg>"}),
                         "<ffmpeg> -i <clips_dir>/dis.mkv -vf a=b,c 'out put'")


class Main(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = os.path.join(self.tmp.name, "clips")

    def fake_run(self, wrong=None):
        """A stand-in for color_cases.run: makes the clip file, reads back the
        intended tags (`wrong` = (clip name, field, value) changes one), and
        gives one MD5 per frame size."""
        def run(argv):
            if "lavfi" in argv:
                with open(argv[-1], "wb") as f:
                    f.write(b"clip")
                return ""
            path = argv[-1] if argv[-1] != "-" else argv[argv.index("-i") + 1]
            name = os.path.basename(path)[:-len(".mkv")]
            x = next(c for c in cc.all_clips() if c.name == name)
            if "md5" in argv:
                return f"MD5={'a' if x.size == cc.REF_SIZE else 'b'}\n"
            tags = cc.expected_tags(x)
            if wrong and wrong[0] == name:
                tags[wrong[1]] = wrong[2]
            return "".join(f"{k}={v}\n" for k, v in tags.items())
        return run

    def main(self, *extra, wrong=None):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(cc, "run", self.fake_run(wrong)), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = cc.main([self.dir, "--ffmpeg", "/opt/ff/ffmpeg", *extra])
        return rc, out.getvalue(), err.getvalue()

    def test_writes_clips_pairs_and_record(self):
        rc, out, err = self.main("--frames", "3")
        self.assertEqual(rc, 0, err)
        names = sorted(os.listdir(self.dir))
        self.assertIn("pairs.txt", names)
        self.assertIn("pairs_refused.txt", names)
        self.assertEqual(sum(n.endswith(".mkv") for n in names), len(cc.all_clips()))
        with open(os.path.join(self.dir, "pairs.txt")) as f:
            self.assertEqual(len(vd.parse_pairs(f.read())), len(cc.CASES))
        with open(os.path.join(self.dir, "pairs_refused.txt")) as f:
            self.assertEqual([p.label for p in vd.parse_pairs(f.read())],
                             [c.label for c in cc.REFUSED])
        with open(os.path.join(self.dir, "clips.txt")) as f:
            record = f.read()
        self.assertNotIn(self.dir, record)
        self.assertIn("$ <ffmpeg> -hide_banner -nostdin -v error -f lavfi", record)
        self.assertIn("<clips_dir>/ref_bt709-tv.mkv", record)
        self.assertIn("$ <ffprobe> -v error", record)
        self.assertIn("-frames:v 3", record)

    def test_a_wrong_tag_fails(self):
        rc, _, err = self.main(wrong=("dis_bt709-pc", "color_range", "tv"))
        self.assertEqual(rc, 1)
        self.assertIn("dis_bt709-pc: color_range is 'tv', not 'pc'", err)

    def test_needs_an_empty_directory(self):
        os.makedirs(self.dir)
        with open(os.path.join(self.dir, "x"), "w"):
            pass
        rc, _, err = self.main()
        self.assertEqual(rc, 2)
        self.assertIn("not an empty directory", err)


if __name__ == "__main__":
    unittest.main()
