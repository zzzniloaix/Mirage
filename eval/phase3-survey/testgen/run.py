"""Runs one command and records it: the command, the directory it ran in, its full stdout and
stderr, its exit status and its wall time, in logs/<name>.txt next to this script.

usage: python3 -I run.py <name> [--cwd DIR] -- <command> [args...]

Local paths in what is recorded are replaced: this survey's output directory by <out>, the
session's scratch directory by <scratch>, the repository's worktree by <repo>, Homebrew's prefix
by <homebrew>, and the home directory, the user's name and the host's name by <home>, <user> and
<host>, so that the records name no local path, user or host. Nothing else is changed.
"""

import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.dirname(HERE)                      # <out>: the survey's output directory
SCRATCH = os.path.dirname(os.path.dirname(OUT))  # <scratch>: the session's scratch directory
REPO = os.path.join(SCRATCH, "phase3-wt")        # <repo>: the worktree of vmaf/phase-3


def brew_prefix():
    try:
        p = subprocess.run(["brew", "--prefix"], capture_output=True, text=True)
        return p.stdout.strip() if p.returncode == 0 else ""
    except OSError:
        return ""


HOMEBREW = brew_prefix()
HOME = os.path.expanduser("~")
USER = os.path.basename(HOME)


def names():
    pairs = [(OUT, "<out>"), (REPO, "<repo>"), (SCRATCH, "<scratch>"),
             ("/private" + OUT, "<out>"), ("/private" + REPO, "<repo>"),
             ("/private" + SCRATCH, "<scratch>"), (HOME, "<home>")]
    if HOMEBREW:
        pairs.append((HOMEBREW, "<homebrew>"))
    for p in list(pairs):
        if p[0].startswith("/private/"):
            pairs.append((p[0][len("/private"):], p[1]))
    host = socket.gethostname()
    words = [(USER, "<user>"), (USER.replace(".", "-"), "<user>")]
    if host:
        words += [(host, "<host>"), (host.split(".")[0], "<host>")]
    # Longest first, so that a path inside another is replaced by its own name.
    return sorted(pairs, key=lambda x: -len(x[0])) + sorted(words, key=lambda x: -len(x[0]))


def shown(text):
    for path, name in names():
        if path:
            text = text.replace(path, name)
    return text


def quote(a):
    if a and all(c.isalnum() or c in "-_./=:,+@%<>" for c in a):
        return a
    return "'" + a.replace("'", "'\\''") + "'"


def main(argv):
    if len(argv) < 3 or "--" not in argv:
        print(__doc__, file=sys.stderr)
        return 2
    name = argv[0]
    rest = argv[1:argv.index("--")]
    cmd = argv[argv.index("--") + 1:]
    cwd = None
    if rest[:1] == ["--cwd"]:
        cwd = rest[1]
    t0 = time.monotonic()
    p = subprocess.run(cmd, cwd=cwd, stdin=subprocess.DEVNULL, capture_output=True)
    wall = time.monotonic() - t0
    out = p.stdout.decode("utf-8", "replace")
    err = p.stderr.decode("utf-8", "replace")
    os.makedirs(os.path.join(HERE, "logs"), exist_ok=True)
    path = os.path.join(HERE, "logs", name + ".txt")
    with open(path, "w") as f:
        f.write("$ " + shown(" ".join(quote(a) for a in cmd)) + "\n")
        f.write("(in " + shown(os.path.abspath(cwd or os.getcwd())) + ")\n")
        f.write("--- stdout ---\n" + shown(out))
        if out and not out.endswith("\n"):
            f.write("\n")
        f.write("--- stderr ---\n" + shown(err))
        if err and not err.endswith("\n"):
            f.write("\n")
        f.write(f"--- exit status {p.returncode}, wall time {wall:.3f} s (time.monotonic) ---\n")
    sys.stdout.write(shown(out))
    sys.stderr.write(shown(err))
    print(f"[run.py] {name}: exit status {p.returncode}, wall time {wall:.3f} s", file=sys.stderr)
    return p.returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
