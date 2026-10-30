#!/usr/bin/env python3
"""PC5's browser page (title/pc5_web_page.html) as C for pc5_web.c: title/pc5_web_page.h, a line
a string (no string longer than ISO C asks compilers to take). Usage: make-web-page.py [out.h]."""
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
source = os.path.join(here, "..", "title", "pc5_web_page.html")
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "title", "pc5_web_page.h")


def c_string(line):
    text = []
    for byte in line.encode("utf-8"):
        c = chr(byte)
        if c == "\\" or c == '"':
            text.append("\\" + c)
        elif c == "\n":
            text.append("\\n")
        elif 0x20 <= byte < 0x7f:
            text.append(c)
        else:
            text.append("\\%03o" % byte)
    return '"' + "".join(text) + '"'


with open(source, encoding="utf-8") as f:
    lines = f.read().splitlines(keepends=True)
with open(out, "w", encoding="utf-8") as f:
    f.write("/* SPDX-License-Identifier: LGPL-2.1-or-later */\n")
    f.write("/* PC5's browser page (pc5_web.h), made from pc5_web_page.html by tools/make-web-page.py:\n")
    f.write(" * change the .html, then run it. */\n")
    f.write("static const char *const pc5_web_page[] = {\n")
    for line in lines:
        f.write("    " + c_string(line) + ",\n")
    f.write("    0\n};\n")
