"""code_refs.py: lists every file:line citation of the repository's own files in the given drafts
(src/, tests/, tools/, docs/, eval/, CMakeLists.txt, with a line or range and more ranges after a
comma) and, with --show, prints the cited lines of <repo> under each, so that each claim can be read
against them. Also lists every eval/phase3-survey/<path> the drafts cite and whether that path is in
the records (the copy to be committed as eval/phase3-survey/), and every repository path cited
without a line and whether it exists in <repo>.

usage: python3 -I -B code_refs.py <repo> <records> [--show] <draft>...
Python 3 standard library only.
"""
import glob
import os
import re
import sys

REF = re.compile(r"(?<![\w/.<-])((?:src|tests|tools|docs|eval)/[A-Za-z0-9_./{},*-]*[A-Za-z0-9_}*]|CMakeLists\.txt)"
                 r"(?::(\d+(?:-\d+)?(?:, ?\d+(?:-\d+)?)*))?")
SURVEY = "eval/phase3-survey/"


def ranges(spec):
    out = []
    for part in re.split(r", ?", spec):
        a, _, b = part.partition("-")
        out.append((int(a), int(b or a)))
    return out


def main(argv):
    show = "--show" in argv
    argv = [a for a in argv if a != "--show"]
    if len(argv) < 4:
        print(__doc__)
        return 2
    repo, records, drafts = argv[1], argv[2], argv[3:]
    seen = {}
    for d in drafts:
        for n, line in enumerate(open(d, encoding="utf-8").read().split("\n"), 1):
            for m in REF.finditer(line):
                path, spec = m.group(1), m.group(2)
                seen.setdefault((path, spec), []).append(f"{os.path.basename(d)}:{n}")
    missing = 0
    with_lines = sorted(k for k in seen if k[1])
    print(f"== repository citations with lines: {len(with_lines)}")
    for path, spec in with_lines:
        where = ", ".join(seen[(path, spec)][:6]) + (" ..." if len(seen[(path, spec)]) > 6 else "")
        full = os.path.join(repo, path)
        if not os.path.isfile(full):
            print(f"MISSING FILE {path}:{spec} (cited at {where})")
            missing += 1
            continue
        lines = open(full, encoding="utf-8", errors="replace").read().split("\n")
        bad = [r for r in ranges(spec) if r[1] > len(lines) or r[0] < 1]
        status = "OUT OF RANGE" if bad else "ok"
        if bad:
            missing += 1
        print(f"{status} {path}:{spec} ({len(lines)} lines; cited at {where})")
        if show and not bad:
            for a, b in ranges(spec):
                for i in range(a, b + 1):
                    print(f"    {i:5d}  {lines[i - 1][:220]}")
    print(f"== survey-copy citations ({SURVEY}...)")
    surv = sorted({k[0] for k in seen if k[0].startswith(SURVEY)})
    for p in surv:
        rel = p[len(SURVEY):]
        hits = glob.glob(os.path.join(records, rel)) if any(c in rel for c in "*{") else (
            [os.path.join(records, rel)] if os.path.exists(os.path.join(records, rel.rstrip("/"))) else [])
        ok = bool(hits)
        if not ok:
            missing += 1
        print(f"{'ok' if ok else 'NOT IN RECORDS'} {p} (cited at {', '.join(seen[next(k for k in seen if k[0] == p)][:4])})")
    print("== other repository paths cited without lines")
    others = sorted({k[0] for k in seen if not k[1] and not k[0].startswith(SURVEY)})
    for p in others:
        full = os.path.join(repo, p.rstrip("/"))
        exists = bool(glob.glob(full)) if any(c in p for c in "*{") else os.path.exists(full)
        print(f"{'exists' if exists else 'absent in <repo>'} {p}")
    print(f"problems: {missing}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
