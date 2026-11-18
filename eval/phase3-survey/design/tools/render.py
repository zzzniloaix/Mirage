"""render.py: assembles <out>/design.md from the merged design's hand-written parts
(design/src/part1.md, design/src/part2.md) and its data (design/owner_questions.json,
design/work_packages.json, design/p_entries.json, design/risks.json), which become sections 12
to 15. It first writes "section N" in the data as "§N", so that the data and the document read
alike. Before writing it checks the data against itself: each question's blocks against the
packages' blocked-by lists, each question's round against the packages it blocks, each package's
round against its dependencies and questions, every question a package's text names among its
blocked-by list, every file two packages name owned in turn (one depending on the other), and the
counts the hand-written parts state. Python 3 standard library only; for the Phase 3 design, not
repository code.

usage: python3 -I -B render.py <design dir> <out file>
"""

import json
import os
import re
import sys

FILE = re.compile(r"(?<![\w/])(?:src|tests|docs|eval|tools)/[A-Za-z0-9_./{},-]+|CMakeLists\.txt")

ROUNDS = {
    1: "Round 1: the path to §3.2's two acceptance tests",
    2: "Round 2: sessions, the session fetch, decoded SCTE-35 messages and the composite example",
    3: "Round 3: the UI, the conditional and later packages, and the differences to report",
}


def sect(s):
    s = re.sub(r"\bsections (\d[\d.]*) and (\d[\d.]*)", r"§\1 and §\2", s)
    s = re.sub(r"\b[Ss]ection (\d)", r"§\1", s)
    return s


def walk(x):
    if isinstance(x, str):
        return sect(x)
    if isinstance(x, list):
        return [walk(v) for v in x]
    if isinstance(x, dict):
        return {k: walk(v) for k, v in x.items()}
    return x


def load(d, name):
    path = os.path.join(d, name)
    data = walk(json.load(open(path, encoding="utf-8")))
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")
    return data


def cell(s):
    return s.replace("|", "\\|")


def words(xs):
    """['a', 'b', 'c'] as 'a, b and c'."""
    xs = list(xs)
    return xs[0] if len(xs) == 1 else ", ".join(xs[:-1]) + " and " + xs[-1]


def files_of(entry):
    """The file names in a package's new_files or changed_files entry, '{h,cpp}' expanded."""
    out = []
    for t in FILE.findall(entry):
        t = t.rstrip(".,;)")
        m = re.match(r"(.*)\{([^}]*)\}(.*)", t)
        out += [m.group(1) + x + m.group(3) for x in m.group(2).split(",")] if m else [t]
    return out


def check(qs, wps, ps, rs, part1):
    """The data's own consistency, and the counts part1 states. Raises on any disagreement."""
    ids = [q["id"] for q in qs]
    assert ids == [f"Q{i:02d}" for i in range(1, len(qs) + 1)], "questions not Q01..Qnn in order"
    wid = {w["id"]: w for w in wps}
    assert len(wid) == len(wps), "duplicate package id"
    for w in wps:
        for d in w["depends_on"]:
            assert d in wid, f"{w['id']} depends on unknown {d}"
            assert wid[d]["round"] <= w["round"], f"{w['id']} (round {w['round']}) depends on {d}"
        for q in w["blocked_by_questions"]:
            assert q in ids, f"{w['id']} blocked by unknown {q}"
    rnd = {q["id"]: q["round"] for q in qs}
    for q in qs:
        listing = sorted(w["id"] for w in wps if q["id"] in w["blocked_by_questions"])
        assert sorted(q["blocks"]) == listing, f"{q['id']} blocks {q['blocks']}, packages say {listing}"
        assert listing, f"{q['id']} blocks no package"
        assert q["round"] == min(wid[w]["round"] for w in listing), f"{q['id']}: round"
        assert isinstance(q["d007"], bool) and q["summary"], f"{q['id']}: d007 or summary"
        if q.get("items"):
            assert not q["options"], f"{q['id']}: options and items"
            for it in q["items"]:
                assert len(it["options"]) >= 2 and it["agent_recommendation"], f"{q['id']} ({it['id']})"
        else:
            assert 2 <= len(q["options"]) <= 5, f"{q['id']}: options"
    for w in wps:
        for q in w["blocked_by_questions"]:
            assert rnd[q] <= w["round"], f"{w['id']} (round {w['round']}) blocked by {q}"
        text = " ".join([w["description"]] + w["tests"] + w["new_files"] + w["changed_files"])
        named = set(re.findall(r"\bQ\d\d\b", text))
        assert named <= set(w["blocked_by_questions"]), \
            f"{w['id']} names {sorted(named - set(w['blocked_by_questions']))} but isn't blocked by them"

    def after(i):
        seen, todo = set(), list(wid[i]["depends_on"])
        while todo:
            j = todo.pop()
            if j not in seen:
                seen.add(j)
                todo += wid[j]["depends_on"]
        return seen

    owners = {}
    for w in wps:
        for entry in w["new_files"] + w["changed_files"]:
            for f in files_of(entry):
                owners.setdefault(f, set()).add(w["id"])
    for f, ids in owners.items():
        ids = sorted(ids)
        for a in ids:
            for b in ids:
                assert a == b or a in after(b) or b in after(a), f"{f}: {a} and {b} not in turn"
    kept = [p for p in ps if "moved_to" not in p]
    count = {r: sum(1 for q in qs if q["round"] == r) for r in ROUNDS}
    stated = {
        "questions": f"{len(qs)} questions in three rounds of {count[1]}, {count[2]} and {count[3]}",
        "P-entries": f"{len(kept)} P-entries",
        "packages": f"{len(wps)} work packages",
        "risks": f"{len(rs)} risks",
        "round 1": f"round 1's {count[1]} questions",
    }
    for what, text in stated.items():
        assert text in part1, f"part1 doesn't state the {what} as '{text}'"
    return count, kept


