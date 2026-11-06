"""vector_status.py: counts the statuses in the scte35 survey's record of its vector checks
(scte35_evidence/runs/check_vectors.txt): for the vectors whose CRC_32 is bad and for those that
don't decode, how many of each the vector's own source expects to fail (an intended negative
test, which the record marks "source expects"). Prints each such vector's line number and name,
and its status, never its bytes, its CRC field or its decoded values, so that the output can be
kept in a public record (the messages themselves are the owner's question Q21). Reads the file
named, writes nothing. Python 3 standard library only; for the third review of the merged Phase 3
design (review-3), not repository code.

usage: python3 -I -B vector_status.py <check_vectors.txt>
"""
import sys


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    counts = {"crc bad": [0, 0], "not decoded": [0, 0]}
    for n, line in enumerate(open(argv[1], encoding="utf-8", errors="replace"), 1):
        fields = [f.strip() for f in line.split("|")]
        if len(fields) < 2:
            continue
        if fields[1].startswith("NOT DECODED"):
            kind = "not decoded"
        elif len(fields) > 2 and fields[2].startswith("CRC BAD"):
            kind = "crc bad"
        else:
            continue
        negative = "source expects" in line
        counts[kind][0] += 1
        counts[kind][1] += negative
        print(f"line {n}: {fields[0]}: {kind}" + ("; its source expects it to fail" if negative else ""))
    for kind, (total, negative) in counts.items():
        print(f"{kind}: {total} vectors, {negative} of them intended negative tests of their source")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
