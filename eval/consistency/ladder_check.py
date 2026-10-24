"""Checks mirage_vmaf's ladder mode (docs/DECISIONS.md D-029) against the
same pairs scored one by one.

Runs `mirage_vmaf --ladder <master>` and, with the same settings,
`mirage_vmaf --ref <reference> --dis <distorted>` on every pair of a pairs
file, and checks that each pair of the ladder report has, value for value,
the keys of that pair's own report that are a pair's (PAIR_KEYS: every
per-frame score at full precision, the frames' timestamps and positions,
the pooled values, the alignment, the conversions, the warnings and the
inputs), that both reports have the same settings and libraries
(RUN_KEYS), that the ladder scored exactly the pairs of the pairs file, and
that its report accounts for every entry of the master once: as the
reference's, one pair's, or skipped. Writes into <out_dir>:

  run.cmd.txt, pairs.txt  this run's command line and the pairs file it read
  environment.txt         machine, mirage_vmaf, git state
  ladder/                 the ladder run: cmd.txt, mirage.json (its report),
                          mirage.stdout.txt, mirage.stderr.log.xz
  <pair>/                 each pair scored on its own: the same files
  check.md, check.json    the comparison, and the ladder's entries, reference,
                          pairs and skipped entries
  SHA256SUMS              every file above

Reports larger than 1 MiB are kept as mirage.json.xz. Local paths in all
of it are replaced by placeholders (<out_dir>, <repo>, <home>, <tmp>); the
program shows as <mirage_vmaf>. See eval/consistency/README.md. Python 3
standard library only; run it with python3 -I -B.

usage: python3 -I -B eval/consistency/ladder_check.py <mirage_vmaf> <out_dir>
           [--ladder URL] [--mezzanine URL] [--pairs FILE] [--frames N]
           [--scaler NAME] [--model NAME | --model-file FILE] [--jobs J]
           [--timeout S]

Exit status: 0 every pair of the ladder has its own report's values; 1 a
pair differs, is missing or extra, or a run failed (check.md says why); 2
bad arguments, or the run could not start; 3 an output still contains a
local path or name after the placeholders (do not commit it).
"""

import argparse
import concurrent.futures
import json
import lzma
import os
import pwd
import re
import socket
import sys
import threading

HERE = os.path.dirname(os.path.realpath(__file__))
sys.dont_write_bytecode = True   # no __pycache__ in the repository's working tree
sys.path.insert(0, HERE)
import run_delta as rd  # noqa: E402
import vmafdelta as vd  # noqa: E402

REPORT_VERSION = 1
DEFAULT_LADDER = ("https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/"
                  "bipbop_16x9_variant.m3u8")   # docs/DECISIONS.md P-002
DEFAULT_PAIRS = os.path.join(HERE, "pairs", "bipbop16x9.txt")
SHOWN = "<mirage_vmaf>"

# The keys of a pair's report (src/VMAFReport.h) that a ladder report's pair
# has too, and the run's keys that both reports have.
PAIR_KEYS = ("scored_as", "dis_conversion", "ref_conversion", "warnings", "frames_scored",
             "pooled", "alignment", "ref", "dis", "frames")
RUN_KEYS = ("tool", "libvmaf_version", "libvmaf_package_version", "ffmpeg_libraries",
            "config_file", "model", "model_source", "model_sha256", "scaler", "n_threads",
            "n_subsample", "frame_limit")
