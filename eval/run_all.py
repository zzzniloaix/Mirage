"""Reruns every committed Phase 1 measurement into a new directory, and
compares what it gets with the committed outputs.

usage: eval/run_all.sh <out_dir> [--mirage-vmaf PROG] [--mirage-vmaf-before PROG]
                       [--mirage-vmaf-before-d022 PROG]
                       [--skip-before] [--skip-full] [--only STEP[,STEP...]]
                       [--jobs J] [--ffmpeg PROG] [--ffprobe PROG]
                       [--model-dir DIR] [--work-dir DIR] [--no-compare]

<out_dir> is a new or empty directory outside the repository. Each step
writes under <out_dir> the path its committed counterpart has under eval/,
in this order:

  streams                streams/fetched/: eval/streams/fetch_streams.py --probe,
                         compared with eval/streams/fetched-2026-10-23/
  clips                  consistency/clips/: eval/consistency/color_cases.py,
                         compared with eval/consistency/color-cases-before/clips.txt
  format-clips           consistency/format_clips/: color_cases.py --set formats,
                         compared with eval/consistency/format-cases-before/clips.txt
  diag-before-300        consistency/diag-before-300/{plain,relabel}/
  color-cases-before     consistency/color-cases-before/{main,refused}/
  format-cases-before    consistency/format-cases-before/{main,refused}/
  diag-after-300         consistency/diag-after-300/plain/
  color-cases-after      consistency/color-cases-after/{main,refused}/
  format-cases-after     consistency/format-cases-after/{main,refused}/
  format-cases-d022      consistency/format-cases-d022/{main,refused}/
  scalers-300            consistency/scalers-300/{bilinear,bicubic,lanczos}/ and
                         scaler_effect.*
  models-300             consistency/models-300/{vmaf_v0.6.1,...}/ and model_effect.*
  defaults-300           consistency/defaults-300/plain/: the first 300 frames of
                         the four pairs with the defaults after D-024 and D-027
  acceptance-full        consistency/acceptance-full/plain/: every frame of the
                         four pairs of D-018
  ffmpeg-scale-default   consistency/ffmpeg-scale-default/record.txt
  ffmpeg-libvmaf-filter  consistency/ffmpeg-libvmaf-filter/record.txt
  pooling                pooling/bipbop16x9/: eval/pooling/pooling.py on the
                         rerun of diag-after-300
  pooling-defaults-300   pooling/defaults-300/: pooling.py --check-pooled on the
                         rerun of defaults-300
  pooling-full           pooling/acceptance-full/: eval/pooling/pooling.py on
                         the rerun of acceptance-full

Each run_delta.sh run gets the options its committed run.cmd.txt records,
the scaler included: defaults-300 was made without --scaler, with
run_delta.sh's default since D-024, which its run.cmd.txt records, and the
runs made before D-024 with bilinear, the default then.
The runs made before D-016 (diag-before-300, color-cases-before,
format-cases-before) use the mirage_vmaf of commit 83c8ed6, which converts
no color; format-cases-after, made after D-016 and before D-022, uses that
of commit d993686, which doesn't convert a reference that isn't yuv420p;
all the others use the mirage_vmaf of the repository's HEAD. --skip-before
leaves out the runs made before D-016, --skip-full the two steps over every
frame (acceptance-full, pooling-full). Unless --mirage-vmaf,
--mirage-vmaf-before or --mirage-vmaf-before-d022 give one, each is built in
a temporary worktree of its commit: `cmake --preset macos-release`, then
only the mirage_vmaf target, which needs no glad venv (docs/BUILD.md, step
3).

Then it compares, step by step, the rerun with the committed outputs: the
per-frame scores of both sides of every pair (Mirage's at full precision,
every libvmaf metric of FFmpeg's log) and each pair's verdict; the
numbers of compare_runs.py and pooling.py; the playlists and stream
parameters; the clips' tags and pixel checksums; and the FFmpeg records'
outputs. It writes:

  run_all.md, run_all.json         every command, its exit status and wall time,
                                   the programs, and the repository's state
  rerun_check.md, rerun_check.json the comparison
  builds/<name>/build.txt          how each mirage_vmaf was built (and its logs);
                                   not build/, which .gitignore leaves out
  logs/                            each command's output

Local paths in those files appear as <out_dir>, <work>, <worktree>, <repo>,
<home> and <tmp>. Needs the network (Apple's CDN, and GitHub for the
builds' dependencies). Python 3 standard library only; run_all.sh runs it
with python3 -I -B.

Exit status: 0 every step ran and everything compared is as committed;
1 a step failed or something differs (rerun_check.md says what); 2 bad
arguments, or the run could not start.
"""

import argparse
import collections
import datetime
import json
import lzma
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.realpath(__file__))
sys.dont_write_bytecode = True   # no __pycache__ in the repository's working tree
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, "consistency"))
import vmafdelta as vd  # noqa: E402

REPORT_VERSION = 1
DEFAULT_FFMPEG = "/opt/homebrew/opt/ffmpeg-full/bin/ffmpeg"   # docs/DECISIONS.md D-009
DEFAULT_MODEL_DIR = "/opt/homebrew/opt/libvmaf/share/libvmaf/model"

# The commit whose mirage_vmaf made the runs before D-016: it was built at
# 83c8ed6, and diag-before-300/build.txt and color-cases-before/build.txt
# check that it was unchanged when they ran; format-cases-before/build.txt
# builds it in a worktree of 83c8ed6.
BEFORE_D016 = "83c8ed6b497783cc6dd265637e7b2298399ba41a"

# The commit whose mirage_vmaf made format-cases-after, after D-016 and
# before D-022 (its build.txt): the head converts a reference that isn't
# yuv420p since D-022, which changes the scores of most of those cases.
BEFORE_D022 = "d993686add8b2af709e1200437e9d643aed26238"

# The programs the steps use, by the name of a step's `binary`: the option
# that gives one, the commit to build otherwise (None: the repository's
# HEAD), and what it is for.
PROGRAMS = (
    ("head", "--mirage-vmaf", None, "every run made after D-016 except format-cases-after"),
    ("before", "--mirage-vmaf-before", BEFORE_D016, "the runs made before D-016"),
    ("before-d022", "--mirage-vmaf-before-d022", BEFORE_D022,
     "format-cases-after, made after D-016 and before D-022"),
)

