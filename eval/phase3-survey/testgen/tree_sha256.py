"""Compares two generated ladders file by file: the SHA-256 and size of every file under each
directory, side by side, and whether they are the same.

usage: python3 -I tree_sha256.py <dir_a> <dir_b>

Prints one line per relative path (sorted): the SHA-256 of each side, the size of each side and
"same" or "DIFFERENT" (or "only in a" / "only in b"), then the counts. Exit status 0 when every
file is the same in both, 1 otherwise.
"""

import hashlib
import os
import sys


def files(root):
    out = {}
    for d, _, names in os.walk(root):
        for n in names:
            p = os.path.join(d, n)
            with open(p, "rb") as f:
                data = f.read()
            out[os.path.relpath(p, root)] = (hashlib.sha256(data).hexdigest(), len(data))
    return out


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    a, b = files(argv[0]), files(argv[1])
    same = differ = only = 0
    total = 0
    for rel in sorted(set(a) | set(b)):
        if rel not in b:
            print(f"{rel}  {a[rel][0]}  -  {a[rel][1]}  -  only in a")
            only += 1
        elif rel not in a:
            print(f"{rel}  -  {b[rel][0]}  -  {b[rel][1]}  only in b")
            only += 1
        else:
            ok = a[rel] == b[rel]
            print(f"{rel}  {a[rel][0]}  {b[rel][0]}  {a[rel][1]}  {b[rel][1]}  "
                  f"{'same' if ok else 'DIFFERENT'}")
            same += ok
            differ += not ok
            total += a[rel][1]
    print(f"{same} files the same, {differ} different, {only} in one directory only; "
          f"{total} bytes in the files of both")
    return 0 if differ == 0 and only == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