def question_md(q):
    out = [f"#### {q['id']}. {q['title']}", ""]
    mark = "; **D-007**: the answer is, or includes, an approval or a ruling under D-007" if q["d007"] else ""
    out += [f"*Round {q['round']}{mark}.*", "", f"**Summary.** {q['summary']}", "", q["question"], ""]
    if q.get("items"):
        for it in q["items"]:
            out.append(f"- **({it['id']}) {it['title']}.**")
            for o in it["options"]:
                out.append(f"  - **{o['label']}.** {o['description']}")
            out.append(f"  - *Agent's recommendation:* {it['agent_recommendation']}. "
                       f"*Why:* {it['why']}")
    else:
        for o in q["options"]:
            out.append(f"- **{o['label']}.** {o['description']}")
    out += ["", f"**Agent's recommendation:** {q['agent_recommendation']}.", "",
            f"**Why:** {q['why']}", "",
            f"**Blocks:** {', '.join(q['blocks'])}.", ""]
    return out


def questions_md(qs, count):
    d007 = [q["id"] for q in qs if q["d007"]]
    out = ["## 12. Owner questions", "",
           f"{len(qs)} questions in three rounds: {count[1]} in round 1, the answers the packages "
           f"on the path to §3.2's two acceptance tests need before they start; {count[2]} in "
           "round 2, those that sessions, the session fetch, the decoded SCTE-35 messages and the "
           f"composite example need; {count[3]} in round 3, those of the UI, the conditional and "
           "later packages, and the differences to report. A question's round is the earliest "
           "round of the packages it blocks. Each question stands alone: its options carry their "
           "trade-offs, the recommendation is the agent's, and nothing is decided. A question of "
           "items (Q26, Q39, Q43, Q44) is answered item by item, an item's options numbered: "
           "\"Q43 (c) 2\". \"D-007\" marks the questions whose answer is, or includes, an approval "
           f"or a ruling under D-007 ({', '.join(d007)}). \"Blocks\" names the work packages of §14 "
           "that wait for the answer. The same text, with each question's one-sentence summary, is "
           "in `design/owner_questions.json`.", ""]
    for r, title in ROUNDS.items():
        rq = [q for q in qs if q["round"] == r]
        out += [f"### {title} ({len(rq)} questions)", "",
                "| # | Question | Agent's recommendation | Blocks | D-007 |",
                "|---|---|---|---|---|"]
        for q in rq:
            out.append(f"| {q['id']} | {cell(q['title'])} | {cell(q['agent_recommendation'])} | "
                       f"{', '.join(q['blocks'])} | {'yes' if q['d007'] else ''} |")
        out.append("")
    for r, title in ROUNDS.items():
        out += [f"### Round {r}, the questions in full", ""]
        for q in qs:
            if q["round"] == r:
                out += question_md(q)
    return out