BUILD_TIMEOUT = 3600      # seconds per configure or build
STEP_TIMEOUT = 6 * 3600   # seconds per measurement command

PAIRS = "eval/consistency/pairs/bipbop16x9.txt"
PAIRS_GEAR2 = "eval/consistency/pairs/bipbop16x9_gear2.txt"

# One run_delta.sh run of a step: its directory and options. In the options,
# {clips} and {format_clips} are the clips directories of the clips and
# format-clips steps, and {model_dir} the libvmaf model directory.
Run = collections.namedtuple("Run", "name options")


def delta_options(frames, pairs=PAIRS, scaler="bilinear", model="vmaf_v0.6.1",
                  model_file=None, relabel=False):
    """run_delta.sh's options; frames None for every frame. The scaler, the
    model and the threads are always given, so that a rerun has the settings
    its run.cmd.txt records whatever run_delta.sh's defaults are: bilinear,
    the scaler of every committed run that names no other, was its default
    until D-024 made it bicubic."""
    opts = [] if frames is None else ["--frames", str(frames)]
    opts += ["--pairs", pairs, "--scaler", scaler]
    opts += ["--model-file", model_file] if model_file else ["--model", model]
    opts += ["--threads", "4", "--threshold", "0.01"]
    return opts + (["--diagnostic-relabel"] if relabel else [])


Step = collections.namedtuple("Step", "name kind binary runs needs committed out effect options")


def step(name, kind, binary=None, runs=(), needs=(), committed=None, out=None, effect=None,
         options=()):
    """`options`: further arguments of a clips step's color_cases.py."""
    return Step(name, kind, binary, tuple(runs), tuple(needs), committed,
                out or committed, effect, tuple(options))


STEPS = (
    step("streams", "streams", committed="streams/fetched-2026-10-23", out="streams/fetched"),
    step("clips", "clips", committed="consistency/color-cases-before/clips.txt",
         out="consistency/clips"),
    step("format-clips", "clips", committed="consistency/format-cases-before/clips.txt",
         out="consistency/format_clips", options=["--set", "formats"]),
    step("diag-before-300", "delta", "before", committed="consistency/diag-before-300", runs=[
        Run("plain", delta_options(300)),
        Run("relabel", delta_options(300, relabel=True))]),
    step("color-cases-before", "delta", "before", needs=["clips"],
         committed="consistency/color-cases-before", runs=[
             Run("main", delta_options(10, pairs="{clips}/pairs.txt")),
             Run("refused", delta_options(10, pairs="{clips}/pairs_refused.txt"))]),
    step("format-cases-before", "delta", "before", needs=["format-clips"],
         committed="consistency/format-cases-before", runs=[
             Run("main", delta_options(10, pairs="{format_clips}/pairs.txt")),
             Run("refused", delta_options(10, pairs="{format_clips}/pairs_refused.txt"))]),
    step("diag-after-300", "delta", "head", committed="consistency/diag-after-300", runs=[
        Run("plain", delta_options(300))]),
    step("color-cases-after", "delta", "head", needs=["clips"],
         committed="consistency/color-cases-after", runs=[
             Run("main", delta_options(10, pairs="{clips}/pairs.txt")),
             Run("refused", delta_options(10, pairs="{clips}/pairs_refused.txt"))]),
    step("format-cases-after", "delta", "before-d022", needs=["format-clips"],
         committed="consistency/format-cases-after", runs=[
             Run("main", delta_options(10, pairs="{format_clips}/pairs.txt")),
             Run("refused", delta_options(10, pairs="{format_clips}/pairs_refused.txt"))]),
    step("format-cases-d022", "delta", "head", needs=["format-clips"],
         committed="consistency/format-cases-d022", runs=[
             Run("main", delta_options(10, pairs="{format_clips}/pairs.txt")),
             Run("refused", delta_options(10, pairs="{format_clips}/pairs_refused.txt"))]),
    step("scalers-300", "delta", "head", committed="consistency/scalers-300",
         effect=("scaler_effect", "bilinear"), runs=[
             Run(s, delta_options(300, scaler=s)) for s in ("bilinear", "bicubic", "lanczos")]),
    step("models-300", "delta", "head", committed="consistency/models-300",
         effect=("model_effect", "vmaf_v0.6.1"), runs=[
             Run(m, delta_options(300, pairs=PAIRS_GEAR2, model=m))
             for m in ("vmaf_v0.6.1", "vmaf_v0.6.1neg", "vmaf_4k_v0.6.1")] + [
             Run("file_vmaf_v0.6.1", delta_options(300, pairs=PAIRS_GEAR2,
                                                   model_file="{model_dir}/vmaf_v0.6.1.json"))]),
    step("defaults-300", "delta", "head", committed="consistency/defaults-300", runs=[
        Run("plain", delta_options(300, scaler="bicubic"))]),
    step("acceptance-full", "delta", "head", committed="consistency/acceptance-full", runs=[
        Run("plain", delta_options(None))]),
    step("ffmpeg-scale-default", "ffmpeg-record", committed="consistency/ffmpeg-scale-default"),
    step("ffmpeg-libvmaf-filter", "ffmpeg-record",
         committed="consistency/ffmpeg-libvmaf-filter"),
    step("pooling", "pooling", needs=["diag-after-300"], committed="pooling/bipbop16x9"),
    step("pooling-defaults-300", "pooling", needs=["defaults-300"],
         committed="pooling/defaults-300"),
    step("pooling-full", "pooling", needs=["acceptance-full"],
         committed="pooling/acceptance-full"),
)
STEP_NAMES = tuple(s.name for s in STEPS)

# The steps over every frame of the four pairs, which --skip-full leaves out.
FULL_LENGTH = ("acceptance-full", "pooling-full")

