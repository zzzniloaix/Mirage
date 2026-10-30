"""Synthetic clips for the alignment cases of docs/DECISIONS.md D-028.

D-028 has Mirage pair the reference's and the distorted input's frames by
their timestamps, on the reference's timeline (rule A, src/VMAFAlign.h),
where FFmpeg's libvmaf filter pairs each distorted frame with the
reference frame at or before its time (rule B, its framesync;
docs/plans/frame-alignment.md). This script makes inputs whose frames
don't line up in the ways the work order's section 1.4 and the plan's
"Tests" name: a dropped distorted frame, a dropped reference frame, a
duplicated timestamp, half the frame rate, a later start, and timestamps
that restart midway; and two controls. Scoring them with run_delta.sh shows
what each side does with them.

usage: python3 -I -B eval/consistency/align_cases.py <clips_dir> [--ffmpeg FFMPEG]
                                                     [--ffprobe FFPROBE]

The clips are the first 10 pictures of FFmpeg's testsrc2 pattern at
30000/1001 fps, as in color_cases.py: references at 1920x1080, distorted
inputs scaled to 640x360, 8-bit yuv420p, all tagged bt709/tv, so that
neither side converts their color. Pictures are left out, repeated or
retimed with FFmpeg's select, loop, setpts and fps filters. Most clips are
uncompressed in Matroska, whose timestamps are milliseconds; the *_ts clips
are MPEG-TS at 90 kHz, coded by FFmpeg's MPEG-2 encoder with every frame a
key frame at a fixed quantizer, so that a clip made in two parts has the
pictures of one made at once.

Writes into <clips_dir>, a new or empty directory:

  ref*.mkv, dis*.mkv, ref_ts.ts, dis_ts*.ts   the clips
  pairs.txt    the cases, for run_delta.sh --pairs
  clips.txt    for every clip: the commands that made it, its streams as
               ffprobe reads them back, each frame's timestamp (ffprobe) and
               the MD5 of its picture (framemd5), and the pictures and
               timestamps as checked

Every clip's frames are checked against what it should hold: each frame's
picture, by its MD5, against the clip that holds every picture at that
size and in that container; and its timestamp, the time of a frame slot of
the pattern: in Matroska that slot's time in milliseconds, in MPEG-TS that
many frames of 3003 ticks after the clip's first frame. Paths in clips.txt
show as <clips_dir>, the programs as <ffmpeg> and <ffprobe>. Python 3
standard library only.

Exit status: 0 the clips are as intended; 1 a program failed or a clip is
not as intended; 2 bad arguments.
"""

import argparse
import collections
import fractions
import os
import sys

HERE = os.path.dirname(os.path.realpath(__file__))
sys.dont_write_bytecode = True   # no __pycache__ in the repository's working tree
sys.path.insert(0, HERE)
import color_cases as cc  # noqa: E402

DEFAULT_FFMPEG = cc.DEFAULT_FFMPEG
REF_SIZE, DIS_SIZE, RATE = cc.REF_SIZE, cc.DIS_SIZE, cc.RATE
PICTURES = 10

# A frame slot's duration: in seconds, and in ticks of MPEG-TS's 90 kHz.
SLOT = fractions.Fraction(1001, 30000)
TS_SLOT = 3003

TAGS = "setparams=colorspace=bt709:range=tv"

# How each container's clips are coded.
CODING = {
    "mkv": ["-c:v", "rawvideo", "-f", "matroska"],
    "ts": ["-c:v", "mpeg2video", "-q:v", "2", "-g", "1", "-bf", "0", "-f", "mpegts"],
}
EXTENSION = {"mkv": ".mkv", "ts": ".ts"}

# A clip made in parts: each part's first filters, which pick its pictures
# and times from the pattern (the rest of the chain is the clip's), and its
# further output options. The parts are written one after the other.
Part = collections.namedtuple("Part", "head options", defaults=((),))
# frames: what the clip holds, (picture, slot) per frame in decode order;
# filters: added after the tags; audio: an audio track from 0 as well.
Clip = collections.namedtuple("Clip", "name size container frames filters options parts audio",
                              defaults=("", (), (Part("trim=end_frame=10"),), False))
Case = collections.namedtuple("Case", "label ref dis about")


def every(skip=()):
    """(picture, slot) for every picture at its own slot, but those in `skip`."""
    return tuple((k, k) for k in range(PICTURES) if k not in skip)


