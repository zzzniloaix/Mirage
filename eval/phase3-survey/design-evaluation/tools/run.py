"""Runs one command and records it in runs/<name>.txt next to this directory: the command, the
directory it ran in, when it started and ended (UTC), its exit status, and its full stdout and
stderr.

usage: python3 -I -B run.py <name> [--cwd DIR] -- <command> [args...]

Local paths and names in the record are replaced by placeholders, so that the record names no
local path, user or host: the survey's output directory by <out>, the worktree of vmaf/phase-3
by <repo>, the session's scratch directory by <scratch>, Homebrew's prefix by <homebrew>, the
home directory by <home>, and the user's and the host's names by <user> and <host>. Nothing else
is changed. Written for the Phase 3 design (design-evaluation.md); Python 3 standard library only.
"""

import datetime
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DESIGN = os.path.dirname(HERE)                   # <out>/design-evaluation
OUT = os.path.dirname(DESIGN)                    # <out>: the survey's output directory
SCRATCH = os.path.dirname(os.path.dirname(OUT))  # <scratch>
REPO = os.path.join(SCRATCH, "phase3-wt")        # <repo>


def brew_prefix():
    try:
        p = subprocess.run(["brew", "--prefix"], capture_output=True, text=True)
        return p.stdout.strip() if p.returncode == 0 else ""
    except OSError:
        return ""


def replacements():
    home = os.path.expanduser("~")
    user = os.path.basename(home)
    paths = [(OUT, "<out>"), (REPO, "<repo>"), (SCRATCH, "<scratch>"), (home, "<home>")]
    brew = brew_prefix()
    if brew:
        paths.append((brew, "<homebrew>"))
    more = []
    for p, n in paths:
        if p.startswith("/private/"):
            more.append((p[len("/private"):], n))
        else:
            more.append(("/private" + p, n))
    paths += more
    host = socket.gethostname()
    words = [(user, "<user>"), (user.replace(".", "-"), "<user>")]
    if host:
        words += [(host, "<host>"), (host.split(".")[0], "<host>")]
    # Longest first, so that a path inside another is replaced by its own name.
    return (sorted(paths, key=lambda x: -len(x[0])) +
            sorted([w for w in words if len(w[0]) >= 3], key=lambda x: -len(x[0])))


REPL = replacements()


def shown(text):
    for old, new in REPL:
        if old:
            text = text.replace(old, new)
    return text


def quote(a):
    if a and all(c.isalnum() or c in "-_./=:,+@%<>" for c in a):
        return a
    return "'" + a.replace("'", "'\\''") + "'"


def utc():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def main(argv):
    if "--" not in argv or argv.index("--") < 1:
        print(__doc__, file=sys.stderr)
        return 2
    name = argv[0]
    opts = argv[1:argv.index("--")]
    cmd = argv[argv.index("--") + 1:]
    cwd = None
    if opts[:1] == ["--cwd"]:
        cwd = opts[1]
    started = utc()
    t0 = time.monotonic()
    p = subprocess.run(cmd, cwd=cwd, stdin=subprocess.DEVNULL, capture_output=True,
                       env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))
    wall = time.monotonic() - t0
    ended = utc()
    out = p.stdout.decode("utf-8", "replace")
    err = p.stderr.decode("utf-8", "replace")
    lines = [
        f"$ {' '.join(quote(a) for a in cmd)}",
        f"(in {cwd or os.getcwd()})",
        f"started {started}, ended {ended}, exit status {p.returncode}, wall {wall:.3f} s",
        "--- stdout ---",
        out.rstrip("\n"),
        "--- stderr ---",
        err.rstrip("\n"),
        "--- end ---",
    ]
    record = shown("\n".join(lines) + "\n")
    os.makedirs(os.path.join(DESIGN, "runs"), exist_ok=True)
    with open(os.path.join(DESIGN, "runs", name + ".txt"), "w", encoding="utf-8") as f:
        f.write(record)
    sys.stdout.write(record)
    return p.returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