# The sources of each pooling step, as eval/pooling/README.md gives its
# command; {out} is <out_dir>, which holds the reruns of the steps it needs.
POOLING_SOURCES = {
    "pooling": ["--gui-report", "eval/baseline/bipbop16x9/report.vmaf.json",
                "--delta-run", "{out}/consistency/diag-after-300/plain"],
    "pooling-defaults-300": ["--delta-run", "{out}/consistency/defaults-300/plain",
                             "--check-pooled"],
    "pooling-full": ["--delta-run", "{out}/consistency/acceptance-full/plain"],
}

# The commands of the FFmpeg records, as their committed checks.txt has them
# for the default FFmpeg, by check number; {ffmpeg} is the FFmpeg and
# {include} its include directory. Checks 1 (git) are left out.
FFMPEG_RECORDS = {
    "ffmpeg-scale-default": {
        2: "{ffmpeg} -hide_banner -version | head -n 1",
        3: "for flags in '' ':flags=bilinear' ':flags=bicubic'; do echo \"scale=1920:1080$flags\"; "
           "{ffmpeg} -hide_banner -nostdin -nostats -v verbose -f lavfi -i "
           "testsrc2=size=640x360:rate=30000/1001:duration=1 -vf \"scale=1920:1080$flags\" "
           "-frames:v 1 -f null - 2>&1 | grep -E '^\\[Parsed_scale_0 @ [^]]*\\] w:[0-9]+ "
           "h:[0-9]+ fmt:' | sed -E 's/ @ 0x[0-9a-f]+//'; done",
        4: "grep -E 'SWS_(BILINEAR|BICUBIC) ' {include}/libswscale/swscale.h",
    },
    "ffmpeg-libvmaf-filter": {
        2: "{ffmpeg} -hide_banner -version | head -n 1",
        3: "{ffmpeg} -hide_banner -h filter=libvmaf",
        4: "{ffmpeg} -hide_banner -nostdin -nostats -v error -f lavfi -i "
           "testsrc2=size=640x360:rate=30000/1001:duration=1 -f lavfi -i "
           "testsrc2=size=1920x1080:rate=30000/1001:duration=1 -lavfi "
           "'[0:v][1:v]libvmaf=model=version=vmaf_v0.6.1' -f null - 2>&1 | "
           "sed -E 's/ @ 0x[0-9a-f]+//'; echo \"exit status ${{PIPESTATUS[0]:-?}}\"",
    },
}

# Heap addresses in FFmpeg's log lines, which differ from run to run.
ADDRESS = re.compile(r"@ 0x[0-9a-fA-F]+")


class SetupError(Exception):
    pass


class Redactor:
    """Replaces local paths with placeholders, longest first: each path as
    given and as its real path, and each of those of two or more components
    also as CMake writes it into the name of a compiler check's variable,
    every character but a letter or a digit as '_' (Catch2's configure
    checks a -ffile-prefix-map flag that holds its source directory's path).
    A one-component path such as /tmp would turn into a word that occurs
    anywhere."""

    def __init__(self, paths):
        subs = {}
        for path, placeholder in paths:
            if not path:
                continue
            for p in (os.path.abspath(path), os.path.realpath(path)):
                p = p.rstrip("/")
                if len(p) > 1:
                    subs.setdefault(p, placeholder)
                    if p.count("/") >= 2:
                        subs.setdefault(re.sub(r"[^A-Za-z0-9]", "_", p), placeholder)
        self._subs = sorted(subs.items(), key=lambda s: (-len(s[0]), s[0]))

    def text(self, s):
        for path, placeholder in self._subs:
            s = s.replace(path, placeholder)
        return s, None

    def data(self, b):
        for path, placeholder in self._subs:
            b = b.replace(path.encode(), placeholder.encode())
        return b, None


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def inside(path, root):
    path, root = os.path.realpath(path), os.path.realpath(root)
    return path == root or path.startswith(root.rstrip(os.sep) + os.sep)


def git(*argv, cwd=REPO):
    """(exit status, stdout) of a git command."""
    try:
        p = subprocess.run(["git", "-C", cwd, *argv], capture_output=True, text=True,
                           stdin=subprocess.DEVNULL, timeout=600)
        return p.returncode, p.stdout
    except (OSError, subprocess.SubprocessError) as e:
        return None, str(e)


def git_state():
    rc, head = git("rev-parse", "HEAD")
    rc2, status = git("status", "--porcelain")
    return (head.strip() if rc == 0 else None), (status == "" if rc2 == 0 else None)


def parse_only(text):
    names = [n.strip() for n in text.split(",") if n.strip()]
    unknown = [n for n in names if n not in STEP_NAMES]
    if unknown or not names:
        raise argparse.ArgumentTypeError(
            f"unknown step {', '.join(unknown) or text!r}; the steps are "
            + ", ".join(STEP_NAMES))
    return names