REF = Clip("ref", REF_SIZE, "mkv", every())
REF_DROP5 = Clip("ref_drop5", REF_SIZE, "mkv", every(skip={5}), ",select=not(eq(n\\,5))")
REF_TS = Clip("ref_ts", REF_SIZE, "ts", every())
DIS = Clip("dis", DIS_SIZE, "mkv", every())
DIS_DROP5 = Clip("dis_drop5", DIS_SIZE, "mkv", every(skip={5}), ",select=not(eq(n\\,5))")
# loop=...:start=6 repeats picture 5: FFmpeg 8.1.1's loop filter compares its
# start with its input's frame count, which counts the frame it is given
# (libavfilter/f_loop.c, filter_frame); the MD5s check the pictures. setpts
# then gives the repeat its picture's timestamp, and -fps_mode passthrough
# keeps both frames.
DIS_DUP5 = Clip("dis_dup5", DIS_SIZE, "mkv", every()[:6] + ((5, 5),) + every()[6:],
                ",loop=loop=1:size=1:start=6,setpts=(N-gt(N\\,5))/(FRAME_RATE*TB)",
                ("-fps_mode", "passthrough"))
DIS_HALF = Clip("dis_half", DIS_SIZE, "mkv", tuple((k, k) for k in range(0, PICTURES, 2)),
                ",fps=15000/1001")
DIS_FROM3 = Clip("dis_from3", DIS_SIZE, "mkv", every(skip={0, 1, 2}), ",select=gte(n\\,3)")
DIS_FROM3_AUDIO = DIS_FROM3._replace(name="dis_from3_audio", audio=True)
DIS_TS = Clip("dis_ts", DIS_SIZE, "ts", every())
# Two MPEG-TS files written one after the other, the second restarting its
# timestamps at those of the first, its first packets marked as discontinuous.
DIS_TS_RESTART5 = Clip("dis_ts_restart5", DIS_SIZE, "ts",
                       every()[:5] + tuple((k, k - 5) for k in range(5, PICTURES)),
                       parts=(Part("trim=end_frame=5"),
                              Part("trim=start_frame=5:end_frame=10,setpts=PTS-STARTPTS",
                                   ("-mpegts_flags", "+initial_discontinuity"))))

CASES = (
    Case("a00_aligned", REF, DIS, "the frames line up (a control)"),
    Case("a01_dropped_distorted", REF, DIS_DROP5,
         "the distorted input without its frame 5, the others' timestamps kept (section 1.4's "
         "acceptance stream)"),
    Case("a02_dropped_reference", REF_DROP5, DIS,
         "the reference without its frame 5, the others' timestamps kept"),
    Case("a03_duplicated_timestamp", REF, DIS_DUP5,
         "the distorted input's frame 5 twice, under its timestamp"),
    Case("a04_half_rate", REF, DIS_HALF,
         "the distorted input at half the frame rate, every other picture"),
    Case("a05_later_start", REF, DIS_FROM3,
         "the distorted input without its first 3 frames, the others' timestamps kept; its "
         "container starts at its first frame"),
    Case("a06_later_start_audio", REF, DIS_FROM3_AUDIO,
         "the same, with an audio track from 0, so that its container starts at 0"),
    Case("a07_restart", REF_TS, DIS_TS_RESTART5,
         "MPEG-TS whose distorted timestamps restart at frame 5, against an MPEG-TS reference"),
    Case("a08_ms_vs_90khz", REF, DIS_TS,
         "the frames line up, the reference's timestamps in milliseconds (Matroska) and the "
         "distorted input's at 90 kHz (MPEG-TS) (a control of time bases)"),
)

# The clips whose pictures the others' are checked against, by size and container.
FULL = {(x.size, x.container): x for x in (REF, REF_TS, DIS, DIS_TS)}


def all_clips(cases=CASES):
    """Every clip the cases use, once each: the full clips first, then the
    references, then the distorted inputs, in case order."""
    seen = {}
    for x in list(FULL.values()) + [c.ref for c in cases] + [c.dis for c in cases]:
        seen.setdefault(x.name, x)
    full = [x for x in seen.values() if x in FULL.values()]
    rest = [x for x in seen.values() if x not in FULL.values()]
    return full + sorted(rest, key=lambda x: (x.name[:3] != "ref", list(seen).index(x.name)))