def p_entries_md(ps, kept):
    moved = [p for p in ps if "moved_to" in p]
    out = ["## 13. Process choices (P-entries, pending the owner's confirmation)", "",
           f"{len(kept)} choices of process and measurement: how something is built, tested or "
           "measured, with no effect on what an output says by default. Each is the agent's, "
           "pending the owner's confirmation, and is numbered from P-082 when recorded in "
           "`docs/DECISIONS.md`. The same text is in `design/p_entries.json`.", "",
           "| | Choice | Alternatives |", "|---|---|---|"]
    for p in kept:
        pid, _, name = p["title"].partition(" ")
        out.append(f"| **{pid}** {cell(name)} | {cell(p['choice'])} | {cell(p['alternatives'])} |")
    out += ["", "Moved to owner questions in revision 1, because each changes what an output says "
            "by default (their letters are not reused):", ""]
    for p in moved:
        pid, _, name = p["title"].partition(" ")
        out.append(f"- **{pid}** {name}: {p['moved_to']}.")
    out.append("")
    return out


def packages_md(wps, qs):
    rnd = {q["id"]: q["round"] for q in qs}
    out = ["## 14. Work packages", "",
           f"{len(wps)} packages. Each is built in a worktree of its own from `vmaf/phase-3` "
           "(P-AH), with its unit tests. New files are the package's own; a shared file belongs "
           "to one package at a time, the later one depending on the earlier, and "
           "`CMakeLists.txt` to the wiring packages alone (P-C). A package's round is the round of "
           "answers it waits for (0: none, so it can start now), and it starts only once every "
           "question of that round is answered. \"Blocked by\" lists the questions whose answers "
           "change what the package builds: its code, its tests or the form of its output. A "
           "question's \"Blocks\" in §12 is the list of packages naming it here. Both are checked "
           "by `design/tools/render.py`, with the order of every shared file's owners. The same "
           "text is in `design/work_packages.json`.", "",
           "| WP | Title | Round | Depends on | Blocked by | Shared files it owns (changed) |",
           "|---|---|---|---|---|---|"]
    for w in wps:
        out.append(f"| {w['id']} | {cell(w['title'])} | {w['round']} | "
                   f"{', '.join(w['depends_on']) or 'none'} | "
                   f"{', '.join(w['blocked_by_questions']) or 'none'} | "
                   f"{cell('; '.join(w['changed_files'])) or 'none'} |")
    out.append("")
    for w in wps:
        out += [f"### {w['id']} {w['title']}", "", w["description"], ""]
        if w["new_files"]:
            out.append("- **New files:** " + ", ".join(f"`{f}`" if " " not in f else f
                                                       for f in w["new_files"]) + ".")
        if w["changed_files"]:
            out.append("- **Changed (owned):** " + "; ".join(f"`{f}`" if " " not in f else f
                                                             for f in w["changed_files"]) + ".")
        out.append(f"- **Round:** {w['round']}.")
        out.append("- **Depends on:** " + (", ".join(w["depends_on"]) or "none") + ".")
        out.append("- **Blocked by:** " + (", ".join(w["blocked_by_questions"]) or "none") + ".")
        out.append("- **Tests:**")
        for t in w["tests"]:
            out.append(f"  - {t}")
        out.append("")

    wid = {w["id"]: w for w in wps}

    def closure(ids):
        seen, todo = set(), list(ids)
        while todo:
            i = todo.pop()
            if i not in seen:
                seen.add(i)
                todo += wid[i]["depends_on"]
        return seen

    acceptance = closure(["WP11a"])
    composite = closure(["WP11b"])
    assert acceptance == {w["id"] for w in wps if w["round"] == 1}, sorted(acceptance)
    assert all(rnd[q] == 1 for i in acceptance for q in wid[i]["blocked_by_questions"])
    extra = sorted(composite - acceptance)
    assert all(wid[i]["round"] == 2 for i in extra), extra
    order = lambda s: sorted(s, key=lambda i: [w["id"] for w in wps].index(i))
    decoded = ["WP05", "WP09c"]
    assert not set(decoded) & composite, decoded
    off = [i for i in order(wid) if i not in acceptance | composite and i not in decoded]
    out += [
        "### Order and the critical path", "",
        "1. Now: WP15a, the pending-decisions document, the UI plan and the P-entries, which wait for no answer.",
        "2. Round 1, once the owner has answered its questions and the lead has recorded the answers (P-AH): WP00a first and alone, since it owns `CMakeLists.txt` and writes the shared declarations.",
        "3. Then in parallel: WP01, WP02, WP03, WP04, WP06 and WP10.",
        "4. WP08 once WP01 and WP03 have merged.",
        "5. WP09a once WP01, WP02, WP03, WP04, WP06 and WP08 have merged.",
        "6. WP11a, §3.2's two acceptance tests, once WP09a and WP10 have merged.",
        "7. Round 2, once its questions are answered: WP00b once WP00a and WP01 have merged (under Q41 (b) the rename first); WP05 once WP04 has; WP07 once WP00b, WP01, WP02, WP03 and WP06 have; WP09b once WP09a and WP07 have; WP11b, the composite example, once WP09b and WP10 have; WP09c once WP09b and WP05 have.",
        "8. Round 3, once its questions are answered: WP00c under Q36, once WP00b has merged; WP12 once WP00c, WP01, WP03, WP07, WP08 and WP09b have; WP13 after WP12; WP17 only after WP13 (both own `src/main.cpp`) and WP07; WP16 whenever the owner's ruling or approved plan allows it, after WP03, WP06 and WP09c; WP14 once WP11a and WP11b have merged; WP15b once Q41 and Q42 are answered; WP15c last, once every package that is built has merged.",
        "",
        "The critical path to the two acceptance tests: " + words(order(acceptance)) +
        ". Each of them depends only on packages of that list, and every question that blocks one "
        "of them is in round 1 (both checked by `design/tools/render.py`); D-069's ladders carry "
        "plain cue tags only (testgen.md §2.3), so the path needs neither the SCTE-35 decoder nor "
        "sessions. The composite example's path, to WP11b, adds " + words(extra) + ", all of round 2; "
        "the SCTE-35 decoder (WP05) and the decoded messages (WP09c) are on neither path (checked), "
        "and nor are " + words(off) + ".",
        "",
        "The path assumes the agent's recommendations for Q01 and Q08, under which the scorer is "
        "unchanged. Under Q08 (b) the analyzer's recording of each frame's segment and key flag, "
        "WP16's part for it, moves into round 1 ahead of WP06, which then reads the map from the "
        "report, and the acceptance tests wait for the owner's ruling under D-007 or approved plan; "
        "under Q01 (C) the same holds for the per-variant events, which WP04 and WP06 would then "
        "read from the report; under Q01 (A) WP08 writes into the ladder report, whose writer and "
        "tests it then owns. The lead would re-plan those packages before round 1 starts.",
        "",
    ]
    count = {}
    for w in wps:
        for q in w["blocked_by_questions"]:
            count.setdefault(q, []).append(w["id"])
    most = sorted((q for q in count if len(count[q]) >= 3), key=lambda q: (-len(count[q]), q))
    free = [w["id"] for w in wps if w["round"] == 0]
    out += [("Packages of round 0, which wait for no answer and can start now: " + ", ".join(free) + ". " if free
             else "Every package waits for at least one answer. ") +
            "The questions that block three packages or more: " +
            "; ".join(f"{q} ({', '.join(count[q])})" for q in most) + ".", ""]
    return out


