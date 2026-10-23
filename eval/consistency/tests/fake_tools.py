"""Stand-ins for mirage_vmaf, ffmpeg and ffprobe in tests/test_run_delta.py.

usage: fake_tools.py <mirage_vmaf|ffmpeg|ffprobe> <the program's arguments>

Their inputs are fake:<width>x<height>:<color_space>[:<option>...]. Frame i
of every input scores 50 + i, except that FFmpeg's stand-in scores 5 less
when it converts the distorted input's color space (its tag differs from
the reference's and no setparams relabels it). Options of the distorted
input: mirage_offset=X adds X to Mirage's scores; ffmpeg_extra_frame makes
FFmpeg score one frame more than there are; mirage_fails makes mirage_vmaf
exit with status 1 and no report. A fake input has 10 frames.

With FAKE_MIRAGE_HAS_SCALER=1 mirage_vmaf's stand-in takes --scaler, with
FAKE_MIRAGE_HAS_MODEL=1 --model and --model-file, and reports the model as
the current mirage_vmaf does (model_source, model_sha256, reference_mode).
With FAKE_MIRAGE_HAS_D022=1 it also reports the FFmpeg libraries' versions
(those of ffmpeg's stand-in), the distorted frames' conversion to the
reference's color space, as ffmpeg's stand-in converts them, and no
conversion of the reference.
"""

import hashlib
import json
import os
import re
import sys

FRAMES = 10

# The libraries ffmpeg's stand-in prints in -version, at run time.
LIBRARIES = {"libavutil": "60.26.101", "libavcodec": "62.28.101", "libavformat": "62.12.101",
             "libavfilter": "11.14.101", "libswscale": "9.5.101"}


def parse_input(url):
    parts = url.split(":")
    width, height = (int(v) for v in parts[1].split("x"))
    opts = dict(p.partition("=")[::2] for p in parts[3:])
    return {"url": url, "width": width, "height": height, "color_space": parts[2],
            "opts": opts}


def stream(i):
    return {"codec_name": "h264", "width": i["width"], "height": i["height"],
            "pix_fmt": "yuv420p", "color_range": "tv", "color_space": i["color_space"],
            "color_transfer": "bt709", "color_primaries": i["color_space"],
            "r_frame_rate": "30000/1001", "avg_frame_rate": "30000/1001",
            "time_base": "1/90000", "start_time": "10.000000"}


def ffprobe(args):
    if "-version" in args:
        print("ffprobe version fake")
        return 0
    print(json.dumps({"programs": [], "streams": [stream(parse_input(args[-1]))]}, indent=4))
    return 0


def ffmpeg(args):
    if "-version" in args:
        print("ffmpeg version fake\nconfiguration: --enable-libvmaf")
        for name, version in LIBRARIES.items():
            major, minor, micro = (int(x) for x in version.split("."))
            print(f"{name:<14}{major:2d}.{minor:3d}.{micro:3d} / {major:2d}.{minor:3d}.{micro:3d}")
        return 0
    dis, ref = (parse_input(args[i + 1]) for i, a in enumerate(args) if a == "-i")
    graph = args[args.index("-lavfi") + 1]
    trim = re.search(r"trim=end_frame=(\d+)", graph)
    n = int(trim.group(1)) if trim else FRAMES + ("ffmpeg_extra_frame" in dis["opts"])
    relabel = re.search(r"setparams=colorspace=(\w+)", graph)
    csp = relabel.group(1) if relabel else dis["color_space"]
    lower = 5 if csp != ref["color_space"] else 0
    scores = [round(50.0 + i - lower, 6) for i in range(n)]
    with open(re.search(r"log_path=([^:]+)$", graph).group(1), "w") as f:
        json.dump({"version": "3.0.0", "fps": 1.0,
                   "frames": [{"frameNum": i, "metrics": {"vmaf": v}}
                              for i, v in enumerate(scores)],
                   "pooled_metrics": {"vmaf": {"min": min(scores), "max": max(scores),
                                               "mean": sum(scores) / n,
                                               "harmonic_mean": 0.0}},
                   "aggregate_metrics": {}}, f, indent=2)
    w, h = (int(v) for v in re.search(r"scale=(\d+):(\d+)", graph).groups())
    err = sys.stderr
    for k, i in enumerate((dis, ref)):
        print(f"  Stream #{k}:0[0x0]: Video: h264 (Main), yuv420p(tv, {i['color_space']}), "
              f"{i['width']}x{i['height']}, 29.97 fps", file=err)
        print(f"[graph 0 input from stream {k}:0 @ 0x{k + 1}] w:{i['width']} h:{i['height']} "
              f"pixfmt:yuv420p tb:1/90000 fr:30000/1001 sar:0/1 csp:{i['color_space']} "
              "range:tv alpha:unspecified", file=err)
    print(f"[Parsed_scale_1 @ 0x9] w:{dis['width']} h:{dis['height']} fmt:yuv420p csp:{csp} "
          f"range:tv sar:0/1 -> w:{w} h:{h} fmt:yuv420p csp:{ref['color_space']} range:tv "
          "sar:0/1 flags:0x00000002", file=err)
    return 0