def selected_steps(only, skip_before, skip_full=False):
    """The steps to run, in order: those asked for and the steps they need."""
    wanted = set(only or STEP_NAMES)
    if skip_before:
        wanted -= {s.name for s in STEPS if s.binary == "before"}
    if skip_full:
        wanted -= set(FULL_LENGTH)
    changed = True
    while changed:
        changed = False
        for s in STEPS:
            if s.name in wanted and not set(s.needs) <= wanted:
                wanted |= set(s.needs)
                changed = True
    return [s for s in STEPS if s.name in wanted]


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="run_all.sh",
        description="Rerun every committed Phase 1 measurement into OUT_DIR, and compare "
                    "the outputs with the committed ones.",
        epilog="Steps: " + ", ".join(STEP_NAMES) + ". Exit status: 0 everything ran and "
               "matches the committed outputs; 1 a step failed or an output differs; 2 bad "
               "arguments or setup error.")
    p.add_argument("out_dir", help="a new or empty directory outside the repository")
    p.add_argument("--mirage-vmaf", metavar="PROG",
                   help="the mirage_vmaf for the runs after D-016 (default: build the "
                        "repository's HEAD in a temporary worktree)")
    p.add_argument("--mirage-vmaf-before", metavar="PROG",
                   help="the mirage_vmaf for the runs before D-016 (default: build commit "
                        f"{BEFORE_D016[:7]} in a temporary worktree)")
    p.add_argument("--mirage-vmaf-before-d022", metavar="PROG",
                   help="the mirage_vmaf for format-cases-after, made before D-022 (default: "
                        f"build commit {BEFORE_D022[:7]} in a temporary worktree)")
    p.add_argument("--skip-before", action="store_true",
                   help="leave out the runs made before D-016")
    p.add_argument("--skip-full", action="store_true",
                   help="leave out the steps over every frame: " + ", ".join(FULL_LENGTH))
    p.add_argument("--only", type=parse_only, metavar="STEP[,STEP...]",
                   help="run only these steps, and the steps they need")
    p.add_argument("--jobs", type=int, default=1, metavar="J",
                   help="run_delta.sh's --jobs (default: 1, as in the committed runs "
                        "other than acceptance-full, which used 4)")
    p.add_argument("--ffmpeg", default=DEFAULT_FFMPEG,
                   help=f"FFmpeg with the libvmaf filter (default: {DEFAULT_FFMPEG})")
    p.add_argument("--ffprobe", help="ffprobe (default: the one next to --ffmpeg)")
    p.add_argument("--model-dir", default=DEFAULT_MODEL_DIR, metavar="DIR",
                   help="libvmaf's model directory, for the model file run of models-300 "
                        f"(default: {DEFAULT_MODEL_DIR})")
    p.add_argument("--work-dir", metavar="DIR",
                   help="where the temporary worktrees go (default: a new directory under "
                        "TMPDIR)")
    p.add_argument("--no-compare", action="store_true",
                   help="do not compare the outputs with the committed ones")
    p.add_argument("--committed", default=HERE, metavar="DIR", help=argparse.SUPPRESS)
    args = p.parse_args(argv)
    if args.jobs < 1:
        p.error("--jobs must be at least 1")
    return args