def file_name(x):
    return x.name + EXTENSION[x.container]


def chain(x, part):
    """The video filters of a clip's part: its pictures, scaled to the
    clip's size, in yuv420p, tagged, then the clip's own filters."""
    scale = "" if x.size == REF_SIZE else f"scale={x.size[0]}:{x.size[1]}:flags=bicubic,"
    return f"{part.head},{scale}format=yuv420p,{TAGS}{x.filters}"


def make_commands(ffmpeg, x, path):
    """The ffmpeg runs that make clip `x` at `path`: one per part, each to
    the clip's path or, for a clip in parts, to path.partN."""
    w, h = REF_SIZE
    out = []
    for n, part in enumerate(x.parts, 1):
        target = path if len(x.parts) == 1 else f"{path}.part{n}"
        argv = [ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-f", "lavfi",
                "-i", f"testsrc2=size={w}x{h}:rate={RATE}"]
        if x.audio:
            samples = PICTURES * 48000 * SLOT
            argv += ["-f", "lavfi", "-i", "anullsrc=r=48000:cl=mono", "-filter_complex",
                     f"[0:v]{chain(x, part)}[v];[1:a]atrim=end_sample={int(samples)}[a]",
                     "-map", "[v]", "-map", "[a]", "-c:a", "pcm_s16le"]
        else:
            argv += ["-vf", chain(x, part)]
        out.append(argv + list(x.options) + list(part.options) + CODING[x.container] + [target])
    return out


def streams_command(ffprobe, path):
    return [ffprobe, "-v", "error", "-show_entries",
            "format=start_time:stream=index,codec_type,codec_name,width,height,pix_fmt,"
            "color_space,color_range,time_base,r_frame_rate,avg_frame_rate,start_time",
            "-of", "default=noprint_wrappers=1", path]


def times_command(ffprobe, path):
    """Each video frame's pts in its stream's time base, as the demuxer and
    decoder give it, without ffmpeg's corrections."""
    return [ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", "frame=pts",
            "-of", "csv=p=0", path]


def md5_command(ffmpeg, path):
    """The MD5 of each decoded video frame, every frame passed through."""
    return [ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-i", path, "-map", "0:v:0",
            "-fps_mode", "passthrough", "-c:v", "rawvideo", "-f", "framemd5", "-"]


def parse_md5s(text):
    """The hashes of framemd5's frame lines."""
    return [line.rsplit(",", 1)[1].strip() for line in text.splitlines()
            if line and not line.startswith("#")]


def parse_times(text):
    """The pts of times_command()'s lines: the first field of each (ffprobe
    ends an MPEG-TS frame's line with an empty field), None for N/A."""
    out = []
    for line in text.splitlines():
        if line.strip():
            s = line.split(",")[0].strip()
            out.append(None if s in ("", "N/A") else int(s))
    return out


def ms(slot):
    """A slot's time in milliseconds, as FFmpeg's Matroska muxer rounds it."""
    t = slot * SLOT * 1000
    return int(t + fractions.Fraction(1, 2)) if t >= 0 else -int(-t + fractions.Fraction(1, 2))


def expected_times(x, times):
    """The pts each frame of clip `x` should have, given the clip's first
    frame's pts `times[0]` for MPEG-TS, whose muxer starts later than 0."""
    if x.container == "mkv":
        return [ms(s) for _, s in x.frames]
    first = times[0] if times and times[0] is not None else 0
    return [first + (s - x.frames[0][1]) * TS_SLOT for _, s in x.frames]


def frame_problems(x, times, md5s, pictures):
    """How the frames of clip `x` differ from what it should hold; `pictures`
    are the MD5s of the full clip of its size and container, by picture."""
    problems = []
    if len(md5s) != len(x.frames) or len(times) != len(x.frames):
        return [f"{x.name}: {len(md5s)} frames (MD5s) and {len(times)} timestamps, not "
                f"{len(x.frames)}"]
    for k, ((picture, slot), md5, t, want) in enumerate(zip(x.frames, md5s, times,
                                                          expected_times(x, times))):
        if md5 != pictures[picture]:
            problems.append(f"{x.name}: frame {k} is not picture {picture}")
        if t != want:
            problems.append(f"{x.name}: frame {k} has pts {t}, not {want} (slot {slot})")
    return problems


