"""The privacy check of this record. Reads every file under a directory, and the text inside any
.xz file, and looks for two things: a case-insensitive extended regular expression given in the
environment variable PRIVACY_PATTERN (the local paths and identities that must not be published;
the pattern names them, so it is not written in this record: the output gives its SHA-256), and
anything shaped like an e-mail address. Each match is printed by file, line and kind, never with
the matched text, so that the output can be kept in a public record.

usage: PRIVACY_PATTERN='...' python3 -I privacy_check.py <dir>
exit status: 0 no match, 1 at least one match, 2 bad arguments or an unreadable file
"""
import hashlib
import lzma
import os
import re
import sys

EMAIL = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}")


def texts(root):
    """(relative path, text) of every file under root; an .xz file's text is its decompressed content."""
    for r, _, fs in sorted(os.walk(root)):
        for f in sorted(fs):
            p = os.path.join(r, f)
            rel = os.path.relpath(p, root)
            raw = open(p, "rb").read()
            if f.endswith(".xz"):
                raw = lzma.decompress(raw)
                rel += " (decompressed)"
            yield rel, raw.decode("utf-8", errors="replace"), f.endswith(".xz")


def main(argv):
    pattern = os.environ.get("PRIVACY_PATTERN", "")
    if len(argv) != 1 or not pattern:
        print(__doc__)
        return 2
    rx = re.compile(pattern, re.IGNORECASE)
    n_files = n_xz = n_lines = n_hits = 0
    for rel, text, is_xz in texts(argv[0]):
        n_files += 1
        n_xz += is_xz
        for n, line in enumerate(text.split("\n"), 1):
            n_lines += 1
            if rx.search(line):
                print(f"{rel}:{n}: matches the pattern")
                n_hits += 1
            if EMAIL.search(line):
                print(f"{rel}:{n}: an e-mail address")
                n_hits += 1
    print(f"pattern: case-insensitive, SHA-256 of its text {hashlib.sha256(pattern.encode()).hexdigest()}")
    print(f"checked: {n_files} files ({n_xz} of them .xz, read decompressed), {n_lines} lines")
    print(f"matches: {n_hits}")
    return 1 if n_hits else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
