"""Stand-ins for mirage_vmaf, ffmpeg and ffprobe in tests/test_run_delta.py.

usage: fake_tools.py <mirage_vmaf|ffmpeg|ffprobe> <the program's arguments>

Their inputs are fake:<width>x<height>:<color_space>[:<option>...]. A fake
input has 10 frames, frame k at k / 30 s from the input's start, 10 s, and
showing picture k. Options: frames=N gives it N frames; drop=K leaves out
its frame K, the others keeping their times. Options of the distorted
input: mirage_offset=X adds X to Mirage's scores; mirage_fails makes
mirage_vmaf exit with status 1 and no report.

A pair of frames of the same picture scores 50 + the picture, except that
FFmpeg's stand-in scores 5 less when it converts the distorted input's
color space (its tag differs from the reference's and no setparams
relabels it); a pair of other pictures scores 20. FFmpeg's stand-in pairs
the frames as its libvmaf filter's framesync does, each distorted frame
with the reference frame at or before its time and, after the distorted
input's last frame, each later reference frame with that frame, and logs
the input files' starts, the frames of the showinfo filters of
vmafdelta.filtergraph() and the swscale flags of its graph's flags= in its
scale filter's line.

mirage_vmaf's stand-in pairs the inputs' frames in decode order, as the
programs before D-028 do, and scores with the fixed bilinear scaler of the
programs before --scaler existed. With FAKE_MIRAGE_HAS_SCALER=1 it takes
--scaler, bicubic unless given, as mirage_vmaf does since D-024; with
FAKE_MIRAGE_HAS_MODEL=1 --model and --model-file, and it reports the model
as the current mirage_vmaf does (model_source, model_sha256,
reference_mode). With FAKE_MIRAGE_HAS_D022=1 it also reports the FFmpeg
libraries' versions (those of ffmpeg's stand-in), the distorted frames'
conversion to the reference's color space, as ffmpeg's stand-in converts
them, and no conversion of the reference; with FAKE_MIRAGE_HAS_D027=1 its
pooled values include the harmonic mean and the minimum without the first
frame; with FAKE_MIRAGE_HAS_D028=1 it pairs frames of the same time only
(rule A), counts the others as failed and reports the alignment, and each
pair's frame positions. FFmpeg's stand-in logs libvmaf's pooled mean and
harmonic mean at the 6 decimals of FFmpeg's log.
"""

import hashlib
import json
import os
import re
import sys

FRAMES = 10
START = 10.0        # every fake input's start, in seconds
TICKS = 3000        # a frame's duration in ticks of 1/90000 s

# The libraries ffmpeg's stand-in prints in -version, at run time.
LIBRARIES = {"libavutil": "60.26.101", "libavcodec": "62.28.101", "libavformat": "62.12.101",
             "libavfilter": "11.14.101", "libswscale": "9.5.101"}

# The swscale flags ffmpeg's stand-in states for its scale filter's flags=.
SWS_FLAGS = {"bilinear": 0x2, "bicubic": 0x4, "lanczos": 0x200}


def harmonic(scores):
    """libvmaf's harmonic mean: n / sum(1 / (x + 1)) - 1."""
    return len(scores) / sum(1.0 / (x + 1.0) for x in scores) - 1.0


def parse_input(url):
    parts = url.split(":")
    width, height = (int(v) for v in parts[1].split("x"))
    opts = dict(p.partition("=")[::2] for p in parts[3:])
    return {"url": url, "width": width, "height": height, "color_space": parts[2],
            "opts": opts}


def frame_times(i):
    """The times of an input's frames, in frames from its start, in decode
    order; a frame shows the picture of its time."""
    n = int(i["opts"].get("frames", FRAMES))
    drop = int(i["opts"]["drop"]) if "drop" in i["opts"] else None
    return [k for k in range(n) if k != drop]


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


def framesync(dis, ref):
    """(distorted position, reference position or None, time) per event."""
    events = []
    for i, t in enumerate(dis):
        before = [j for j, r in enumerate(ref) if r <= t]
        events.append((i, before[-1] if before else None, t))
    if dis:
        events += [(len(dis) - 1, j, r) for j, r in enumerate(ref) if r > dis[-1]]
    return events