def risks_md(rs):
    out = ["## 15. Risks", ""]
    for r in rs:
        rid, _, text = r.partition(" ")
        out.append(f"- **{rid}** {text}")
    out.append("")
    return out


def main(argv):
    d, target = argv[1], argv[2]
    qs = load(d, "owner_questions.json")
    wps = load(d, "work_packages.json")
    ps = load(d, "p_entries.json")
    rs = load(d, "risks.json")
    part1 = open(os.path.join(d, "src", "part1.md"), encoding="utf-8").read().rstrip("\n")
    part2 = open(os.path.join(d, "src", "part2.md"), encoding="utf-8").read().rstrip("\n")
    count, kept = check(qs, wps, ps, rs, part1)
    body = "\n".join(questions_md(qs, count) + ["---", ""] + p_entries_md(ps, kept) + ["---", ""] +
                     packages_md(wps, qs) + ["---", ""] + risks_md(rs))
    text = part1 + "\n\n---\n\n" + body + "\n---\n\n" + part2 + "\n"
    with open(target, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"{target}: {len(text.encode('utf-8'))} bytes; {len(qs)} questions "
          f"({count[1]}, {count[2]} and {count[3]} in rounds 1 to 3), {len(wps)} packages, "
          f"{len(kept)} P-entries ({len(ps) - len(kept)} moved), {len(rs)} risks")


if __name__ == "__main__":
    main(sys.argv)