ABSENT = object()   # a key a report doesn't have


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="ladder_check.py",
        description="Check that mirage_vmaf --ladder gives each pair of a master playlist the "
                    "report values of the same pair scored on its own.",
        epilog="Exit status: 0 every pair is the same; 1 a pair differs, is missing or extra, "
               "or a run failed; 2 bad arguments; 3 an output contains a local path or name.")
    p.add_argument("mirage_vmaf", help="the mirage_vmaf program to check")
    p.add_argument("out_dir", help="a new or empty directory for the outputs")
    p.add_argument("--ladder", default=DEFAULT_LADDER, metavar="URL",
                   help="the master playlist (default: Apple's bipbop 16x9)")
    p.add_argument("--mezzanine", metavar="URL",
                   help="mezzanine mode: the reference of every pair, given to the --ladder "
                        "run as --mezzanine; the pairs file's references must be this string "
                        "(default: top-rung mode)")
    p.add_argument("--pairs", default=DEFAULT_PAIRS, metavar="FILE",
                   help="the pairs the ladder is expected to score, '<label> <reference> "
                        "<distorted>' per line (default: eval/consistency/pairs/bipbop16x9.txt)")
    p.add_argument("--frames", type=rd.positive_int, metavar="N",
                   help="score only the first N frame pairs of each pair (default: every frame)")
    p.add_argument("--scaler", default="bicubic",
                   help="mirage_vmaf's --scaler, given to every run (default: bicubic)")
    p.add_argument("--model", metavar="NAME",
                   help="mirage_vmaf's --model (default: vmaf_v0.6.1)")
    p.add_argument("--model-file", metavar="FILE",
                   help="mirage_vmaf's --model-file instead of a built-in model")
    p.add_argument("--jobs", type=rd.positive_int, default=1, metavar="J",
                   help="mirage_vmaf processes run at the same time (default: 1)")
    p.add_argument("--timeout", type=rd.positive_int, default=rd.DEFAULT_TIMEOUT, metavar="S",
                   help=f"seconds after which a run is stopped (default: {rd.DEFAULT_TIMEOUT})")
    args = p.parse_args(argv)
    if args.model and args.model_file:
        p.error("--model and --model-file can't be given together")
    if not args.model_file:
        args.model = args.model or "vmaf_v0.6.1"
    return args


class Check(rd.Run):
    """One run of the check: its paths and placeholders, as run_delta's Run
    has them, without FFmpeg."""

    def __init__(self, args):
        self.args = args
        self.mirage = rd.program(args.mirage_vmaf, "mirage_vmaf")
        self.repo = rd.git_toplevel(HERE)
        self.out = os.path.abspath(args.out_dir)
        if os.path.exists(self.out) and (not os.path.isdir(self.out) or os.listdir(self.out)):
            raise rd.SetupError(f"{args.out_dir} exists and is not an empty directory")
        try:
            with open(args.pairs, encoding="utf-8") as f:
                self.pairs_text = f.read()
            self.pairs = vd.parse_pairs(self.pairs_text)
        except (OSError, UnicodeDecodeError, ValueError) as e:
            raise rd.SetupError(f"pairs file {args.pairs}: {e}") from None
        self.model_file = os.path.abspath(args.model_file) if args.model_file else None
        if self.model_file and not os.path.isfile(self.model_file):
            raise rd.SetupError(f"--model-file {args.model_file} is not a file")
        home = os.path.expanduser("~")
        tmp = os.environ.get("TMPDIR") or "/tmp"
        self.redactor = vd.Redactor([(self.out, "<out_dir>"), (self.repo, "<repo>"),
                                     (home, "<home>"), (tmp, "<tmp>")])
        user = pwd.getpwuid(os.getuid()).pw_name
        host = socket.gethostname()
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


class Environment(rd.Environment):
    """environment.txt: the machine, mirage_vmaf and the git state."""

    def capture_machine_and_programs(self):
        r = self.run
        if sys.platform == "darwin":
            _, out = self.cmd(["sw_vers"])
            sv = dict(re.findall(r"^(\w+):\s*(.*)$", out, re.M))
            self.fields["os"] = (f"{sv.get('ProductName', '?')} {sv.get('ProductVersion', '?')}"
                                 f" ({sv.get('BuildVersion', '?')})")
            self.fields["cpu"] = self.cmd(["sysctl", "-n", "machdep.cpu.brand_string"])[1].strip()
        else:
            _, out = self.cmd(["cat", "/etc/os-release"])
            m = re.search(r'^PRETTY_NAME="?([^"\n]*)', out, re.M)
            self.fields["os"] = m.group(1) if m else "unknown"
            self.fields["cpu"] = self.cmd(["sh", "-c", "grep -m 1 'model name' /proc/cpuinfo"]
                                          )[1].strip()
        self.fields["kernel"] = self.cmd(["uname", "-srm"])[1].strip()
        self.note("python3 -I (the interpreter running ladder_check.py)", sys.version)
        self.fields["python"] = sys.version.split()[0]
        mine = rd.linked_library(r.mirage, "libvmaf")
        _, help_text = rd.capture([r.mirage, "--help"])
        sha = vd.sha256_file(r.mirage)
        self.note(SHOWN,
                  f"path: {r.shown_path(r.mirage, rd.OUTSIDE)}\nsha256: {sha}\n"
                  f"size: {os.path.getsize(r.mirage)} bytes\n"
                  f"libvmaf: {r.redact(mine or 'not found')}\n"
                  f"real path: {r.redact(os.path.realpath(mine)) if mine else '-'}\n"
                  "--help, first line: "
                  f"{r.redact(help_text.splitlines()[0]) if help_text else '-'}")
        self.mirage_fields = {"path": r.shown_path(r.mirage, rd.OUTSIDE), "sha256": sha,
                              "libvmaf": r.redact(os.path.realpath(mine)) if mine else None}

    def text(self):
        r = self.run
        head = ("# Environment of an eval/consistency/ladder_check.py run. <mirage_vmaf> is the "
                "program below; <out_dir>, <repo>, <home> and <tmp> stand for local paths.\n"
                f"<mirage_vmaf> = {r.shown_path(r.mirage, rd.OUTSIDE)}\n\n")
        return head + "".join(self.parts)


