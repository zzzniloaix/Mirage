#!/usr/bin/env python3
"""Minimal PDF text extractor (Python standard library only).

Written for this survey because no PDF tool is installed on the machine.
Handles what the SCTE 35 PDF uses: FlateDecode streams, object streams
(/ObjStm), simple and Type0 fonts with ToUnicode CMaps, and the text
operators Tj, TJ, ', ". It does not lay out columns; it starts a new line
when the text position moves down, and inserts a space for a large gap
within a TJ array.

usage: python3 -I pdftext.py <in.pdf> <out.txt>
Writes one "=== page N ===" block per page to <out.txt> and prints a
summary (pages, characters) to stdout.
"""
import re
import sys
import zlib

WS = b" \t\r\n\f\x00"
DELIM = b"()<>[]{}/%"


# ── Object model ──────────────────────────────────────────────────────────────

class Ref:
    __slots__ = ("num",)

    def __init__(self, num):
        self.num = num

    def __repr__(self):
        return f"Ref({self.num})"


class Name(str):
    pass


class Lexer:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos

    def skip_ws(self):
        d, n = self.d, len(self.d)
        while self.p < n:
            c = d[self.p:self.p + 1]
            if c in (b" ", b"\t", b"\r", b"\n", b"\f", b"\x00"):
                self.p += 1
            elif c == b"%":
                while self.p < n and d[self.p:self.p + 1] not in (b"\r", b"\n"):
                    self.p += 1
            else:
                break

    def token(self):
        """Next raw token: bytes for keywords/numbers, or a parsed literal."""
        self.skip_ws()
        d = self.d
        if self.p >= len(d):
            return None
        c = d[self.p:self.p + 1]
        if c == b"/":
            self.p += 1
            s = self.p
            while self.p < len(d) and d[self.p:self.p + 1] not in WS and d[self.p:self.p + 1] not in DELIM:
                self.p += 1
            raw = d[s:self.p]
            raw = re.sub(rb"#([0-9A-Fa-f]{2})", lambda m: bytes([int(m.group(1), 16)]), raw)
            return Name(raw.decode("latin-1"))
        if c == b"(":
            return self.literal_string()
        if c == b"<":
            if d[self.p + 1:self.p + 2] == b"<":
                self.p += 2
                return b"<<"
            self.p += 1
            e = d.index(b">", self.p)
            hx = re.sub(rb"\s", b"", d[self.p:e])
            self.p = e + 1
            if len(hx) % 2:
                hx += b"0"
            return bytearray(bytes.fromhex(hx.decode("ascii")))  # bytearray marks a string
        if c == b">" and d[self.p + 1:self.p + 2] == b">":
            self.p += 2
            return b">>"
        if c in (b"[", b"]", b"{", b"}"):
            self.p += 1
            return c
        s = self.p
        while self.p < len(d) and d[self.p:self.p + 1] not in WS and d[self.p:self.p + 1] not in DELIM:
            self.p += 1
        if self.p == s:
            self.p += 1
        return d[s:self.p]

    def literal_string(self):
        d = self.d
        self.p += 1
        depth = 1
        out = bytearray()
        while self.p < len(d):
            c = d[self.p]
            if c == 0x5C:  # backslash
                self.p += 1
                e = d[self.p]
                m = {0x6E: 10, 0x72: 13, 0x74: 9, 0x62: 8, 0x66: 12, 0x28: 0x28, 0x29: 0x29, 0x5C: 0x5C}
                if e in m:
                    out.append(m[e])
                    self.p += 1
                elif 0x30 <= e <= 0x37:
                    s = self.p
                    while self.p < len(d) and self.p - s < 3 and 0x30 <= d[self.p] <= 0x37:
                        self.p += 1
                    out.append(int(d[s:self.p], 8) & 0xFF)
                elif e in (0x0D, 0x0A):
                    self.p += 1
                    if e == 0x0D and d[self.p:self.p + 1] == b"\n":
                        self.p += 1
                else:
                    out.append(e)
                    self.p += 1
                continue
            if c == 0x28:
                depth += 1
            elif c == 0x29:
                depth -= 1
                if depth == 0:
                    self.p += 1
                    break
            out.append(c)
            self.p += 1
        return out

    def obj(self):
        """Parse one object (dict, array, ref, number, name, string)."""
        t = self.token()
        return self._obj(t)

    def _obj(self, t):
        if type(t) is bytes and t == b"<<":
            dct = {}
            while True:
                k = self.token()
                if k is None or (type(k) is bytes and k == b">>"):
                    return dct
                v = self.obj()
                dct[str(k)] = v
        if type(t) is bytes and t == b"[":
            arr = []
            while True:
                save = self.p
                k = self.token()
                if k is None or (type(k) is bytes and k == b"]"):
                    return arr
                self.p = save
                arr.append(self.obj())
        if isinstance(t, (bytes,)) and re.fullmatch(rb"[+-]?\d+", t):
            save = self.p
            t2 = self.token()
            if isinstance(t2, bytes) and re.fullmatch(rb"\d+", t2):
                t3 = self.token()
                if t3 == b"R":
                    return Ref(int(t))
            self.p = save
            return int(t)
        if isinstance(t, bytes) and re.fullmatch(rb"[+-]?(\d+\.?\d*|\.\d+)", t):
            return float(t)
        return t


