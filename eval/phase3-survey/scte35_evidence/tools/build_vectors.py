#!/usr/bin/env python3
"""Builds vectors_src.json: SCTE-35 test vectors copied from the fetched
sources, with where each was found and what its source says it decodes to.

Every vector string is cut out of a fetched file by a pattern, never typed
in: the only edits are the joining of lines a document wrapped (its line
breaks, and the "\\" continuation marks of the SCTE PDF and of
draft-pantos-hls-rfc8216bis), which the record of each vector states. The
"expected" fields are the agent's transcription of what a source states
(copied from the printed listing, a Go literal or a JSON block), for
tools/check_vectors.py to compare with; "source_decoded" keeps the
source's own text.

usage (cwd <out>/scte35_evidence): python3 -I tools/build_vectors.py . vectors_src.json
"""
import json
import os
import re
import sys

EV = sys.argv[1]
OUT = sys.argv[2]


def path(*p):
    return os.path.join(EV, *p)


def read(*p):
    with open(path(*p), encoding="utf-8", errors="replace") as fh:
        return fh.read()


def fetch_info(name):
    """URL, date and SHA-256 of downloads/<name>/body from runs/fetch-<name>.txt."""
    log = read("runs", f"fetch-{name}.txt")
    url = re.search(r"^url_effective=(.*)$", log, re.M).group(1)
    date = re.search(r"^date_utc: (.*)$", log, re.M).group(1)
    sha = re.search(r"^sha256=(\w+)$", log, re.M).group(1)
    code = re.search(r"^http_code=(\d+)$", log, re.M).group(1)
    return {"fetched_url": url, "retrieved_utc": date, "http_code": int(code),
            "fetched_file": f"scte35_evidence/downloads/{name}/body", "fetched_sha256": sha}


def line_of(text, needle, start=0):
    i = text.index(needle, start)
    return text.count("\n", 0, i) + 1


SOURCES = {}
VECTORS = []
TAGS = []
SKIPPED = []


def source(sid, **kw):
    SOURCES[sid] = kw


def vector(**kw):
    VECTORS.append(kw)


def tag(**kw):
    TAGS.append(kw)


# ── 1. ANSI/SCTE 35 2023r1, §14 sample messages ───────────────────────────────

pdf = fetch_info("scte35-go-docs_SCTE_35_2023r1.pdf")
source("scte35-2023r1",
       title="ANSI/SCTE 35 2023r1 Digital Program Insertion Cueing Message, §14 Sample SCTE 35 Messages (Informative)",
       publisher_url="https://www.scte.org/documents/6710/SCTE_35_2023r1.pdf",
       note=("scte.org returned HTTP 403 to this agent (runs/fetch-scte35-2023r1-pdf.txt, runs/webfetch-attempts.txt); "
             "the copy read is the one committed in Comcast/scte35-go at docs/SCTE_35_2023r1.pdf. Text extracted by "
             "tools/pdftext.py (runs/derive_all.txt); page numbers are the PDF's."),
       license="© 2023 Society of Cable Telecommunications Engineers, Inc., All Rights Reserved (PDF page 2). No reuse licence is stated.",
       **pdf)

txt = read("derived", "SCTE_35_2023r1.txt")
pages = [(m.start(), int(m.group(1))) for m in re.finditer(r"^=== page (\d+) ===$", txt, re.M)]


def page_at(pos):
    p = 0
    for s, n in pages:
        if s <= pos:
            p = n
    return p


HEX_CONT = re.compile(r"^[0-9A-Fa-f\\ ]+$")
B64_CONT = re.compile(r"^[A-Za-z0-9+/= ]+$")


def take(lines, i, first, cont):
    """The string that starts after `first` on lines[i], with the lines
    after it that continue it (only its characters, a '\\' or spaces)."""
    parts = [lines[i].split(first, 1)[1]]
    printed = [lines[i].strip()]
    j = i + 1
    while j < len(lines) and lines[j].strip() and cont.match(lines[j].strip()):
        parts.append(lines[j])
        printed.append(lines[j].strip())
        j += 1
    value = re.sub(r"[\s\\]", "", "".join(parts))
    return value, printed, j