class Runner:
    """One run_all run: where it writes, what it ran, and how to show paths."""

    def __init__(self, args):
        self.args = args
        self.out = os.path.abspath(args.out_dir)
        if inside(self.out, REPO):
            raise SetupError(f"{args.out_dir} is inside the repository; its runs record the "
                             "repository's working tree as clean only if nothing else is in it")
        if os.path.exists(self.out) and (not os.path.isdir(self.out) or os.listdir(self.out)):
            raise SetupError(f"{args.out_dir} exists and is not an empty directory")
        self.ffmpeg = self.program(args.ffmpeg, "--ffmpeg")
        self.ffprobe = self.program(args.ffprobe or os.path.join(os.path.dirname(self.ffmpeg),
                                                                 "ffprobe"), "--ffprobe")
        os.makedirs(self.out, exist_ok=True)
        self.work = os.path.abspath(args.work_dir) if args.work_dir else \
            tempfile.mkdtemp(prefix="run_all-")
        os.makedirs(self.work, exist_ok=True)
        self.set_redactor()
        self.commands = []

    def set_redactor(self, worktree=None):
        """Local paths and their placeholders; a build's worktree as <worktree>."""
        self.redactor = Redactor([(worktree, "<worktree>"), (self.out, "<out_dir>"),
                                     (self.work, "<work>"), (REPO, "<repo>"),
                                     (os.path.expanduser("~"), "<home>"),
                                     (os.environ.get("TMPDIR") or "/tmp", "<tmp>")])

    @staticmethod
    def program(path, what):
        found = path if os.sep in path else shutil.which(path)
        if not found or not os.path.isfile(found) or not os.access(found, os.X_OK):
            raise SetupError(f"{what} {path!r} is not an executable file")
        return os.path.abspath(found)

    def shown(self, text):
        return self.redactor.text(text)[0]

    def shown_path(self, path):
        """A path as the outputs show it: relative to the repository inside it."""
        if inside(path, REPO):
            return os.path.relpath(os.path.realpath(path), os.path.realpath(REPO))
        return self.shown(os.path.abspath(path))

    def write(self, rel, text):
        path = os.path.join(self.out, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(self.shown(text))

    def run(self, step_name, argv, log_rel, cwd=REPO, env=None, timeout=STEP_TIMEOUT,
            shown_argv=None):
        """Runs a command with its output to <out_dir>/<log_rel>; its record."""
        log = os.path.join(self.out, log_rel)
        os.makedirs(os.path.dirname(log), exist_ok=True)
        rec = {"step": step_name, "command": None, "cwd": self.shown_path(cwd),
               "log": log_rel, "started_utc": utc_now(), "exit_code": None,
               "timed_out": False, "error": None}
        rec["command"] = self.shown(vd.shell_line(shown_argv or argv)) if not \
            isinstance(shown_argv, str) else shown_argv
        print(f"[{step_name}] $ {rec['command']}", flush=True)
        t0 = time.monotonic()
        with open(log, "wb") as f:
            try:
                rec["exit_code"] = subprocess.run(
                    argv, cwd=cwd, stdin=subprocess.DEVNULL, stdout=f,
                    stderr=subprocess.STDOUT, timeout=timeout,
                    env=dict(os.environ, **(env or {}))).returncode
            except subprocess.TimeoutExpired:
                rec["timed_out"] = True
            except OSError as e:
                rec["error"] = str(e)
        rec["elapsed_s"] = round(time.monotonic() - t0, 1)
        rec["ended_utc"] = utc_now()
        with open(log, "rb") as f:
            data, _ = self.redactor.data(f.read())
        with open(log, "wb") as f:
            f.write(data)
        self.commands.append(rec)
        status = ("timed out" if rec["timed_out"] else rec["error"] if rec["error"]
                  else f"exit {rec['exit_code']}")
        print(f"[{step_name}] {status} after {rec['elapsed_s']:.1f} s", flush=True)
        return rec


# ── Building mirage_vmaf ─────────────────────────────────────────────────────

def build_mirage_vmaf(r, name, commit, purpose):
    """Builds the mirage_vmaf of `commit` in a temporary worktree; the path of
    the copy kept in <out_dir>/builds/<name>/, and the build's record."""
    bdir = os.path.join("builds", name)
    wt = os.path.join(r.work, f"{name}-worktree")
    r.set_redactor(worktree=wt)
    lines = [f"# The mirage_vmaf of {purpose}, built by eval/run_all.py in a temporary "
             f"worktree of commit {commit}: each command, then its output or exit status."]
    rec = {"name": name, "commit": commit, "purpose": purpose, "built": False, "sha256": None,
           "path": None}
    add = r.run(f"build {name}", ["git", "-C", REPO, "worktree", "add", "--detach", wt,
                                  commit], os.path.join(bdir, "worktree_add.log"))
    lines.append(f"$ {add['command']}\nexit code: {add['exit_code']}")
    if add["exit_code"] != 0:
        r.write(os.path.join(bdir, "build.txt"), "\n\n".join(lines) + "\n")
        return None, rec
    try:
        _, head = git("rev-parse", "HEAD", cwd=wt)
        _, status = git("status", "--porcelain", cwd=wt)
        lines.append(f"$ git -C <worktree> rev-parse HEAD\n{head.strip()}")
        lines.append("$ git -C <worktree> status --porcelain\n"
                     + (status.strip() or "(no output: the working tree is clean)"))
        rec["worktree_head"], rec["worktree_clean"] = head.strip(), status.strip() == ""
        ok = True
        for argv, log in ((["cmake", "--preset", "macos-release"], "configure.log"),
                          (["cmake", "--build", "--preset", "macos-release", "--target",
                            "mirage_vmaf"], "build.log")):
            c = r.run(f"build {name}", argv, os.path.join(bdir, log), cwd=wt,
                      timeout=BUILD_TIMEOUT)
            lines.append(f"$ {c['command']}\nexit code: {c['exit_code']} "
                         f"(log: {os.path.join(bdir, log)})")
            if c["exit_code"] != 0:
                ok = False
                break
        binary = os.path.join(wt, "build", "macos-release", "mirage_vmaf")
        if ok and os.path.isfile(binary):
            kept = os.path.join(r.out, bdir, "mirage_vmaf")
            shutil.copy2(binary, kept)
            rec.update(built=True, sha256=vd.sha256_file(kept),
                       path=os.path.join(bdir, "mirage_vmaf"))
            lines.append(f"$ shasum -a 256 build/macos-release/mirage_vmaf\n"
                         f"{rec['sha256']}  build/macos-release/mirage_vmaf")
            lines.append(f"Kept as <out_dir>/{rec['path']}.")
            r.write(os.path.join(bdir, "build.txt"), "\n\n".join(lines) + "\n")
            return kept, rec
        r.write(os.path.join(bdir, "build.txt"), "\n\n".join(lines) + "\n")
        return None, rec
    finally:
        git("worktree", "remove", "--force", wt)
        r.set_redactor()


# ── The steps ────────────────────────────────────────────────────────────────

def python(script):
    """A Python script of the repository, run as run_delta.sh runs its script; the
    commands run in the repository, so `script` is its path there."""
    return [sys.executable, "-I", "-B", script]


def expand(options, r):
    clips = os.path.join(r.out, "consistency", "clips")
    format_clips = os.path.join(r.out, "consistency", "format_clips")
    return [o.format(clips=clips, format_clips=format_clips, model_dir=r.args.model_dir)
            for o in options]


def run_step(r, s, binaries):
    """Runs one step; whether every command of it ran (exit status 0, or 1
    for run_delta.sh, whose pairs may fail by design)."""
    out = os.path.join(r.out, s.out)
    if s.kind == "streams":
        c = r.run(s.name, python("eval/streams/fetch_streams.py")
                  + [out, "--probe", "--ffprobe", r.ffprobe], f"logs/{s.name}.log")
        return c["exit_code"] == 0
    if s.kind == "clips":
        c = r.run(s.name, python("eval/consistency/color_cases.py")
                  + [out, *s.options, "--ffmpeg", r.ffmpeg, "--ffprobe", r.ffprobe],
                  f"logs/{s.name}.log")
        return c["exit_code"] == 0
    if s.kind == "delta":
        prog = binaries.get(s.binary)
        if not prog:
            print(f"[{s.name}] no mirage_vmaf for it: skipped", flush=True)
            return False
        ok = True
        # The clips show as <tmp>/clips or <tmp>/format_clips, as in the committed
        # runs (P-013, P-029).
        clip_steps = {x.name for x in STEPS if x.kind == "clips"}
        env = {"TMPDIR": os.path.join(r.out, "consistency")} if clip_steps & set(s.needs) \
            else None
        for run in s.runs:
            argv = ["eval/consistency/run_delta.sh", prog,
                    os.path.join(out, run.name)] + expand(run.options, r) + [
                    "--jobs", str(r.args.jobs), "--ffmpeg", r.ffmpeg, "--ffprobe", r.ffprobe]
            c = r.run(s.name, argv, f"logs/{s.name}_{run.name}.log", env=env)
            ok = ok and c["exit_code"] in (0, 1)
        if s.effect and ok:
            prefix, base = s.effect
            argv = python("eval/consistency/compare_runs.py") + [
                "--base", f"{base}={os.path.join(out, base)}"]
            for run in s.runs:
                if run.name != base:
                    argv += ["--run", f"{run.name}={os.path.join(out, run.name)}"]
            c = r.run(s.name, argv + ["--out", os.path.join(out, prefix)],
                      f"logs/{s.name}_{prefix}.log")
            ok = c["exit_code"] == 0
        return ok
    if s.kind == "ffmpeg-record":
        os.makedirs(out, exist_ok=True)
        include = os.path.join(os.path.dirname(os.path.dirname(r.ffmpeg)), "include")
        parts = [f"# The commands of {s.committed}/checks.txt, checks 2 and later, rerun by "
                 "eval/run_all.py: each command, then its output."]
        ok = True
        for n, template in FFMPEG_RECORDS[s.name].items():
            cmd = template.format(ffmpeg=shlex.quote(r.ffmpeg), include=shlex.quote(include))
            log = f"logs/{s.name}_{n}.log"
            c = r.run(s.name, ["bash", "-c", cmd], log, shown_argv=r.shown(cmd))
            with open(os.path.join(r.out, log), encoding="utf-8", errors="replace") as f:
                output = f.read()
            parts.append(f"# {n}.\n\n$ {r.shown(cmd)}\n{output.rstrip()}")
            ok = ok and c["exit_code"] == 0
        r.write(os.path.join(s.out, "record.txt"), "\n\n".join(parts) + "\n")
        return ok
    if s.kind == "pooling":
        os.makedirs(out, exist_ok=True)
        sources = [a.format(out=r.out) for a in POOLING_SOURCES[s.name]]
        c = r.run(s.name, python("eval/pooling/pooling.py") + sources + [
            "--out", os.path.join(out, "pooling")], f"logs/{s.name}.log")
        return c["exit_code"] == 0
    raise ValueError(s.kind)


# ── Comparing with the committed outputs ─────────────────────────────────────

def read_json(path):
    """The JSON of path or path.xz; None when neither exists."""
    for name, opener in ((path, open), (path + ".xz", lzma.open)):
        if os.path.exists(name):
            with opener(name, "rt", encoding="utf-8") as f:
                return json.load(f)
    return None


def compare_values(new, old):
    """(frames compared, frames that differ, largest |difference| of the numbers)
    of two per-frame lists."""
    differ, largest = 0, 0.0
    for a, b in zip(new, old):
        if a != b:
            differ += 1
            for x, y in (zip(a.values(), b.values()) if isinstance(a, dict) else [(a, b)]):
                if vd.is_number(x) and vd.is_number(y):
                    largest = max(largest, abs(x - y))
    return min(len(new), len(old)), differ, largest


def compare_pair(new_dir, old_dir):
    """Both sides' per-frame scores of one pair, rerun against committed."""
    out = {}
    for side, name, key in (("mirage", "mirage.json", "vmaf"), ("ffmpeg", "ffmpeg.json", None)):
        new, old = read_json(os.path.join(new_dir, name)), read_json(os.path.join(old_dir, name))
        rec = {"rerun": None, "committed": None, "identical": None}
        if new is None or old is None:
            rec.update(rerun=None if new is None else "present",
                       committed=None if old is None else "present",
                       identical=new is None and old is None)
            out[side] = rec
            continue
        if side == "mirage":
            a = [f.get("vmaf") for f in new.get("frames") or []]
            b = [f.get("vmaf") for f in old.get("frames") or []]
        else:
            a = [f.get("metrics") for f in new.get("frames") or []]
            b = [f.get("metrics") for f in old.get("frames") or []]
        compared, differ, largest = compare_values(a, b)
        rec.update(rerun=len(a), committed=len(b), differing=differ, largest_difference=largest,
                   identical=len(a) == len(b) and differ == 0)
        out[side] = rec
    return out


def compare_delta_run(new_dir, old_dir):
    """A run_delta.sh run: its verdicts and every pair's scores."""
    new, old = read_json(os.path.join(new_dir, "delta.json")), \
        read_json(os.path.join(old_dir, "delta.json"))
    if new is None or old is None:
        return {"identical": False, "problem": "delta.json is missing from the "
                + ("rerun" if new is None else "committed run")}
    pairs = []
    new_pairs = {p["pair"]: p for p in new.get("pairs") or []}
    for p in old.get("pairs") or []:
        n = new_pairs.get(p["pair"])
        rec = {"pair": p["pair"], "status": {"rerun": n and n.get("status"),
                                             "committed": p.get("status")}}
        if n is None:
            rec["identical"] = False
        else:
            rec.update(compare_pair(os.path.join(new_dir, p["pair"]),
                                    os.path.join(old_dir, p["pair"])))
            rec["identical"] = (n.get("status") == p.get("status")
                                and rec["mirage"]["identical"] and rec["ffmpeg"]["identical"])
        pairs.append(rec)
    extra = sorted(set(new_pairs) - {p["pair"] for p in old.get("pairs") or []})
    return {"status": {"rerun": new.get("status"), "committed": old.get("status")},
            "pairs": pairs, "extra_pairs": extra,
            "identical": (new.get("status") == old.get("status") and not extra
                          and all(p["identical"] for p in pairs))}


def compare_effect(new_path, old_path):
    """compare_runs.py's JSON: every pair's shifts and verdicts ("settings",
    which name a model file by its path, left out)."""
    new, old = read_json(new_path), read_json(old_path)
    if new is None or old is None:
        return {"identical": False, "problem": "missing from the "
                + ("rerun" if new is None else "committed outputs")}

    def strip(pairs):
        out = json.loads(json.dumps(pairs))
        for p in out:
            for side in [p.get("base") or {}] + list((p.get("runs") or {}).values()):
                side.pop("settings", None)
        return out
    same = {k: strip(new.get(k) or []) == strip(old.get(k) or []) if k == "pairs"
            else new.get(k) == old.get(k) for k in ("pairs", "parameters_that_differ")}
    return {"compared": sorted(same), "differing": sorted(k for k, v in same.items() if not v),
            "identical": all(same.values())}


def compare_pooling(new_path, old_path):
    """pooling.py's JSON: each source's series and skipped entries."""
    new, old = read_json(new_path), read_json(old_path)
    if new is None or old is None:
        return {"identical": False, "problem": "missing from the "
                + ("rerun" if new is None else "committed outputs")}
    a = [(s.get("kind"), s.get("series"), s.get("skipped")) for s in new.get("sources") or []]
    b = [(s.get("kind"), s.get("series"), s.get("skipped")) for s in old.get("sources") or []]
    differ = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
    return {"sources": {"rerun": len(a), "committed": len(b)}, "differing_sources": differ,
            "identical": len(a) == len(b) and not differ}


def compare_streams(new_path, old_path):
    """fetch_streams.py's JSON: every playlist's bytes and every probed stream."""
    new, old = read_json(new_path), read_json(old_path)
    if new is None or old is None:
        return {"identical": False, "problem": "missing from the "
                + ("rerun" if new is None else "committed outputs")}

    def items(report):
        out = {}
        for s in report.get("streams") or []:
            for p in [s.get("master") or {}] + (s.get("playlists") or []):
                out[p.get("file")] = (p.get("sha256"), (p.get("probe") or {}).get("stream"))
        return out
    a, b = items(new), items(old)
    differ = sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))
    return {"playlists": {"rerun": len(a), "committed": len(b)}, "differing": differ,
            "identical": not differ}


