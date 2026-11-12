"""ctest_registration.py: how the debug build registers mirage_tests' Catch2 test cases with
ctest, read from the file catch_discover_tests generated (CMakeLists.txt:371). Prints the number
of add_test() calls, whether each one runs mirage_tests with a single test-case name (one process
per test case), and the test cases whose names say they use a ladder fixture. Read-only. Python 3
standard library only; for the adversarial review of the Phase 3 design, not repository code.

usage: python3 -I -B ctest_registration.py <build dir>
"""

import glob
import os
import re
import sys


def main(argv):
    build = argv[1]
    files = sorted(glob.glob(os.path.join(build, "mirage_tests-*_tests.cmake")))
    print("generated files:", [os.path.basename(f) for f in files])
    for f in files:
        text = open(f, encoding="utf-8").read()
        adds = re.findall(r"add_test\(\s*\[==\[(.*?)\]==\]\s+(\S+)\s+\[==\[(.*?)\]==\]", text)
        print(f"{os.path.basename(f)}: {len(adds)} add_test() calls")
        # Catch2's test spec escapes ',' and other special characters with '\'.
        unescape = lambda s: re.sub(r"\\(.)", r"\1", s)
        same = sum(1 for name, prog, arg in adds
                   if unescape(arg) == name and prog.endswith("/mirage_tests"))
        print(f"  each runs the program mirage_tests with its own test-case name (Catch2's "
              f"escapes removed) as the only filter: {same} of {len(adds)}")
        ladder = [name for name, _, _ in adds if "ladder" in name.lower()]
        print(f"  test cases with 'ladder' in their name: {len(ladder)}; the first 5:")
        for n in ladder[:5]:
            print("   ", n)


if __name__ == "__main__":
    main(sys.argv)
