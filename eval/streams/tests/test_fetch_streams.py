"""Unit tests of eval/streams/fetch_streams.py, without network: the
playlists come from the copies Phase 0 kept in eval/baseline/inputs/, and
ffprobe is a stand-in.

python3 -I -B -m unittest discover -s eval/streams/tests -v
"""

import decimal
import json
import os
import re
import sys
import tempfile
import types
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(HERE))
import fetch_streams as fs  # noqa: E402

INPUTS = fs.PHASE0_INPUTS
EXAMPLES = "https://devstreaming-cdn.apple.com/videos/streaming/examples/"
BIPBOP = EXAMPLES + "bipbop_16x9/bipbop_16x9_variant.m3u8"
ADVANCED = EXAMPLES + "img_bipbop_adv_example_ts/master.m3u8"


def committed(name):
    with open(os.path.join(INPUTS, name), "rb") as f:
        return f.read()


SMALL_PLAYLIST = (b"#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXT-X-PLAYLIST-TYPE:VOD\n"
                  b"#EXTINF:9.5,\na.ts\n#EXTINF:3.25,\nb.ts\n#EXT-X-ENDLIST\n")


class FakeWeb:
    """Serves the copies Phase 0 kept, and SMALL_PLAYLIST for the playlists it
    kept no copy of; `fail` and `body` override URLs."""

    def __init__(self, fail=(), body=None):
        self.fail, self.body, self.asked = set(fail), dict(body or {}), []

    def __call__(self, url):
        self.asked.append(url)
        if url in self.fail:
            raise OSError("connection refused")
        if url in self.body:
            data = self.body[url]
        else:
            name = fs.file_name(BIPBOP if "bipbop_16x9/" in url else ADVANCED, url)
            path = os.path.join(INPUTS, name)
            data = committed(name) if os.path.isfile(path) else SMALL_PLAYLIST
        return 200, {"Content-Length": str(len(data)), "ETag": '"x"', "Server": "test",
                     "last-modified": "Mon, 29 Jan 2024 21:26:16 GMT"}, data


def fake_ffprobe(fail_for=()):
    calls = []

    def run(argv, **kwargs):
        calls.append(argv)
        if argv[1:] == ["-hide_banner", "-version"]:
            return types.SimpleNamespace(returncode=0, stdout=b"ffprobe version 8.1.1 test\n",
                                         stderr=b"")
        url = argv[-1]
        if any(s in url for s in fail_for):
            return types.SimpleNamespace(returncode=1, stdout=b"",
                                         stderr=b"Server returned 404 Not Found\n")
        stream = {"codec_name": "h264", "width": 640, "height": 360, "pix_fmt": "yuv420p",
                  "color_space": "smpte170m", "r_frame_rate": "30000/1001"}
        return types.SimpleNamespace(returncode=0, stderr=b"",
                                     stdout=json.dumps({"streams": [stream]}).encode())
    return run, calls