def settings_options(args, model_file):
    """The scoring settings every mirage_vmaf run of the check is given."""
    opts = ["--frames", str(args.frames)] if args.frames else []
    opts += ["--scaler", args.scaler]
    return opts + (["--model-file", model_file] if model_file else ["--model", args.model])


def run_mirage(check, name, argv, shown):
    """Runs one mirage_vmaf in <out_dir>/<name>; its record and report."""
    d = os.path.join(check.out, name)
    os.makedirs(d, exist_ok=True)
    check.write_text(f"{name}/cmd.txt",
                     f"# {name}: the command eval/consistency/ladder_check.py ran, in "
                     f"<out_dir>/{name}. Its stderr log is kept xz-compressed "
                     "(mirage.stderr.log.xz), as is a report over 1 MiB (mirage.json.xz).\n"
                     f"$ {shown} > mirage.stdout.txt 2> mirage.stderr.log\n")
    rec = rd.run_process(argv, d, os.path.join(d, "mirage.stdout.txt"),
                         os.path.join(d, "mirage.stderr.log"), check.args.timeout)
    stdout = os.path.join(d, "mirage.stdout.txt")
    if os.path.getsize(stdout) == 0:
        os.remove(stdout)
    else:
        check.redact_file(f"{name}/mirage.stdout.txt")
    check.store_log(f"{name}/mirage.stderr.log")
    path = os.path.join(d, "mirage.json")
    report, error = None, None
    if os.path.exists(path):
        check.redact_file(f"{name}/mirage.json")
        report, error = rd.read_json(path)
        if os.path.getsize(path) > rd.COMPRESS_JSON_ABOVE:
            with open(path, "rb") as f:
                data = f.read()
            with open(path + ".xz", "wb") as f:
                f.write(vd.xz(data))
            os.remove(path)
    else:
        error = "mirage.json was not written"
    status = ("timed out" if rec["timed_out"] else rec["error"] if rec["error"]
              else f"exit {rec['exit_code']}")
    check.log(f"{name}: mirage_vmaf {status} after {rec['elapsed_s']:.1f} s")
    return {"run": rec, "report": report, "error": error}


def score_differences(a, b):
    """(frames that differ, largest |difference| of their scores) of two
    reports' frames lists."""
    differ, largest = 0, 0.0
    for x, y in zip(a or [], b or []):
        if x != y:
            differ += 1
            if vd.is_number(x.get("vmaf")) and vd.is_number(y.get("vmaf")):
                largest = max(largest, abs(x["vmaf"] - y["vmaf"]))
    return differ + abs(len(a or []) - len(b or [])), largest


def accounting(report):
    """Problems with how the ladder report accounts for the master's entries:
    each must be the reference's, one pair's, or skipped, once."""
    entries = [e.get("index") for e in (report.get("ladder") or {}).get("entries") or []]
    seen = {}
    for i in (report.get("reference") or {}).get("entries") or []:
        seen.setdefault(i, []).append("the reference")
    for p in report.get("pairs") or []:
        for i in p.get("entries") or []:
            seen.setdefault(i, []).append(f"pair {p.get('index')}")
    for s in report.get("skipped") or []:
        seen.setdefault(s.get("entry"), []).append("skipped")
    problems = [f"entry {i} is {' and '.join(seen[i])}" for i in sorted(seen, key=str)
                if len(seen[i]) > 1]
    problems += [f"entry {i} is neither the reference's, a pair's nor skipped"
                 for i in entries if i not in seen]
    problems += [f"entry {i} is listed but the master has no such entry"
                 for i in sorted(set(seen) - set(entries), key=str)]
    return problems


