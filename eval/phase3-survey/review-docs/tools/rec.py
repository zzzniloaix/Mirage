"""rec.py: runs one shell command and saves its command, working directory, UTC date, full output
(stdout and stderr together) and exit status to a record file, with local paths written as
<repo>, <out>, <records>, <drafts>, <scratch> and <home>.

usage: python3 -I -B rec.py <record file> <cwd> <shell command>
The command is shown as given, with the same path rewriting. Python 3 standard library only.
"""
import datetime
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# verify/tools -> verify -> docs-draft -> phase3 -> scratch
SCRATCH = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
HOME = os.path.expanduser("~")


def subs():
    pairs = []
    for base in {SCRATCH, os.path.realpath(SCRATCH)}:
        alts = {base}
        if base.startswith("/private/"):
            alts.add(base[len("/private"):])
        for b in alts:
            pairs += [
                (os.path.join(b, "phase3-wt"), "<repo>"),
                (os.path.join(b, "phase3", "records", "phase3-survey"), "<records>"),
                (os.path.join(b, "phase3", "survey"), "<out>"),
                (os.path.join(b, "phase3", "docs-draft"), "<drafts>"),
                (b, "<scratch>"),
            ]
    pairs.append((HOME, "<home>"))
    pairs.sort(key=lambda p: -len(p[0]))
    return pairs


def clean(s):
    for a, b in subs():
        s = s.replace(a, b)
    return s


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    target, cwd, cmd = argv[1], argv[2], argv[3]
    date = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    p = subprocess.run(["/bin/bash", "-c", cmd], cwd=cwd, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    out = p.stdout.decode("utf-8", errors="replace")
    text = (f"command (cwd: {clean(cwd)}): {clean(cmd)}\n"
            f"date_utc: {date}\n"
            f"output (stdout and stderr):\n{clean(out)}"
            f"{'' if out.endswith(chr(10)) or not out else chr(10)}"
            f"exit={p.returncode}\n")
    os.makedirs(os.path.dirname(os.path.abspath(target)), exist_ok=True)
    with open(target, "w", encoding="utf-8") as f:
        f.write(text)
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