class Parsing(unittest.TestCase):
    def test_attributes_with_quoted_commas(self):
        a = fs.parse_attributes('BANDWIDTH=263851,CODECS="mp4a.40.2, avc1.4d400d",'
                                'RESOLUTION=416x234,AUDIO="bipbop_audio",FRAME-RATE=60.000')
        self.assertEqual(a, {"BANDWIDTH": "263851", "CODECS": "mp4a.40.2, avc1.4d400d",
                             "RESOLUTION": "416x234", "AUDIO": "bipbop_audio",
                             "FRAME-RATE": "60.000"})

    def test_bipbop_master(self):
        m = fs.parse_master(committed("bipbop_16x9__bipbop_16x9_variant.m3u8").decode())
        uris = [e["uri"] for e in m["entries"]]
        self.assertEqual(uris, [f"gear{i}/prog_index.m3u8" for i in (1, 2, 3, 4, 5, 0)])
        self.assertEqual([fs.has_video(e["attributes"]) for e in m["entries"]],
                         [True] * 5 + [False])
        self.assertEqual(m["entries"][0]["attributes"]["CODECS"], "mp4a.40.2, avc1.4d400d")
        self.assertEqual(len(m["iframe_entries"]), 5)
        self.assertEqual(sorted({x["TYPE"] for x in m["media"]}), ["AUDIO", "SUBTITLES"])
        self.assertEqual(len(m["media"]), 10)

    def test_advanced_master(self):
        m = fs.parse_master(committed("img_bipbop_adv_example_ts__master.m3u8").decode())
        self.assertEqual(len(m["entries"]), 24)
        self.assertEqual(sorted({e["uri"] for e in m["entries"]}),
                         [f"v{i}/prog_index.m3u8" for i in range(2, 10)])
        self.assertEqual({e["attributes"]["FRAME-RATE"] for e in m["entries"]},
                         {"30.000", "60.000"})
        self.assertTrue(all(fs.has_video(e["attributes"]) for e in m["entries"]))
        self.assertEqual(len(m["iframe_entries"]), 6)
        self.assertEqual([x["TYPE"] for x in m["media"]],
                         ["AUDIO", "AUDIO", "AUDIO", "CLOSED-CAPTIONS", "SUBTITLES"])

    def test_has_video_from_codecs_alone(self):
        self.assertTrue(fs.has_video({"CODECS": "mp4a.40.2,hvc1.2.4.L123.B0"}))
        self.assertFalse(fs.has_video({"CODECS": "mp4a.40.2"}))
        self.assertFalse(fs.has_video({}))

    def test_media_playlist_of_gear5(self):
        text = committed("bipbop_16x9__gear5__prog_index.m3u8").decode()
        md = fs.parse_media(text)
        extinf = re.findall(r"^#EXTINF:([0-9.]+)", text, re.M)
        self.assertEqual(md["segments"], len(extinf))
        self.assertEqual(decimal.Decimal(md["duration"]),
                         sum(map(decimal.Decimal, extinf), decimal.Decimal(0)))
        self.assertEqual((md["first_extinf"], md["last_extinf"]), (extinf[0], extinf[-1]))
        self.assertEqual(md["byte_ranges"], len(extinf))
        self.assertEqual(md["segment_files"], 1)
        self.assertEqual((md["target_duration"], md["playlist_type"], md["version"]),
                         ("11", "VOD", "4"))
        self.assertTrue(md["endlist"])
        self.assertEqual((md["discontinuities"], md["maps"]), (0, 0))

    def test_media_playlist_tags(self):
        md = fs.parse_media("#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MAP:URI=\"init.mp4\"\n"
                            "#EXTINF:6.006,\nseg1.m4s\n#EXT-X-DISCONTINUITY\n"
                            "#EXT-X-DISCONTINUITY-SEQUENCE:2\n#EXTINF:2.002,\nseg2.m4s\n")
        self.assertEqual((md["segments"], md["duration"], md["discontinuities"], md["maps"]),
                         (2, "8.008", 1, 1))
        self.assertEqual(md["segment_files"], 2)
        self.assertFalse(md["endlist"])
        self.assertIsNone(md["playlist_type"])

    def test_names(self):
        self.assertEqual(fs.stream_name(BIPBOP), "bipbop_16x9")
        self.assertEqual(fs.file_name(BIPBOP, BIPBOP), "bipbop_16x9__bipbop_16x9_variant.m3u8")
        self.assertEqual(fs.file_name(BIPBOP, EXAMPLES + "bipbop_16x9/gear5/prog_index.m3u8"),
                         "bipbop_16x9__gear5__prog_index.m3u8")
        self.assertEqual(fs.file_name(ADVANCED, ADVANCED),
                         "img_bipbop_adv_example_ts__master.m3u8")
        self.assertEqual(fs.file_name(BIPBOP, "https://other.example/x/y.m3u8?a=1"),
                         "bipbop_16x9__other.example__x__y.m3u8")

    def test_streams_file(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "s.txt")
            with open(path, "w") as f:
                f.write(f"# comment\n\n{BIPBOP}\n {ADVANCED} \n")
            self.assertEqual(fs.read_streams(path), [BIPBOP, ADVANCED])
            with open(path, "w") as f:
                f.write("# only a comment\n")
            self.assertRaises(ValueError, fs.read_streams, path)
            with open(path, "w") as f:
                f.write("ftp://example.com/a.m3u8\n")
            self.assertRaises(ValueError, fs.read_streams, path)

    def test_default_streams_file_lists_both_masters(self):
        self.assertEqual(fs.read_streams(fs.DEFAULT_STREAMS), [BIPBOP, ADVANCED])