def mirage_vmaf(args):
    has_scaler = os.environ.get("FAKE_MIRAGE_HAS_SCALER") == "1"
    has_model = os.environ.get("FAKE_MIRAGE_HAS_MODEL") == "1"
    has_d022 = os.environ.get("FAKE_MIRAGE_HAS_D022") == "1"
    if "--help" in args:
        print("Usage: mirage_vmaf --ref <url|path> --dis <url|path> --out <report.json> "
              "[--frames N]\n\nOptions:\n  --ref <url|path>      reference\n"
              "  --dis <url|path>      distorted\n  --out <report.json>   report\n"
              "  --frames N            limit\n"
              + ("  --scaler NAME         scaler\n" if has_scaler else "")
              + ("  --model NAME          model\n  --model-file <model.json>  model file\n"
                 if has_model else "")
              + "  -h, --help            print this help and exit")
        return 0
    opts = dict(zip(args[::2], args[1::2]))
    ref, dis = parse_input(opts["--ref"]), parse_input(opts["--dis"])
    print("fake mirage_vmaf: a warning", file=sys.stderr)
    if "mirage_fails" in dis["opts"]:
        return 1
    limit = int(opts["--frames"]) if "--frames" in opts else None
    n = limit or FRAMES
    offset = float(dis["opts"].get("mirage_offset", 0))
    scores = [50.0 + i + offset for i in range(n)]

    def info(i):
        s = stream(i)
        return {"url": i["url"], "width": s["width"], "height": s["height"],
                "pix_fmt": "yuv420p", "color_space": s["color_space"], "color_range": "tv",
                "color_primaries": s["color_primaries"], "color_trc": "bt709",
                "time_base": "1/90000", "ended": limit is None}

    model = {"model": "vmaf_v0.6.1"}
    if has_model:
        model = {"reference_mode": "explicit-pair", "model": opts.get("--model", "vmaf_v0.6.1"),
                 "model_source": "built-in", "model_sha256": None}
        if "--model-file" in opts:
            with open(opts["--model-file"], "rb") as f:
                model.update(model=opts["--model-file"], model_source="file",
                             model_sha256=hashlib.sha256(f.read()).hexdigest())
    report = {"tool": "mirage_vmaf", "libvmaf_version": "3.0.0",
              "libvmaf_package_version": "3.1.0", **model,
              "scaler": opts.get("--scaler", "bilinear") if has_scaler else "bilinear",
              "n_threads": 4, "n_subsample": 1,
              "scored_as": {"width": ref["width"], "height": ref["height"],
                            "pix_fmt": "yuv420p", "bit_depth": 8},
              "frame_limit": limit, "frames_scored": n,
              "pooled": {"mean": sum(scores) / n, "min": min(scores)},
              "ref": info(ref), "dis": info(dis),
              "frames": [{"index": i, "ref_pts": 10 + i / 30, "dis_pts": 10 + i / 30,
                          "vmaf": v} for i, v in enumerate(scores)]}
    if has_d022:
        report.update(
            ffmpeg_libraries={k: LIBRARIES[k] for k in ("libavutil", "libavcodec", "libavformat",
                                                        "libswscale", "libavfilter")},
            dis_conversion=[{"from": {"color_space": dis["color_space"], "color_range": "tv"},
                             "to": {"color_space": ref["color_space"], "color_range": "tv"},
                             "matrix_changed": dis["color_space"] != ref["color_space"],
                             "range_changed": False, "first_index": 0, "pairs": n}],
            ref_conversion=[])
    with open(opts["--out"], "w") as f:
        json.dump(report, f, indent=2)
    print(f"mirage_vmaf: {n} frame pairs scored, report written to {opts['--out']}")
    return 0


if __name__ == "__main__":
    sys.exit({"ffprobe": ffprobe, "ffmpeg": ffmpeg, "mirage_vmaf": mirage_vmaf}[sys.argv[1]](
        sys.argv[2:]))
