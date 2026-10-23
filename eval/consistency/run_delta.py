"""Section 5 consistency check: Mirage's VMAF scores against FFmpeg's libvmaf filter.

For every pair of a pairs file, runs mirage_vmaf and FFmpeg's libvmaf filter
on the same two inputs, over the same frames, with the same model, scaler
and number of libvmaf threads, and compares the scores frame by frame. It
writes into <out_dir>:

  run.cmd.txt, pairs.txt    this run's command line and the pairs it read
  environment.txt           machine, programs, libvmaf builds, git state
  delta.md, delta.json      the delta report of the whole run
  <pair>/                   per pair: cmd.txt (every command), ffprobe_*.json,
                            mirage.json, ffmpeg.json (xz when over 1 MiB),
                            the stderr logs (xz),
                            ffmpeg_colorspace_lines.txt, delta.md, delta.json
  SHA256SUMS                every file above

Local paths in all of it are replaced by placeholders (<out_dir>, <repo>,
<home>, <tmp>); the programs show as <mirage_vmaf>, <ffmpeg>, <ffprobe>.
See eval/consistency/README.md. Run through run_delta.sh, which uses
python3 -I -B; standard library only.

Exit status: 0 every pair passed; 1 at least one pair failed (the reports
say why); 2 bad arguments, or the run could not start; 3 an output still
contains a local path or name after the placeholders (do not commit it).
"""

import argparse
import concurrent.futures
import datetime
import json
import lzma
import os
import pwd
import re
import shlex
import shutil
import socket
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.realpath(__file__))
sys.dont_write_bytecode = True   # no __pycache__ in the repository's working tree
sys.path.insert(0, HERE)
import vmafdelta as vd  # noqa: E402

DEFAULT_PAIRS = os.path.join(HERE, "pairs", "bipbop16x9.txt")
DEFAULT_FFMPEG = "/opt/homebrew/opt/ffmpeg-full/bin/ffmpeg"   # docs/DECISIONS.md D-009
DEFAULT_TIMEOUT = 6 * 3600   # seconds per scoring process, as the Phase 0 run's alarm

SHOWN = {"mirage": "<mirage_vmaf>", "ffmpeg": "<ffmpeg>", "ffprobe": "<ffprobe>"}

# Raw JSON outputs larger than this many bytes are kept xz-compressed, as
# full-length runs make them (<name>.json.xz).
COMPRESS_JSON_ABOVE = 1 << 20

# How a program outside the repository shows whose path would reveal local details.
OUTSIDE = "(a local path outside the repository)"


class SetupError(Exception):
    pass


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def positive_int(s):
    v = int(s)
    if v < 1:
        raise argparse.ArgumentTypeError(f"{s} is not a whole number >= 1")
    return v


