"""Unit tests of eval/consistency/vmafdelta.py.

python3 -I -B -m unittest discover -s eval/consistency/tests -v
"""

import hashlib
import json
import lzma
import os
import shlex
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(HERE))
import vmafdelta as vd  # noqa: E402

REF = "https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8"
DIS = "https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear2/prog_index.m3u8"

# Lines of an FFmpeg 8.1.1 verbose log of a 640x360 smpte170m rung against
# a 1920x1080 bt709 one, as run_delta.py runs it.
LOG = """\
[Parsed_scale_1 @ 0x9c50880c0] w:1920 h:1080 flags:'bilinear' interl:0
  Stream #0:1[0x0]: Video: h264 (Main), 1 reference frame ([27][0][0][0] / 0x001B), yuv420p(tv, smpte170m/smpte170m/bt709, topleft), 640x360, 29.97 fps, 29.97 tbr, 90k tbn, start 10.000000
  Stream #1:1[0x0]: Video: h264 (Main), 1 reference frame ([27][0][0][0] / 0x001B), yuv420p(tv, bt709, topleft), 1920x1080, 29.97 fps, 29.97 tbr, 90k tbn, start 10.000000
  Stream #0:0[0x0]: Audio: aac (LC) ([15][0][0][0] / 0x000F), 22050 Hz, stereo, fltp, start 10.000000
[Parsed_scale_1 @ 0x9c508bf00] w:1920 h:1080 flags:'bilinear' interl:0
[graph 0 input from stream 0:1 @ 0x9c84446c0] w:640 h:360 pixfmt:yuv420p tb:1/90000 fr:30000/1001 sar:0/1 csp:smpte170m range:tv alpha:unspecified
[graph 0 input from stream 1:1 @ 0x9c8444840] w:1920 h:1080 pixfmt:yuv420p tb:1/90000 fr:30000/1001 sar:0/1 csp:bt709 range:tv alpha:unspecified
[Parsed_scale_1 @ 0x9c508bf00] w:640 h:360 fmt:yuv420p csp:smpte170m range:tv sar:0/1 -> w:1920 h:1080 fmt:yuv420p csp:bt709 range:tv sar:0/1 flags:0x00000002
[Parsed_scale_1 @ 0x9c508bf00] [framesync @ 0x9c508c790] Selected 1/90000 time base
[swscaler @ 0x9c9ba4000] YUV color matrix differs for YUV->YUV, using intermediate RGB to convert
  Stream #0:0: Video: wrapped_avframe, 1 reference frame, yuv420p(tv, bt709/smpte170m/bt709, progressive), 1920x1080, q=2-31, 200 kb/s, 29.97 fps, 29.97 tbn
[Parsed_libvmaf_5 @ 0x9c8444240] VMAF score: 26.276917
"""

PARAMS = {"variant": vd.VARIANT_PLAIN, "frames": 3, "threshold": 0.01,
          "model": "vmaf_v0.6.1", "scaler": "bilinear", "threads": 4}

PROBE_REF = {"codec_name": "h264", "width": 1920, "height": 1080, "pix_fmt": "yuv420p",
             "color_space": "bt709", "color_range": "tv", "color_primaries": "bt709",
             "color_transfer": "bt709", "time_base": "1/90000", "r_frame_rate": "30000/1001",
             "avg_frame_rate": "30000/1001", "start_time": "10.000000"}
PROBE_DIS = dict(PROBE_REF, width=640, height=360, color_space="smpte170m",
                 color_primaries="smpte170m")


def mirage_report(scores, frame_limit=3, **settings):
    """A mirage_vmaf report (src/VMAFReport.h) with these per-frame scores."""
    def info(url, probe):
        return {"url": url, "width": probe["width"], "height": probe["height"],
                "pix_fmt": "yuv420p", "color_space": probe["color_space"], "color_range": "tv",
                "color_primaries": probe["color_primaries"], "color_trc": "bt709",
                "time_base": "1/90000", "ended": False}
    rep = {"tool": "mirage_vmaf", "libvmaf_version": "3.0.0", "libvmaf_package_version": "3.1.0",
           "model": "vmaf_v0.6.1", "scaler": "bilinear", "n_threads": 4, "n_subsample": 1,
           "scored_as": {"width": 1920, "height": 1080, "pix_fmt": "yuv420p", "bit_depth": 8},
           "frame_limit": frame_limit, "frames_scored": len(scores),
           "pooled": {"mean": sum(scores) / len(scores) if scores else None,
                      "min": min(scores) if scores else None},
           "ref": info(REF, PROBE_REF), "dis": info(DIS, PROBE_DIS),
           "frames": [{"index": i, "ref_pts": 10 + i / 30, "dis_pts": 10 + i / 30, "vmaf": v}
                      for i, v in enumerate(scores)]}
    rep.update(settings)
    return rep


def ffmpeg_log(scores, version="3.0.0"):
    """FFmpeg's libvmaf JSON log with these per-frame scores."""
    return {"version": version, "fps": 1.0,
            "frames": [{"frameNum": i, "metrics": {"integer_motion2": 0.0, "vmaf": v}}
                       for i, v in enumerate(scores)],
            "pooled_metrics": {"vmaf": {"min": min(scores), "max": max(scores),
                                        "mean": sum(scores) / len(scores),
                                        "harmonic_mean": 0.0}},
            "aggregate_metrics": {}}