def pairs_text(cases, clips_dir, title):
    """A pairs file for run_delta.sh: '<label> <reference> <distorted>' per line."""
    lines = [f"# {title}", "# Made by eval/consistency/align_cases.py; <label> <reference> "
             "<distorted>. The cases:"]
    lines += [f"#   {c.label}: {c.about}" for c in cases]
    lines += [f"{c.label} {os.path.join(clips_dir, file_name(c.ref))} "
              f"{os.path.join(clips_dir, file_name(c.dis))}" for c in cases]
    return "\n".join(lines) + "\n"


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="align_cases.py",
        description="Make the synthetic clips and the pairs file of the alignment cases "
                    "(docs/DECISIONS.md D-028) in CLIPS_DIR.")
    p.add_argument("clips_dir", help="a new or empty directory")
    p.add_argument("--ffmpeg", default=DEFAULT_FFMPEG, help=f"default: {DEFAULT_FFMPEG}")
    p.add_argument("--ffprobe", help="default: the ffprobe next to --ffmpeg")
    return p.parse_args(argv)


def run(argv):
    return cc.run(argv)


def concatenate(parts, path):
    with open(path, "wb") as out:
        for part in parts:
            with open(part, "rb") as f:
                out.write(f.read())
    for part in parts:
        os.remove(part)


def main(argv=None):
    args = parse_args(argv)
    clips_dir = os.path.abspath(args.clips_dir)
    if os.path.exists(clips_dir) and (not os.path.isdir(clips_dir) or os.listdir(clips_dir)):
        print(f"align_cases: {args.clips_dir} exists and is not an empty directory",
              file=sys.stderr)
        return 2
    os.makedirs(clips_dir, exist_ok=True)
    ffprobe = args.ffprobe or os.path.join(os.path.dirname(args.ffmpeg), "ffprobe")
    programs = {args.ffmpeg: "<ffmpeg>", ffprobe: "<ffprobe>"}
    clips = all_clips()

    def shown(argv):
        return cc.shown(argv, clips_dir, programs)
    record = ["# The clips of eval/consistency/align_cases.py, made in <clips_dir>: for each, "
              "the commands that made it, its streams as <ffprobe> reads them back, each "
              "frame's pts in its stream's time base, the MD5 of each decoded frame, and the "
              "frames as checked: picture@slot, the pattern's picture each shows and the frame "
              "slot whose time it has.", ""]
    problems, pictures = [], {}
    try:
        for x in clips:
            path = os.path.join(clips_dir, file_name(x))
            record.append(f"## {file_name(x)}")
            commands = make_commands(args.ffmpeg, x, path)
            for c in commands:
                run(c)
                record.append(f"$ {shown(c)}")
            if len(commands) > 1:
                parts = [c[-1] for c in commands]
                concatenate(parts, path)
                record.append(f"$ cat {' '.join(shown([p]) for p in parts)} > {shown([path])}")
            streams = run(streams_command(ffprobe, path))
            times_text = run(times_command(ffprobe, path))
            md5_text = run(md5_command(args.ffmpeg, path))
            md5s, times = parse_md5s(md5_text), parse_times(times_text)
            if x in FULL.values():
                pictures[(x.size, x.container)] = md5s
            problems += frame_problems(x, times, md5s, pictures[(x.size, x.container)])
            record += [f"$ {shown(streams_command(ffprobe, path))}", streams.rstrip(),
                       f"$ {shown(times_command(ffprobe, path))}", times_text.rstrip(),
                       f"$ {shown(md5_command(args.ffmpeg, path))}", md5_text.rstrip(),
                       "frames as checked (picture@slot): "
                       + " ".join(f"{p}@{s}" for p, s in x.frames), ""]
    except (OSError, RuntimeError) as e:
        print(f"align_cases: {e}", file=sys.stderr)
        return 1

    with open(os.path.join(clips_dir, "clips.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(record))
    with open(os.path.join(clips_dir, "pairs.txt"), "w", encoding="utf-8") as f:
        f.write(pairs_text(CASES, clips_dir, "Alignment cases (docs/DECISIONS.md D-028)."))
    for x in problems:
        print(f"align_cases: {x}", file=sys.stderr)
    print(f"align_cases: {len(clips)} clips, {len(CASES)} cases in {args.clips_dir}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