def non_negative(s):
    v = float(s)
    if not v >= 0:
        raise argparse.ArgumentTypeError(f"{s} is not a number >= 0")
    return v


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="run_delta.sh",
        description="Compare Mirage's per-frame VMAF scores (mirage_vmaf) with FFmpeg's "
                    "libvmaf filter on the pairs of a pairs file, and write delta reports "
                    "and the raw outputs of both sides into OUT_DIR.",
        epilog="Exit status: 0 every pair passed; 1 a pair failed; 2 bad arguments or the "
               "run could not start; 3 an output contains a local path or name.")
    p.add_argument("mirage_vmaf", help="the mirage_vmaf program to check")
    p.add_argument("out_dir", help="a new or empty directory for the outputs")
    p.add_argument("--frames", type=positive_int, metavar="N",
                   help="compare only the first N frame pairs (default: every frame)")
    p.add_argument("--jobs", type=positive_int, default=1, metavar="J",
                   help="scoring processes run at the same time (default: 1)")
    p.add_argument("--pairs", default=DEFAULT_PAIRS, metavar="FILE",
                   help="pairs file, '<label> <reference> <distorted>' per line "
                        "(default: eval/consistency/pairs/bipbop16x9.txt)")
    p.add_argument("--scaler", default="bilinear",
                   help="FFmpeg's scale flags; must be the scaler mirage_vmaf uses "
                        "(default: bilinear)")
    p.add_argument("--model", default="vmaf_v0.6.1",
                   help="libvmaf built-in model version (default: vmaf_v0.6.1)")
    p.add_argument("--threads", type=positive_int, default=4,
                   help="libvmaf threads on FFmpeg's side; must be mirage_vmaf's (default: 4)")
    p.add_argument("--threshold", type=non_negative, default=vd.DEFAULT_THRESHOLD,
                   help="a frame fails if |Mirage - FFmpeg| > THRESHOLD (default: 0.01, "
                        "D-019)")
    p.add_argument("--diagnostic-relabel", action="store_true",
                   help="DIAGNOSTIC, not the section 5 comparison: give the distorted "
                        "input the reference's color_space and color_range tags before "
                        "FFmpeg's scale filter where they differ, so FFmpeg does not "
                        "convert them")
    p.add_argument("--ffmpeg", default=DEFAULT_FFMPEG,
                   help=f"FFmpeg with the libvmaf filter (default: {DEFAULT_FFMPEG})")
    p.add_argument("--ffprobe", help="ffprobe (default: the one next to --ffmpeg)")
    p.add_argument("--timeout", type=positive_int, default=DEFAULT_TIMEOUT, metavar="S",
                   help=f"seconds after which a scoring process is stopped "
                        f"(default: {DEFAULT_TIMEOUT})")
    return p.parse_args(argv)


def program(path, what):
    """The absolute path of an executable given as a path or a name on PATH."""
    found = path if os.sep in path else shutil.which(path)
    if not found or not os.path.isfile(found) or not os.access(found, os.X_OK):
        raise SetupError(f"{what} {path!r} is not an executable file")
    return os.path.abspath(found)


def git_toplevel(path):
    try:
        out = subprocess.run(["git", "-C", path, "rev-parse", "--show-toplevel"],
                             stdin=subprocess.DEVNULL, capture_output=True, text=True,
                             timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    return out.stdout.strip() if out.returncode == 0 and out.stdout.strip() else None


def inside(path, root):
    path, root = os.path.realpath(path), os.path.realpath(root)
    return path == root or path.startswith(root.rstrip(os.sep) + os.sep)


class Run:
    """One run of the tool: its paths, placeholders and the files it wrote."""

    def __init__(self, args):
        self.args = args
        self.mirage = program(args.mirage_vmaf, "mirage_vmaf")
        self.ffmpeg = program(args.ffmpeg, "--ffmpeg")
        self.ffprobe = program(args.ffprobe or os.path.join(os.path.dirname(self.ffmpeg),
                                                            "ffprobe"), "--ffprobe")
        self.repo = git_toplevel(HERE)
        self.out = os.path.abspath(args.out_dir)
        if os.path.exists(self.out) and (not os.path.isdir(self.out) or os.listdir(self.out)):
            raise SetupError(f"{args.out_dir} exists and is not an empty directory")
        try:
            with open(args.pairs, encoding="utf-8") as f:
                self.pairs_text = f.read()
            self.pairs = vd.parse_pairs(self.pairs_text)
        except (OSError, UnicodeDecodeError, ValueError) as e:
            raise SetupError(f"pairs file {args.pairs}: {e}") from None
        home = os.path.expanduser("~")
        tmp = os.environ.get("TMPDIR") or "/tmp"
        self.redactor = vd.Redactor([(self.out, "<out_dir>"), (self.repo, "<repo>"),
                                     (home, "<home>"), (tmp, "<tmp>")])
        user = pwd.getpwuid(os.getuid()).pw_name
        host = socket.gethostname()
        # Also the system's temporary directory, by its real path, wherever an
        # output names it.
        system_tmp = os.path.realpath(os.sep + "tmp") + os.sep
        self.needles = [(p, "a local path") for p in
                        {os.path.realpath(home), home, os.path.realpath(tmp), tmp,
                         os.path.realpath(self.out), self.out, system_tmp}
                        if len(p) > 1]
        self.needles += [(self.repo, "a local path")] if self.repo else []
        self.needles += [(s, "the user name") for s in {user} if len(s) >= 3]
        self.needles += [(s, "the host name") for s in {host, host.split(".")[0]} if len(s) >= 3]
        self.redactions = {}
        self.lock = threading.Lock()

    # Files in <out_dir>: always through these, so that nothing local is left.
    def redact(self, text):
        return self.redactor.text(text)[0]

    def write_text(self, rel, text):
        text, n = self.redactor.text(text)
        if n:
            with self.lock:
                self.redactions[rel] = self.redactions.get(rel, 0) + n
        path = os.path.join(self.out, rel)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)

    def redact_file(self, rel):
        """Replaces local paths in a file a program wrote; returns its text."""
        path = os.path.join(self.out, rel)
        with open(path, "rb") as f:
            data, n = self.redactor.data(f.read())
        if n:
            with open(path, "wb") as f:
                f.write(data)
            with self.lock:
                self.redactions[rel] = self.redactions.get(rel, 0) + n
        return data.decode("utf-8", "replace")

    def store_log(self, rel):
        """Replaces the log `rel` with rel.xz (local paths replaced); returns its text."""
        path = os.path.join(self.out, rel)
        with open(path, "rb") as f:
            data, n = self.redactor.data(f.read())
        if n:
            with self.lock:
                self.redactions[rel + ".xz"] = n
        with open(path + ".xz", "wb") as f:
            f.write(vd.xz(data))
        os.remove(path)
        return data.decode("utf-8", "replace")

    def shown_path(self, path, fallback):
        """A path as the outputs show it: relative to the repository when inside
        it, else with placeholders, or `fallback` where those leave something local."""
        if self.repo and inside(path, self.repo):
            return os.path.relpath(os.path.realpath(path), os.path.realpath(self.repo))
        shown = self.redact(os.path.abspath(path))
        return fallback if vd.local_strings(shown, self.needles) else shown

    def log(self, msg):
        with self.lock:
            print(msg, flush=True)


