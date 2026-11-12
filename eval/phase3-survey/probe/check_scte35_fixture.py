"""Checks that dis/tagged.m3u8 of the fixture carries RFC 8216 section 8.10's
example SCTE-35 bytes verbatim, and that its #EXT-OATCLS-SCTE35 line is the
base64 of the SCTE35-OUT bytes.

    python3 -I check_scte35_fixture.py <rfc8216.txt> <dis/tagged.m3u8>
"""
import base64
import re
import sys

rfc = open(sys.argv[1], encoding="ascii").read().split("\n")
i = next(k for k, l in enumerate(rfc) if 'splice-6FFFFFF0",START-DATE' in l)
out = re.search(r"SCTE35-OUT=(0x[0-9A-F]+)", "".join(l.strip() for l in rfc[i:i + 4])).group(1)
j = next(k for k, l in enumerate(rfc) if "DURATION=59.993,SCTE35-IN=" in l)
inn = re.search(r"SCTE35-IN=(0x[0-9A-F]+)", "".join(l.strip() for l in rfc[j:j + 3])).group(1)
text = open(sys.argv[2], encoding="ascii").read()
print("RFC 8216 section 8.10 example tags at text lines", i + 1, "and", j + 1)
print("SCTE35-OUT in the fixture is the RFC's:", ("SCTE35-OUT=" + out + "\n") in text, len(out) - 2, "hex digits")
print("SCTE35-IN in the fixture is the RFC's:", ("SCTE35-IN=" + inn + "\n") in text, len(inn) - 2, "hex digits")
b64 = re.search(r"#EXT-OATCLS-SCTE35:(\S+)", text).group(1)
print("#EXT-OATCLS-SCTE35 decodes to the SCTE35-OUT bytes:", base64.b64decode(b64).hex().upper() == out[2:])