class Pdf:
    def __init__(self, data):
        self.d = data
        self.objs = {}      # num -> parsed object
        self.streams = {}   # num -> raw stream bytes (still encoded)
        self._scan()
        self._objstreams()

    def _scan(self):
        for m in re.finditer(rb"(\d+)\s+(\d+)\s+obj\b", self.d):
            num = int(m.group(1))
            lx = Lexer(self.d, m.end())
            try:
                o = lx.obj()
            except Exception:
                continue
            self.objs[num] = o
            lx.skip_ws()
            if self.d.startswith(b"stream", lx.p):
                s = lx.p + 6
                if self.d[s:s + 2] == b"\r\n":
                    s += 2
                elif self.d[s:s + 1] in (b"\n", b"\r"):
                    s += 1
                ln = o.get("Length") if isinstance(o, dict) else None
                if isinstance(ln, int) and self.d[s + ln:s + ln + 20].lstrip().startswith(b"endstream"):
                    self.streams[num] = self.d[s:s + ln]
                else:
                    e = self.d.index(b"endstream", s)
                    self.streams[num] = self.d[s:e].rstrip(b"\r\n")

    def resolve(self, o):
        seen = 0
        while isinstance(o, Ref) and seen < 50:
            o = self.objs.get(o.num)
            seen += 1
        return o

    def stream_data(self, num):
        raw = self.streams.get(num)
        if raw is None:
            return b""
        dct = self.objs.get(num) or {}
        filt = self.resolve(dct.get("Filter"))
        filters = filt if isinstance(filt, list) else ([filt] if filt else [])
        data = raw
        for f in filters:
            f = self.resolve(f)
            if f == "FlateDecode":
                try:
                    data = zlib.decompress(data)
                except zlib.error:
                    data = zlib.decompressobj().decompress(data)
                parms = self.resolve(dct.get("DecodeParms"))
                if isinstance(parms, list):
                    parms = parms[0] if parms else None
                parms = self.resolve(parms)
                if isinstance(parms, dict) and (self.resolve(parms.get("Predictor")) or 1) >= 10:
                    data = png_unpredict(data, self.resolve(parms.get("Columns")) or 1)
            else:
                return b""
        return data

    def _objstreams(self):
        for num, o in list(self.objs.items()):
            if isinstance(o, dict) and o.get("Type") == "ObjStm":
                data = self.stream_data(num)
                n = self.resolve(o.get("N"))
                first = self.resolve(o.get("First"))
                hdr = data[:first].split()
                for i in range(n):
                    onum = int(hdr[2 * i])
                    off = int(hdr[2 * i + 1])
                    lx = Lexer(data, first + off)
                    try:
                        self.objs.setdefault(onum, lx.obj())
                    except Exception:
                        pass

    def pages(self):
        root = None
        for o in self.objs.values():
            if isinstance(o, dict) and o.get("Type") == "Catalog":
                root = o
                break
        out = []

        def walk(node, inherited):
            node = self.resolve(node)
            if not isinstance(node, dict):
                return
            res = node.get("Resources", inherited)
            if node.get("Type") == "Pages":
                for k in self.resolve(node.get("Kids")) or []:
                    walk(k, res)
            else:
                out.append((node, res))

        walk(root.get("Pages"), None)
        return out


