"""show_lines.py: prints the lines of each 'file:first-last' (or 'file:line') given, with their
numbers, so that the review's checks of the design's file:line citations are on record. Run in
the repository's worktree; reads the files named, writes nothing. Python 3 standard library only;
for the review (review-2), not repository code.

usage: python3 -I -B show_lines.py <file:first-last>...
"""
import sys


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    for spec in argv[1:]:
        path, _, rng = spec.rpartition(":")
        a, _, b = rng.partition("-")
        a, b = int(a), int(b or a)
        lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
        print(f"== {path}:{a}-{b}")
        for n in range(a, b + 1):
            print(f"{n:5d}  {lines[n - 1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
