"""render_pending.py: writes docs/PHASE3_DECISIONS_PENDING.md from the Phase 3 design's data, as
the repository keeps it under eval/phase3-survey/: the owner questions
(design/owner_questions.json), grouped by round; the differences from the work order to report
(design.md, section 11); the agent's proposed process choices (design/p_entries.json); and the work
packages the questions block (design/work_packages.json). The design's paths are written as the
repository has them: a path of the survey directory as eval/phase3-survey/<path>, a bare survey name
such as 'sources.md §7' as 'eval/phase3-survey/sources.md §7', a run of either design as its record's
file. Nothing else of the design's text is changed: the third review's corrections are in the
design itself, since its revision 3 (eval/phase3-survey/design/revision-3.md).

The document it writes is the first version; the lead marks the owner's answers in it as each
round's come in (the design's P-AH), so it is not rendered again over them.

Reads only; writes the one output file. Python 3 standard library only.

usage: python3 -I -B render_pending.py <survey copy, e.g. eval/phase3-survey> <output file>
"""

import json
import os
import re
import sys

P = "eval/phase3-survey/"

ROUNDS = {
    1: "Round 1: the path to §3.2's two acceptance tests",
    2: "Round 2: sessions, the session fetch, decoded SCTE-35 messages and the composite example",
    3: "Round 3: the UI, the conditional and later packages, and the differences to report",
}

def runs_of(root, sub):
    return sorted(f[:-4] for f in os.listdir(os.path.join(root, sub, "runs")) if f.endswith(".txt"))


def code_span(s, pos):
    """Whether pos lies inside a `code span` of s."""
    return s.count("`", 0, pos) % 2 == 1


def rewriter(root):
    de = {n[:2]: n for n in runs_of(root, "design-evaluation")}
    dl = set(runs_of(root, "design-library"))

    def tick(s, m, text):
        return text if code_span(s, m.start()) else f"`{text}`"

    def de_runs(s):
        def sub(m):
            nums = re.findall(r"\d\d", m.group(0))
            for n in nums:
                assert n in de, f"design-evaluation has no run {n}"
            first = tick(s, m, f"{P}design-evaluation/runs/{de[nums[0]]}.txt")
            rest = [f"`{de[n]}.txt`" for n in nums[1:]]
            return first + "".join(f" and {r}" for r in rest)
        return re.sub(r"design-evaluation runs? \d\d(?: and \d\d)*", sub, s)

    def dl_runs(s):
        def sub(m):
            names = re.split(r", | and ", m.group(1))
            for n in names:
                assert n in dl or (n.endswith("*") and any(x.startswith(n[:-1]) for x in dl)), \
                    f"design-library has no run {n}"
            first = tick(s, m, f"{P}design-library/runs/{names[0]}.txt")
            return first + "".join(f", `{n}.txt`" for n in names[1:])
        return re.sub(r"design-library runs? ([a-z0-9*-]+(?:(?:, | and )(?:axis|map)[a-z0-9*-]*)*)",
                      sub, s)

    def rewrite(s):
        s = s.replace("<out>/", P)
        s = s.replace("this survey and design", "the survey and the design")
        s = re.sub(r"\b[Tt]his design\b", lambda m: "the design" if m.group(0)[0] == "t" else "The design", s)
        s = re.sub(r"\bRun (\d\d)'s prototype", r"The prototype of design-evaluation run \1", s)
        s = de_runs(s)
        s = dl_runs(s)
        s = re.sub(r"design-(library|evaluation) (P-[A-Z]+(?:, P-[A-Z]+)*)",
                   lambda m: f"`{P}design-{m.group(1)}.md`, {m.group(2)}", s)
        s = re.sub(r"\b[Dd]esign §", lambda m: f"`{P}design.md` §", s)
        s = re.sub(r"(?<![\w/.`<-])(sources|scte35|testgen|schema|ui|design-library|design-evaluation|design)\.md\b",
                   lambda m: m.group(0) if code_span(s, m.start()) else f"`{P}{m.group(0)}`", s)
        dirs = r"(?:design|review-adversarial|review-2|review-3|testgen|scte35_evidence|experiments|schema-survey|probe|design-library|design-evaluation|results)"

        def dsub(m):
            if code_span(s, m.start()):
                return P + m.group(0)
            return f"`{P}{m.group(0)}`"
        s = re.sub(rf"(?<![\w/.`<-]){dirs}/[A-Za-z0-9_./*-]*[A-Za-z0-9_*/]"
                   r"(?::\d+(?:-\d+)?(?:, \d+(?:-\d+)?)*)?", dsub, s)
        # the repository's own paths, as its documents write them: in backticks
        s = re.sub(r"(?<![\w/.`<-])(?:(?:src|tests|tools|docs|eval)/[A-Za-z0-9_./{},*-]*[A-Za-z0-9_}*/]|CMakeLists\.txt)"
                   r"(?::\d+(?:-\d+)?(?:, \d+(?:-\d+)?)*)?",
                   lambda m: m.group(0) if code_span(s, m.start()) else f"`{m.group(0)}`", s)
        s = s.replace("``", "`")
        return s

    return rewrite