def command_line(argv):
    """A command line in which placeholders ("<...>") stay unquoted."""
    return " ".join(a if a.startswith("<") else shlex.quote(a) for a in argv)


def run_process(argv, cwd, stdout_path, stderr_path, timeout):
    """Runs argv in cwd with stdout and stderr to files; the run's record."""
    rec = {"started_utc": utc_now(), "exit_code": None, "timed_out": False,
           "timeout_s": timeout, "error": None}
    t0 = time.monotonic()
    with open(stdout_path, "wb") as so, open(stderr_path, "wb") as se:
        try:
            rec["exit_code"] = subprocess.run(argv, cwd=cwd, stdin=subprocess.DEVNULL,
                                              stdout=so, stderr=se, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            rec["timed_out"] = True
        except OSError as e:
            rec["error"] = str(e)
    rec["elapsed_s"] = round(time.monotonic() - t0, 3)
    rec["ended_utc"] = utc_now()
    return rec


def capture(argv, timeout=120):
    """(exit status or None, combined output) of a short command."""
    try:
        p = subprocess.run(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=timeout)
        return p.returncode, p.stdout.decode("utf-8", "replace")
    except (OSError, subprocess.SubprocessError) as e:
        return None, f"(could not run: {e})\n"


def linked_library(binary, name):
    """The path of the library `name` (e.g. "libvmaf") that `binary` links,
    from otool -L (macOS) or ldd; None if not found."""
    for argv in (["otool", "-L", binary], ["ldd", binary]):
        if not shutil.which(argv[0]):
            continue
        rc, out = capture(argv)
        if rc != 0:
            continue
        for line in out.splitlines()[1 if argv[0] == "otool" else 0:]:
            part = line.split("=>")[-1].strip().split(" (")[0].strip()
            if os.path.basename(part).startswith(name + ".") or \
                    os.path.basename(part).startswith(name + "-"):
                return part
        return None
    return None


def package_version(path):
    """The Homebrew package version in a library's real path, or "unknown"."""
    m = re.search(r"/Cellar/[^/]+/([^/]+)/", os.path.realpath(path)) if path else None
    return m.group(1) if m else "unknown"


class Environment:
    """environment.txt, built as the commands run, and its fields for delta.json."""

    def __init__(self, run):
        self.run = run
        self.parts = []
        self.fields = {}

    def cmd(self, argv, shown=None):
        rc, out = capture(argv)
        out = self.run.redact(out)
        if out and not out.endswith("\n"):
            out += "\n"
        status = "" if rc == 0 else f"(exit status {rc})\n"
        self.parts.append(f"$ {shown or vd.shell_line(argv)}\n{out}{status}\n")
        return rc, out

    def note(self, title, text):
        self.parts.append(f"$ {title}\n{text.rstrip()}\n\n")

    def capture_machine_and_programs(self):
        r = self.run
        if sys.platform == "darwin":
            _, out = self.cmd(["sw_vers"])
            sv = dict(re.findall(r"^(\w+):\s*(.*)$", out, re.M))
            self.fields["os"] = (f"{sv.get('ProductName', '?')} {sv.get('ProductVersion', '?')}"
                                 f" ({sv.get('BuildVersion', '?')})")
            self.fields["cpu"] = self.cmd(["sysctl", "-n", "machdep.cpu.brand_string"])[1].strip()
            self.fields["logical_cpus"] = self.cmd(["sysctl", "-n", "hw.ncpu"])[1].strip()
        else:
            _, out = self.cmd(["cat", "/etc/os-release"])
            m = re.search(r'^PRETTY_NAME="?([^"\n]*)', out, re.M)
            self.fields["os"] = m.group(1) if m else "unknown"
            _, out = self.cmd(["sh", "-c", "grep -m 1 'model name' /proc/cpuinfo; nproc"])
            self.fields["cpu"] = out.strip().replace("\n", ", ")
        self.fields["kernel"] = self.cmd(["uname", "-srm"])[1].strip()
        self.note("python3 -I (the interpreter running run_delta.py)", sys.version)
        self.fields["python"] = sys.version.split()[0]

        _, out = self.cmd([r.ffmpeg, "-hide_banner", "-version"], "<ffmpeg> -hide_banner -version")
        self.fields["ffmpeg_version"] = out.splitlines()[0] if out else "unknown"
        _, out = self.cmd([r.ffprobe, "-hide_banner", "-version"],
                          "<ffprobe> -hide_banner -version")
        self.fields["ffprobe_version"] = out.splitlines()[0] if out else "unknown"

        # The libvmaf FFmpeg's libvmaf filter uses: libavfilter's, found through otool/ldd.
        avfilter = linked_library(r.ffmpeg, "libavfilter")
        vmaf = linked_library(avfilter, "libvmaf") if avfilter else None
        self.note("libvmaf linked by <ffmpeg> (through libavfilter; otool -L / ldd, real path)",
                  f"libavfilter: {r.redact(avfilter or 'not found')}\n"
                  f"libvmaf: {r.redact(vmaf or 'not found')}\n"
                  f"real path: {r.redact(os.path.realpath(vmaf)) if vmaf else '-'}")
        self.fields["ffmpeg_libvmaf"] = r.redact(os.path.realpath(vmaf)) if vmaf else None
        self.fields["ffmpeg_libvmaf_package_version"] = package_version(vmaf)

        mine = linked_library(r.mirage, "libvmaf")
        _, help_text = capture([r.mirage, "--help"])
        self.help_text = help_text
        sha = vd.sha256_file(r.mirage)
        self.note("<mirage_vmaf>",
                  f"path: {r.shown_path(r.mirage, OUTSIDE)}\nsha256: {sha}\n"
                  f"size: {os.path.getsize(r.mirage)} bytes\n"
                  f"libvmaf: {r.redact(mine or 'not found')}\n"
                  f"real path: {r.redact(os.path.realpath(mine)) if mine else '-'}\n"
                  "--help, first line: "
                  f"{r.redact(help_text.splitlines()[0]) if help_text else '-'}")
        self.mirage_fields = {"path": r.shown_path(r.mirage, OUTSIDE), "sha256": sha,
                              "libvmaf": r.redact(os.path.realpath(mine)) if mine else None,
                              "options": sorted(vd.help_options(help_text))}

    def git(self, when):
        """The repository's HEAD and working tree state; <out_dir> excluded if inside it."""
        r = self.run
        if not r.repo:
            self.note(f"git ({when} of the run)", "not in a git repository")
            return None, None
        _, head = self.cmd(["git", "-C", r.repo, "rev-parse", "HEAD"],
                           "git -C <repo> rev-parse HEAD")
        argv = ["git", "-C", r.repo, "status", "--porcelain"]
        if inside(r.out, r.repo):
            argv += ["--", ".", ":(exclude)" + os.path.relpath(os.path.realpath(r.out),
                                                                os.path.realpath(r.repo))]
        rc, status = capture(argv)
        status = r.redact(status)
        shown = command_line(["git", "-C", "<repo>"] + argv[3:])
        self.parts.append(f"$ {shown}   ({when} of the run)\n" +
                          (status if status else "(no output: the working tree is clean)\n")
                          + "\n")
        clean = rc == 0 and status == ""
        return head.strip() or None, clean

    def text(self):
        r = self.run
        head = ("# Environment of an eval/consistency/run_delta.py run. <mirage_vmaf>, "
                "<ffmpeg> and <ffprobe> are the programs below; <out_dir>, <repo>, <home> "
                "and <tmp> stand for local paths.\n"
                f"<mirage_vmaf> = {r.shown_path(r.mirage, OUTSIDE)}\n"
                f"<ffmpeg> = {r.shown_path(r.ffmpeg, OUTSIDE)}\n"
                f"<ffprobe> = {r.shown_path(r.ffprobe, OUTSIDE)}\n\n")
        return head + "".join(self.parts)


def read_json(path):
    """(parsed JSON or None, error or None) of a file a program wrote."""
    if not os.path.exists(path):
        return None, f"{os.path.basename(path)} was not written"
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f), None
    except (OSError, ValueError) as e:
        return None, f"{os.path.basename(path)} is not valid JSON: {e}"


