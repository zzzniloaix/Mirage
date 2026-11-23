#!/usr/bin/env python3
"""Plain text of an HTML page (standard library only): drops script/style,
keeps <pre>/<code> blocks' line breaks, puts block elements on new lines.
usage: python3 -I html2text.py <in.html> <out.txt>"""
import html
import re
import sys
from html.parser import HTMLParser

BLOCK = {"p", "div", "br", "li", "ul", "ol", "tr", "table", "h1", "h2", "h3", "h4", "h5", "h6",
         "pre", "section", "article", "dt", "dd", "dl", "blockquote", "td", "th", "hr", "header", "footer"}


class P(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.out = []
        self.skip = 0
        self.pre = 0

    def handle_starttag(self, tag, attrs):
        if tag in ("script", "style", "noscript", "svg"):
            self.skip += 1
        if tag == "pre":
            self.pre += 1
        if tag in BLOCK:
            self.out.append("\n")
        if tag in ("td", "th"):
            self.out.append(" | ")

    def handle_endtag(self, tag):
        if tag in ("script", "style", "noscript", "svg") and self.skip:
            self.skip -= 1
        if tag == "pre" and self.pre:
            self.pre -= 1
        if tag in BLOCK:
            self.out.append("\n")

    def handle_data(self, data):
        if self.skip:
            return
        if self.pre:
            self.out.append(data)
        else:
            self.out.append(re.sub(r"\s+", " ", data))


def main():
    p = P()
    p.feed(open(sys.argv[1], encoding="utf-8", errors="replace").read())
    txt = "".join(p.out)
    txt = re.sub(r"[ \t]+\n", "\n", txt)
    txt = re.sub(r"\n{3,}", "\n\n", txt)
    # Email addresses are written <email>, so that the text holds none.
    txt = re.sub(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}", "<email>", txt)
    open(sys.argv[2], "w", encoding="utf-8").write(txt)
    print(f"characters={len(txt)}")


if __name__ == "__main__":
    main()