def png_unpredict(data, columns):
    row = columns + 1
    out = bytearray()
    prev = bytearray(columns)
    for i in range(0, len(data), row):
        ft = data[i]
        line = bytearray(data[i + 1:i + row])
        for j in range(len(line)):
            a = line[j - 1] if j > 0 else 0
            b = prev[j] if j < len(prev) else 0
            c = prev[j - 1] if j > 0 and j - 1 < len(prev) else 0
            if ft == 1:
                line[j] = (line[j] + a) & 0xFF
            elif ft == 2:
                line[j] = (line[j] + b) & 0xFF
            elif ft == 3:
                line[j] = (line[j] + (a + b) // 2) & 0xFF
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[j] = (line[j] + pr) & 0xFF
        out += line
        prev = line
    return bytes(out)


# ── Fonts ─────────────────────────────────────────────────────────────────────

WINANSI_EXTRA = {0x91: "‘", 0x92: "’", 0x93: "“", 0x94: "”", 0x96: "–",
                 0x97: "—", 0x95: "•", 0x85: "…", 0xA0: " "}

GLYPHS = {"space": " ", "quoteright": "’", "quoteleft": "‘", "quotedblleft": "“",
          "quotedblright": "”", "endash": "–", "emdash": "—", "bullet": "•",
          "hyphen": "-", "period": ".", "comma": ",", "colon": ":", "semicolon": ";",
          "parenleft": "(", "parenright": ")", "bracketleft": "[", "bracketright": "]",
          "slash": "/", "underscore": "_", "equal": "=", "plus": "+", "less": "<", "greater": ">",
          "zero": "0", "one": "1", "two": "2", "three": "3", "four": "4", "five": "5", "six": "6",
          "seven": "7", "eight": "8", "nine": "9", "registered": "®", "copyright": "©",
          "trademark": "™", "degree": "°", "multiply": "×", "percent": "%",
          "ampersand": "&", "quotesingle": "'", "quotedbl": '"', "asterisk": "*", "numbersign": "#",
          "dollar": "$", "at": "@", "exclam": "!", "question": "?", "bar": "|", "braceleft": "{",
          "braceright": "}", "asciitilde": "~", "asciicircum": "^", "backslash": "\\", "grave": "`"}


def parse_cmap(data):
    m = {}
    two = False
    for blk in re.finditer(rb"begincodespacerange(.*?)endcodespacerange", data, re.S):
        if re.search(rb"<[0-9A-Fa-f]{4}>", blk.group(1)):
            two = True
    for blk in re.finditer(rb"beginbfchar(.*?)endbfchar", data, re.S):
        for a, b in re.findall(rb"<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>", blk.group(1)):
            m[int(a, 16)] = bytes.fromhex(b.decode()).decode("utf-16-be", "replace")
    for blk in re.finditer(rb"beginbfrange(.*?)endbfrange", data, re.S):
        body = blk.group(1)
        for mm in re.finditer(rb"<([0-9A-Fa-f]+)>\s*<([0-9A-Fa-f]+)>\s*(<[0-9A-Fa-f]+>|\[[^\]]*\])", body):
            lo, hi = int(mm.group(1), 16), int(mm.group(2), 16)
            dst = mm.group(3)
            if dst.startswith(b"["):
                items = re.findall(rb"<([0-9A-Fa-f]+)>", dst)
                for i, it in enumerate(items):
                    m[lo + i] = bytes.fromhex(it.decode()).decode("utf-16-be", "replace")
            else:
                base = bytes.fromhex(dst[1:-1].decode())
                bv = int.from_bytes(base, "big")
                for c in range(lo, hi + 1):
                    v = (bv + (c - lo)).to_bytes(len(base), "big")
                    m[c] = v.decode("utf-16-be", "replace")
    return m, two


class Font:
    def __init__(self, pdf, fdict):
        self.map = {}
        self.two = False
        sub = pdf.resolve(fdict.get("Subtype"))
        tu = fdict.get("ToUnicode")
        if isinstance(tu, Ref):
            self.map, _ = parse_cmap(pdf.stream_data(tu.num))
        # A simple font's codes are single bytes whatever its ToUnicode
        # CMap's codespace says; only a Type0 font's are two bytes here.
        self.two = sub == "Type0"
        self.diff = {}
        enc = pdf.resolve(fdict.get("Encoding"))
        if isinstance(enc, dict):
            code = 0
            for it in pdf.resolve(enc.get("Differences")) or []:
                it = pdf.resolve(it)
                if isinstance(it, int):
                    code = it
                elif isinstance(it, Name):
                    self.diff[code] = it
                    code += 1

    def decode(self, s):
        out = []
        if self.two:
            for i in range(0, len(s) - 1, 2):
                c = (s[i] << 8) | s[i + 1]
                out.append(self.map.get(c, ""))
            return "".join(out)
        for c in s:
            if c in self.map:
                out.append(self.map[c])
            elif c in self.diff:
                g = self.diff[c]
                if len(g) == 1:
                    out.append(g)
                elif g in GLYPHS:
                    out.append(GLYPHS[g])
                elif g.startswith("uni") and len(g) == 7:
                    out.append(chr(int(g[3:], 16)))
                else:
                    out.append("?")
            elif c in WINANSI_EXTRA:
                out.append(WINANSI_EXTRA[c])
            else:
                out.append(chr(c))
        return "".join(out)


# ── Content streams ───────────────────────────────────────────────────────────

def page_text(pdf, page, res):
    res = pdf.resolve(res) or {}
    fonts = {}
    fd = pdf.resolve(res.get("Font")) or {}
    for name, ref in fd.items():
        f = pdf.resolve(ref)
        if isinstance(f, dict):
            fonts[name] = Font(pdf, f)
    contents = pdf.resolve(page.get("Contents"))
    refs = contents if isinstance(contents, list) else [page.get("Contents")]
    data = b""
    for r in refs:
        if isinstance(r, Ref):
            data += pdf.stream_data(r.num) + b"\n"
    lx = Lexer(data)
    stack = []
    font = None
    out = []
    last_y = None
    y = 0.0
    lead = 0.0
    while True:
        t = lx.token()
        if t is None:
            break
        if type(t) is bytes and t == b"[":  # a string "[" is a bytearray
            arr = []
            while True:
                save = lx.p
                k = lx.token()
                if k is None or (type(k) is bytes and k == b"]"):
                    break
                lx.p = save
                arr.append(lx.obj())
            stack.append(arr)
            continue
        if type(t) is bytes and t == b"<<":
            lx.p -= 2
            stack.append(lx.obj())
            continue
        if isinstance(t, (bytearray, Name)) or (isinstance(t, bytes) and re.fullmatch(rb"[+-]?(\d+\.?\d*|\.\d+)", t)):
            stack.append(t if not isinstance(t, bytes) else float(t))
            continue
        op = t
        if op == b"BI":  # inline image: skip to EI
            e = data.find(b"EI", lx.p)
            lx.p = e + 2 if e >= 0 else len(data)
            stack = []
            continue

        def newline_if_moved(ny):
            nonlocal last_y
            if last_y is not None and abs(ny - last_y) > 1.0:
                out.append("\n")
            elif last_y is not None and out and not out[-1].endswith((" ", "\n")):
                out.append(" ")
            last_y = ny

        if op == b"Tf" and len(stack) >= 2:
            font = fonts.get(str(stack[-2]))
        elif op == b"TL" and stack:
            lead = float(stack[-1])
        elif op in (b"Td", b"TD") and len(stack) >= 2:
            y += float(stack[-1])
            if op == b"TD":
                lead = -float(stack[-1])
        elif op == b"Tm" and len(stack) >= 6:
            y = float(stack[-1])
        elif op == b"T*":
            y -= lead
        elif op == b"BT":
            y = 0.0
        elif op in (b"Tj", b"'", b'"') and stack and isinstance(stack[-1], bytearray):
            if op != b"Tj":
                y -= lead
            newline_if_moved(y)
            if font:
                out.append(font.decode(stack[-1]))
        elif op == b"TJ" and stack and isinstance(stack[-1], list):
            newline_if_moved(y)
            for it in stack[-1]:
                if isinstance(it, bytearray) and font:
                    out.append(font.decode(it))
                elif isinstance(it, (int, float)) and it < -200:
                    out.append(" ")
        elif op == b"cm" or op == b"q" or op == b"Q":
            pass
        stack = []
    return "".join(out)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    pdf = Pdf(open(src, "rb").read())
    pages = pdf.pages()
    total = 0
    with open(dst, "w", encoding="utf-8") as fh:
        for i, (pg, res) in enumerate(pages, 1):
            # Email addresses are written <email>, so that the text holds none.
            txt = re.sub(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}", "<email>", page_text(pdf, pg, res))
            total += len(txt)
            fh.write(f"=== page {i} ===\n{txt}\n")
    print(f"pages={len(pages)} characters={total}")


if __name__ == "__main__":
    main()
