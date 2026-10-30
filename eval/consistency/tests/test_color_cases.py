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
        self.assertEqual((x.pix_fmt, x.codec, x.probed), ("yuv420p", "rawvideo", ()))

    def test_clip_names_follow_codec_and_decoded_format(self):
        self.assertEqual(cc.REF10_709_TV.name, "ref_ffv1-yuv420p10le_bt709-tv")
        self.assertEqual(cc.REFJ_709_PC.name, "ref_libx264-yuvj420p_bt709-pc")
        self.assertEqual(cc.DISJ_709_PC.name, "dis_libx264-yuvj420p_bt709-pc")
        self.assertEqual(cc.REFJ_709_PC.pix_fmt, "yuv420p")

    def test_format_cases(self):
        s = cc.SETS["formats"]
        cases = s.cases + s.refused
        self.assertEqual(len({c.label for c in cases}), len(cases))
        for c in cases:
            self.assertEqual(c.ref.size, cc.REF_SIZE, c.label)
            self.assertEqual(c.dis.size, cc.DIS_SIZE, c.label)
        clips = cc.all_clips(cases)
        self.assertEqual(len({x.name for x in clips}), len(clips))
        self.assertEqual(len({(x.size, x.tags, x.pix_fmt, x.codec) for x in clips}), len(clips))
        # Every reference format other than yuv420p, with other tags and with the same
        # tags as the distorted input; distorted inputs in those formats; YCgCo on both.
        refs = {(c.ref.pix_fmt, c.ref.codec) for c in s.cases}
        self.assertTrue({("yuv420p10le", "ffv1"), ("yuv422p", "ffv1"), ("yuv444p", "ffv1"),
                         ("yuv420p", "libx264")} <= refs)
        self.assertTrue({(c.dis.pix_fmt, c.dis.codec) for c in s.cases}
                        > {("yuv420p", "rawvideo")})
        self.assertEqual([(c.ref, c.dis) for c in s.refused],
                         [(cc.REF_YCGCO_TV, cc.DIS_YCGCO_TV)])
        # The tag cases' set is the one the committed color tag runs used.
        self.assertEqual(cc.SETS["tags"], (cc.CASES, cc.REFUSED, "", "Color tag cases"))

    def test_negotiation_cases(self):
        # D-035: references that aren't yuv420p, against a distorted input without a
        # range tag (a full range reference, and a limited range one as the control),
        # and against distorted inputs that aren't plain YUV.
        s = cc.SETS["negotiation"]
        self.assertEqual(s.refused, ())
        self.assertEqual(len({c.label for c in s.cases}), len(s.cases))
        self.assertTrue(all(c.ref.pix_fmt != "yuv420p" or c.ref.codec != "rawvideo"
                            for c in s.cases))
        no_range = [c for c in s.cases if ("range", "unknown") in c.dis.tags]
        self.assertEqual({c.ref for c in no_range}, {cc.REFJ_709_PC, cc.REF10_709_TV})
        self.assertEqual({c.dis.pix_fmt for c in s.cases} - {"yuv420p"}, {"gray", "gbrp"})
        self.assertEqual(cc.DIS_RGB_PC.name, "dis_ffv1-bgr0_gbr-pc")
        clips = cc.all_clips(s.cases)
        self.assertEqual(len({x.name for x in clips}), len(clips))

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

    def test_make_command_of_a_tag_case_clip_is_unchanged(self):
        # The commands color-cases-before/clips.txt records.
        self.assertEqual(cc.make_command("ffmpeg", cc.REF_709_TV, "/c/r.mkv"), [
            "ffmpeg", "-hide_banner", "-nostdin", "-v", "error", "-f", "lavfi", "-i",
            "testsrc2=size=1920x1080:rate=30000/1001", "-frames:v", "10", "-vf",
            "format=yuv420p,setparams=colorspace=bt709:range=tv", "-c:v", "rawvideo",
            "-f", "matroska", "/c/r.mkv"])

    def test_make_command_of_format_clips(self):
        ten = cc.make_command("ffmpeg", cc.REF10_170M_TV, "/c/r.mkv")
        self.assertEqual(ten[ten.index("-vf") + 1],
                         "format=yuv420p10le,setparams=colorspace=smpte170m:range=tv")
        self.assertEqual(ten[ten.index("-c:v"):], ["-c:v", "ffv1", "-f", "matroska", "/c/r.mkv"])
        j = cc.make_command("ffmpeg", cc.DISJ_709_PC, "/c/d.mkv")
        self.assertEqual(j[j.index("-vf") + 1],
                         "scale=640:360:flags=bicubic,format=yuv420p,"
                         "setparams=colorspace=bt709:range=pc")
        self.assertEqual(j[j.index("-c:v"):],
                         ["-c:v", "libx264", "-qp", "0", "-f", "matroska", "/c/d.mkv"])

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
        self.assertEqual(cc.expected_tags(cc.REF444_709_TV)["pix_fmt"], "yuv444p")
        j = cc.expected_tags(cc.REFJ_709_PC)
        self.assertEqual((j["pix_fmt"], j["color_space"], j["color_range"], j["chroma_location"]),
                         ("yuvj420p", "bt709", "pc", "left"))

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

    def fake_run(self, wrong=None, odd_md5=None):
        """A stand-in for color_cases.run: makes the clip file, reads back the
        intended tags (`wrong` = (clip name, field, value) changes one), and
        gives one MD5 per frame size and pixel format (another for the clip
        named `odd_md5`)."""
        every = cc.all_clips(cc.CASES + cc.REFUSED + cc.FORMAT_CASES + cc.FORMAT_REFUSED)

        def run(argv):
            if "lavfi" in argv:
                with open(argv[-1], "wb") as f:
                    f.write(b"clip")
                return ""
            path = argv[-1] if argv[-1] != "-" else argv[argv.index("-i") + 1]
            name = os.path.basename(path)[:-len(".mkv")]
            x = next(c for c in every if c.name == name)
            if "md5" in argv:
                return f"MD5={x.size[0]}-{x.pix_fmt}{'-odd' if name == odd_md5 else ''}\n"
            tags = cc.expected_tags(x)
            if wrong and wrong[0] == name:
                tags[wrong[1]] = wrong[2]
            return "".join(f"{k}={v}\n" for k, v in tags.items())
        return run

    def main(self, *extra, wrong=None, odd_md5=None):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(cc, "run", self.fake_run(wrong, odd_md5)), \
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

    def test_the_tag_cases_record_begins_as_before(self):
        rc, _, err = self.main()
        self.assertEqual(rc, 0, err)
        with open(os.path.join(self.dir, "clips.txt")) as f:
            first = f.readline()
        self.assertEqual(first, "# The clips of eval/consistency/color_cases.py, made in "
                                "<clips_dir>: for each, the command that made it, its stream "
                                "as <ffprobe> reads it back, and the MD5 of its decoded "
                                "frames.\n")

    def test_the_format_cases(self):
        rc, out, err = self.main("--set", "formats")
        self.assertEqual(rc, 0, err)
        s = cc.SETS["formats"]
        clips = cc.all_clips(s.cases + s.refused)
        names = os.listdir(self.dir)
        self.assertEqual(sorted(n for n in names if n.endswith(".mkv")),
                         sorted(x.name + ".mkv" for x in clips))
        with open(os.path.join(self.dir, "pairs.txt")) as f:
            text = f.read()
        self.assertTrue(text.startswith("# Reference format cases that FFmpeg's section 5 "
                                        "graph scores"))
        self.assertEqual([p.label for p in vd.parse_pairs(text)], [c.label for c in s.cases])
        with open(os.path.join(self.dir, "pairs_refused.txt")) as f:
            self.assertEqual([p.label for p in vd.parse_pairs(f.read())],
                             ["u03_ycgco_vs_ycgco"])
        with open(os.path.join(self.dir, "clips.txt")) as f:
            record = f.read()
        self.assertTrue(record.startswith("# The clips of eval/consistency/color_cases.py "
                                          "--set formats, made in <clips_dir>"))
        self.assertIn("<clips_dir>/ref_libx264-yuvj420p_bt709-pc.mkv", record)
        self.assertIn(f"{len(clips)} clips, {len(s.cases)} + 1 cases", out)

    def test_clips_of_one_format_with_other_pixels_fail(self):
        rc, _, err = self.main("--set", "formats", odd_md5="ref_libx264-yuvj420p_bt709-pc")
        self.assertEqual(rc, 1)
        self.assertIn("the 1920x1080 clips made from yuv420p do not all have the same pixels",
                      err)

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