def compare_text(new_path, old_path):
    try:
        with open(new_path, encoding="utf-8") as f:
            new = f.read()
        with open(old_path, encoding="utf-8") as f:
            old = f.read()
    except OSError as e:
        return {"identical": False, "problem": str(e)}
    return {"identical": new == old}


def checks_blocks(text):
    """{check number: its output} of a checks.txt or record.txt: the lines
    after a check's command, up to the next check."""
    blocks, n, lines, in_output = {}, None, [], False
    for line in text.splitlines():
        m = re.match(r"^# (\d+)\.", line)
        if m:
            if n is not None:
                blocks[n] = "\n".join(lines).strip("\n")
            n, lines, in_output = int(m.group(1)), [], False
        elif n is not None:
            if line.startswith("$ ") and not in_output:
                in_output = True
            elif in_output:
                lines.append(line)
    if n is not None:
        blocks[n] = "\n".join(lines).strip("\n")
    return blocks


def compare_ffmpeg_record(new_path, old_path, checks):
    try:
        with open(new_path, encoding="utf-8") as f:
            new = checks_blocks(f.read())
        with open(old_path, encoding="utf-8") as f:
            old = checks_blocks(f.read())
    except OSError as e:
        return {"identical": False, "problem": str(e)}
    differ = [n for n in checks if ADDRESS.sub("@ 0x?", new.get(n, "")) !=
              ADDRESS.sub("@ 0x?", old.get(n, "\0"))]
    return {"checks": list(checks), "differing": differ, "identical": not differ}


