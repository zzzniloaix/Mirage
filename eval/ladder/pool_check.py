"""The acceptance of the work order's section 2 (docs/DECISIONS.md D-049): a ladder scored at
several pool sizes gives every pair the same scores.

usage: python3 -I -B eval/ladder/pool_check.py <mirage_vmaf> <out_dir> --ladder <master>
                                               [--jobs J[,J...]] [--frames N]
                                               [--memory-limit SIZE]

Runs `mirage_vmaf --ladder <master> --jobs J` once for each J (default 1,2,4 and the default
size, given as 0: no --jobs), the first --frames N frame pairs of each pair (default 300; 0:
every frame, no --frames), each with `/usr/bin/time -l` for its peak resident memory, and
compares the reports: the same plan, the same pairs complete, and every pair's per-frame
scores, frame indices and timestamps equal at full precision. With --memory-limit, given to
every run, it also checks each run's peak resident memory against the limit as its report
gives it (pool.memory_limit, in bytes): the report's pool.peak_resident and /usr/bin/time -l's
maximum resident set size. Writes into <out_dir>, a new or empty directory:
jobs-<J>/report.json.xz, stdout.txt, stderr.txt and time.txt for each run; cmd.txt (every
command, with <mirage_vmaf>, <ladder>, <ladder_dir> and <out_dir> for paths); check.md and
check.json (the verdict, each run's pool fields and times, each pair's comparison); and
SHA256SUMS. PASS when every run is complete, every pair is the same in every run, and no
run's peak is over a memory limit it was given. Python 3 standard library only.
"""

import argparse
import hashlib
import json
import lzma
import os
import re
import shlex
import subprocess
import sys
import time


def parse_args(argv):
    p = argparse.ArgumentParser(prog="pool_check.py")
    p.add_argument("mirage_vmaf")
    p.add_argument("out_dir")
    p.add_argument("--ladder", required=True)
    p.add_argument("--jobs", default="1,2,4,0",
                   help="pool sizes, 0 for mirage_vmaf's default (default: 1,2,4,0)")
    p.add_argument("--frames", type=int, default=300,
                   help="frame pairs of each pair, 0 for every frame (default: 300)")
    p.add_argument("--memory-limit")
    return p.parse_args(argv)


