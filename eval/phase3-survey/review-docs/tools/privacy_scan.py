"""privacy_scan.py: looks for local paths, identities and e-mail addresses in files, the way the
task's check asks: the case-insensitive extended regex the task gave (passed in the environment
variable PRIVACY_PATTERN, so that this file doesn't hold it), any e-mail address, and, as extra
hints, a list of other local-path and host forms. Reads every file under each root, .xz files
decompressed, and also checks each file's own path. Prints file:line:column and which pattern
matched (its number), never the matched text, so that its output can be kept.

usage: PRIVACY_PATTERN='<pattern>' python3 -I -B privacy_scan.py <root or file>...
Python 3 standard library only.
"""
import hashlib
import lzma
import os
import re
import sys

EMAIL = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}")
# extra hints, not part of the task's check: other local paths and host forms
EXTRA = [
    ("opt-homebrew", re.compile(r"/opt/homebrew", re.I)),
    ("usr-local", re.compile(r"/usr/local/", re.I)),
    ("volumes", re.compile(r"/Volumes/", re.I)),
    ("home-dir", re.compile(r"/home/[a-z]", re.I)),
    ("tilde-path", re.compile(r"(?<![\w<])~/[A-Za-z]")),
    ("tmp-path", re.compile(r"(?<![\w<>.-])/tmp/")),
    ("var-path", re.compile(r"(?<![\w<>.-])/var/")),
    ("dot-local-host", re.compile(r"\b[\w-]+\.local\b(?!_)", re.I)),
    ("mac-host", re.compile(r"\b(?:MacBook|iMac|Mac-mini|Mac-Pro|Macmini)[\w-]*", re.I)),
    ("ipv4", re.compile(r"\b(?!127\.0\.0\.1\b)(?!0\.0\.0\.0\b)\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b")),
]


def files(roots):
    for root in roots:
        if os.path.isfile(root):
            yield root
            continue
        for d, subdirs, names in os.walk(root):
            subdirs.sort()
            for n in sorted(names):
                yield os.path.join(d, n)


def read(path):
    data = open(path, "rb").read()
    if path.endswith(".xz"):
        data = lzma.decompress(data)
    return data.decode("utf-8", errors="replace")


def main(argv):
    pat = os.environ.get("PRIVACY_PATTERN")
    if not pat or len(argv) < 2:
        print(__doc__)
        return 2
    task = re.compile(pat, re.I)
    print(f"pattern: case-insensitive, SHA-256 of its text {hashlib.sha256(pat.encode()).hexdigest()}")
    n_files = n_xz = n_lines = 0
    hits = {"task": 0, "email": 0}
    extra = {name: 0 for name, _ in EXTRA}
    for path in files(argv[1:]):
        n_files += 1
        n_xz += path.endswith(".xz")
        rel = path
        for m in task.finditer(rel):
            print(f"PATH {rel}: task pattern")
            hits["task"] += 1
        for m in EMAIL.finditer(rel):
            print(f"PATH {rel}: e-mail")
            hits["email"] += 1
        text = read(path)
        lines = text.split("\n")
        n_lines += len(lines)
        for i, line in enumerate(lines, 1):
            for m in task.finditer(line):
                print(f"{rel}:{i}:{m.start() + 1}: task pattern")
                hits["task"] += 1
            for m in EMAIL.finditer(line):
                print(f"{rel}:{i}:{m.start() + 1}: e-mail address")
                hits["email"] += 1
            for name, rx in EXTRA:
                for m in rx.finditer(line):
                    print(f"{rel}:{i}:{m.start() + 1}: hint {name}")
                    extra[name] += 1
    print(f"checked: {n_files} files ({n_xz} of them .xz, read decompressed), {n_lines} lines")
    print(f"task pattern matches: {hits['task']}; e-mail addresses: {hits['email']}")
    print("hints (not the task's check): " + ", ".join(f"{k} {v}" for k, v in extra.items()))
    return 1 if hits["task"] or hits["email"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
