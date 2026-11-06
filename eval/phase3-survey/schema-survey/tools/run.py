"""Runs one command of the schema survey and saves it with its full output.

usage: python3 -I <out>/schema-survey/tools/run.py NAME 'COMMAND'

COMMAND is a shell command line in which <repo>, <out> and <scratch> stand for
the repository's worktree, the survey's output directory and the scratch
directory; it runs with /bin/sh in <repo>. The record, <out>/schema-survey/
runs/NAME.txt, holds the command as given (with the placeholders), its
standard output and standard error as they came, merged, with every local
path, the user's name and the host's name replaced by placeholders, and its
exit status. Python 3 standard library only.
"""

import datetime
import getpass
import os
import pathlib
import socket
import subprocess
import sys

TOOLS = pathlib.Path(__file__).resolve().parent
OUT = TOOLS.parent.parent                 # <out>: the survey's output directory
SCRATCH = OUT.parent.parent               # <scratch>: <scratch>/phase3/survey is <out>
REPO = SCRATCH / "phase3-wt"              # <repo>: the worktree of vmaf/phase-3


def placeholders():
    """(real text, placeholder) pairs, longest first."""
    pairs = [(str(REPO), "<repo>"), (str(OUT), "<out>"), (str(SCRATCH), "<scratch>"),
             (os.path.realpath(str(REPO)), "<repo>"), (os.path.realpath(str(OUT)), "<out>"),
             (os.path.realpath(str(SCRATCH)), "<scratch>"),
             (str(pathlib.Path.home()), "<home>")]
    try:
        pairs.append((getpass.getuser(), "<user>"))
    except Exception:
        pass
    host = socket.gethostname()
    if host:
        pairs.append((host, "<host>"))
        pairs.append((host.split(".")[0], "<host>"))
    seen, out = set(), []
    for real, name in sorted(pairs, key=lambda x: -len(x[0])):
        if real and real not in seen and len(real) > 2:
            seen.add(real)
            out.append((real, name))
    return out


def shown(text, subs):
    for real, name in subs:
        text = text.replace(real, name)
    return text


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    name, command = argv[1], argv[2]
    real = (command.replace("<repo>", str(REPO)).replace("<out>", str(OUT))
            .replace("<scratch>", str(SCRATCH)))
    subs = placeholders()
    started = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    p = subprocess.run(["/bin/sh", "-c", real], cwd=REPO, stdin=subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    ended = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    output = shown(p.stdout.decode("utf-8", "replace"), subs)
    record = (f"# One command of the schema survey, run in <repo> by {TOOLS.name}/run.py, "
              f"started {started}, ended {ended} (UTC).\n"
              "# <repo>: the worktree of vmaf/phase-3 at 75cf6e0; <out>: the survey's output "
              "directory; <scratch>: the scratch directory.\n"
              "# The command, then its standard output and standard error as they came, then "
              "its exit status.\n\n"
              f"$ {command}\n{output}"
              + ("" if output.endswith("\n") or not output else "\n")
              + f"(exit status {p.returncode})\n")
    path = OUT / "schema-survey" / "runs" / f"{name}.txt"
    path.write_text(record, encoding="utf-8")
    sys.stdout.write(record)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
