"""pending_vs_json.py: checks the pending document against the design's owner questions.

1. Every question of design/owner_questions.json is in the document once, under the heading of its
   round, with its D-007 mark, and in its round's table.
2. Each field the document renders from a question (title, summary, question, each option's label and
   description, each item's options, recommendation and why, the agent's recommendation and why) is
   found in the document as the render script's rewriter gives it, and every change the rewriter
   makes is listed as a pair (design's words -> document's words), counted, so that each kind of
   change can be read for whether it changes substance.
3. The text the document adds to a question beyond the design (corrections, notes, extra Blocks) is
   listed by question.

usage: python3 -I -B pending_vs_json.py <render_pending.py> <records> <pending document>
Python 3 standard library only.
"""
import collections
import difflib
import json
import os
import re
import sys


def load_render(path):
    ns = {"__name__": "render_pending"}
    exec(compile(open(path, encoding="utf-8").read(), path, "exec"), ns)
    return ns


def words(s):
    return re.findall(r"\S+", s)


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    rp, root, doc_path = argv[1:]
    ns = load_render(rp)
    rw = ns["rewriter"](root)
    qs = json.load(open(os.path.join(root, "design", "owner_questions.json"), encoding="utf-8"))
    doc = open(doc_path, encoding="utf-8").read()
    problems = 0
    # 1. presence, round, D-007 mark
    ids = [q["id"] for q in qs]
    print(f"questions in the JSON: {len(qs)}; ids unique: {len(set(ids)) == len(ids)}; "
          f"ids Q01..Q{len(qs):02d}: {sorted(ids) == [f'Q{i:02d}' for i in range(1, len(qs) + 1)]}")
    heads = [m.start() for m in re.finditer(r"^### Round (\d)", doc, re.M)]
    rounds_at = [(m.start(), int(m.group(1))) for m in re.finditer(r"^### Round (\d)", doc, re.M)]
    diff_head = doc.index("## Differences from the work order, to report")
    by_round = collections.Counter()
    for q in qs:
        h = f"#### {q['id']}. {q['title']}"
        n = doc.count(h)
        if n != 1:
            print(f"PROBLEM {q['id']}: heading found {n} times")
            problems += 1
            continue
        pos = doc.index(h)
        r = max((x for x in rounds_at if x[0] < pos), default=(0, 0))[1]
        if r != q["round"] or pos > diff_head:
            print(f"PROBLEM {q['id']}: under round {r}, the JSON says {q['round']}")
            problems += 1
        by_round[r] += 1
        block = doc[pos:doc.find("\n#### ", pos + 1) if doc.find("\n#### ", pos + 1) != -1 else diff_head]
        mark = "**D-007**" in block.split("\n")[2] if len(block.split("\n")) > 2 else False
        if mark != bool(q["d007"]):
            print(f"PROBLEM {q['id']}: D-007 mark {mark}, the JSON says {q['d007']}")
            problems += 1
        row = re.search(rf"^\| {q['id']} \|.*\| (yes|) \|$", doc, re.M)
        if not row:
            print(f"PROBLEM {q['id']}: no table row")
            problems += 1
    print(f"questions by round in the document: {dict(sorted(by_round.items()))}")
    # 2. fields, and the rewriter's changes
    changes = collections.Counter()
    examples = {}
    fields = 0
    for q in qs:
        pos = doc.index(f"#### {q['id']}. ")
        end = doc.find("\n#### ", pos + 1)
        end = diff_head if end == -1 or end > diff_head else end
        block = doc[pos:end]
        texts = [("title", q["title"]), ("summary", q["summary"]),
                 ("question", ns["corrected"](q, "question", q["question"])),
                 ("agent_recommendation", q["agent_recommendation"]), ("why", q["why"])]
        for o in q.get("options", []):
            texts += [(f"option {o['label'][:30]} label", o["label"]),
                      (f"option {o['label'][:30]} description", o["description"])]
        for it in q.get("items", []):
            texts += [(f"item {it['id']} title", it["title"]),
                      (f"item {it['id']} recommendation", it["agent_recommendation"]),
                      (f"item {it['id']} why", it["why"])]
            for o in it["options"]:
                texts += [(f"item {it['id']} option {o['label'][:20]} label", o["label"]),
                          (f"item {it['id']} option {o['label'][:20]} description", o["description"])]
        for name, t in texts:
            fields += 1
            out = rw(t)
            if out not in block:
                print(f"PROBLEM {q['id']} {name}: the rewritten text is not in the question's block")
                problems += 1
            a, b = words(t), words(out)
            sm = difflib.SequenceMatcher(a=a, b=b, autojunk=False)
            for tag, i1, i2, j1, j2 in sm.get_opcodes():
                if tag == "equal":
                    continue
                old, new = " ".join(a[i1:i2]), " ".join(b[j1:j2])
                key = (re.sub(r"\d+", "N", old), re.sub(r"\d+", "N", new))
                changes[key] += 1
                examples.setdefault(key, f"{q['id']} {name}: {old!r} -> {new!r}")
    print(f"fields checked: {fields}")
    print(f"kinds of rewriter change (digits as N): {len(changes)}")
    for key, n in sorted(changes.items(), key=lambda kv: -kv[1]):
        print(f"  {n:4d}  {examples[key][:300]}")
    # 3. additions
    print("additions beyond the design:")
    for (qid, field), (old, new, note) in ns["CORRECTIONS"].items():
        print(f"  {qid} {field}: correction replaces {len(words(old))} words with {len(words(new))}, plus a note")
    for qid in ns["NOTES"]:
        print(f"  {qid}: note")
    for qid, (extra, _) in ns["EXTRA_BLOCKS"].items():
        print(f"  {qid}: Blocks adds {', '.join(extra)}")
    print(f"problems: {problems}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