def prepare_pair(run, pair, params):
    """Probes both inputs and builds the pair's commands; writes cmd.txt."""
    a = run.args
    pdir = os.path.join(run.out, pair.label)
    os.makedirs(pdir)
    sides = {"probe": {"ref": None, "dis": None, "errors": []}, "relabel": None,
             "relabel_warnings": [], "mirage": {"run": None}, "ffmpeg": {"run": None}}
    lines = [f"# {pair.label}: the commands eval/consistency/run_delta.py ran, in "
             f"<out_dir>/{pair.label}. Their stderr logs are kept xz-compressed "
             "(*.stderr.log.xz), as are JSON outputs over 1 MiB (*.json.xz); empty "
             "outputs are not kept."]
    for side, url in (("ref", pair.ref), ("dis", pair.dis)):
        argv = vd.probe_command(run.ffprobe, url)
        out_rel, err_rel = f"ffprobe_{side}.json", f"ffprobe_{side}.stderr.txt"
        lines.append(f"$ {vd.shell_line(argv, SHOWN['ffprobe'])} > {out_rel} 2> {err_rel}")
        rec = run_process(argv, pdir, os.path.join(pdir, out_rel), os.path.join(pdir, err_rel),
                          300)
        text = run.redact_file(f"{pair.label}/{out_rel}")
        try:
            if rec["exit_code"] != 0:
                raise ValueError(f"ffprobe exited with status {rec['exit_code']}"
                                 + (" (timed out)" if rec["timed_out"] else ""))
            sides["probe"][side] = vd.parse_probe(text)
        except ValueError as e:
            which = "reference" if side == "ref" else "distorted input"
            sides["probe"]["errors"].append(f"ffprobe of the {which}: {e}")
        for rel in (out_rel, err_rel):
            path = os.path.join(pdir, rel)
            if os.path.exists(path) and os.path.getsize(path) == 0:
                os.remove(path)
            elif rel == err_rel and os.path.exists(path):
                run.redact_file(f"{pair.label}/{rel}")

    ref_probe, dis_probe = sides["probe"]["ref"], sides["probe"]["dis"]
    if a.diagnostic_relabel and ref_probe and dis_probe:
        sides["relabel"], sides["relabel_warnings"] = vd.relabel_filter(ref_probe, dis_probe)

    m_argv = vd.mirage_command(run.mirage, pair.ref, pair.dis, frames=a.frames,
                               options=vd.forwarded_options(run.mirage_options, a.scaler,
                                                            a.model))
    lines.append(f"$ {vd.shell_line(m_argv, SHOWN['mirage'])} > mirage.stdout.txt "
                 "2> mirage.stderr.log")
    tasks = [("mirage", "mirage_vmaf", m_argv, "mirage.json")]
    if ref_probe:
        graph = vd.filtergraph(ref_probe["width"], ref_probe["height"], a.scaler, a.model,
                               a.threads, frames=a.frames, relabel=sides["relabel"])
        f_argv = vd.ffmpeg_command(run.ffmpeg, pair.ref, pair.dis, graph)
        lines.append(f"$ {vd.shell_line(f_argv, SHOWN['ffmpeg'])} > ffmpeg.stdout.txt "
                     "2> ffmpeg.stderr.log")
        tasks.append(("ffmpeg", "FFmpeg", f_argv, "ffmpeg.json"))
    else:
        sides["ffmpeg"]["error"] = "the reference's size is unknown (ffprobe failed)"
    run.write_text(f"{pair.label}/cmd.txt", "\n".join(lines) + "\n")
    return pdir, sides, tasks