def compare_step(r, s):
    """The rerun of step s against its committed outputs."""
    old_root, new = r.args.committed, os.path.join(r.out, s.out)
    old = os.path.join(old_root, s.committed)
    if s.kind == "streams":
        return {"what": "every playlist's SHA-256 and every probed stream's parameters",
                **compare_streams(os.path.join(new, "streams.json"),
                                  os.path.join(old, "streams.json"))}
    if s.kind == "clips":
        return {"what": "clips.txt: each clip's command, tags and MD5 of its frames",
                **compare_text(os.path.join(new, "clips.txt"), old)}
    if s.kind == "delta":
        runs = {run.name: compare_delta_run(os.path.join(new, run.name),
                                            os.path.join(old, run.name)) for run in s.runs}
        rec = {"what": "each run's verdict, and every pair's verdict and per-frame scores on "
                       "both sides", "runs": runs}
        ok = all(v["identical"] for v in runs.values())
        if s.effect:
            prefix = s.effect[0]
            rec["effect"] = compare_effect(os.path.join(new, prefix + ".json"),
                                           os.path.join(old, prefix + ".json"))
            ok = ok and rec["effect"]["identical"]
        rec["identical"] = ok
        return rec
    if s.kind == "ffmpeg-record":
        return {"what": "the output of each command of checks.txt after check 1",
                **compare_ffmpeg_record(os.path.join(new, "record.txt"),
                                        os.path.join(old, "checks.txt"),
                                        sorted(FFMPEG_RECORDS[s.name]))}
    if s.kind == "pooling":
        return {"what": "every series and skipped entry of pooling.json",
                **compare_pooling(os.path.join(new, "pooling.json"),
                                  os.path.join(old, "pooling.json"))}
    raise ValueError(s.kind)


def committed_binary(r, s):
    """The SHA-256 of the mirage_vmaf the committed runs of step s were made
    with, from their build.txt."""
    path = os.path.join(r.args.committed, s.committed, "build.txt")
    try:
        with open(path, encoding="utf-8") as f:
            m = re.findall(r"^([0-9a-f]{64})  build/macos-release/mirage_vmaf$", f.read(), re.M)
    except OSError:
        return None
    return m[-1] if m else None


# ── Reports ──────────────────────────────────────────────────────────────────

def yes(v):
    return "yes" if v else "NO"


def check_markdown(report):
    lines = ["# Rerun of the committed Phase 1 measurements, against the committed outputs", "",
             f"`eval/run_all.py`, {report['started_utc']} to {report['ended_utc']}; "
             f"repository at {report['git']['head_start']}, working tree "
             + ("clean at the start and end." if report["git"]["clean_start"]
                and report["git"]["clean_end"] else "NOT CLEAN during the run."), "",
             f"**{'Everything compared is as committed.' if report['identical'] else 'NOT everything is as committed: see below.'}**",
             ""]
    lines += ["| step | ran | as committed | compared |", "|---|---|---|---|"]
    for s in report["steps"]:
        c = s.get("comparison") or {}
        lines.append(f"| {s['name']} | {yes(s['ran'])} | "
                     f"{'-' if not c else yes(c.get('identical'))} | {c.get('what', '-')} |")
    lines.append("")
    for b in report["binaries"]:
        lines.append(f"- mirage_vmaf `{b['name']}` ({b['purpose']}): "
                     + (f"built from commit {b['commit']}" if b.get("commit") else
                        f"given as {b['path']}")
                     + f", sha256 `{b['sha256']}`.")
    lines.append("")
    for s in report["steps"]:
        c = s.get("comparison") or {}
        if not c.get("runs"):
            continue
        lines += [f"## {s['name']}", ""]
        if s.get("binary_sha256"):
            theirs = s.get("committed_binary_sha256")
            lines += [f"mirage_vmaf sha256 `{s['binary_sha256']}`; "
                      + (f"the committed runs' (their build.txt): `{theirs}`, "
                         + ("the same program." if theirs == s["binary_sha256"] else
                            "a different build; the table shows whether it scores alike.")
                         if theirs else "the committed runs' build.txt gives none."), ""]
        lines += ["| run | pair | verdict, rerun / committed | Mirage frames, rerun / "
                  "committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |",
                  "|---|---|---|---|---|---|---|"]
        for name, run in c["runs"].items():
            if run.get("problem"):
                lines.append(f"| {name} | | {run['problem']} | | | | |")
                continue
            for p in run["pairs"]:
                m, f = p.get("mirage") or {}, p.get("ffmpeg") or {}
                lines.append(
                    f"| {name} | {p['pair']} | {p['status']['rerun']} / "
                    f"{p['status']['committed']} | {m.get('rerun')} / {m.get('committed')} | "
                    f"{yes(m.get('identical'))}"
                    + ("" if m.get("identical") or m.get("differing") is None else
                       f" ({m['differing']} differ, largest {vd.num(m['largest_difference'])})")
                    + f" | {f.get('rerun')} / {f.get('committed')} | {yes(f.get('identical'))}"
                    + ("" if f.get("identical") or f.get("differing") is None else
                       f" ({f['differing']} differ)") + " |")
        if c.get("effect"):
            e = c["effect"]
            lines += ["", f"{s['effect_name']}: " + ("the same as committed"
                      if e["identical"] else f"DIFFERS ({e.get('problem') or e['differing']})")]
        lines.append("")
    others = [s for s in report["steps"] if (s.get("comparison") or {}).get("runs") is None
              and s.get("comparison") and not s["comparison"]["identical"]]
    if others:
        lines += ["## Other differences", ""]
        for s in others:
            lines.append(f"- {s['name']}: {json.dumps(s['comparison'], ensure_ascii=False)}")
        lines.append("")
    return "\n".join(lines)


