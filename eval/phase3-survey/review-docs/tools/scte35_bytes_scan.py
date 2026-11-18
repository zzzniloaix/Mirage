"""scte35_bytes_scan.py: looks for SCTE-35 splice_info_section bytes in files: a hex run that starts
with table_id 0xFC followed by a second byte 0x30 or 0x31 (section_syntax_indicator 0,
private_indicator 0, sap_type 3, the length's top bits 0 or 1), with or without 0x and spaces, and a
base64 run that decodes to bytes starting 0xFC 0x30 or 0xFC 0x31, at least 8 bytes long. Prints
file:line:column, the form (hex or base64) and the run's length in bytes, never the bytes, so that
its output can be kept. Also counts the <vector id> placeholders that replaced
third-party messages.

usage: python3 -I -B scte35_bytes_scan.py <root>...
Python 3 standard library only.
"""
import base64
import binascii
import os
import re
import sys

HEX = re.compile(r"(?i)(?:0x)?fc\s?3[01](?:\s?[0-9a-f]{2}){2,}")
B64 = re.compile(r"(?<![A-Za-z0-9+/])/D[A-H][A-Za-z0-9+/]{6,}={0,2}")
PLACEHOLDER = re.compile(r"<vector [^>]+>")
# looser forms: any table_id 0xFC run of at least 20 bytes (the shortest splice_info_section,
# a splice_null with no descriptor), whatever its second byte
HEX_LOOSE = re.compile(r"(?i)(?<![0-9a-f])(?:0x)?fc[0-9a-f]{38,}")
B64_LOOSE = re.compile(r"(?<![A-Za-z0-9+/])/[A-P][A-Za-z0-9+/]{24,}={0,2}")


def files(roots):
    for root in roots:
        if os.path.isfile(root):
            yield root
            continue
        for d, subdirs, names in os.walk(root):
            subdirs.sort()
            for n in sorted(names):
                yield os.path.join(d, n)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    n_hex = n_b64 = n_ph = n_files = n_loose = 0
    for path in files(argv[1:]):
        n_files += 1
        text = open(path, "rb").read().decode("utf-8", errors="replace")
        for i, line in enumerate(text.split("\n"), 1):
            n_ph += len(PLACEHOLDER.findall(line))
            for m in HEX_LOOSE.finditer(line):
                print(f"{path}:{i}:{m.start() + 1}: loose hex form, {len(m.group(0)) // 2} bytes")
                n_loose += 1
            for m in B64_LOOSE.finditer(line):
                t = m.group(0).rstrip("=")
                t = t[: len(t) - len(t) % 4] or t
                try:
                    b = base64.b64decode(t + "=" * (-len(t) % 4))
                except (binascii.Error, ValueError):
                    continue
                if len(b) >= 20 and b[0] == 0xFC:
                    print(f"{path}:{i}:{m.start() + 1}: loose base64 form, {len(b)} bytes")
                    n_loose += 1
            for m in HEX.finditer(line):
                digits = re.sub(r"(?i)0x|\s", "", m.group(0))
                print(f"{path}:{i}:{m.start() + 1}: hex, {len(digits) // 2} bytes")
                n_hex += 1
            for m in B64.finditer(line):
                s = m.group(0)
                s = s[: len(s) - len(s) % 4] if not s.endswith("=") else s
                try:
                    b = base64.b64decode(s + "=" * (-len(s) % 4), validate=False)
                except (binascii.Error, ValueError):
                    continue
                if len(b) >= 8 and b[0] == 0xFC and b[1] in (0x30, 0x31):
                    print(f"{path}:{i}:{m.start() + 1}: base64, {len(b)} bytes")
                    n_b64 += 1
    print(f"checked: {n_files} files; hex runs: {n_hex}; base64 runs: {n_b64}; "
          f"looser forms: {n_loose}; <vector id> placeholders: {n_ph}")
    return 1 if n_hex or n_b64 or n_loose else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
