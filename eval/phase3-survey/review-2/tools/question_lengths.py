"""question_lengths.py: counts the sentences of each owner question of the merged Phase 3 design
(design/owner_questions.json) against the lead's item L4: a question's text two to four sentences,
each option (its description, after its label) one to three, and the summary one. A sentence ends
at '.', '?' or '!' followed by white space and a capital letter, a digit, a backtick, a quote, an
opening parenthesis or '§'; 'e.g.', 'i.e.', 'cf.', 'vs.' and 'No.' never end one. Prints every
count outside its range, then the totals. Reads the file named, writes nothing. Python 3 standard
library only; for the review of the design (review-2), not repository code.

usage: python3 -I -B question_lengths.py <owner_questions.json>
"""
import json
import re
import sys

END = re.compile(r"(?<=[.?!])\s+(?=[A-Z0-9`\"'(§*])")
ABBREV = ("e.g.", "i.e.", "cf.", "vs.", "No.")


def sentences(text):
    parts = END.split(text.strip())
    out = []
    for p in parts:
        if out and out[-1].endswith(ABBREV):
            out[-1] += " " + p
        else:
            out.append(p)
    return [p for p in out if p.strip()]


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    qs = json.load(open(argv[1], encoding="utf-8"))
    bad = 0
    total_opts = 0
    for q in qs:
        n = len(sentences(q["question"]))
        if not 2 <= n <= 4:
            bad += 1
            print(f"{q['id']}: question {n} sentences")
        s = len(sentences(q["summary"]))
        if s != 1:
            bad += 1
            print(f"{q['id']}: summary {s} sentences")
        opts = [(o["label"], o["description"]) for o in q.get("options", [])]
        for it in q.get("items") or []:
            opts += [(f"({it['id']}) {o['label']}", o["description"]) for o in it["options"]]
        for label, desc in opts:
            total_opts += 1
            m = len(sentences(desc))
            if not 1 <= m <= 3:
                bad += 1
                print(f"{q['id']} {label}: option {m} sentences")
    print(f"{len(qs)} questions, {total_opts} options; {bad} counts outside L4's ranges")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
