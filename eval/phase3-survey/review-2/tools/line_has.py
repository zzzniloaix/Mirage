"""line_has.py: for each 'file:line:token' given, says whether that line of the file contains the
token, and prints each file's SHA-256 once, without printing the lines themselves: a check of a
citation by line against a third-party copy that is not committed. Reads the files named, writes
nothing. Python 3 standard library only; for the review (review-2), not repository code.

usage: python3 -I -B line_has.py <file:line:token>...
"""
import hashlib
import sys


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    seen = set()
    bad = 0
    for spec in argv[1:]:
        path, line, token = spec.split(":", 2)
        data = open(path, "rb").read()
        if path not in seen:
            seen.add(path)
            lines = data.count(bytes([10]))
            print(f"{path}: sha256 {hashlib.sha256(data).hexdigest()}, {lines} lines")
        text = data.decode("utf-8", errors="replace").split("\n")[int(line) - 1]
        ok = token in text
        bad += not ok
        print(f"{path}:{line} contains '{token}': {'yes' if ok else 'NO'}")
    print(f"{len(argv) - 1} checks, {bad} not found")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
