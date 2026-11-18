"""revise3.py <design dir>: revision 3 of the merged Phase 3 design, by the lead, against the third
review's seven minor issues (review-3/review.json), applied to revision 2's sources. Every edit
asserts that the text it replaces occurs exactly once, so a changed source stops the script instead
of being edited blindly; run on revision 3's own sources it stops at its first edit. Edits the
sources only (design/src/*.md, design/*.json, design/tools/render.py); design.md is then rendered
with design/tools/render.py. design/revision-3.md says what each edit is for. Python 3 standard
library only; for the Phase 3 design, not repository code.

usage: python3 -I -B revise3.py <design dir>
"""
import json
import os
import sys

D = sys.argv[1]


def path(*p):
    return os.path.join(D, *p)


def sub(text, old, new, what):
    n = text.count(old)
    assert n == 1, f"{what}: found {n} times: {old[:80]!r}"
    return text.replace(old, new)


def edit_file(rel, pairs):
    p = path(*rel.split("/"))
    t = open(p, encoding="utf-8").read()
    for what, old, new in pairs:
        t = sub(t, old, new, f"{rel} ({what})")
    open(p, "w", encoding="utf-8").write(t)


SEQ = "review-3/runs/seconds-check-sequence-start.txt"

# ---- part1.md
edit_file("src/part1.md", [
    ("header: three revisions",
     "and is revised twice: against its adversarial review (`review-adversarial/review.json`; "
     "`design/revision-1.md`) and against a second review (`review-2/review.json`; "
     "`design/revision-2.md`), each log saying what changed, issue by issue.",
     "and is revised three times: against its adversarial review (`review-adversarial/review.json`; "
     "`design/revision-1.md`), against a second review (`review-2/review.json`; "
     "`design/revision-2.md`) and, by the lead, against the seven minor issues of a third review "
     "(`review-3/review.json`; `design/revision-3.md`), each log saying what changed, issue by issue."),
    ("conventions: P-082",
     "They await the owner's confirmation and are numbered from P-081 when recorded in "
     "`docs/DECISIONS.md`.",
     "They await the owner's confirmation and are numbered from P-082 when recorded in "
     "`docs/DECISIONS.md`, P-081 being the entry that records how this survey and design were made "
     "and what of their records is committed."),
    ("stands: date", "## Where Phase 3 stands (2026-10-31)", "## Where Phase 3 stands (2026-11-02)"),
    ("stands: done",
     "a second review (`review-2/`) and revision 2 against it (`design/revision-2.md`). The design's "
     "own records",
     "a second review (`review-2/`) and revision 2 against it (`design/revision-2.md`); a third "
     "review (`review-3/`), which found no blocker and no major issue, and revision 3, the lead's, "
     "against its seven minor issues (`design/revision-3.md`). The design's own records"),
    ("stands: not done",
     "- **Not done.** Any Phase 3 code. Nothing in `<repo>` was changed: `vmaf/phase-3` is at "
     "`75cf6e0` and its status is clean (read-only git commands at the start and at the end of this "
     "work).",
     "- **Not done.** Any Phase 3 code. The code is as at `75cf6e0`: the only commit since on "
     "`vmaf/phase-3`, `7c0e8d2`, adds this survey's records under `eval/phase3-survey/` and changes "
     "no other file (`design/runs/repo-state.txt`)."),
    ("summary item 3",
     "and after an EXTINF simulated a frame too long the reverse (`design/runs/seconds-check.txt`).",
     "and after an EXTINF simulated a frame too long the reverse (`design/runs/seconds-check.txt`); "
     "a frame missing at the start of a playlist discontinuity sequence put both a frame early within "
     "its segment, where neither part of §4.2's check sees it, and only the frame index's from the "
     f"next segment on (`{SEQ}`)."),
    ("summary item 9", "24 work packages (§14)", "25 work packages (§14)"),
    ("new in revision 3",
     "every conditional option given a package (§3.6), and the Blocked-by lists completed and checked "
     "by `design/tools/render.py`.",
     "every conditional option given a package (§3.6), and the Blocked-by lists completed and checked "
     "by `design/tools/render.py`.\n"
     "- **New in revision 3**, by the lead, against the third review's seven minor issues: a limit of "
     "§4.2's check stated and tested (a reference frame missing at the start of a playlist "
     "discontinuity sequence); RFC 8216's rule on whole-second EXTINF stated as it is; Q16's count "
     "corrected; the Blocked-by lists completed with the eight answers the review named; WP00b's "
     "session types put in headers it owns in turn; WP00a's list of new files and P-C made to say "
     "what WP00a's description says; WP15b split so that the closing documents come last (WP15c)."),
    ("4.2 RFC bullet, the rule",
     "allowing whole seconds only below compatibility version 3 "
     "([§4.3.2.1](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.1))",
     "requiring whole seconds below compatibility version 3 and allowing them at every version "
     "([§4.3.2.1](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.1))"),
    ("4.2 RFC bullet, the conclusion",
     "and so is a playlist of an older version with whole-second EXTINF.",
     "and so is a playlist with whole-second EXTINF."),
    ("4.2 the check: what a run of (1) alone means",
     "so that a run of (1) without one comes from the playlist's EXTINF disagreeing with the frames' "
     "count.",
     "so that a run of (1) without one comes from the playlist's EXTINF disagreeing with the frames' "
     "count, or from a reference frame missing at the start of a playlist discontinuity sequence: "
     "(2) starts each sequence's grid at its first scored frame and can't see that frame missing, and "
     "within the segment that lost it (c) too starts at its first remaining frame, so neither test "
     "warns there and (1) warns from the next segment on, where the case can't be told from an "
     "EXTINF one frame too long."),
    ("4.2 what the check did: the third review's case",
     "every step there staying within half a frame (same record; "
     "`review-2/runs/rate-change-check.txt`).",
     "every step there staying within half a frame (same record; "
     "`review-2/runs/rate-change-check.txt`). The third review removed the first frame of each of "
     "the ad ladder's three playlist discontinuity sequences in turn (frames 0, 75 and 125, "
     "simulated): (2) warned of nothing, and (1) from frame 24, 99 or 149 to frame 198, at "
     f"−0.040 s, from the next segment on (`{SEQ}`)."),
    ("4.2 recommendation",
     "and is off only where an EXTINF departs from its frames, which RFC 8216 advises against.",
     "and is off only where an EXTINF departs from its frames, which RFC 8216 advises against, and "
     "within a segment that lost its playlist discontinuity sequence's first frame, where (d) is off "
     "as well."),
    ("3.2 table: the status document",
     "| `docs/PHASE3_STATUS.md` | docs | the status, item by item | WP15b |",
     "| `docs/PHASE3_STATUS.md` | docs | the status, item by item: committed with the survey, "
     "updated by the lead as each round is answered, completed last | WP15c |"),
    ("3.2 shared files: the documents",
     "`docs/DECISIONS.md` (WP15a, then WP15b, with the lead's records of each round's answers "
     "between); `docs/AI_CONTRIBUTIONS.md`, `docs/PAPER_METHOD_NOTES.md` and, under Q42 (b), "
     "`docs/VMAF_GAP_REPORT.md` (WP15b).",
     "`docs/DECISIONS.md` (WP15a, then WP15c, with the lead's records of each round's answers "
     "between); `docs/PHASE3_DECISIONS_PENDING.md` (WP15a, then WP15b for the differences' wording, "
     "with the lead's marks of each round's answers between); under Q42 (b), "
     "`docs/VMAF_GAP_REPORT.md` (WP15b); `docs/AI_CONTRIBUTIONS.md`, `docs/PAPER_METHOD_NOTES.md` "
     "and `docs/PHASE3_STATUS.md` (WP15c)."),
    ("9.1 axis test",
     "the PTS check (2) warning for the first two only;",
     "the PTS check (2) warning for the first two only; a reference frame missing at the start of "
     f"each playlist discontinuity sequence, in the shapes of `{SEQ}`, warned by (1) alone, from the "
     "next segment on;"),
    ("11 closing: the PR description",
     "and the description of Phase 3's PR (WP15b)",
     "and the description of Phase 3's PR (WP15c, in the wording WP15b settles)"),
    ("11 item 14: its record",
     "with FFmpeg 8.1.1 (measured);",
     "with FFmpeg 8.1.1 (measured: ui.md §2.4, from `experiments/hls-seek-probe/run-output.txt`);"),
])

