# usage: python3 -I page_script.py <report.html> <out.js>
# The page's last script (the one that builds the report), wrapped in a function no one calls: a JavaScript
# engine then parses it without running it, and says "parsed" (run.sh gives it to JavaScriptCore's jsc).
import re, sys

html, out = sys.argv[1], sys.argv[2]
scripts = re.findall(r"<script[^>]*>(.*?)</script>", open(html).read(), re.S)
with open(out, "w") as f:
    f.write("function page() {\n" + scripts[-1] + "\n}\nprint(\"parsed\");\n")
print("%d scripts; the last, %d characters, in %s" % (len(scripts), len(scripts[-1]), out))