def run_markdown(report):
    lines = ["# eval/run_all.py run", "",
             f"From {report['started_utc']} to {report['ended_utc']}, "
             f"{report['elapsed_s']:.1f} s of wall time in all; repository at "
             f"{report['git']['head_start']}. Command:", "", f"    {report['command']}", "",
             "| step | command | exit status | wall time (s) |", "|---|---|---|---|"]
    for c in report["commands"]:
        status = "timed out" if c["timed_out"] else c["error"] or c["exit_code"]
        command = c["command"].replace("|", "\\|")
        lines.append(f"| {c['step']} | `{command}` | {status} | {c['elapsed_s']:.1f} |")
    lines += ["", "Wall time per step (s): " + ", ".join(
        f"{s['name']} {s['elapsed_s']:.1f}" for s in report["steps"]), ""]
    return "\n".join(lines)


def main(argv=None):
    try:
        args = parse_args(argv)
        r = Runner(args)
    except SetupError as e:
        print(f"run_all: {e}", file=sys.stderr)
        return 2
    started, t0 = utc_now(), time.monotonic()
    head_start, clean_start = git_state()
    steps = selected_steps(args.only, args.skip_before, args.skip_full)
    binaries, built = {}, []
    needed = {s.binary for s in steps if s.binary}
    for name, option, commit, purpose in PROGRAMS:
        given = getattr(args, option[2:].replace("-", "_"))
        commit = commit or head_start
        if name not in needed:
            continue
        if given:
            prog = Runner.program(given, option)
            binaries[name] = prog
            built.append({"name": name, "purpose": purpose, "commit": None,
                          "path": r.shown_path(prog), "sha256": vd.sha256_file(prog)})
        elif commit:
            prog, rec = build_mirage_vmaf(r, name, commit, purpose)
            if prog:
                binaries[name] = prog
            built.append(rec)
    results = []
    for s in steps:
        t = time.monotonic()
        ran = run_step(r, s, binaries)
        rec = {"name": s.name, "ran": ran, "elapsed_s": round(time.monotonic() - t, 1),
               "out": s.out, "committed": f"eval/{s.committed}"}
        if s.effect:
            rec["effect_name"] = s.effect[0]
        if s.binary and binaries.get(s.binary):
            rec["binary_sha256"] = vd.sha256_file(binaries[s.binary])
            rec["committed_binary_sha256"] = committed_binary(r, s)
        if not args.no_compare:
            rec["comparison"] = compare_step(r, s)
        results.append(rec)
    head_end, clean_end = git_state()
    command = ["eval/run_all.sh", "<out_dir>"] + [
        a for a in (argv if argv is not None else sys.argv[1:])][1:]
    report = {"report_version": REPORT_VERSION, "tool": "eval/run_all.py",
              "command": r.shown(" ".join(shlex.quote(a) if not a.startswith("<") else a
                                          for a in command)),
              "started_utc": started, "ended_utc": utc_now(),
              "elapsed_s": round(time.monotonic() - t0, 1),
              "git": {"head_start": head_start, "clean_start": clean_start,
                      "head_end": head_end, "clean_end": clean_end},
              "ffmpeg": r.shown_path(r.ffmpeg), "ffprobe": r.shown_path(r.ffprobe),
              "binaries": built, "steps": results, "commands": r.commands}
    report["identical"] = (not args.no_compare and all(
        s["ran"] and s["comparison"]["identical"] for s in results)
        and all(b.get("sha256") for b in built))
    r.write("run_all.json", json.dumps({k: v for k, v in report.items() if k != "identical"},
                                       indent=1, ensure_ascii=False) + "\n")
    r.write("run_all.md", run_markdown(report))
    if not args.no_compare:
        check = {k: report[k] for k in ("report_version", "tool", "started_utc", "ended_utc",
                                        "git", "binaries", "identical")}
        check["steps"] = [{k: v for k, v in s.items() if k not in ("out",)} for s in results]
        r.write("rerun_check.json", json.dumps(check, indent=1, ensure_ascii=False) + "\n")
        r.write("rerun_check.md", check_markdown(check))
    if not args.work_dir:
        shutil.rmtree(r.work, ignore_errors=True)
    failed = [s["name"] for s in results if not s["ran"]]
    print(f"run_all: {len(results)} steps, {len(failed)} failed"
          + (f" ({', '.join(failed)})" if failed else "")
          + ("" if args.no_compare else
             "; everything compared is as committed" if report["identical"] else
             "; SOME OUTPUTS DIFFER from the committed ones")
          + f"; reports in {args.out_dir}", flush=True)
    if failed or any(not b.get("sha256") for b in built):
        return 1
    return 0 if args.no_compare or report["identical"] else 1


if __name__ == "__main__":
    sys.exit(main())