# ---- part2.md
edit_file("src/part2.md", [
    ("appendix A: the third review's runs",
     "cited by name: `rate-change-check` (revision 1's check on the ad's frames re-timed at other "
     "rates, simulated).",
     "cited by name: `rate-change-check` (revision 1's check on the ad's frames re-timed at other "
     "rates, simulated). **The third review's runs** (`<out>/review-3/runs/`), cited by name: "
     "`seconds-check-sequence-start` (revision 2's check with the first frame of each of the ad "
     "ladder's playlist discontinuity sequences removed, simulated), `vector-status` (the CRC and "
     "decode status of the collected messages, from the scte35 survey's record)."),
    ("appendix B: SessionPlan.h holds the fetch's types",
     "// src/SessionPlan.h — sessions (WP07; types declared by WP00b).\n"
     "namespace timeline {\n"
     "struct PlanSpan { std::size_t entry = 0; std::int64_t first_seq = 0, last_seq = 0; };\n"
     "struct SessionPlan { std::string name; std::vector<PlanSpan> spans; };\n",
     "// src/SessionPlan.h — sessions and their fetched requests (WP07; types declared by WP00b).\n"
     "namespace timeline {\n"
     "struct PlanSpan { std::size_t entry = 0; std::int64_t first_seq = 0, last_seq = 0; };\n"
     "struct SessionPlan { std::string name; std::vector<PlanSpan> spans; };\n"
     "struct RequestRow { /* one NetworkLogger row: URL, status, status text, times, bytes, error */ };\n"
     "struct Request { std::string file; std::vector<RequestRow> chain; };      // one fetched file (Q44 (d))\n"),
    ("appendix B: TimelineBuild.h without them",
     "struct RequestRow { /* one NetworkLogger row: URL, status, status text, times, bytes, error */ };\n"
     "struct Request { std::string file; std::vector<RequestRow> chain; };      // one fetched file (Q44 (d))\n"
     "struct BuildInputs {",
     "struct BuildInputs {"),
    ("appendix B: BuildInputs' round-2 members",
     "std::vector<Request> requests;     // round 2\n",
     "std::vector<Request> requests;     // round 2, added by WP09b\n"),
])