def main(argv=None):
    a = parse_args(argv)
    out = os.path.abspath(a.out_dir)
    if os.path.exists(out) and os.listdir(out):
        print(f"pool_check: {a.out_dir} is not an empty directory", file=sys.stderr)
        return 2
    os.makedirs(out, exist_ok=True)
    # The ladder's playlists resolve against its directory: <ladder_dir>.
    names = {os.path.abspath(a.mirage_vmaf): "<mirage_vmaf>", a.mirage_vmaf: "<mirage_vmaf>",
             os.path.abspath(a.ladder): "<ladder>", a.ladder: "<ladder>", out: "<out_dir>"}
    if "://" not in a.ladder:
        names[os.path.dirname(os.path.abspath(a.ladder))] = "<ladder_dir>"
        names[os.path.realpath(os.path.dirname(os.path.abspath(a.ladder)))] = "<ladder_dir>"

    def shown(text):
        for path, name in sorted(names.items(), key=lambda x: -len(x[0])):
            text = text.replace(path, name)
        return text

    runs, cmds = [], ["# The commands of this eval/ladder/pool_check.py run, one per pool size."]
    for j in [int(x) for x in a.jobs.split(",")]:
        d = os.path.join(out, f"jobs-{j or 'default'}")
        os.makedirs(d)
        report = os.path.join(d, "report.json")
        argv_ = ["/usr/bin/time", "-l", a.mirage_vmaf, "--ladder", a.ladder]
        if a.frames:
            argv_ += ["--frames", str(a.frames)]
        argv_ += ["--out", report]
        if j:
            argv_ += ["--jobs", str(j)]
        if a.memory_limit:
            argv_ += ["--memory-limit", a.memory_limit]
        cmds.append("$ " + shown(" ".join(shlex.quote(x) for x in argv_)))
        t = time.monotonic()
        p = subprocess.run(argv_, stdin=subprocess.DEVNULL, capture_output=True, text=True)
        wall = time.monotonic() - t
        # /usr/bin/time -l writes its lines after the program's stderr.
        k = p.stderr.find("real")
        k = p.stderr.rfind("\n", 0, k) + 1 if k >= 0 else len(p.stderr)
        for name, text in (("stdout.txt", p.stdout), ("stderr.txt", p.stderr[:k]),
                           ("time.txt", p.stderr[k:])):
            with open(os.path.join(d, name), "w") as f:
                f.write(shown(text))
        rep = None
        if os.path.exists(report):
            with open(report, "rb") as f:
                data = f.read()
            rep = json.loads(data)
            with open(report + ".xz", "wb") as f:
                f.write(lzma.compress(shown(data.decode()).encode()))
            os.remove(report)
        m = re.search(r"(\d+)\s+maximum resident set size", p.stderr)
        runs.append({"jobs_given": j, "exit": p.returncode, "wall_s": round(wall, 3),
                     "max_rss_time_l": int(m.group(1)) if m else None, "report": rep})

    first = runs[0]["report"]
    problems, pairs_check = [], []
    for r in runs:
        rep = r["report"]
        if rep is None:
            problems.append(f"jobs {r['jobs_given']}: no report")
        elif rep.get("status") != "complete":
            problems.append(f"jobs {r['jobs_given']}: the run is {rep.get('status')}")
        limit = ((rep or {}).get("pool") or {}).get("memory_limit")
        if a.memory_limit and limit is None:
            problems.append(f"jobs {r['jobs_given']}: the report gives no memory limit")
        elif limit is not None:
            for what, peak in (("the report's peak resident memory",
                                ((rep or {}).get("pool") or {}).get("peak_resident")),
                               ("/usr/bin/time -l's maximum resident set size",
                                r["max_rss_time_l"])):
                if peak is None or peak > limit:
                    problems.append(f"jobs {r['jobs_given']}: {what}, {peak}, is over the "
                                    f"memory limit, {limit}")
    if first is not None:
        for k, p0 in enumerate(first.get("pairs") or []):
            row = {"index": p0.get("index"), "dis": shown((p0.get("dis") or {}).get("url") or ""),
                   "frames": len(p0.get("frames") or []), "same_in": []}
            for r in runs[1:]:
                rep = r["report"] or {}
                match = [p for p in rep.get("pairs") or [] if p.get("index") == p0.get("index")]
                same = bool(match) and match[0].get("frames") == p0.get("frames") and \
                    match[0].get("pooled") == p0.get("pooled")
                row["same_in"].append({"jobs_given": r["jobs_given"], "same": same})
                if not same:
                    problems.append(f"pair {p0.get('index')} differs at jobs {r['jobs_given']}")
            pairs_check.append(row)
        for r in runs[1:]:
            rep = r["report"] or {}
            for key in ("ladder", "reference", "skipped", "not_scored"):
                if rep.get(key) != first.get(key):
                    problems.append(f"jobs {r['jobs_given']}: {key} differs")
    verdict = "PASS" if not problems else "FAIL"
    summary = {"verdict": verdict, "problems": problems, "frames": a.frames,
               "memory_limit_given": a.memory_limit,
               "runs": [{k: v for k, v in r.items() if k != "report"} |
                        {"pool": (r["report"] or {}).get("pool"),
                         "pairs_complete": len((r["report"] or {}).get("pairs") or []),
                         "status": (r["report"] or {}).get("status")} for r in runs],
               "pairs": pairs_check}
    with open(os.path.join(out, "check.json"), "w") as f:
        json.dump(summary, f, indent=1)
    lines = [f"# The ladder at several pool sizes: {verdict}", "",
             f"- Ladder: `<ladder>`, " + (f"the first {a.frames} frame pairs of each pair"
                                           if a.frames else "every frame pair of each pair")
             + " (`cmd.txt`).",
             "- PASS when every run is complete and each pair's frames (indices, timestamps, "
             "scores at full precision) and pooled values are the same in every run.", "",
             "| --jobs | exit | status | pool size | most at once | pair memory (report) | "
             "memory limit | peak resident (report) | max RSS (time -l) | wall s |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    for r in summary["runs"]:
        pool = r["pool"] or {}
        lines.append(f"| {r['jobs_given'] or 'default'} | {r['exit']} | {r['status']} | "
                     f"{pool.get('jobs')} | {pool.get('max_running')} | "
                     f"{pool.get('pair_memory') or '-'} | {pool.get('memory_limit') or '-'} | "
                     f"{pool.get('peak_resident')} | {r['max_rss_time_l']} | {r['wall_s']} |")
    lines += ["", "| pair | distorted | frames | " +
              " | ".join(f"same at --jobs {r['jobs_given'] or 'default'}" for r in runs[1:]) + " |",
              "|---|---|---|" + "---|" * (len(runs) - 1)]
    for row in pairs_check:
        lines.append(f"| {row['index']} | `{row['dis']}` | {row['frames']} | " +
                     " | ".join("yes" if s["same"] else "NO" for s in row["same_in"]) + " |")
    if problems:
        lines += ["", "Problems:"] + [f"- {x}" for x in problems]
    with open(os.path.join(out, "check.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    with open(os.path.join(out, "cmd.txt"), "w") as f:
        f.write("\n".join(cmds) + "\n")
    sums = []
    for root, _, files in os.walk(out):
        for n in files:
            rel = os.path.relpath(os.path.join(root, n), out)
            with open(os.path.join(root, n), "rb") as f:
                sums.append(f"{hashlib.sha256(f.read()).hexdigest()}  {rel}")
    with open(os.path.join(out, "SHA256SUMS"), "w") as f:
        f.write("\n".join(sorted(sums, key=lambda x: x.split('  ', 1)[1])) + "\n")
    print(f"pool_check: {verdict}" + ("" if not problems else ": " + "; ".join(problems)))
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