def compare(ladder, alone, pairs, redact=lambda s: s):
    """The check of a ladder report (`ladder`, None if there is none) against
    the reports of the pairs scored on their own (`alone`: label -> report or
    None), for the pairs of the pairs file (`pairs`). Returns the check's
    verdict, problems and per-pair records."""
    problems, records = [], []
    if ladder is None:
        problems.append("the ladder run left no report")
    ladder_pairs = (ladder or {}).get("pairs") or []
    by_inputs = {}
    for p in ladder_pairs:
        key = ((p.get("ref") or {}).get("url"), (p.get("dis") or {}).get("url"))
        by_inputs.setdefault(key, []).append(p)
    matched = set()
    for pair in pairs:
        key = (redact(pair.ref), redact(pair.dis))
        found = by_inputs.get(key) or []
        mine = alone.get(pair.label)
        rec = {"pair": pair.label, "ref": key[0], "dis": key[1],
               "ladder_index": found[0].get("index") if found else None,
               "entries": found[0].get("entries") if found else None,
               "frames": {"ladder": found[0].get("frames_scored") if found else None,
                          "alone": (mine or {}).get("frames_scored")},
               "differing": [], "differing_frames": None, "largest_score_difference": None}
        if len(found) > 1:
            rec["status"] = "TWICE"
            problems.append(f"{pair.label}: the ladder scored this pair {len(found)} times")
        elif not found:
            rec["status"] = "MISSING" if ladder is not None else "NO LADDER REPORT"
            if ladder is not None:
                problems.append(f"{pair.label}: the ladder didn't score this pair")
        elif mine is None:
            rec["status"] = "NO REPORT"
            problems.append(f"{pair.label}: the pair scored on its own left no report")
        else:
            p = found[0]
            matched.add(id(p))
            # A key both reports lack can't differ; one that one of them lacks does.
            rec["differing"] = [k for k in PAIR_KEYS if p.get(k, ABSENT) != mine.get(k, ABSENT)]
            rec["differing"] += [f"run's {k}" for k in RUN_KEYS
                                 if ladder.get(k, ABSENT) != mine.get(k, ABSENT)]
            if mine.get("reference_mode") != "explicit-pair":
                rec["differing"].append("run's reference_mode")
            rec["differing_frames"], rec["largest_score_difference"] = score_differences(
                p.get("frames"), mine.get("frames"))
            rec["status"] = "SAME" if not rec["differing"] else "DIFFERS"
            if rec["differing"]:
                problems.append(f"{pair.label}: the ladder's pair differs in "
                                + ", ".join(rec["differing"]))
        records.append(rec)
    extra = [p for p in ladder_pairs if id(p) not in matched
             and ((p.get("ref") or {}).get("url"), (p.get("dis") or {}).get("url"))
             not in {(redact(x.ref), redact(x.dis)) for x in pairs}]
    for p in extra:
        problems.append(f"the ladder's pair {p.get('index')} ({(p.get('dis') or {}).get('url')}) "
                        "is not in the pairs file")
    entry_problems = accounting(ladder) if ladder is not None else []
    problems += entry_problems
    return {"status": "PASS" if not problems else "FAIL", "problems": problems,
            "pairs": records,
            "extra_pairs": [{"index": p.get("index"), "dis": (p.get("dis") or {}).get("url"),
                             "entries": p.get("entries")} for p in extra],
            "entries_accounted_once": ladder is not None and not entry_problems}


def ladder_summary(report):
    """What the ladder report says about the master and its plan."""
    if report is None:
        return None
    role = {}
    for i in (report.get("reference") or {}).get("entries") or []:
        role[i] = "reference"
    for p in report.get("pairs") or []:
        for i in p.get("entries") or []:
            role[i] = f"pair {p.get('index')}"
    for s in report.get("skipped") or []:
        role[s.get("entry")] = f"skipped: {s.get('reason')}"
    lad = report.get("ladder") or {}
    return {"reference_mode": report.get("reference_mode"), "url": lad.get("url"),
            "location": lad.get("location"), "sha256": lad.get("sha256"),
            "reference": report.get("reference"), "skipped": report.get("skipped"),
            "warnings": report.get("warnings"),
            "entries": [dict(e, role=role.get(e.get("index"))) for e in lad.get("entries") or []],
            "pairs": [{"index": p.get("index"), "entries": p.get("entries"),
                       "dis": (p.get("dis") or {}).get("url"),
                       "frames_scored": p.get("frames_scored"),
                       "pooled_mean": (p.get("pooled") or {}).get("mean")}
                      for p in report.get("pairs") or []]}