def score(run, pair, pdir, sides, task):
    """Runs one side of a pair and reads what it wrote."""
    key, name, argv, out_rel = task
    rec = run_process(argv, pdir, os.path.join(pdir, f"{key}.stdout.txt"),
                      os.path.join(pdir, f"{key}.stderr.log"), run.args.timeout)
    stdout_path = os.path.join(pdir, f"{key}.stdout.txt")
    if os.path.getsize(stdout_path) == 0:
        os.remove(stdout_path)
    else:
        run.redact_file(f"{pair.label}/{key}.stdout.txt")
    stderr = run.store_log(f"{pair.label}/{key}.stderr.log")
    side = sides[key]
    side["run"] = rec
    out_path = os.path.join(pdir, out_rel)
    if os.path.exists(out_path):
        run.redact_file(f"{pair.label}/{out_rel}")
    result, side["error"] = read_json(out_path)
    side["report" if key == "mirage" else "log"] = result
    if os.path.exists(out_path) and os.path.getsize(out_path) > COMPRESS_JSON_ABOVE:
        with open(out_path, "rb") as f:
            data = f.read()
        with open(out_path + ".xz", "wb") as f:
            f.write(vd.xz(data))
        os.remove(out_path)
    if key == "ffmpeg":
        side["stderr"] = stderr
        run.write_text(f"{pair.label}/ffmpeg_colorspace_lines.txt",
                       "\n".join(vd.colorspace_lines(stderr)) + "\n")
    status = ("timed out" if rec["timed_out"] else rec["error"] if rec["error"]
              else f"exit {rec['exit_code']}")
    run.log(f"{pair.label}: {name} {status} after {rec['elapsed_s']:.1f} s")


