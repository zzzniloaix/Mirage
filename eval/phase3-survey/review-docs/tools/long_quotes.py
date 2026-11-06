"""long_quotes.py: lists the quotations in files (text between straight or curly double quotes on
one line, or a Markdown block quote line) of at least K words (K = 20 by default), by file, line and
length in words, never the words, so that its output can be kept. It finds candidates for copied
third-party text; whether one is a short citation is read by hand.

usage: python3 -I -B long_quotes.py [--k K] <root or file>...
Python 3 standard library only.
"""
import os
import re
import sys

QUOTE = re.compile(r"\"([^\"\n]{20,})\"|“([^”\n]{20,})”")
WORD = re.compile(r"[A-Za-z][A-Za-z'-]*")


def files(roots):
    for root in roots:
        if os.path.isfile(root):
            yield root
            continue
        for d, subdirs, names in os.walk(root):
            subdirs.sort()
            for n in sorted(names):
                yield os.path.join(d, n)


def main(argv):
    k = 20
    if "--k" in argv:
        i = argv.index("--k")
        k = int(argv[i + 1])
        del argv[i:i + 2]
    if len(argv) < 2:
        print(__doc__)
        return 2
    n = 0
    longest = 0
    for path in files(argv[1:]):
        if not path.endswith((".md", ".txt", ".json")):
            continue
        for i, line in enumerate(open(path, encoding="utf-8", errors="replace").read().split("\n"), 1):
            spans = [m.group(1) or m.group(2) for m in QUOTE.finditer(line)]
            if line.lstrip().startswith("> "):
                spans.append(line.lstrip()[2:])
            for s in spans:
                w = len(WORD.findall(s))
                if w >= k:
                    print(f"{path}:{i}: quotation of {w} words")
                    n += 1
                    longest = max(longest, w)
    print(f"quotations of {k} words or more: {n}; longest: {longest} words")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