# ---- render.py
edit_file("tools/render.py", [
    ("P-082", "pending the owner's confirmation, and is numbered from P-081 when recorded in ",
     "pending the owner's confirmation, and is numbered from P-082 when recorded in "),
    ("order item 8",
     "WP14 once WP11a and WP11b have merged; WP15b once Q41 and Q42 are answered, its status document last.\",",
     "WP14 once WP11a and WP11b have merged; WP15b once Q41 and Q42 are answered; WP15c last, once every package that is built has merged.\","),
    ("free packages by round 0",
     "    free = [w[\"id\"] for w in wps if not w[\"blocked_by_questions\"]]\n"
     "    out += [(\"Packages no question blocks, which can start now: \" + \", \".join(free) + \". \" if free\n",
     "    free = [w[\"id\"] for w in wps if w[\"round\"] == 0]\n"
     "    out += [(\"Packages of round 0, which wait for no answer and can start now: \" + \", \".join(free) + \". \" if free\n"),
])


def load(name):
    return json.load(open(path(name), encoding="utf-8"))


def dump(name, data):
    with open(path(name), "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")


# ---- owner_questions.json
qs = load("owner_questions.json")
q = {x["id"]: x for x in qs}
c = q["Q02"]["options"][2]
assert c["label"].startswith("(c)")
c["description"] = sub(
    c["description"],
    "after which (d) was 0.040 s and 0.083 to 1.167 s off (simulated, design/runs/seconds-check.txt).",
    "after which (d) was 0.040 s and 0.083 to 1.167 s off (simulated, design/runs/seconds-check.txt), "
    "except within a segment that lost its playlist discontinuity sequence's first frame, where both "
    f"were a frame early ({SEQ}).", "Q02 (c)")