def sides(m_scores, f_scores, m_exit=0, f_exit=0, stderr=LOG, **rep_settings):
    run = lambda rc: {"exit_code": rc, "timed_out": False, "timeout_s": 60,  # noqa: E731
                      "error": None, "elapsed_s": 1.0}
    return {"probe": {"ref": PROBE_REF, "dis": PROBE_DIS, "errors": []},
            "relabel": None, "relabel_warnings": [],
            "mirage": {"run": run(m_exit), "error": None,
                       "report": mirage_report(m_scores, **rep_settings)},
            "ffmpeg": {"run": run(f_exit), "error": None, "log": ffmpeg_log(f_scores),
                       "stderr": stderr}}


PAIR = vd.Pair("gear2_vs_gear5", REF, DIS)


class ParsePairs(unittest.TestCase):
    def test_lines_comments_and_blank_lines(self):
        pairs = vd.parse_pairs("# a comment\n\n  a_1 r1 d1 \nb.2\tr2   d2\n")
        self.assertEqual(pairs, [vd.Pair("a_1", "r1", "d1"), vd.Pair("b.2", "r2", "d2")])

    def test_errors(self):
        for text, part in (("a r\n", "found 2 fields"), ("a r d x\n", "found 4 fields"),
                           ("../x r d\n", "may only use"), ("a/b r d\n", "may only use"),
                           ("a r d\na r2 d2\n", "used twice"), ("# only\n\n", "no pairs")):
            with self.subTest(text=text):
                with self.assertRaises(ValueError) as cm:
                    vd.parse_pairs(text)
                self.assertIn(part, str(cm.exception))

    def test_committed_pairs_file(self):
        with open(os.path.join(os.path.dirname(HERE), "pairs", "bipbop16x9.txt")) as f:
            pairs = vd.parse_pairs(f.read())
        self.assertEqual([p.label for p in pairs],
                         ["gear4_vs_gear5", "gear3_vs_gear5", "gear2_vs_gear5", "gear1_vs_gear5"])
        base = "https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/"
        for p in pairs:
            self.assertEqual(p.ref, base + "gear5/prog_index.m3u8")
            self.assertEqual(p.dis, base + p.label[:5] + "/prog_index.m3u8")
        with open(os.path.join(os.path.dirname(HERE), "pairs", "bipbop16x9_gear2.txt")) as f:
            self.assertEqual(vd.parse_pairs(f.read()), [pairs[2]])