samples = list(re.finditer(r"^(14\.[1-8])\.\s+(.+?)\s*$", txt, re.M))
samples = [m for m in samples if m.start() > re.search(r"^14\.\s+Sample SCTE 35 Messages \(Informative\)\s*$", txt, re.M).start()]
for k, m in enumerate(samples):
    sec, title = m.group(1), re.sub(r"\s+", " ", m.group(2))
    end = samples[k + 1].start() if k + 1 < len(samples) else len(txt)
    block = txt[m.start():end]
    lines = block.split("\n")
    hx = next(i for i, l in enumerate(lines) if l.startswith("Hex="))
    hex_value, hex_printed, _ = take(lines, hx, "Hex=", HEX_CONT)
    bx = next(i for i, l in enumerate(lines) if l.startswith("Base64="))
    b64_value, b64_printed, after = take(lines, bx, "Base64=", B64_CONT)
    listing = []
    for l in lines[after:]:
        s = re.sub(r"\s+", " ", l).strip()
        if not s or s.startswith("=== page") or s == "SCTE 35 2023r1":
            continue
        listing.append(s)
    vector(id=f"scte35-2023r1-{sec}", source="scte35-2023r1",
           locator=f"§{sec} {title}, PDF page {page_at(m.start())}",
           encoding="base64", value=b64_value,
           also_published_as=[{"encoding": "hex", "value": hex_value,
                               "printed_lines": hex_printed}],
           printed_lines=b64_printed,
           source_note=("value and also_published_as: the strings after 'Base64=' and 'Hex=' as printed, joined over the "
                        "PDF's line breaks, with the spaces of the text extraction and the PDF's '\\' continuation marks removed"),
           source_decoded={"format": "the decoder listing printed in the standard, lines as extracted (spaces normalized)",
                           "lines": listing},
           expected={"from": "scte35-2023r1 listing"})

# ── 2. Comcast/scte35-go, pkg/scte35/scte35_test.go ───────────────────────────

go = fetch_info("scte35-go-pkg_scte35_scte35_test.go")
source("scte35-go",
       title="Comcast/scte35-go, pkg/scte35/scte35_test.go (commonDecodeTestcases)",
       repository="https://github.com/Comcast/scte35-go",
       commit="06d0ba5099d939c89a276629701b42221d3ecf30",
       license="Apache-2.0 (LICENSE; NOTICE.md: Copyright 2021 Comcast Cable Communications Management, LLC)",
       note=("The test decodes each base64 and hex string and compares the result's XML and JSON encodings with "
             "the Go literal in `expected`; fields the literal omits are Go zero values, which the comparison "
             "asserts too. A case with `err` expects that error."),
       **go)
gotext = read("downloads", "scte35-go-pkg_scte35_scte35_test.go", "body")