class EndToEnd(unittest.TestCase):
    def run_tool(self, argv, web, runner=None):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        out = os.path.join(tmp.name, "out")
        streams = os.path.join(tmp.name, "streams.txt")
        with open(streams, "w") as f:
            f.write(BIPBOP + "\n")
        with mock.patch("sys.stdout"), mock.patch("sys.stderr"):
            rc = fs.main([out, "--streams", streams] + argv, fetch=web,
                         runner=runner or fake_ffprobe()[0])
        report = None
        if os.path.exists(os.path.join(out, "streams.json")):
            with open(os.path.join(out, "streams.json")) as f:
                report = json.load(f)
        return rc, out, report

    def test_fetches_the_master_and_every_playlist_once(self):
        web = FakeWeb()
        rc, out, report = self.run_tool([], web)
        self.assertEqual(rc, 0)
        self.assertEqual(len(web.asked), 7)            # the master and six playlists
        self.assertEqual(len(set(web.asked)), 7)
        s = report["streams"][0]
        self.assertEqual(s["name"], "bipbop_16x9")
        self.assertEqual(s["master"]["phase0_copy"], "same")
        copies = {p["file"]: p["phase0_copy"] for p in s["playlists"]}
        self.assertEqual(copies["bipbop_16x9__gear5__prog_index.m3u8"], "same")
        self.assertEqual(copies["bipbop_16x9__gear1__prog_index.m3u8"], "same")
        self.assertIsNone(copies["bipbop_16x9__gear2__prog_index.m3u8"])
        self.assertEqual(s["master"]["headers"],
                         {"Last-Modified": "Mon, 29 Jan 2024 21:26:16 GMT", "ETag": '"x"',
                          "Content-Length": str(len(committed(
                              "bipbop_16x9__bipbop_16x9_variant.m3u8")))})
        gear0 = [p for p in s["playlists"] if "gear0" in p["file"]][0]
        self.assertFalse(gear0["video"])
        # The playlists are kept as fetched, with their headers and checksums.
        with open(os.path.join(out, "bipbop_16x9__gear5__prog_index.m3u8"), "rb") as f:
            self.assertEqual(f.read(), committed("bipbop_16x9__gear5__prog_index.m3u8"))
        with open(os.path.join(out, "bipbop_16x9__gear5__prog_index.m3u8.headers")) as f:
            self.assertEqual(f.readline(), "HTTP status: 200\n")
        with open(os.path.join(out, "SHA256SUMS")) as f:
            sums = dict(line.split()[::-1] for line in f)
        self.assertEqual(set(sums), set(os.listdir(out)) - {"SHA256SUMS"})
        with open(os.path.join(out, "streams.md")) as f:
            md = f.read()
        self.assertIn("same as Phase 0's copy", md)
        self.assertIn("6 EXT-X-STREAM-INF entries naming 6 playlists, 5 of them with video", md)
        self.assertNotIn(out, md)
        with open(os.path.join(out, "cmd.txt")) as f:
            self.assertIn("eval/streams/fetch_streams.py <out_dir>", f.read())

    def test_a_changed_playlist_is_flagged(self):
        gear5 = EXAMPLES + "bipbop_16x9/gear5/prog_index.m3u8"
        web = FakeWeb(body={gear5: committed("bipbop_16x9__gear5__prog_index.m3u8") + b"\n"})
        rc, out, report = self.run_tool([], web)
        self.assertEqual(rc, 0)
        files = {p["file"]: p for p in report["streams"][0]["playlists"]}
        self.assertEqual(files["bipbop_16x9__gear5__prog_index.m3u8"]["phase0_copy"], "differs")
        with open(os.path.join(out, "streams.md")) as f:
            self.assertIn("| `bipbop_16x9__gear5__prog_index.m3u8` | differs |", f.read())

    def test_a_failed_fetch_fails_the_run(self):
        gear3 = EXAMPLES + "bipbop_16x9/gear3/prog_index.m3u8"
        rc, out, report = self.run_tool([], FakeWeb(fail=[gear3]))
        self.assertEqual(rc, 1)
        self.assertEqual(len(report["errors"]), 1)
        self.assertIn("gear3", report["errors"][0])
        with open(os.path.join(out, "streams.md")) as f:
            md = f.read()
        self.assertIn("**Failures:**", md)
        self.assertIn("not fetched: OSError: connection refused", md)

    def test_a_failed_master_stops_that_stream(self):
        web = FakeWeb(fail=[BIPBOP])
        rc, _, report = self.run_tool([], web)
        self.assertEqual(rc, 1)
        self.assertEqual(web.asked, [BIPBOP])
        self.assertEqual(report["streams"][0]["playlists"], [])

    def test_probe_runs_ffprobe_on_the_video_playlists_only(self):
        runner, calls = fake_ffprobe()
        rc, out, report = self.run_tool(["--probe", "--ffprobe", "/x/ffprobe"], FakeWeb(),
                                        runner)
        self.assertEqual(rc, 0)
        probed = [c[-1] for c in calls if c[1:] != ["-hide_banner", "-version"]]
        self.assertEqual(sorted(probed), sorted(
            EXAMPLES + f"bipbop_16x9/gear{i}/prog_index.m3u8" for i in (1, 2, 3, 4, 5)))
        self.assertEqual(report["ffprobe"], {"version": "ffprobe version 8.1.1 test"})
        p = [p for p in report["streams"][0]["playlists"] if "gear2" in p["file"]][0]
        self.assertEqual(p["probe"]["stream"]["color_space"], "smpte170m")
        self.assertIsNone(p["probe"]["stream"]["color_primaries"])
        self.assertTrue(os.path.isfile(os.path.join(
            out, "bipbop_16x9__gear2__prog_index.m3u8.ffprobe.json")))
        with open(os.path.join(out, "cmd.txt")) as f:
            cmd = f.read()
        self.assertIn("--probe --ffprobe <ffprobe>", cmd)
        self.assertIn("$ <ffprobe> -hide_banner -v error -select_streams v:0", cmd)
        self.assertNotIn("/x/ffprobe", cmd + json.dumps(report))

    def test_a_failed_probe_fails_the_run(self):
        runner, _ = fake_ffprobe(fail_for=["gear4"])
        rc, out, report = self.run_tool(["--probe"], FakeWeb(), runner)
        self.assertEqual(rc, 1)
        self.assertIn("404 Not Found", report["errors"][0])
        with open(os.path.join(out, "streams.md")) as f:
            self.assertIn("failed: ffprobe exited with status 1", f.read())

    def test_out_dir_must_be_new_or_empty(self):
        with tempfile.TemporaryDirectory() as d:
            open(os.path.join(d, "x"), "w").close()
            with mock.patch("sys.stderr"):
                self.assertEqual(fs.main([d], fetch=FakeWeb()), 2)


if __name__ == "__main__":
    unittest.main()