class Commands(unittest.TestCase):
    def test_graph_is_phase_0s(self):
        # The FFmpeg command of Phase 0's diagnostic comparison (P-005).
        path = os.path.join(REPO, "eval", "baseline", "diagnostics",
                            "ffmpeg_libvmaf_gear2_vs_gear5_first300.cmd.txt")
        with open(path) as f:
            argv = shlex.split(f.read())
        graph = argv[argv.index("-lavfi") + 1]
        log_path = graph.rsplit("log_path=", 1)[1]
        self.assertEqual(vd.filtergraph(1920, 1080, "bilinear", "vmaf_v0.6.1", 4,
                                        log_path=log_path), graph)
        # Input 0 is the distorted rung there and here.
        inputs = [argv[i + 1] for i, a in enumerate(argv) if a == "-i"]
        cmd = vd.ffmpeg_command("ffmpeg", REF, DIS, graph)
        self.assertEqual([cmd[i + 1] for i, a in enumerate(cmd) if a == "-i"], inputs)
        self.assertEqual(inputs, [DIS, REF])

    def test_graph_with_frames_and_relabel(self):
        g = vd.filtergraph(1920, 1080, "bicubic", "vmaf_v0.6.1neg", 8, frames=300,
                           relabel="setparams=colorspace=bt709")
        self.assertEqual(g, "[0:v]trim=end_frame=300,setparams=colorspace=bt709,"
                            "scale=1920:1080:flags=bicubic,format=yuv420p[d];"
                            "[1:v]trim=end_frame=300,format=yuv420p[r];"
                            "[d][r]libvmaf=model=version=vmaf_v0.6.1neg:n_threads=8"
                            ":log_fmt=json:log_path=ffmpeg.json")

    def test_graph_with_a_model_file(self):
        g = vd.filtergraph(1920, 1080, "lanczos", None, 4, model_file="/m/vmaf_v0.6.1neg.json")
        self.assertEqual(g, "[0:v]scale=1920:1080:flags=lanczos,format=yuv420p[d];"
                            "[1:v]format=yuv420p[r];"
                            "[d][r]libvmaf=model=path=/m/vmaf_v0.6.1neg.json:n_threads=4"
                            ":log_fmt=json:log_path=ffmpeg.json")

    def test_filter_safe(self):
        self.assertTrue(vd.filter_safe("/opt/homebrew/share/libvmaf/model/vmaf_v0.6.1.json"))
        for bad in ("", "a:b", "a b", "a,b", "a;b", "a=b", "a[b]", "a'b", "a\\b", "a|b", "a%b",
                    "a\tb"):
            with self.subTest(bad=bad):
                self.assertFalse(vd.filter_safe(bad))

    def test_ffmpeg_command(self):
        cmd = vd.ffmpeg_command("/x/ffmpeg", REF, DIS, "G")
        self.assertEqual(cmd, ["/x/ffmpeg", "-hide_banner", "-nostdin", "-nostats", "-v",
                               "verbose", "-i", DIS, "-i", REF, "-an", "-lavfi", "G",
                               "-f", "null", "-"])

    def test_mirage_command(self):
        self.assertEqual(vd.mirage_command("m", REF, DIS),
                         ["m", "--ref", REF, "--dis", DIS, "--out", "mirage.json"])
        self.assertEqual(vd.mirage_command("m", REF, DIS, frames=300, options=["--x", "y"]),
                         ["m", "--ref", REF, "--dis", DIS, "--out", "mirage.json",
                          "--frames", "300", "--x", "y"])

    def test_probe_command_and_parse(self):
        cmd = vd.probe_command("ffprobe", REF)
        self.assertEqual(cmd[-1], REF)
        self.assertIn("stream=" + ",".join(vd.PROBE_FIELDS), cmd)
        text = json.dumps({"programs": [], "streams": [{"width": 640, "height": 360,
                                                        "color_space": "smpte170m"}]})
        info = vd.parse_probe(text)
        self.assertEqual((info["width"], info["height"], info["color_space"]),
                         (640, 360, "smpte170m"))
        self.assertIsNone(info["color_range"])
        for bad in ("not json", "{}", '{"streams": []}', '{"streams": [{"width": 640}]}'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                vd.parse_probe(bad)

    def test_shell_line(self):
        self.assertEqual(vd.shell_line(["/a b/ffmpeg", "-lavfi", "[0:v]scale=1:2[d];x", "-"],
                                       "<ffmpeg>"),
                         "<ffmpeg> -lavfi '[0:v]scale=1:2[d];x' -")
        self.assertEqual(vd.shell_line(["a b", REF]), f"'a b' {REF}")

    def test_help_options_and_forwarding(self):
        text = ("Usage: mirage_vmaf --ref <url|path> --dis <url|path> --out <report.json>\n\n"
                "Options:\n  --ref <url|path>      reference\n  --frames N            limit\n"
                "  -h, --help            help\nThe --scaler option is not here.\n")
        self.assertEqual(vd.help_options(text), {"--ref", "--frames"})
        self.assertEqual(vd.forwarded_options({"--ref"}, "bicubic", "m"), [])
        self.assertEqual(vd.forwarded_options({"--scaler", "--model"}, "bicubic", "m"),
                         ["--scaler", "bicubic", "--model", "m"])
        both = {"--scaler", "--model", "--model-file"}
        self.assertEqual(vd.forwarded_options(both, "bilinear", None, "/m/x.json"),
                         ["--scaler", "bilinear", "--model-file", "/m/x.json"])
        # Without --model-file the file is not passed on, nor the default model instead.
        self.assertEqual(vd.forwarded_options({"--model"}, "bilinear", None, "/m/x.json"), [])


class Relabel(unittest.TestCase):
    def test_same_tags(self):
        self.assertEqual(vd.relabel_filter(PROBE_REF, dict(PROBE_REF, width=1)), (None, []))

    def test_color_space_differs(self):
        self.assertEqual(vd.relabel_filter(PROBE_REF, PROBE_DIS),
                         ("setparams=colorspace=bt709", []))

    def test_range_differs_too(self):
        self.assertEqual(vd.relabel_filter(PROBE_REF, dict(PROBE_DIS, color_range="pc")),
                         ("setparams=colorspace=bt709:range=tv", []))

    def test_untagged_reference(self):
        f, warnings = vd.relabel_filter(dict(PROBE_REF, color_space="unknown"), PROBE_DIS)
        self.assertIsNone(f)
        self.assertEqual(len(warnings), 1)
        self.assertIn("no color_space tag", warnings[0])


class Redaction(unittest.TestCase):
    def test_longest_first_text_and_bytes(self):
        r = vd.Redactor([("/u/me", "<home>"), ("/u/me/repo", "<repo>"), (None, "<x>")])
        self.assertEqual(r.text("/u/me/repo/a /u/me/b /u/other"),
                         ("<repo>/a <home>/b /u/other", 2))
        self.assertEqual(r.data(b"x /u/me/repo"), (b"x <repo>", 1))
        self.assertEqual(r.text("nothing"), ("nothing", 0))

    def test_real_path_too(self):
        with tempfile.TemporaryDirectory() as d:
            real = os.path.join(d, "real")
            os.mkdir(real)
            link = os.path.join(d, "link")
            os.symlink(real, link)
            r = vd.Redactor([(link, "<out_dir>")])
            text, n = r.text(f"{os.path.realpath(real)}/x {os.path.abspath(link)}/y")
            self.assertEqual((text, n), ("<out_dir>/x <out_dir>/y", 2))

    def test_local_strings(self):
        needles = [("/scratch/", "a local path"), ("host9", "the host name"), ("", "x")]
        self.assertEqual(vd.local_strings("a /scratch/b host9", needles),
                         ["a local path", "the host name"])
        self.assertEqual(vd.local_strings("clean", needles), [])


class Scores(unittest.TestCase):
    def test_mirage_scores(self):
        self.assertEqual(vd.mirage_scores(mirage_report([1, 2.5])), [1.0, 2.5])
        bad = mirage_report([1, 2])
        bad["frames"][1]["index"] = 5
        with self.assertRaisesRegex(ValueError, "frame 1 has index 5"):
            vd.mirage_scores(bad)
        bad = mirage_report([1, 2])
        bad["frames"][0]["vmaf"] = None
        with self.assertRaisesRegex(ValueError, "frame 0 has no score"):
            vd.mirage_scores(bad)

    def test_ffmpeg_scores(self):
        self.assertEqual(vd.ffmpeg_scores(ffmpeg_log([3, 4])), [3.0, 4.0])
        bad = ffmpeg_log([3, 4])
        bad["frames"][0]["frameNum"] = 1
        with self.assertRaisesRegex(ValueError, "frameNum 1"):
            vd.ffmpeg_scores(bad)
        bad = ffmpeg_log([3, 4])
        del bad["frames"][1]["metrics"]["vmaf"]
        with self.assertRaisesRegex(ValueError, "no vmaf score"):
            vd.ffmpeg_scores(bad)

    def test_compare(self):
        c = vd.compare_scores([1.0, 2.5, 3.0, 4.0], [1.0, 2.0, 3.25, 4.0], 0.01, worst=2)
        self.assertEqual(c["compared"], 4)
        self.assertEqual((c["max_abs"], c["max_abs_index"]), (0.5, 1))
        self.assertEqual(c["mean_abs"], 0.1875)
        self.assertEqual(c["mean_signed"], 0.0625)
        self.assertEqual((c["over_threshold"], c["over_threshold_ranges"]), (2, [[1, 2]]))
        self.assertEqual(c["worst"], [{"index": 1, "mirage": 2.5, "ffmpeg": 2.0, "delta": 0.5},
                                      {"index": 2, "mirage": 3.0, "ffmpeg": 3.25, "delta": -0.25}])
        self.assertEqual((c["mean_mirage"], c["mean_ffmpeg"]), (2.625, 2.5625))

    def test_threshold_is_exclusive(self):
        # D-019: a frame fails if |delta| > threshold; exactly at it passes.
        c = vd.compare_scores([1.25, 1.2500001], [1.0, 1.0], 0.25)
        self.assertEqual(c["over_threshold_ranges"], [[1, 1]])

    def test_unequal_lengths_and_empty(self):
        c = vd.compare_scores([1.0, 2.0, 3.0], [1.0, 2.0], 0.01)
        self.assertEqual((c["compared"], c["max_abs"]), (2, 0.0))
        e = vd.compare_scores([], [1.0], 0.01)
        self.assertEqual((e["compared"], e["max_abs"], e["mean_abs"], e["worst"]),
                         (0, None, None, []))

    def test_index_ranges(self):
        self.assertEqual(vd.index_ranges([]), [])
        self.assertEqual(vd.index_ranges([0, 1, 2, 5, 7, 8]), [[0, 2], [5, 5], [7, 8]])


class Log(unittest.TestCase):
    def test_colorspace_lines(self):
        lines = vd.colorspace_lines(LOG)
        self.assertEqual(len(lines), 7)
        self.assertTrue(lines[0].startswith("  Stream #0:1[0x0]: Video: h264"))
        self.assertTrue(lines[2].startswith("[graph 0 input from stream 0:1"))
        self.assertTrue(lines[4].startswith("[Parsed_scale_1 @ 0x9c508bf00] w:640 h:360"))
        self.assertTrue(lines[5].startswith("[swscaler @"))
        self.assertTrue(lines[6].startswith("  Stream #0:0: Video: wrapped_avframe"))
        self.assertFalse(any("Audio" in l or "interl" in l or "framesync" in l for l in lines))

    def test_colorspace_lines_warnings_and_refusals(self):
        # Lines in the form FFmpeg 8.1.1 writes them (libavfilter/vf_libvmaf.c,
        # libavfilter/avfiltergraph.c): libvmaf's warning, and the error of a
        # graph whose distorted input has a matrix libswscale lacks.
        warning = ("[Parsed_libvmaf_5 @ 0x7c4c25080] distorted and reference frames use "
                   "different color ranges (pc != tv)")
        refusal = ("Impossible to convert between the formats supported by the filter "
                   "'Parsed_trim_0' and the filter 'auto_scale_0'")
        prefixed = "[fc#0 @ 0x6000] " + refusal
        other = "[Parsed_libvmaf_5 @ 0x7c4c25080] VMAF score: 26.276917"
        text = "\n".join([warning, other, refusal, prefixed, "[fc#0 @ 0x6000] Error "
                          "reinitializing filters!"])
        self.assertEqual(vd.colorspace_lines(text), [warning, refusal, prefixed])

    def test_scale_lines_and_conversions(self):
        scales = vd.scale_lines(LOG)
        self.assertEqual(scales, [{
            "filter": "Parsed_scale_1",
            "in": {"width": 640, "height": 360, "pix_fmt": "yuv420p",
                   "color_space": "smpte170m", "color_range": "tv"},
            "out": {"width": 1920, "height": 1080, "pix_fmt": "yuv420p",
                    "color_space": "bt709", "color_range": "tv"},
            "flags": "0x00000002"}])
        self.assertEqual(vd.conversions(scales + scales),
                         ["Parsed_scale_1: color_space smpte170m -> bt709"])
        auto = ("[auto_scale_0 @ 0x1] w:1920 h:1080 fmt:yuv420p csp:bt709 range:tv sar:1/1 -> "
                "w:1920 h:1080 fmt:yuv420p csp:bt709 range:pc sar:1/1")
        self.assertEqual(vd.conversions(vd.scale_lines(auto)),
                         ["auto_scale_0: color_range tv -> pc"])

    def test_library_versions(self):
        # Two lines of `ffmpeg -version`: the version built with, then the one run with.
        text = ("ffmpeg version 8.1.1 Copyright (c) 2000-2025 the FFmpeg developers\n"
                "libavutil      60. 26.101 / 60. 26.101\n"
                "libswscale      9.  5.101 /  9.  6.100\n")
        self.assertEqual(vd.library_versions(text), {"libavutil": "60.26.101",
                                                     "libswscale": "9.6.100"})
        self.assertEqual(vd.library_versions("ffmpeg version fake\n"), {})

    def test_graph_inputs(self):
        self.assertEqual(vd.graph_inputs(LOG), [
            {"input": 0, "stream": 1, "color_space": "smpte170m", "color_range": "tv"},
            {"input": 1, "stream": 1, "color_space": "bt709", "color_range": "tv"}])


class EvaluatePair(unittest.TestCase):
    def test_equal_scores_pass(self):
        r = vd.evaluate_pair(PAIR, PARAMS, sides([50.0, 60.0, 70.0], [50.0, 60.0, 70.0]))
        self.assertEqual(r["status"], "PASS", r["fail_reasons"])
        self.assertEqual(r["fail_reasons"], [])
        self.assertEqual(r["frames"], {"mirage": 3, "ffmpeg": 3, "compared": 3,
                                       "counts_match": True})
        self.assertEqual(r["delta"]["max_abs"], 0.0)
        self.assertEqual(r["pooled"]["difference"], {"mean": 0.0, "min": 0.0})
        self.assertEqual(r["ffmpeg_color"]["conversions"],
                         ["Parsed_scale_1: color_space smpte170m -> bt709"])
        self.assertEqual(r["mirage_vmaf"]["pairs_with_different_pts"], 0)
        self.assertEqual(r["warnings"], [])
        json.loads(vd.to_json(r))

    def test_frames_over_the_threshold_fail(self):
        r = vd.evaluate_pair(PAIR, PARAMS, sides([50.0, 60.02, 70.0], [50.0, 60.0, 70.0]))
        self.assertEqual(r["status"], "FAIL")
        self.assertEqual(r["fail_reasons"], ["1 of 3 frames have |delta| > 0.01"])
        self.assertEqual(r["delta"]["over_threshold_ranges"], [[1, 1]])

    def test_frame_count_mismatch_fails_first(self):
        r = vd.evaluate_pair(PAIR, PARAMS, sides([50.0, 60.0, 70.0], [50.0, 60.0, 70.0, 80.0]))
        self.assertEqual(r["status"], "FAIL")
        self.assertTrue(r["fail_reasons"][0].startswith(
            "FRAME COUNT MISMATCH: Mirage scored 3 frame pairs, FFmpeg 4"))
        self.assertEqual(r["frames"]["compared"], 3)
        self.assertIn("**MISMATCH**", vd.pair_markdown(r))

    def test_settings_mismatch_fails(self):
        r = vd.evaluate_pair(PAIR, dict(PARAMS, scaler="bicubic"),
                             sides([50.0, 60.0, 70.0], [50.0, 60.0, 70.0]))
        self.assertEqual(r["status"], "FAIL")
        self.assertIn("SETTINGS MISMATCH: mirage_vmaf's report has scaler 'bilinear', "
                      "FFmpeg's run 'bicubic'", r["fail_reasons"])
        # FFmpeg's scale line has bilinear's flags, which the report flags too.
        self.assertTrue(any("flags 0x00000002" in w for w in r["warnings"]))
        for key, value in (("model", "vmaf_v0.6.1neg"), ("n_threads", 1), ("n_subsample", 2),
                           ("frame_limit", None)):
            with self.subTest(key=key):
                r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0], [1.0], **{key: value}))
                self.assertTrue(any(x.startswith(f"SETTINGS MISMATCH: mirage_vmaf's report "
                                                 f"has {key}") for x in r["fail_reasons"]))

    def test_failed_runs(self):
        r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0], [1.0], m_exit=1))
        self.assertEqual(r["fail_reasons"], ["mirage_vmaf exited with status 1"])
        self.assertIsNone(r["delta"])
        s = sides([1.0], [1.0])
        s["ffmpeg"] = {"run": None, "error": "the reference's size is unknown"}
        r = vd.evaluate_pair(PAIR, PARAMS, s)
        self.assertEqual(r["fail_reasons"], ["FFmpeg was not run: the reference's size is unknown"])
        s = sides([1.0], [1.0])
        s["ffmpeg"]["run"]["timed_out"] = True
        s["ffmpeg"]["run"]["exit_code"] = None
        r = vd.evaluate_pair(PAIR, PARAMS, s)
        self.assertEqual(r["fail_reasons"], ["FFmpeg was stopped after 60 s"])

    def test_warnings_do_not_fail(self):
        s = sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0])
        s["ffmpeg"]["log"]["version"] = "3.1.0"
        s["mirage"]["report"]["frames"][2]["dis_pts"] = 99.0
        s["mirage"]["report"]["dis"]["color_space"] = "bt470bg"
        r = vd.evaluate_pair(PAIR, PARAMS, s)
        self.assertEqual(r["status"], "PASS")
        self.assertEqual(len(r["warnings"]), 3, r["warnings"])
        self.assertTrue(any(w.startswith("libvmaf versions differ") for w in r["warnings"]))
        self.assertTrue(any(w.startswith("1 of mirage_vmaf's 3 pairs") for w in r["warnings"]))
        self.assertTrue(any(w.startswith("dis: mirage_vmaf's decoder has color_space")
                            for w in r["warnings"]))

    def test_mirage_vmafs_own_warnings(self):
        w = "the reference's frames are yuv422p, not yuv420p: ..."
        r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0],
                                                 warnings=[w]))
        self.assertEqual(r["status"], "PASS")
        self.assertEqual(r["warnings"], ["mirage_vmaf: " + w])
        self.assertEqual(r["mirage_vmaf"]["warnings"], [w])
        self.assertIn("mirage_vmaf: " + w, vd.pair_markdown(r))
        # A report without the field, as mirage_vmaf wrote before it had one.
        r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0]))
        self.assertEqual((r["warnings"], r["mirage_vmaf"]["warnings"]), ([], []))

    def test_model_file_settings(self):
        params = dict(PARAMS, model=None, model_file={"path": "/m/x.json", "sha256": "ab12"})
        good = sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0], model="/m/x.json", model_source="file",
                     model_sha256="ab12")
        r = vd.evaluate_pair(PAIR, params, good)
        self.assertEqual(r["status"], "PASS", r["fail_reasons"])
        self.assertEqual(r["mirage_vmaf"]["model_sha256"], "ab12")
        self.assertIn("mirage_vmaf: model file /m/x.json (sha256 ab12), scaler bilinear",
                      vd.pair_markdown(r))
        other = sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0], model="/m/x.json", model_source="file",
                      model_sha256="cd34")
        r = vd.evaluate_pair(PAIR, params, other)
        self.assertIn("SETTINGS MISMATCH: mirage_vmaf's report has model_sha256 'cd34', "
                      "FFmpeg's run 'ab12'", r["fail_reasons"])
        # A mirage_vmaf without the model options scored with its built-in model.
        r = vd.evaluate_pair(PAIR, params, sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0]))
        self.assertIn("SETTINGS MISMATCH: mirage_vmaf's report has model_source None, "
                      "FFmpeg's run 'file'", r["fail_reasons"])

    def test_a_model_file_where_ffmpeg_had_a_built_in_model(self):
        r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0], [1.0], model_source="file",
                                                 model_sha256="ab12"))
        self.assertIn("SETTINGS MISMATCH: mirage_vmaf's report has model_source 'file', "
                      "FFmpeg's run 'built-in'", r["fail_reasons"])
        ok = vd.evaluate_pair(PAIR, PARAMS, sides([1.0], [1.0], model_source="built-in",
                                                  model_sha256=None,
                                                  reference_mode="explicit-pair"))
        self.assertEqual(ok["status"], "PASS", ok["fail_reasons"])
        md = vd.pair_markdown(ok)
        self.assertIn("mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bilinear", md)
        self.assertIn("reference mode explicit-pair", md)

    def test_both_libvmaf_versions_of_both_sides(self):
        # D-006: the run time version and the package version, Mirage's and FFmpeg's.
        s = sides([1.0], [1.0])
        s["ffmpeg"]["libvmaf_package_version"] = "3.1.0"
        r = vd.evaluate_pair(PAIR, PARAMS, s)
        self.assertEqual(r["ffmpeg"]["libvmaf_version"], "3.0.0")
        self.assertEqual(r["ffmpeg"]["libvmaf_package_version"], "3.1.0")
        md = vd.pair_markdown(r)
        self.assertIn("| libvmaf version (run time) | 3.0.0 | 3.0.0 | |", md)
        self.assertIn("| libvmaf package version | 3.1.0 | 3.1.0 | |", md)

    def test_the_ffmpeg_libraries_of_both_sides(self):
        # D-021: mirage_vmaf's report and FFmpeg's `-version` output.
        libs = {"libavutil": "60.26.101", "libswscale": "9.5.101"}
        s = sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0], ffmpeg_libraries=libs)
        s["ffmpeg"]["ffmpeg_libraries"] = dict(libs, libavdevice="62.3.101")
        r = vd.evaluate_pair(PAIR, PARAMS, s)
        self.assertEqual(r["warnings"], [])
        self.assertEqual(r["mirage_vmaf"]["ffmpeg_libraries"], libs)
        self.assertEqual(r["ffmpeg"]["ffmpeg_libraries"]["libavdevice"], "62.3.101")
        md = vd.pair_markdown(r)
        self.assertIn("| libavutil version (run time) | 60.26.101 | 60.26.101 | |", md)
        self.assertIn("| libavdevice version (run time) | - | 62.3.101 | |", md)
        s["ffmpeg"]["ffmpeg_libraries"]["libswscale"] = "9.6.100"
        r = vd.evaluate_pair(PAIR, PARAMS, s)
        self.assertEqual(r["status"], "PASS")
        self.assertEqual(r["warnings"], ["FFmpeg library versions differ: libswscale "
                                         "mirage_vmaf '9.5.101', FFmpeg '9.6.100'"])
        # A report without the field, as mirage_vmaf wrote before D-021.
        r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0]))
        self.assertEqual((r["warnings"], r["mirage_vmaf"]["ffmpeg_libraries"]), ([], None))

    def test_frames_scored_must_match_the_frames(self):
        r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0],
                                                 frames_scored=4))
        self.assertEqual(r["status"], "FAIL")


