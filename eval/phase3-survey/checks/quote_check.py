"""Finds the passages files share with a reference text (the work order's translation): every run
of K or more consecutive words (K = 6 by default) that also occurs, in that order, in the
reference. Prints each run's file, first and last line and length in words, and whether its words
are mostly names (identifiers such as field, tag and option names, which may stay) or prose (which
is a quotation, to be put in other words). It never prints the shared words themselves, so that
its output can be kept in a public record.

usage: python3 -I quote_check.py <reference text> <file>... [--k K]
exit status: 0 no prose run, 1 at least one prose run, 2 bad arguments
"""
import re
import sys

TOKEN = re.compile(r"[A-Za-z0-9_]+(?:[-'.][A-Za-z0-9_]+)*")


def tokens(text):
    """The words of text with the line each starts on (1-based), as (word, line, original)."""
    out = []
    for n, line in enumerate(text.split("\n"), 1):
        line = line.replace("’", "'").replace("‘", "'")
        for m in TOKEN.finditer(line):
            out.append((m.group(0).lower(), n, m.group(0)))
    return out


def is_name(word):
    return any(c in word for c in "_-") or any(c.isdigit() for c in word) or (
        len(word) > 1 and word.isupper())


def main(argv):
    k = 6
    if "--k" in argv:
        i = argv.index("--k")
        k = int(argv[i + 1])
        del argv[i:i + 2]
    if len(argv) < 2:
        print(__doc__)
        return 2
    ref = [w for w, _, _ in tokens(open(argv[0], encoding="utf-8").read())]
    shingles = {tuple(ref[i:i + k]) for i in range(len(ref) - k + 1)}
    prose_runs = name_runs = 0
    for path in argv[1:]:
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError as e:
            print(f"{path}: cannot read ({e.strerror})")
            continue
        toks = tokens(text)
        hit = [tuple(w for w, _, _ in toks[i:i + k]) in shingles
               for i in range(len(toks) - k + 1)]
        i = 0
        while i < len(hit):
            if not hit[i]:
                i += 1
                continue
            j = i
            while j + 1 < len(hit) and hit[j + 1]:
                j += 1
            run = toks[i:j + k]
            names = sum(1 for _, _, o in run if is_name(o))
            kind = "names" if names >= 0.8 * len(run) else "prose"
            if kind == "prose":
                prose_runs += 1
            else:
                name_runs += 1
            print(f"{path}:{run[0][1]}-{run[-1][1]}: {len(run)} words shared, {kind}")
            i = j + 1
    print(f"in all: {prose_runs} prose runs, {name_runs} runs of names, K = {k}")
    return 1 if prose_runs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