def main(argv=None):
    try:
        args = parse_args(argv)
        run = Run(args)
    except SetupError as e:
        print(f"run_delta: {e}", file=sys.stderr)
        return 2
    os.makedirs(run.out, exist_ok=True)

    params = {"variant": vd.VARIANT_RELABEL if args.diagnostic_relabel else vd.VARIANT_PLAIN,
              "frames": args.frames, "threshold": args.threshold, "model": args.model,
              "scaler": args.scaler, "threads": args.threads}
    env = Environment(run)
    started = utc_now()
    head_start, clean_start = env.git("start")
    env.capture_machine_and_programs()
    run.mirage_options = vd.help_options(env.help_text)

    shown_pairs = run.shown_path(args.pairs, "<pairs>")
    command = ["eval/consistency/run_delta.sh", SHOWN["mirage"], "<out_dir>"]
    if args.frames:
        command += ["--frames", str(args.frames)]
    command += ["--jobs", str(args.jobs), "--pairs", shown_pairs, "--scaler", args.scaler,
                "--model", args.model, "--threads", str(args.threads),
                "--threshold", repr(args.threshold), "--ffmpeg", SHOWN["ffmpeg"],
                "--ffprobe", SHOWN["ffprobe"], "--timeout", str(args.timeout)]
    if args.diagnostic_relabel:
        command.append("--diagnostic-relabel")
    run.write_text("run.cmd.txt",
                   "# The command of this eval/consistency/run_delta.py run, from <repo>; the "
                   "programs behind the placeholders are in environment.txt, and <pairs>, if "
                   "it appears, is the pairs file copied to pairs.txt.\n"
                   f"$ {command_line(command)}\n")
    run.write_text("pairs.txt", run.pairs_text)

    prepared = []
    for pair in run.pairs:
        pdir, sides, tasks = prepare_pair(run, pair, params)
        prepared.append((pair, pdir, sides, tasks))
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(score, run, pair, pdir, sides, task)
                   for pair, pdir, sides, tasks in prepared for task in tasks]
        for fut in futures:
            fut.result()

    reports = []
    for pair, pdir, sides, _ in prepared:
        r = vd.evaluate_pair(pair, params, sides)
        reports.append(r)
        run.write_text(f"{pair.label}/delta.json", vd.to_json(r))
        run.write_text(f"{pair.label}/delta.md", vd.pair_markdown(r))

    head_end, clean_end = env.git("end")
    ended = utc_now()
    fields = [r["mirage_vmaf"] for r in reports if r["mirage_vmaf"]]
    mirage_fields = dict(env.mirage_fields)
    for k in ("libvmaf_version", "libvmaf_package_version"):
        values = sorted({f[k] for f in fields if f.get(k) is not None})
        mirage_fields[k] = values[0] if len(values) == 1 else values or None
    warnings = []
    if head_start != head_end:
        warnings.append(f"HEAD changed during the run: {head_start} -> {head_end}")
    if not (clean_start and clean_end):
        warnings.append("the working tree was not clean during the run (environment.txt)")
    overall = vd.overall_report(params, reports, {
        "command": command_line(command),
        "started_utc": started, "ended_utc": ended,
        "git": {"head_start": head_start, "head_end": head_end,
                "clean_start": clean_start, "clean_end": clean_end},
        "environment": env.fields,
        "mirage_vmaf": mirage_fields,
        "pairs_file": {"path": shown_pairs, "sha256": vd.sha256_file(args.pairs)},
        "warnings": warnings,
    })
    run.write_text("environment.txt", env.text())
    overall["redactions"] = dict(sorted(run.redactions.items()))
    run.write_text("delta.json", vd.to_json(overall))
    run.write_text("delta.md", vd.overall_markdown(overall))

    # Nothing local may be left in any output, compressed logs included.
    findings = []
    for dirpath, _, names in os.walk(run.out):
        for name in sorted(names):
            path = os.path.join(dirpath, name)
            with open(path, "rb") as f:
                data = f.read()
            if name.endswith(".xz"):
                data = lzma.decompress(data)
            for what in vd.local_strings(data.decode("utf-8", "replace"), run.needles):
                findings.append(f"{os.path.relpath(path, run.out)} contains {what}")
    with open(os.path.join(run.out, "SHA256SUMS"), "w", encoding="utf-8") as f:
        f.write(vd.sha256sums(run.out))

    for s in overall["pairs"]:
        print(f"{s['pair']}: {s['status']}  frames {s['frames']['mirage']} / "
              f"{s['frames']['ffmpeg']}  max |delta| {vd.num(s['max_abs'])}  "
              f"over {params['threshold']}: {vd.num(s['over_threshold'])}")
    print(f"overall: {overall['status']}; reports in {args.out_dir}/delta.md")
    if findings:
        print("run_delta: these outputs contain local information; do not commit them:",
              file=sys.stderr)
        for x in findings:
            print(f"  {x}", file=sys.stderr)
        return 3
    return 0 if overall["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