# The dis_conversion of a mirage_vmaf report that converts as LOG's FFmpeg does.
CONVERTED = [{"from": {"color_space": "smpte170m", "color_range": "tv"},
              "to": {"color_space": "bt709", "color_range": "tv"},
              "matrix_changed": True, "range_changed": False, "first_index": 0, "pairs": 3}]


class Conversions(unittest.TestCase):
    def test_both_sides_listed(self):
        self.assertEqual(vd.scale_conversions(vd.scale_lines(LOG)),
                         ["smpte170m/tv -> bt709/tv"])
        self.assertEqual(vd.mirage_conversions(CONVERTED), ["smpte170m/tv -> bt709/tv"])
        self.assertEqual(vd.mirage_conversions([]), [])

    def test_the_same_conversion_is_no_warning(self):
        r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0],
                                                 dis_conversion=CONVERTED))
        self.assertEqual(r["warnings"], [])
        self.assertEqual(r["mirage_color"], {"dis_conversion": CONVERTED, "ref_conversion": None})
        self.assertEqual(vd.pair_summary(r)["mirage_distorted_csp"], "smpte170m -> bt709")
        md = vd.pair_markdown(r)
        self.assertIn("- smpte170m/tv -> bt709/tv: matrix changed; 3 pairs from index 0", md)

    def test_another_conversion_is_a_warning(self):
        kept = [dict(CONVERTED[0], to={"color_space": "smpte170m", "color_range": "tv"},
                     matrix_changed=False)]
        r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0],
                                                 dis_conversion=kept))
        self.assertEqual(r["status"], "PASS")
        self.assertEqual(r["warnings"], [
            "the distorted frames' color conversions differ: mirage_vmaf "
            "smpte170m/tv -> smpte170m/tv, FFmpeg's scale filter smpte170m/tv -> bt709/tv"])
        self.assertIn("- smpte170m/tv -> smpte170m/tv: matrix and range unchanged; 3 pairs "
                      "from index 0", vd.pair_markdown(r))

    def test_a_report_without_conversions(self):
        r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0]))
        self.assertEqual(r["mirage_color"], {"dis_conversion": None, "ref_conversion": None})
        self.assertEqual(vd.pair_summary(r)["mirage_distorted_csp"], "-")
        self.assertIn("- not in the report: this mirage_vmaf does not record a conversion",
                      vd.pair_markdown(r))