def cell(s):
    return s.replace("|", "\\|")


def words(xs):
    xs = list(xs)
    return xs[0] if len(xs) == 1 else ", ".join(xs[:-1]) + " and " + xs[-1]


def question_md(q, rw):
    out = [f"#### {q['id']}. {q['title']}", ""]
    mark = (" **D-007**: the answer is, or includes, an approval or a ruling under D-007."
            if q["d007"] else "")
    out += [f"*Round {q['round']}.{mark}*", "",
            f"**Summary.** {rw(q['summary'])}", "",
            f"**Question.** {rw(q['question'])}", ""]
    if q.get("items"):
        out += ["**Items**, each answered on its own:", ""]
        for it in q["items"]:
            out.append(f"- **({it['id']}) {rw(it['title'])}.**")
            for o in it["options"]:
                out.append(f"  - **{rw(o['label'])}.** {rw(o['description'])}")
            out.append(f"  - *Agent's recommendation:* {rw(it['agent_recommendation'])}. "
                       f"*Why:* {rw(it['why'])}")
    else:
        out += ["**Options.**", ""]
        for o in q["options"]:
            out.append(f"- **{rw(o['label'])}.** {rw(o['description'])}")
    out += ["", f"**Agent's recommendation.** {rw(q['agent_recommendation'])}.", "",
            f"**Why.** {rw(q['why'])}", ""]
    out += [f"**Blocks.** {', '.join(q['blocks'])}.", ""]
    return out


def table_blocks(q):
    return ", ".join(q["blocks"])