q["Q02"]["why"] = sub(q["Q02"]["why"],
                      "(c) stays right after a missing frame or a change of rate,",
                      "(c) stays right after a missing frame or a change of rate (from the next "
                      "segment on if the frame missing is a sequence's first),", "Q02 why")
q["Q02"]["why"] = sub(q["Q02"]["why"],
                      "while (c) is off only where an EXTINF departs from its frames;",
                      "while (c) is otherwise off only where an EXTINF departs from its frames;",
                      "Q02 why, otherwise")
q["Q16"]["question"] = sub(
    q["Q16"]["question"],
    "9 have a bad CRC_32, 3 of them a library's intended negative tests (scte35.md §5).",
    "9 have a bad CRC_32, 1 of them a library's intended negative test (scte35.md §5; "
    "scte35_evidence/runs/check_vectors.txt; review-3/runs/vector-status.txt; the survey's summary, "
    "scte35.md §0 item 5, gives 3 in error).", "Q16")

# ---- work_packages.json
wps = load("work_packages.json")
w = {x["id"]: x for x in wps}
qorder = [x["id"] for x in qs]
for pkg, extra in {"WP00a": ["Q43"], "WP00b": ["Q44"], "WP09a": ["Q20"], "WP09b": ["Q01", "Q05"],
                   "WP11a": ["Q23"], "WP11b": ["Q23", "Q25"], "WP14": ["Q23", "Q25"],
                   "WP12": ["Q05"]}.items():
    b = w[pkg]["blocked_by_questions"]
    assert not set(extra) & set(b), (pkg, extra)
    w[pkg]["blocked_by_questions"] = sorted(b + extra, key=qorder.index)

old_new = ("the empty files of round 1's packages listed in design §3.2, and the shared declarations "
           "(contents then owned by WP01 to WP11a)")
assert w["WP00a"]["new_files"] == [old_new], w["WP00a"]["new_files"]
w["WP00a"]["new_files"] = [
    "the empty files of design §3.2 whose existence the owner's decisions or round 1's answers "
    "settle: those of round 1's packages, and of round 2's src/SessionPlan.{h,cpp}, "
    "src/Scte35.{h,cpp}, src/Scte35Events.{h,cpp}, tests/test_session_plan.cpp, tests/test_scte35.cpp, "
    "tests/test_scte35_events.cpp and tests/test_attribution_session.cpp, which D-068 and D-070 "
    "settle; and the shared declarations (contents then owned by WP01 to WP11b)"]

w["WP00b"]["description"] = sub(
    w["WP00b"]["description"],
    "the session declarations, encoding Q11 and Q12, added to the shared headers through the lead "
    "(P-AG): Span, Session, Timeline::sessions, CurveRef's session kind, PlanSpan and Request.",
    "the session declarations, encoding Q11, Q12 and Q44 (d), added through the lead (P-AG) to "
    "headers WP00a created: Span, Session, Timeline::sessions and CurveRef's session kind in "
    "src/TimelineModel.h; PlanSpan, SessionPlan, RequestRow and Request in src/SessionPlan.h, whose "
    "contents WP07 then owns.", "WP00b")
chg = w["WP00b"]["changed_files"]
i = chg.index("src/TimelineModel.h (the session declarations, through the lead)")
chg.insert(i + 1, "src/SessionPlan.h (the session plan's and the fetch's types, through the lead)")

t = w["WP01"]["tests"]
k = [n for n, x in enumerate(t) if x.startswith("The check of design §4.2")]
assert len(k) == 1
t[k[0]] = sub(t[k[0]],
              "the PTS check (2) warning for the first two and not for the third; a reference that "
              "isn't a playlist",
              "the PTS check (2) warning for the first two and not for the third; a reference frame "
              f"missing at the start of each playlist discontinuity sequence, in the shapes of {SEQ}, "
              "warned by (1) alone, from the next segment on; a reference that isn't a playlist",
              "WP01 test")

w["WP15a"]["description"] = sub(w["WP15a"]["description"], "from P-081 on", "from P-082 on", "WP15a")