def showinfo(err, index, times):
    tag = f"[Parsed_showinfo_{index} @ 0x{index}0]"
    print(f"{tag} config in time_base: 1/90000, frame_rate: 30000/1001", file=err)
    for n, t in enumerate(times):
        print(f"{tag} n:{n:4d} pts:{t * TICKS:7d} pts_time:{t / 30:<7.6g} duration:   {TICKS} "
              "duration_time:0.0333333 fmt:yuv420p cl:left sar:1/1 s:1920x1080 i:P iskey:1 "
              "type:I ", file=err)


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
    dis_t, ref_t = frame_times(dis), frame_times(ref)
    if trim:
        dis_t, ref_t = dis_t[:int(trim.group(1))], ref_t[:int(trim.group(1))]
    events = framesync(dis_t, ref_t)
    relabel = re.search(r"setparams=colorspace=(\w+)", graph)
    csp = relabel.group(1) if relabel else dis["color_space"]
    lower = 5 if csp != ref["color_space"] else 0
    scores = [round(50.0 + dis_t[i] - lower, 6) if dis_t[i] == ref_t[j] else 20.0
              for i, j, _ in events if j is not None]
    n = len(scores)
    with open(re.search(r"log_path=([^:,]+)", graph).group(1), "w") as f:
        json.dump({"version": "3.0.0", "fps": 1.0,
                   "frames": [{"frameNum": i, "metrics": {"vmaf": v}}
                              for i, v in enumerate(scores)],
                   "pooled_metrics": {"vmaf": {"min": min(scores), "max": max(scores),
                                               "mean": round(sum(scores) / n, 6),
                                               "harmonic_mean": round(harmonic(scores), 6)}},
                   "aggregate_metrics": {}}, f, indent=2)
    w, h = (int(v) for v in re.search(r"scale=(\d+):(\d+)", graph).groups())
    flags = SWS_FLAGS[re.search(r"scale=\d+:\d+:flags=(\w+)", graph).group(1)]
    err = sys.stderr
    for k, i in enumerate((dis, ref)):
        print(f"Input #{k}, fake, from '{i['url']}':\n  Duration: 00:00:00.33, start: "
              f"{START:.6f}, bitrate: N/A", file=err)
    for k, i in enumerate((dis, ref)):
        print(f"  Stream #{k}:0[0x0]: Video: h264 (Main), yuv420p(tv, {i['color_space']}), "
              f"{i['width']}x{i['height']}, 29.97 fps", file=err)
        print(f"[graph 0 input from stream {k}:0 @ 0x{k + 1}] w:{i['width']} h:{i['height']} "
              f"pixfmt:yuv420p tb:1/90000 fr:30000/1001 sar:0/1 csp:{i['color_space']} "
              "range:tv alpha:unspecified", file=err)
    print(f"[Parsed_scale_1 @ 0x9] w:{dis['width']} h:{dis['height']} fmt:yuv420p csp:{csp} "
          f"range:tv sar:0/1 -> w:{w} h:{h} fmt:yuv420p csp:{ref['color_space']} range:tv "
          f"sar:0/1 flags:0x{flags:08x}", file=err)
    showinfo(err, 3, dis_t)
    showinfo(err, 5, ref_t)
    showinfo(err, 7, [t for _, _, t in events])
    return 0


def rule_a(ref_t, dis_t, limit):
    """mirage_vmaf's pairing since D-028, for frames on one grid: the frames of
    the same time pair; (pairs as (ref position, dis position), failures)."""
    pairs, failures = [], []
    by_time = {t: i for i, t in enumerate(dis_t)}
    for j, t in enumerate(ref_t):
        if limit and len(pairs) == limit:
            break
        if t in by_time:
            pairs.append((j, by_time.pop(t)))
        else:
            failures.append({"input": "reference", "index": j, "pts": START + t / 30,
                             "reason": "unmatched_reference"})
    if not limit or len(pairs) < limit:
        failures += [{"input": "distorted", "index": i, "pts": START + dis_t[i] / 30,
                      "reason": "extra_distorted"} for i in sorted(by_time.values())]
    return pairs, failures


def mirage_vmaf(args):
    has_scaler = os.environ.get("FAKE_MIRAGE_HAS_SCALER") == "1"
    has_model = os.environ.get("FAKE_MIRAGE_HAS_MODEL") == "1"
    has_d022 = os.environ.get("FAKE_MIRAGE_HAS_D022") == "1"
    has_d027 = os.environ.get("FAKE_MIRAGE_HAS_D027") == "1"
    has_d028 = os.environ.get("FAKE_MIRAGE_HAS_D028") == "1"
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
    ref_t, dis_t = frame_times(ref), frame_times(dis)
    if has_d028:
        pairs, failures = rule_a(ref_t, dis_t, limit)
    else:
        n = min(len(ref_t), len(dis_t))
        pairs, failures = [(k, k) for k in range(min(n, limit) if limit else n)], []
    n = len(pairs)
    offset = float(dis["opts"].get("mirage_offset", 0))
    scores = [50.0 + dis_t[i] + offset if ref_t[j] == dis_t[i] else 20.0 for j, i in pairs]

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
              "scaler": opts.get("--scaler", "bicubic") if has_scaler else "bilinear",
              "n_threads": 4, "n_subsample": 1,
              "scored_as": {"width": ref["width"], "height": ref["height"],
                            "pix_fmt": "yuv420p", "bit_depth": 8},
              "frame_limit": limit, "frames_scored": n,
              "pooled": {"mean": sum(scores) / n, "min": min(scores)}
                        | ({"harmonic_mean": harmonic(scores),
                            "min_without_first_frame": min(scores[1:]) if n > 1 else None}
                           if has_d027 else {}),
              "ref": info(ref), "dis": info(dis),
              "frames": [{"index": k, "ref_pts": START + ref_t[j] / 30,
                          "dis_pts": START + dis_t[i] / 30, "vmaf": v}
                         for k, ((j, i), v) in enumerate(zip(pairs, scores))]}
    if has_d028:
        for f, (j, i) in zip(report["frames"], pairs):
            f.update(ref_index=j, dis_index=i)
        rate = len(failures) / (n + len(failures))
        report["alignment"] = {
            "rule": "reference-timeline", "timestamp_origin": "input-start", "grid": "reference",
            "tolerance": 1 / 60,
            "reference": {"frame_rate": "30/1", "start": START, "start_from": "container"},
            "distorted": {"frame_rate": "30/1", "start": START, "start_from": "container"},
            "paired": n, "failed": len(failures), "failure_rate": rate, "threshold": 0.01,
            "exceeded": rate > 0.01,
            "unmatched_reference": sum(1 for f in failures if f["input"] == "reference"),
            "extra_distorted": sum(1 for f in failures if f["input"] == "distorted"),
            "duplicate_distorted": 0, "no_timestamp": 0, "after_discontinuity": 0,
            "discontinuities": 0, "discontinuity_jump": 10, "off_grid_reference": 0,
            "off_grid_distorted": 0, "vmaf_contexts": [{"first_index": 0, "pairs": n}],
            "failures": failures}
        report["warnings"] = ([f"{100 * rate:.2f}% of the frames failed to pair by timestamp"]
                              if rate > 0.01 else [])
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