def section_11(design_md, rw):
    """The items of design.md's section 11 and its closing paragraph, paths rewritten."""
    start = design_md.index("## 11. Differences from the work order, to report")
    end = design_md.index("\n---\n", start)
    paras = design_md[start:end].strip().split("\n\n")[1:]
    assert paras[0].startswith("None of these is fixed"), "section 11's opening changed"
    items = paras[1].split("\n")
    assert all(re.match(r"\d+\. \*\*", x) for x in items), "section 11's items changed"
    return [rw(x) for x in items], [rw(x) for x in paras[2:]]


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    root, target = argv[1], argv[2]
    rw = rewriter(root)
    load = lambda name: json.load(open(os.path.join(root, "design", name), encoding="utf-8"))
    qs, ps, wps = load("owner_questions.json"), load("p_entries.json"), load("work_packages.json")
    design_md = open(os.path.join(root, "design.md"), encoding="utf-8").read()
    count = {r: sum(1 for q in qs if q["round"] == r) for r in ROUNDS}
    d007 = [q["id"] for q in qs if q["d007"]]
    items = [q["id"] for q in qs if q.get("items")]
    kept = [p for p in ps if "moved_to" not in p]
    moved = [p for p in ps if "moved_to" in p]
    diffs, after = section_11(design_md, rw)

    out = [
        "# Phase 3: decisions pending for the owner",
        "",
        "What is open for the owner on `vmaf/phase-3`, §3 of the VMAF work order (the unified event "
        "timeline), after the owner's decisions of 2026-10-30 that started it (`docs/DECISIONS.md` "
        "D-066 to D-070). Nothing of Phase 3 is implemented. The survey and the design behind these "
        "questions are summarized in `docs/PHASE3_SURVEY.md`, and their records, the design "
        f"(`{P}design.md`) and its three reviews among them, are in `{P}`. The decisions are the "
        "owner's (hard rule 3 of the work order); every recommendation below is labeled as the "
        "agent's, and none is a decision. Every number comes from a committed record or a cited "
        "source, named next to it; where there is none, it says MEASUREMENT PENDING. The state of "
        "each item of §3 is in `docs/PHASE3_STATUS.md`.",
        "",
        "## Open",
        "",
        f"{len(qs)} questions, Q01 to Q{len(qs):02d}, in three rounds: {count[1]} in round 1, the "
        "answers the work packages on the path to §3.2's two acceptance tests need before they "
        f"start; {count[2]} in round 2, those of sessions, the session fetch, the decoded SCTE-35 "
        f"messages and the composite example; {count[3]} in round 3, those of the UI, the "
        "conditional and later packages, and the differences to report. A question's round is the "
        "earliest round of the packages it blocks, and a round's packages start once all of its "
        f"questions are answered (`{P}design.md` §12, §14). {words(items)} are answered item by "
        "item, an item's options numbered: \"Q43 (c) 2\". The questions marked D-007 "
        f"({words(d007)}) have an answer that is, or includes, an approval or a ruling under D-007. "
        "\"Blocks\" names the work packages that wait for the answer; the packages are listed at "
        "the end. After the questions come the differences from the work order to report, and the "
        "agent's proposed process choices, to be recorded from P-082 on.",
        "",
        f"The questions are the design's (`{P}design/owner_questions.json`), rendered by "
        f"`{P}tools/render_pending.py` with the design's paths written as the repository has them "
        "and nothing else changed. They are as the design's revision 3 left them, after the third "
        f"review's seven minor issues (`{P}review-3/review.md`; `{P}design/revision-3.md`). The "
        "owner's answers are marked here as each round's come in.",
        "",
    ]
    for r, title in ROUNDS.items():
        rq = [q for q in qs if q["round"] == r]
        out += [f"### {title} ({len(rq)} questions)", "",
                "| # | Question | Agent's recommendation | Blocks | D-007 |",
                "|---|---|---|---|---|"]
        for q in rq:
            out.append(f"| {q['id']} | {cell(rw(q['title']))} | {cell(rw(q['agent_recommendation']))} "
                       f"| {table_blocks(q)} | {'yes' if q['d007'] else ''} |")
        out.append("")
        for q in rq:
            out += question_md(q, rw)
    out += [
        "## Differences from the work order, to report",
        "",
        f"The design lists {len(diffs)} differences between what Phase 3 would build and what the "
        f"work order's §3 asks (`{P}design.md` §11). None of them is removed by editing a document, "
        "as the work order's constraints require: each is reported here and in the description of "
        "Phase 3's PR (WP15c, in the wording WP15b settles), and both pass the quote check before "
        "they are committed or posted "
        "(P-AK). Each names the question that settles it; \"Q42\" marks those that no option of "
        "Phase 3 removes, which Q42 asks the owner how to handle.",
        "",
    ]
    out += diffs + [""] + after + [""]
    out += [
        "## The agent's proposed process choices, to be recorded from P-082 on",
        "",
        f"{len(kept)} choices of process and measurement: how something is built, tested or "
        "measured, with no effect on what an output says by default "
        f"(`{P}design/p_entries.json`; `{P}design.md` §13). They are the agent's, pending the "
        "owner's confirmation. Each is recorded in `docs/DECISIONS.md`, from P-082 on, when the "
        "first package that uses it starts, marked pending until the owner confirms it, and later "
        "lines are only added (D-060; P-081 says why not all at once); the design's letters are "
        "its own.",
        "",
        "| | Choice | Alternatives |",
        "|---|---|---|",
    ]
    for p in kept:
        pid, _, name = p["title"].partition(" ")
        out.append(f"| **{pid}** {cell(rw(name))} | {cell(rw(p['choice']))} | {cell(rw(p['alternatives']))} |")
    out += ["", f"{len(moved)} more were moved into questions by the design's first revision, because "
            "each changes what an output says by default:", ""]
    for p in moved:
        pid, _, name = p["title"].partition(" ")
        out.append(f"- **{pid}** {rw(name)}: {rw(p['moved_to'])}.")
    out += [
        "",
        "## The work packages the questions block",
        "",
        f"{len(wps)} packages (`{P}design/work_packages.json`; `{P}design.md` §14). A package's "
        "round is the round of answers it waits for, 0 meaning none; \"Blocked by\" lists the "
        "questions whose answers change what it builds, as the design gives them.",
        "",
        "| WP | Title | Round | Depends on | Blocked by |",
        "|---|---|---|---|---|",
    ]
    for w in wps:
        out.append(f"| {w['id']} | {cell(rw(w['title']))} | {w['round']} | "
                   f"{', '.join(w['depends_on']) or 'none'} | "
                   f"{', '.join(w['blocked_by_questions']) or 'none'} |")
    out.append("")
    text = "\n".join(out)
    with open(target, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"{target}: {len(text.encode('utf-8'))} bytes; {len(qs)} questions ({count[1]}, "
          f"{count[2]} and {count[3]}), {len(diffs)} differences, {len(kept)} process choices "
          f"({len(moved)} moved), {len(wps)} packages")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