b15 = w["WP15b"]
assert b15["depends_on"] == ["WP15a"] and b15["blocked_by_questions"] == ["Q41", "Q42"]
b15["title"] = "The differences' wording and the gap report's correction"
b15["description"] = (
    "Once the answers to Q41 and Q42 are in: design §11's differences in the wording those answers "
    "settle, in the list of differences of docs/PHASE3_DECISIONS_PENDING.md, from which WP15c's PR "
    "description takes them; under Q42 (b), a dated correction line in docs/VMAF_GAP_REPORT.md for "
    "its factual error (design §11). Each gives the work order's requirements in its own words, "
    "quoting no passage of it (D-013, D-060), and neither is edited to match the implementation; "
    "differences are listed.")
b15["new_files"] = []
b15["changed_files"] = ["docs/PHASE3_DECISIONS_PENDING.md (after WP15a has merged)",
                        "docs/VMAF_GAP_REPORT.md (only under Q42 (b))"]
b15["tests"] = ["Before each commit: the cross-reference check and the quote check of P-AK, the "
                "checker's output kept as the record."]
c15 = {
    "id": "WP15c",
    "title": "The closing documents and the PR description",
    "round": 3,
    "depends_on": ["WP09c", "WP13", "WP14", "WP15b"],
    "blocked_by_questions": [],
    "new_files": [],
    "changed_files": ["docs/PHASE3_STATUS.md", "docs/DECISIONS.md (after WP15a has merged)",
                      "docs/AI_CONTRIBUTIONS.md", "docs/PAPER_METHOD_NOTES.md"],
    "description": (
        "Last, once every package that is built has merged: those it depends on and, through them, "
        "every other package of design §14 but WP16 and WP17, which the lead adds to its "
        "dependencies when the owner's answers start them. The Phase 3 rows of "
        "docs/AI_CONTRIBUTIONS.md; docs/PAPER_METHOD_NOTES.md once there is something measured to "
        "state; dated lines in docs/DECISIONS.md's pending entries where the implementation found "
        "more (D-060); docs/PHASE3_STATUS.md completed item by item with the output that shows it; "
        "and Phase 3's PR description, with the differences in WP15b's wording and the acceptance "
        "outputs pasted in. Every document and the PR description give the work order's "
        "requirements in their own words, quoting no passage of it (D-013, D-060), and none is "
        "edited to match the implementation; differences are listed."),
    "tests": ["Before each commit, and before the PR description or any other text about Phase 3 "
              "is posted: the cross-reference check and the quote check of P-AK, the checker's "
              "output kept as the record."],
}
wps.insert([x["id"] for x in wps].index("WP15b") + 1, c15)

# each question's Blocks: the packages naming it, in the packages' order
for x in qs:
    x["blocks"] = [p["id"] for p in wps if x["id"] in p["blocked_by_questions"]]

# ---- p_entries.json
ps = load("p_entries.json")
pc = next(p for p in ps if p["title"].startswith("P-C "))
pc["choice"] = sub(pc["choice"],
                   "each adding that round's files as empty compiling files with their lines once the "
                   "answers that decide them are in,",
                   "each adding, as empty compiling files with their lines, the files whose existence "
                   "that round's answers or the owner's decisions settle, once those answers are in,",
                   "P-C")

# ---- risks.json
rs = load("risks.json")
k = [n for n, r in enumerate(rs) if r.startswith("R11. ")]
assert len(k) == 1
rs[k[0]] = sub(rs[k[0]],
               "and how often D-011's references trip the check is MEASUREMENT PENDING.",
               "and how often D-011's references trip the check is MEASUREMENT PENDING. A reference "
               "frame missing at the start of a playlist discontinuity sequence puts both clocks a "
               "frame early within its segment, where neither test of the check warns, and (d) alone "
               "from the next segment on, where (1) warns but can't tell it from an EXTINF a frame "
               f"too long ({SEQ}).", "R11")

dump("owner_questions.json", qs)
dump("work_packages.json", wps)
dump("p_entries.json", ps)
dump("risks.json", rs)
print("revision 3 applied:", len(qs), "questions,", len(wps), "packages,",
      sum(1 for p in ps if "moved_to" not in p), "P-entries kept,", len(rs), "risks")