def go_block(text, start):
    """The text of the `{...}` that opens at text[start]."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    raise ValueError("unbalanced")


GO_EXPECTED = {
    # The agent's transcription of each Go literal (the literal itself is in source_decoded).
    # <Removed from this copy: the transcriptions of the 11 positive cases' Go literals, which are
    # decoded field values of third-party SCTE-35 messages (owner question Q21). What was checked
    # against them is in runs/check_vectors.txt: the fields compared for each case, no mismatch.>
}

for m in re.finditer(r'^\t\t"([^"]+)": \{$', gotext, re.M):
    name = m.group(1)
    blk = go_block(gotext, m.end() - 1)
    b64 = re.search(r'^\t\t\tbase64:\s+"([^"]*)"', blk, re.M)
    hx = re.search(r'^\t\t\thex:\s+"([^"]*)"', blk, re.M)
    err = re.search(r'^\t\t\terr:\s+(scte35\.\w+)', blk, re.M)
    if not b64:
        continue
    exp_text = None
    em = re.search(r"^\t\t\texpected: scte35\.SpliceInfoSection\{", blk, re.M)
    if em:
        exp_text = go_block(blk, em.end() - 1)
    lineno = line_of(gotext, m.group(0))
    rec = dict(id=f"scte35-go:{name}", source="scte35-go",
               locator=f"pkg/scte35/scte35_test.go:{lineno} (case \"{name}\")",
               encoding="base64", value=b64.group(1))
    if hx and hx.group(1):
        rec["also_published_as"] = [{"encoding": "hex", "value": hx.group(1)}]
    if err:
        rec["source_expects_error"] = err.group(1)
    if re.search(r"^\t\t\tlegacy: true", blk, re.M):
        rec["source_note"] = "legacy: true (the test does not require the re-encoded binary to match)"
    if exp_text:
        rec["source_decoded"] = {"format": "Go literal of the expected scte35.SpliceInfoSection", "text": exp_text}
    if name.startswith("Sample 14."):
        rec["expected"] = {"from": "same value as scte35-2023r1-" + name.split()[1] + " (checked by check_vectors.py)"}
    elif name in GO_EXPECTED:
        rec["expected"] = GO_EXPECTED[name]
    vector(**rec)

# ── 3. threefive ──────────────────────────────────────────────────────────────

source("threefive",
       title="superkabuki/threefive_is_scte35 (threefive), documentation",
       repository="https://github.com/superkabuki/threefive_is_scte35",
       commit="cfc84da937eda48da7fa81046ea9a52632ee8ec0",
       license="GPL-2.0 (GitHub's licence detection; the repository's LICENSE is the GNU GPL version 2 text)",
       files={"cue.md": fetch_info("threefive-new-cue.md"), "README.md": fetch_info("threefive-new-README.md")})
source("threefive-2024",
       title="futzu/threefive (an earlier repository of threefive), examples and smoke test",
       repository="https://github.com/futzu/threefive",
       commit="b93135f2d98b946acf44e192ed6d72e8230ee9d4",
       license="MIT (GitHub's licence detection; LICENSE is the MIT text)",
       files={"examples/hls/scte35.m3u8": fetch_info("threefive-old-examples_hls_scte35.m3u8"),
              "examples/hls/simple-scte35.m3u8": fetch_info("threefive-old-examples_hls_simple-scte35.m3u8"),
              "threefive/smoketest.py": fetch_info("threefive-old-threefive_smoketest.py")})


def json_after(text, start):
    i = text.index("{", start)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return json.loads(text[i:j + 1]), i
    raise ValueError("unbalanced JSON")


cue = read("downloads", "threefive-new-cue.md", "body")
s = cue.index("'<vector threefive:cue.md:time_signal-immediate>'")
dec, _ = json_after(cue, s)
vector(id="threefive:cue.md:time_signal-immediate", source="threefive",
       locator=f"cue.md:{line_of(cue, chr(39) + '<vector threefive:cue.md:time_signal-immediate>' + chr(39))} (output of cue.encode(), then cue.show())",
       encoding="base64", value=re.search(r"'(<vector threefive:cue.md:time_signal-immediate>)'", cue).group(1),
       source_decoded={"format": "threefive cue.show() JSON (times in seconds)", "json": dec},
       expected={"from": "threefive json"})
s = cue.index("# Output")
s = cue.index("'<vector threefive:cue.md:splice_insert-immediate>'", s)
dec, _ = json_after(cue, cue.index(">>>> cue.show()", cue.index("### json", s)))
hexv = re.search(r"'(<vector threefive:cue.md:splice_insert-immediate>)'", cue).group(1)
xml_dur = re.search(r'<scte35:BreakDuration autoReturn="true" duration="(\d+)"/>', cue).group(1)
vector(id="threefive:cue.md:splice_insert-immediate", source="threefive",
       locator=f"cue.md:{line_of(cue, chr(39) + '<vector threefive:cue.md:splice_insert-immediate>' + chr(39), cue.index('# Output'))} (the Output section: cue.base64(), cue.show(), cue.hex(), cue.xml())",
       encoding="base64", value="<vector threefive:cue.md:splice_insert-immediate>",
       also_published_as=[{"encoding": "hex", "value": hexv}],
       source_decoded={"format": "threefive cue.show() JSON (times in seconds); the same page's cue.xml() gives the BreakDuration in ticks",
                       "json": dec, "xml_break_duration_ticks": xml_dur},
       expected={"from": "threefive json"})
readme = read("downloads", "threefive-new-README.md", "body")
s = readme.index("'<vector threefive:README.md:time_signal-pts_adjustment>'")
dec, _ = json_after(readme, readme.index("cue.show()", s))
hexv = re.search(r"'(<vector threefive:README.md:time_signal-pts_adjustment>)'", readme).group(1)
xml = re.search(r'ptsAdjustment="(\d+)".*?ptsTime="(\d+)"', readme, re.S)
vector(id="threefive:README.md:time_signal-pts_adjustment", source="threefive",
       locator=f"README.md:{line_of(readme, chr(39) + '<vector threefive:README.md:time_signal-pts_adjustment>' + chr(39))} (cue.encode(), cue.hex(), cue.show(), cue.xml())",
       encoding="base64", value="<vector threefive:README.md:time_signal-pts_adjustment>",
       also_published_as=[{"encoding": "hex", "value": hexv}],
       source_decoded={"format": "threefive cue.show() JSON (times in seconds); cue.xml() gives ticks",
                       "json": dec, "xml_ptsAdjustment_ticks": xml.group(1), "xml_ptsTime_ticks": xml.group(2)},
       expected={"from": "threefive json"})

for fname, key in (("examples/hls/scte35.m3u8", "threefive-old-examples_hls_scte35.m3u8"),
                   ("examples/hls/simple-scte35.m3u8", "threefive-old-examples_hls_simple-scte35.m3u8")):
    pl = read("downloads", key, "body")
    seen = {}
    for i, l in enumerate(pl.split("\n"), 1):
        m = re.match(r"^#EXT-OATCLS-SCTE35:(\S+)$", l.strip())
        if m and m.group(1) not in seen:
            seen[m.group(1)] = i
            vector(id=f"threefive-2024:{fname}:{i}", source="threefive-2024", locator=f"{fname}:{i}",
                   encoding="base64", value=m.group(1), hls_tag_line=l.strip(),
                   source_decoded=None,
                   source_note="an example playlist: the source gives no decoded fields; the playlist around it is in the fetched file")
smoke = read("downloads", "threefive-old-threefive_smoketest.py", "body")
m = re.search(r'"Hex String": "(<vector threefive-2024:smoketest.py:Hex String>)"', smoke)
vector(id="threefive-2024:smoketest.py:Hex String", source="threefive-2024",
       locator=f"threefive/smoketest.py:{line_of(smoke, m.group(0))}", encoding="hex", value=m.group(1),
       source_decoded=None, source_note="a smoke test input: the source gives no decoded fields")

# ── 4. RFC 8216 §8.10 and draft-pantos-hls-rfc8216bis-22 §9.10 ────────────────

source("rfc8216", title="RFC 8216 HTTP Live Streaming, §8.10 EXT-X-DATERANGE Carrying SCTE-35 Tags",
       url="https://www.rfc-editor.org/rfc/rfc8216.txt",
       license="Copyright (c) 2017 IETF Trust and the persons identified as the document authors; BCP 78 and the IETF Trust's Legal Provisions",
       **fetch_info("rfc8216.txt"))
source("rfc8216bis-22", title="draft-pantos-hls-rfc8216bis-22 HTTP Live Streaming 2nd Edition, §9.10 EXT-X-DATERANGE Carrying SCTE-35 Tags",
       url="https://www.ietf.org/archive/id/draft-pantos-hls-rfc8216bis-22.txt",
       license="Copyright (c) 2026 IETF Trust and the persons identified as the document authors; BCP 78 and the IETF Trust's Legal Provisions",
       **fetch_info("rfc8216bis-22.txt"))

rfc = read("downloads", "rfc8216.txt", "body").split("\n")
i = next(k for k, l in enumerate(rfc) if l.startswith("8.10.  EXT-X-DATERANGE"))
ex = rfc[i:i + 20]
out_lines = [l.strip() for l in ex[8:12]]
in_lines = [l.strip() for l in ex[15:18]]
out_joined = "".join(out_lines)
in_joined = "".join(in_lines)
vector(id="rfc8216-8.10-SCTE35-OUT", source="rfc8216", locator=f"§8.10, lines {i + 9}-{i + 12} of rfc8216.txt",
       encoding="hex", value=re.search(r"SCTE35-OUT=(0x[0-9A-F]+)", out_joined).group(1),
       printed_lines=out_lines, hls_tag_line=out_joined,
       source_note="the RFC's text wraps the tag over lines; the value joins them",
       source_decoded={"format": "the tag's attributes; the RFC decodes nothing else",
                       "text": "an SCTE-35 \"out\" splice_insert() command (§8.10); ID=\"splice-6FFFFFF0\", PLANNED-DURATION=59.993"})
vector(id="rfc8216-8.10-SCTE35-IN", source="rfc8216", locator=f"§8.10, lines {i + 16}-{i + 18} of rfc8216.txt",
       encoding="hex", value=re.search(r"SCTE35-IN=(0x[0-9A-F]+)", in_joined).group(1),
       printed_lines=in_lines, hls_tag_line=in_joined,
       source_note="the RFC's text wraps the tag over lines; the value joins them",
       source_decoded={"format": "the tag's attributes; the RFC decodes nothing else",
                       "text": "an SCTE-35 \"in\" splice_insert() command (§8.10); ID=\"splice-6FFFFFF0\", DURATION=59.993"})

bis = read("downloads", "rfc8216bis-22.txt", "body").split("\n")
i = next(k for k, l in enumerate(bis) if l.startswith("9.10.  EXT-X-DATERANGE"))
ex = bis[i:i + 20]
out_lines = [l.strip() for l in ex[8:12]]
in_lines = [l.strip() for l in ex[15:19]]
out_joined = "".join(l.rstrip("\\") for l in out_lines)
in_joined = "".join(l.rstrip("\\") for l in in_lines)
vector(id="rfc8216bis-22-9.10-SCTE35-OUT", source="rfc8216bis-22", locator=f"§9.10, lines {i + 9}-{i + 12} of the draft's text",
       encoding="hex", value=re.search(r"SCTE35-OUT=(0x[0-9A-F]+)", out_joined).group(1),
       printed_lines=out_lines, hls_tag_line=out_joined,
       source_note="the draft folds the tag with '\\' line continuations; the value joins them",
       source_decoded={"format": "the tag's attributes; the draft decodes nothing else",
                       "text": "an SCTE-35 \"out\" splice_insert() command (§9.10); ID=\"splice-6FFFFFF0\", PLANNED-DURATION=59.993"})
vector(id="rfc8216bis-22-9.10-SCTE35-IN", source="rfc8216bis-22", locator=f"§9.10, lines {i + 16}-{i + 19} of the draft's text",
       encoding="hex", value=re.search(r"SCTE35-IN=(0x[0-9A-F]+)", in_joined).group(1),
       printed_lines=in_lines, hls_tag_line=in_joined,
       source_note="the draft folds the tag with '\\' line continuations; the value joins them",
       source_decoded={"format": "the tag's attributes; the draft decodes nothing else",
                       "text": "an SCTE-35 \"in\" splice_insert() command (§9.10); ID=\"splice-6FFFFFF0\", DURATION=59.993"})

# ── 5. Vendor documentation ──────────────────────────────────────────────────


def doc_vectors(sid, text, file_label, notes, decoded_for=None):
    """Every SCTE-35 value in the tag lines of a document, once each."""
    decoded_for = decoded_for or {}
    seen = set()
    for n, raw in enumerate(text.split("\n"), 1):
        l = raw.strip()
        for m in re.finditer(r"(SCTE35-OUT|SCTE35-IN|SCTE35-CMD)=?(0[xX][0-9A-Fa-f]+)", l):
            val = m.group(2)
            if val in seen:
                continue
            seen.add(val)
            if len(val) - 2 < 40:
                SKIPPED.append({"source": sid, "locator": f"{file_label}:{n}", "line": l,
                                "reason": "the value is elided in the source: fewer hex digits than the 20 bytes of the smallest splice_info_section"})
                continue
            rec = dict(id=f"{sid}:{n}:{m.group(1)}", source=sid, locator=f"{file_label}:{n}",
                       encoding="hex", value=val, hls_tag_line=l)
            rec.update(decoded_for.get(val, {"source_decoded": None, "source_note": notes}))
            rest = l[m.end():]
            if rest and rest[0] not in ", \t":
                rec["source_note"] = (rec.get("source_note", "") + " The page prints this value cut short: it runs into "
                                      + json.dumps(rest[:40]) + ".").strip()
            vector(**rec)
        for m in re.finditer(r"^#(EXT-OATCLS-SCTE35|EXT-X-SPLICEPOINT-SCTE35):(\S+)$", l):
            val = m.group(2)
            if val in seen:
                continue
            if "…" in val or "..." in val:
                SKIPPED.append({"source": sid, "locator": f"{file_label}:{n}", "line": l,
                                "reason": "the value is elided in the source"})
                seen.add(val)
                continue
            seen.add(val)
            rec = dict(id=f"{sid}:{n}:{m.group(1)}", source=sid, locator=f"{file_label}:{n}",
                       encoding="hex" if val[:2].lower() == "0x" else "base64", value=val, hls_tag_line=l)
            rec.update(decoded_for.get(val, {"source_decoded": None, "source_note": notes}))
            vector(**rec)
        m = re.match(r"^(/D[A-Za-z0-9+/]+=*)$", l)
        if m and m.group(1) not in seen:
            val = m.group(1)
            seen.add(val)
            rec = dict(id=f"{sid}:{n}:binary", source=sid, locator=f"{file_label}:{n}", encoding="base64", value=val)
            rec.update(decoded_for.get(val, {"source_decoded": None, "source_note": notes}))
            vector(**rec)


source("aws-medialive", title="AWS Elemental MediaLive User Guide, Sample manifests - HLS",
       url="https://docs.aws.amazon.com/medialive/latest/ug/sample-manifests-hls.html",
       license="AWS documentation; no reuse licence stated on the page",
       markdown=fetch_info("aws-medialive-sample-manifests-hls.md"), html=fetch_info("aws-medialive-sample-manifests-hls"))
md = read("downloads", "aws-medialive-sample-manifests-hls.md", "body")
doc_vectors("aws-medialive", md, "sample-manifests-hls.md", "",
            {"<vector aws-medialive:126:EXT-OATCLS-SCTE35>": {
                "source_decoded": {"format": "what the page states about this example",
                                   "text": ("SCTE 35 enhanced ad markers: OATCLS-SCTE35 carries 'the base64 encoded raw bytes of the original "
                                            "SCTE 35 ad avail message'; the example's next lines are #EXT-X-ASSET:CAID=<the UPID of vector aws-medialive:126:EXT-OATCLS-SCTE35> "
                                            "('the CAID or UPID as specified in the original SCTE35 message') and #EXT-X-CUE-OUT:201.467")}}})

source("aws-mediatailor", title="AWS Elemental MediaTailor User Guide, HLS supported ad markers",
       url="https://docs.aws.amazon.com/mediatailor/latest/ug/hls-ad-markers.html",
       license="AWS documentation; no reuse licence stated on the page",
       markdown=fetch_info("aws-mediatailor-hls-ad-markers.md"))
md = read("downloads", "aws-mediatailor-hls-ad-markers.md", "body")
sp = re.findall(r"#EXT-X-SPLICEPOINT-SCTE35:(\S+)", md)
doc_vectors("aws-mediatailor", md, "hls-ad-markers.md", "",
            {sp[0]: {"source_decoded": {"format": "what the page states", "text": "the cue-out: a splice_info_section containing segmentation type 0x34 (provider placement opportunity start)"},
                     "expected": {"segmentation_type_ids": [0x34]}},
             sp[1]: {"source_decoded": {"format": "what the page states", "text": "the cue-in: a splice_info_section containing segmentation type 0x35 (provider placement opportunity end)"},
                     "expected": {"segmentation_type_ids": [0x35]}}})

source("aws-mediapackage-v1", title="AWS Elemental MediaPackage User Guide, SCTE-35 message options (Example HLS manifest showing SCTE-35 EXT-X-DATERANGE signaling)",
       url="https://docs.aws.amazon.com/mediapackage/latest/ug/scte.html",
       license="AWS documentation; no reuse licence stated on the page",
       markdown=fetch_info("aws-mediapackage-v1-scte.md"))
md = read("downloads", "aws-mediapackage-v1-scte.md", "body")
doc_vectors("aws-mediapackage-v1", md, "scte.md",
            "a manifest example generated by MediaPackage: the page gives the DATERANGE attributes around it, no decoded fields")

source("unified-origin", title="Unified Streaming documentation, Unified Origin - Live: SCTE 35 (Signaling of SCTE 35 in HLS)",
       url="https://docs.unified-streaming.com/documentation/live/scte-35.html",
       license="Copyright 2007-2026, Unified Streaming (page footer); no reuse licence stated",
       **fetch_info("unified-live-scte-35"))
doc_vectors("unified-origin", read("derived", "unified-live-scte-35.txt"), "derived/unified-live-scte-35.txt (text of the page)",
            "a manifest example of a live stream: the page gives the DATERANGE attributes around it (PLANNED-DURATION = the break_duration), no decoded fields")

source("wowza", title="Wowza Streaming Engine documentation, Insert ad markers for SSAI delivery with a Wowza Streaming Engine live stream",
       url="https://www.wowza.com/docs/insert-ad-markers-for-ssai-delivery-with-a-wowza-streaming-engine-live-stream",
       license="© 2007–2025 Wowza Media Systems (page footer); no reuse licence stated",
       **fetch_info("wowza-scte-35"))
doc_vectors("wowza", read("derived", "wowza-scte-35.txt"), "derived/wowza-scte-35.txt (text of the page)",
            "a generated-playlist example of a live stream: no decoded fields; the page's DATERANGE lines have no '=' after SCTE35-OUT/SCTE35-IN and an unquoted START-DATE")

source("broadpeak", title="broadpeak.io developer documentation, SCTE-35 markers",
       url="https://developers.broadpeak.io/docs/dai-scte35",
       license="broadpeak.io documentation; no reuse licence stated on the page",
       **fetch_info("broadpeak-dai-scte35"))
doc_vectors("broadpeak", read("derived", "broadpeak-dai-scte35.txt"), "derived/broadpeak-dai-scte35.txt (text of the page)",
            "a live playlist example: no decoded fields")

source("google-admanager", title="Google Ad Manager Help, Live linear stream ad break encoding (HLS)",
       url="https://support.google.com/admanager/answer/13049027?hl=en",
       license="©2026 Google (page footer); no reuse licence stated",
       **fetch_info("google-admanager-13049027"))
ga = read("derived", "google-admanager-13049027.txt")
gdec = {}
for val, ctype, seg, what in (
        ("<vector google-admanager:237:binary>", 6, 34, "Break Start (a valid CUE-OUT)"),
        ("<vector google-admanager:257:binary>", 6, 35, "Break End (a valid CUE-IN)"),
        ("<vector google-admanager:267:binary>", 6, 48, "Provider Advertisement Start (a valid CUE-OUT)"),
        ("<vector google-admanager:287:binary>", 6, 49, "Provider Advertisement End (a valid CUE-IN)"),
        ("<vector google-admanager:297:binary>", 6, 52, "Provider Placement Opportunity Start (a valid CUE-OUT)"),
        ("<vector google-admanager:317:binary>", 6, 53, "Provider Placement Opportunity End (a valid CUE-IN)")):
    assert val in ga, val
    gdec[val] = {"source_decoded": {"format": "what the page states after the example",
                                    "text": f"splice_command_type set to a value of {ctype} (a time signal); segmentation_type_id {seg}: {what}"},
                 "expected": {"splice_command_type": ctype, "segmentation_type_ids": [seg]}}
doc_vectors("google-admanager", ga, "derived/google-admanager-13049027.txt (text of the page)",
            "the page names it a CUE-OUT or CUE-IN example and decodes nothing else", gdec)

source("google-video-stitcher", title="Google Cloud Video Stitcher API documentation, Ad markers",
       url="https://docs.cloud.google.com/video-stitcher/docs/concepts/ad-markers",
       license="CC BY 4.0 for the page's content, Apache 2.0 for its code samples (the page's footer)",
       **fetch_info("gcloud-video-stitcher-ad-markers"))
doc_vectors("google-video-stitcher", read("derived", "gcloud-video-stitcher-ad-markers.txt"),
            "derived/gcloud-video-stitcher-ad-markers.txt (text of the page)",
            "an example the page gives with its tag (SCTE35-OUT: the start splice point; SCTE35-IN: the end); no decoded fields")

# ── 6. A public VOD stream (fetched by the agent) ────────────────────────────

source("unified-avod-demo", title="Unified Streaming demo: Remix AVOD SCTE-35 (AWS Elemental MediaTailor), the origin's 4017000 b/s video playlist",
       page="https://demo.unified-streaming.com/k8s/avod-scte35-mediatailor/stable/",
       license="a public demo stream; no licence stated",
       **fetch_info("stream-unified-avod-k8s-video4017000"))
pl = read("downloads", "stream-unified-avod-k8s-video4017000", "body")
for n, l in enumerate(pl.split("\n"), 1):
    m = re.search(r"SCTE35-OUT=(0x[0-9A-F]+)", l)
    if m:
        vector(id=f"unified-avod-demo:{n}:SCTE35-OUT", source="unified-avod-demo", locator=f"media playlist line {n}",
               encoding="hex", value=m.group(1), hls_tag_line=re.sub(r"eyJ[A-Za-z0-9_=-]+", "<b64>", l.strip()),
               source_decoded={"format": "the comment line the origin writes before it", "text": "## splice_insert(auto_return)"},
               expected={"splice_command_type": 5})

# ── Tag lines: the syntax variants the sources print ─────────────────────────

TAG_RE = re.compile(r"#\s?(EXT-X-CUE-OUT-CONT|EXT-X-CUE-OUT|EXT-X-CUE-IN|EXT-OATCLS-SCTE35|EXT-X-ASSET|EXT-X-SPLICEPOINT-SCTE35|EXT-X-SCTE35|EXT-X-CUE|EXT-X-CUEPOINT|EXT-X-AD|EXT-X-DATERANGE)\b[^\n]*")


def doc_tags(sid, text, label, keep=None):
    seen = set()
    for n, raw in enumerate(text.split("\n"), 1):
        for m in TAG_RE.finditer(raw):
            line = m.group(0).strip()
            line = re.sub(r"eyJ[A-Za-z0-9_=-]+", "<b64>", line)
            key = re.sub(r"[0-9A-Fa-f]{16,}|/D[A-Za-z0-9+/=]{8,}", "…", line)
            if key in seen:
                continue
            seen.add(key)
            tag(source=sid, locator=f"{label}:{n}", tag=m.group(1), line=line)


doc_tags("aws-medialive", read("downloads", "aws-medialive-sample-manifests-hls.md", "body"), "sample-manifests-hls.md")
doc_tags("aws-mediatailor", read("downloads", "aws-mediatailor-hls-ad-markers.md", "body"), "hls-ad-markers.md")
doc_tags("aws-mediapackage-v1", read("downloads", "aws-mediapackage-v1-scte.md", "body"), "scte.md")
doc_tags("unified-origin", read("derived", "unified-live-scte-35.txt"), "derived/unified-live-scte-35.txt")
doc_tags("wowza", read("derived", "wowza-scte-35.txt"), "derived/wowza-scte-35.txt")
doc_tags("broadpeak", read("derived", "broadpeak-dai-scte35.txt"), "derived/broadpeak-dai-scte35.txt")
doc_tags("google-admanager", read("derived", "google-admanager-13049027.txt"), "derived/google-admanager-13049027.txt")
doc_tags("google-video-stitcher", read("derived", "gcloud-video-stitcher-ad-markers.txt"), "derived/gcloud-video-stitcher-ad-markers.txt")
source("adobe-psdk", title="Adobe Primetime PSDK for iOS, Live and Linear Ad Resolving and Insertion",
       url="https://helpx.adobe.com/primetime/psdk/ios/PSDKs/concept/c_psdk_ios_live-and-linear-client-ad-resolving-insertion.html",
       license="Adobe documentation; no reuse licence stated on the page",
       **fetch_info("adobe-psdk-ios-live-linear"))
doc_tags("adobe-psdk", read("derived", "adobe-psdk-ios-live-linear.txt"), "derived/adobe-psdk-ios-live-linear.txt")
_t = read("derived", "SCTE_35_2023r1.txt")
_a = re.search(r"^12\.2\.4\.\s+HLS playlist example\s*$", _t, re.M).start()
_b = re.search(r"^13\.\s+SCTE 35 XML elements and types\s*$", _t, re.M).start()
_off = _t.count("\n", 0, _a)
for _n, _raw in enumerate(_t[_a:_b].split("\n"), _off + 1):
    _l = _raw.strip()
    if _l.startswith("#") and "SCTE35" in _l.replace(" ", ""):
        tag(source="scte35-2023r1", locator=f"§12.2.4, derived/SCTE_35_2023r1.txt:{_n}", tag="EXT-X-SCTE35",
            line=re.sub(r"\s*-\s*(?=[A-Z0-9])", "-", re.sub(r"^#\s*", "#", _l)).replace("SCTE 35", "SCTE35"),
            note="spaces the PDF text extraction put around hyphens removed; the typographic quotes are the PDF's")
doc_tags("threefive-2024", read("downloads", "threefive-old-examples_hls_scte35.m3u8", "body"), "examples/hls/scte35.m3u8")

doc = {
    "title": "SCTE-35 test vectors from published sources (survey for Mirage Phase 3, §3.1)",
    "layout": ("sources{id: where the vectors were found, licence}, vectors[{id, source, locator, encoding, value, ...}], "
               "hls_tag_lines[{source, locator, tag, line}]. `value` is the string as its source publishes it; "
               "`also_published_as` is the same message in another encoding from the same source; `source_decoded` "
               "is what the source says it decodes to (null when it says nothing); `expected` is the agent's "
               "transcription of source_decoded for the check; `agent_check` is the result of "
               "tools/check_vectors.py (scte35_evidence/runs/check_vectors.txt), not the source's."),
    "sources": SOURCES,
    "vectors": VECTORS,
    "hls_tag_lines": TAGS,
    "skipped_values": SKIPPED,
}
with open(OUT, "w", encoding="utf-8") as fh:
    json.dump(doc, fh, indent=1, ensure_ascii=False)
by = {}
for v in VECTORS:
    by[v["source"]] = by.get(v["source"], 0) + 1
print(f"sources={len(SOURCES)} vectors={len(VECTORS)} hls_tag_lines={len(TAGS)} skipped_values={len(SKIPPED)}")
for k in SKIPPED:
    print("  skipped:", k["source"], k["locator"], k["line"][:110])
for k, n in by.items():
    print(f"  {k}: {n} vectors")