def markdown(r):
    lines = ["# Ladder check: mirage_vmaf --ladder against the pairs scored one by one", "",
             f"**{'PASS' if r['status'] == 'PASS' else 'FAIL'}**: " +
             ("every pair of the ladder has the values of the same pair scored on its own."
              if r["status"] == "PASS" else "see the problems below."), ""]
    p = r["params"]
    lines += [f"- Ladder: {p['ladder']}; reference mode "
              f"{'mezzanine, ' + p['mezzanine'] if p['mezzanine'] else 'top-rung'}.",
              f"- Pairs file: {p['pairs_file']['path']} (sha256 {p['pairs_file']['sha256']}).",
              f"- Settings given to every run: {' '.join(p['settings'])}.",
              f"- Compared, value for value, for each pair: {', '.join(PAIR_KEYS)}; and of each "
              f"run: {', '.join(RUN_KEYS)}.",
              f"- mirage_vmaf: sha256 {r['mirage_vmaf']['sha256']}.",
              f"- Code: commit {r['git']['head_start']}; the working tree was "
              + ("clean at the start of the run and clean at its end"
                 if r["git"]["clean_start"] and r["git"]["clean_end"] else "NOT clean")
              + " (environment.txt).",
              f"- Run: {r['started_utc']} to {r['ended_utc']} (UTC).", ""]
    if r["problems"]:
        lines += ["## Problems", ""] + [f"- {vd.cell(x)}" for x in r["problems"]] + [""]
    lines += ["## The pairs", "",
              "| pair | ladder pair | entries | frames, ladder / alone | the same | differing | "
              "frames that differ |", "|---|---|---|---|---|---|---|"]
    for x in r["pairs"]:
        lines.append(f"| {x['pair']} | {vd.num(x['ladder_index'])} | "
                     f"{', '.join(str(e) for e in x['entries'] or []) or '-'} | "
                     f"{vd.num(x['frames']['ladder'])} / {vd.num(x['frames']['alone'])} | "
                     f"{x['status']} | {vd.cell(', '.join(x['differing'])) or '-'} | "
                     f"{vd.num(x['differing_frames'])} |")
    lad = r["ladder"]
    if lad:
        lines += ["", "## The ladder", "",
                  f"Master playlist {lad['url']}, read from {lad['location']}, sha256 "
                  f"{lad['sha256']}. Reference ({lad['reference_mode']}): "
                  f"{(lad['reference'] or {}).get('url')}.", "",
                  "| entry | line | URI | BANDWIDTH | RESOLUTION | CODECS | video | role |",
                  "|---|---|---|---|---|---|---|---|"]
        for e in lad["entries"]:
            lines.append(f"| {e.get('index')} | {e.get('line')} | {vd.cell(e.get('uri'))} | "
                         f"{e.get('bandwidth')} | {vd.cell(e.get('resolution') or '-')} | "
                         f"{vd.cell(e.get('codecs') or '-')} | "
                         f"{'yes' if e.get('video') else 'no'} | {vd.cell(e.get('role'))} |")
        lines += ["", "| ladder pair | entries | distorted | frames scored | pooled mean |",
                  "|---|---|---|---|---|"]
        for x in lad["pairs"]:
            lines.append(f"| {x['index']} | {', '.join(str(e) for e in x['entries'] or [])} | "
                         f"{vd.cell(x['dis'])} | {x['frames_scored']} | "
                         f"{vd.num(x['pooled_mean'])} |")
        lines += ["", "Warnings of the ladder report: "
                  + ("; ".join(lad["warnings"]) if lad["warnings"] else "none") + "."]
    lines += ["", "Each run's directory has its command (cmd.txt), mirage_vmaf's report "
              "(mirage.json) and its output; environment.txt records the machine, the program "
              "and the git state; SHA256SUMS covers every file.", ""]
    return "\n".join(lines)


