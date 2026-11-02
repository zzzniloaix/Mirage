"""package_check.py: checks the merged Phase 3 design's work packages (design/work_packages.json)
for the review (review-2): every file two packages name (as new or changed) must be owned in turn,
one package depending, directly or through others, on the other; and lists, for each package, the
packages it waits for and the rounds involved. File names are taken from the 'new_files' and
'changed_files' strings: each 'src/...', 'tests/...', 'docs/...', 'eval/...', 'tools/...' or
'CMakeLists.txt' token, with '{h,cpp}' expanded. Descriptive entries ("the empty files of ...")
name no file and are skipped, and so are noted. Reads the file named, writes nothing. Python 3
standard library only; not repository code.

usage: python3 -I -B package_check.py <work_packages.json>
"""
import json
import re
import sys

TOKEN = re.compile(r"(?:src|tests|docs|eval|tools)/[A-Za-z0-9_./{},-]+|CMakeLists\.txt")


def files_of(entry):
    out = []
    for t in TOKEN.findall(entry):
        t = t.rstrip(".,;)")
        m = re.match(r"(.*)\{([^}]*)\}(.*)", t)
        if m:
            out += [m.group(1) + x + m.group(3) for x in m.group(2).split(",")]
        else:
            out.append(t)
    return out


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    wps = json.load(open(argv[1], encoding="utf-8"))
    wid = {w["id"]: w for w in wps}

    def closure(i):
        seen, todo = set(), list(wid[i]["depends_on"])
        while todo:
            j = todo.pop()
            if j not in seen:
                seen.add(j)
                todo += wid[j]["depends_on"]
        return seen

    deps = {w["id"]: closure(w["id"]) for w in wps}
    owners, skipped = {}, []
    for w in wps:
        for kind in ("new_files", "changed_files"):
            for entry in w[kind]:
                fs = files_of(entry)
                if not fs:
                    skipped.append(f"{w['id']} {kind}: {entry}")
                for f in fs:
                    owners.setdefault(f, []).append((w["id"], kind))
    unordered = 0
    for f, ws in sorted(owners.items()):
        ids = sorted({i for i, _ in ws})
        if len(ids) < 2:
            continue
        for a in range(len(ids)):
            for b in range(a + 1, len(ids)):
                x, y = ids[a], ids[b]
                if x in deps[y] or y in deps[x]:
                    order = f"{x} before {y}" if x in deps[y] else f"{y} before {x}"
                    print(f"{f}: {x} and {y} in turn ({order})")
                else:
                    unordered += 1
                    print(f"{f}: {x} (round {wid[x]['round']}) and {y} (round {wid[y]['round']}) "
                          f"NOT ordered by their dependencies")
    for s in skipped:
        print(f"no file named: {s}")
    print(f"{len(wps)} packages, {len(owners)} files named; {unordered} pairs of owners not "
          f"ordered by the dependencies")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