# FFmpeg's log of a 10-bit bt709 reference against a 640x360 smpte170m rung:
# the scale filter FFmpeg inserts converts the reference to the distorted
# frames' tags, which the distorted input's scale filter keeps.
LOG_INSERTED = LOG.replace(
    "csp:smpte170m range:tv sar:0/1 -> w:1920 h:1080 fmt:yuv420p csp:bt709 range:tv",
    "csp:smpte170m range:tv sar:0/1 -> w:1920 h:1080 fmt:yuv420p csp:smpte170m range:tv") + \
    ("[auto_scale_0 @ 0x9c508c000] w:1920 h:1080 fmt:yuv420p10le csp:bt709 range:tv sar:1/1 -> "
     "w:1920 h:1080 fmt:yuv420p csp:smpte170m range:tv sar:1/1 flags:0x00000004\n")
KEPT = [dict(CONVERTED[0], to={"color_space": "smpte170m", "color_range": "tv"},
             matrix_changed=False)]
REF_CONVERTED = [{"from": {"color_space": "bt709", "color_range": "tv"},
                  "to": {"color_space": "smpte170m", "color_range": "tv"},
                  "matrix_changed": True, "range_changed": False, "first_index": 0, "pairs": 3}]


class ReferenceConversions(unittest.TestCase):
    def test_the_inserted_scale_filter(self):
        scales = vd.scale_lines(LOG_INSERTED)
        self.assertEqual(vd.inserted_conversions(scales), ["bt709/tv -> smpte170m/tv"])
        self.assertEqual(vd.scale_conversions(scales), ["smpte170m/tv -> smpte170m/tv"])
        self.assertEqual(vd.inserted_conversions(vd.scale_lines(LOG)), [])

    def test_the_same_conversions_are_no_warning(self):
        r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0],
                                                 stderr=LOG_INSERTED, dis_conversion=KEPT,
                                                 ref_conversion=REF_CONVERTED))
        self.assertEqual(r["warnings"], [])
        self.assertEqual(r["mirage_color"]["ref_conversion"], REF_CONVERTED)
        summary = vd.pair_summary(r)
        self.assertEqual(summary["ffmpeg_reference_csp"], "bt709 -> smpte170m")
        self.assertEqual(summary["mirage_reference_csp"], "bt709 -> smpte170m")
        self.assertEqual(summary["ffmpeg_distorted_csp"], "smpte170m -> smpte170m")
        md = vd.pair_markdown(r)
        self.assertIn("Mirage's color handling of the reference frames (mirage_vmaf's report, "
                      "ref_conversion; DECISIONS.md D-022):\n- bt709/tv -> smpte170m/tv: matrix "
                      "changed; 3 pairs from index 0", md)

    def test_other_conversions_are_a_warning(self):
        # A reference converted where FFmpeg passes it on, and the reverse.
        r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0],
                                                 dis_conversion=CONVERTED,
                                                 ref_conversion=REF_CONVERTED))
        self.assertEqual(r["status"], "PASS")
        self.assertEqual(r["warnings"], [
            "the reference frames' color conversions differ: mirage_vmaf bt709/tv -> "
            "smpte170m/tv, FFmpeg's inserted scale filter none"])
        r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0],
                                                 stderr=LOG_INSERTED, dis_conversion=KEPT,
                                                 ref_conversion=[]))
        self.assertEqual(r["warnings"], [
            "the reference frames' color conversions differ: mirage_vmaf none, FFmpeg's "
            "inserted scale filter bt709/tv -> smpte170m/tv"])
        self.assertIn("- none: the reference's frames are used as they are", vd.pair_markdown(r))

    def test_a_report_without_reference_conversions(self):
        # As mirage_vmaf wrote them before D-022: nothing to compare.
        r = vd.evaluate_pair(PAIR, PARAMS, sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0],
                                                 stderr=LOG_INSERTED, dis_conversion=KEPT))
        self.assertEqual(r["warnings"], [])
        self.assertEqual(vd.pair_summary(r)["mirage_reference_csp"], "-")
        self.assertIn("- not in the report: this mirage_vmaf does not record one",
                      vd.pair_markdown(r))