def main(argv=None):
    try:
        args = parse_args(argv)
        check = Check(args)
    except rd.SetupError as e:
        print(f"ladder_check: {e}", file=sys.stderr)
        return 2
    os.makedirs(check.out, exist_ok=True)

    env = Environment(check)
    started = rd.utc_now()
    head_start, clean_start = env.git("start")
    env.capture_machine_and_programs()

    model_shown = check.shown_path(check.model_file, "<model_file>") if check.model_file else None
    shown_pairs = check.shown_path(args.pairs, "<pairs>")
    settings = settings_options(args, check.model_file)
    shown_settings = settings_options(args, model_shown)
    command = ["python3", "-I", "-B", "eval/consistency/ladder_check.py", SHOWN, "<out_dir>",
               "--ladder", check.redact(args.ladder)]
    if args.mezzanine:
        command += ["--mezzanine", check.redact(args.mezzanine)]
    command += ["--pairs", shown_pairs] + shown_settings + [
        "--jobs", str(args.jobs), "--timeout", str(args.timeout)]
    check.write_text("run.cmd.txt",
                     "# The command of this eval/consistency/ladder_check.py run, from <repo>; "
                     "<mirage_vmaf> is the program of environment.txt, and <pairs>, if it "
                     "appears, is the pairs file copied to pairs.txt.\n"
                     f"$ {rd.command_line(command)}\n")
    check.write_text("pairs.txt", check.pairs_text)

    mezzanine = ["--mezzanine", args.mezzanine] if args.mezzanine else []
    tasks = [("ladder", [check.mirage, "--ladder", args.ladder] + mezzanine
              + ["--out", "mirage.json"] + settings)]
    tasks += [(p.label, vd.mirage_command(check.mirage, p.ref, p.dis, options=settings))
              for p in check.pairs]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {name: pool.submit(run_mirage, check, name, argv, vd.shell_line(argv, SHOWN))
                   for name, argv in tasks}
        results = {name: f.result() for name, f in futures.items()}

    ladder = results["ladder"]
    alone = {p.label: results[p.label]["report"] for p in check.pairs}
    verdict = compare(ladder["report"], alone, check.pairs, check.redact)
    for name, res in results.items():
        if res["run"]["exit_code"] != 0:
            verdict["problems"].append(
                f"{name}: mirage_vmaf "
                + ("timed out" if res["run"]["timed_out"] else res["run"]["error"]
                   or f"exited with status {res['run']['exit_code']}"))
            verdict["status"] = "FAIL"

    head_end, clean_end = env.git("end")
    report = {
        "report_version": REPORT_VERSION, "tool": "eval/consistency/ladder_check.py",
        **verdict,
        "params": {"ladder": check.redact(args.ladder),
                   "mezzanine": check.redact(args.mezzanine) if args.mezzanine else None,
                   "pairs_file": {"path": shown_pairs, "sha256": vd.sha256_file(args.pairs)},
                   "settings": shown_settings},
        "ladder": ladder_summary(ladder["report"]),
        "runs": {name: res["run"] for name, res in results.items()},
        "command": rd.command_line(command), "started_utc": started, "ended_utc": rd.utc_now(),
        "git": {"head_start": head_start, "head_end": head_end, "clean_start": clean_start,
                "clean_end": clean_end},
        "environment": env.fields, "mirage_vmaf": env.mirage_fields,
    }
    check.write_text("environment.txt", env.text())
    report["redactions"] = dict(sorted(check.redactions.items()))
    check.write_text("check.json", vd.to_json(report))
    check.write_text("check.md", markdown(report))

    findings = []
    for dirpath, _, names in os.walk(check.out):
        for name in sorted(names):
            path = os.path.join(dirpath, name)
            with open(path, "rb") as f:
                data = f.read()
            if name.endswith(".xz"):
                data = lzma.decompress(data)
            for what in vd.local_strings(data.decode("utf-8", "replace"), check.needles):
                findings.append(f"{os.path.relpath(path, check.out)} contains {what}")
    with open(os.path.join(check.out, "SHA256SUMS"), "w", encoding="utf-8") as f:
        f.write(vd.sha256sums(check.out))

    for x in report["pairs"]:
        print(f"{x['pair']}: {x['status']}  frames {vd.num(x['frames']['ladder'])} / "
              f"{vd.num(x['frames']['alone'])}")
    print(f"overall: {report['status']}; report in {args.out_dir}/check.md")
    if findings:
        print("ladder_check: these outputs contain local information; do not commit them:",
              file=sys.stderr)
        for x in findings:
            print(f"  {x}", file=sys.stderr)
        return 3
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