class Reports(unittest.TestCase):
    def reports(self):
        ok = vd.evaluate_pair(vd.Pair("gear3_vs_gear5", REF, DIS), PARAMS,
                              sides([1.0, 2.0, 3.0], [1.0, 2.0, 3.0]))
        bad = vd.evaluate_pair(PAIR, PARAMS, sides([6.0, 7.0, 8.0], [1.0, 2.0, 3.0]))
        return ok, bad

    def test_pair_markdown(self):
        ok, bad = self.reports()
        md = vd.pair_markdown(bad)
        self.assertTrue(md.startswith("# gear2_vs_gear5: Mirage vs FFmpeg's libvmaf filter\n"))
        self.assertIn("**FAIL**:\n- 3 of 3 frames have \\|delta\\| > 0.01\n", md)
        self.assertIn("- max \\|delta\\|: 5.000000 (index 0)", md)
        self.assertIn("(index 0-2)", md)
        self.assertIn("Converts: Parsed_scale_1: color_space smpte170m -> bt709", md)
        self.assertIn("| color_space | bt709 | smpte170m |", md)
        self.assertIn("**PASS**\n", vd.pair_markdown(ok))

    def test_overall_report_and_markdown(self):
        ok, bad = self.reports()
        o = vd.overall_report(PARAMS, [ok, bad], {"started_utc": "s", "ended_utc": "e",
                                                  "git": {"head_start": "abc", "head_end": "abc",
                                                          "clean_start": True,
                                                          "clean_end": True}})
        self.assertEqual((o["status"], o["pairs_total"], o["pairs_failed"]),
                         ("FAIL", 2, ["gear2_vs_gear5"]))
        self.assertEqual(o["pairs"][1]["ffmpeg_distorted_csp"], "smpte170m -> bt709")
        md = vd.overall_markdown(o)
        self.assertIn("**Overall: FAIL**: 1 of 2 pairs fail.", md)
        self.assertIn("| gear3_vs_gear5 | PASS | 3 / 3 | 0.000000 | 0.000000 | 0 | 2.000000 "
                      "| 2.000000 | 0.000000 | smpte170m -> bt709 | - |\n", md)
        self.assertIn("- gear2_vs_gear5: 3 of 3 frames have \\|delta\\| > 0.01", md)
        self.assertIn("the working tree was clean at the start of the run and clean at its end",
                      md)
        json.loads(vd.to_json(o))

    def test_overall_markdown_states_both_libvmaf_versions(self):
        ok, _ = self.reports()
        o = vd.overall_report(PARAMS, [ok], {
            "environment": {"ffmpeg_version": "ffmpeg version 8.1.1",
                            "ffmpeg_libvmaf_package_version": "3.1.0", "os": "o", "cpu": "c"},
            "mirage_vmaf": {"sha256": "s", "libvmaf_version": "3.0.0",
                            "libvmaf_package_version": "3.1.0"},
            "ffmpeg_libvmaf": {"libvmaf_version": "3.0.0", "libvmaf_package_version": "3.1.0"}})
        md = vd.overall_markdown(o)
        self.assertIn("- mirage_vmaf: sha256 s; libvmaf 3.0.0 at run time, package 3.1.0.", md)
        self.assertNotIn("FFmpeg libraries at run time", md)
        o["mirage_vmaf"]["ffmpeg_libraries"] = {"libavutil": "60.26.101"}
        o["environment"]["ffmpeg_libraries"] = {"libavutil": "60.26.101", "libavdevice": "62.3.101"}
        self.assertIn("- FFmpeg libraries at run time: mirage_vmaf libavutil 60.26.101; FFmpeg "
                      "libavutil 60.26.101, libavdevice 62.3.101.", vd.overall_markdown(o))
        self.assertIn("- FFmpeg: ffmpeg version 8.1.1; libvmaf 3.0.0 at run time (its libvmaf "
                      "logs), package 3.1.0.", md)
        o = vd.overall_report(dict(PARAMS, model=None,
                                   model_file={"path": "/m/x.json", "sha256": "ab12"}), [ok], {})
        self.assertIn("- Settings given to FFmpeg: model file /m/x.json (sha256 ab12), scaler "
                      "bilinear, n_threads 4.", vd.overall_markdown(o))

    def test_relabel_variant_is_labeled(self):
        ok, _ = self.reports()
        o = vd.overall_report(dict(PARAMS, variant=vd.VARIANT_RELABEL), [ok], {})
        self.assertIn("Variant: DIAGNOSTIC, not the section 5 comparison", vd.overall_markdown(o))

    def test_numbers(self):
        self.assertEqual(vd.num(None), "-")
        self.assertEqual(vd.num(3), "3")
        self.assertEqual(vd.num(-1e-9), "0.000000")
        self.assertEqual(vd.num(-0.5), "-0.500000")
        self.assertEqual(vd.num(-10.0), "-10.000000")
        self.assertEqual(vd.num(1.23456, 1), "1.2")
        self.assertEqual(vd.cell("a|b\nc"), "a\\|b c")


class Files(unittest.TestCase):
    def test_xz_round_trip(self):
        data = b"Invalid timestamps\n" * 1000
        packed = vd.xz(data)
        self.assertTrue(packed.startswith(b"\xfd7zXZ\x00"))
        self.assertEqual(lzma.decompress(packed), data)

    def test_sha256sums(self):
        with tempfile.TemporaryDirectory() as d:
            os.mkdir(os.path.join(d, "p"))
            for rel, data in (("b.txt", b"b"), ("p/a.json", b"{}"), ("SHA256SUMS", b"old")):
                with open(os.path.join(d, rel), "wb") as f:
                    f.write(data)
            self.assertEqual(vd.sha256sums(d),
                             f"{hashlib.sha256(b'b').hexdigest()}  b.txt\n"
                             f"{hashlib.sha256(b'{}').hexdigest()}  p/a.json\n")


if __name__ == "__main__":
    unittest.main()
