#!/usr/bin/env python3
"""Этап C: замена server.arg(x).toInt()/.toFloat() на getArgInt/getArgFloat.

Семантика 1-в-1: getArgInt/Float = server.arg(name).toInt()/.toFloat().
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FILES = [
    "src/web/web_handlers_settings_wifi.cpp",
    "src/web/web_handlers_pins.cpp",
    "src/web/web_handlers_emulation.cpp",
]

PAT_I = re.compile(r'server\.arg\(((?:"[^"]*"|[A-Za-z_][A-Za-z0-9_]*))\)\.toInt\(\)')
PAT_F = re.compile(r'server\.arg\(((?:"[^"]*"|[A-Za-z_][A-Za-z0-9_]*))\)\.toFloat\(\)')
INCLUDE = '#include "web/param_utils.h"   // getArgInt/getArgFloat (семантика toInt/toFloat)'


def main() -> int:
    for rel in FILES:
        p = ROOT / rel
        t = p.read_text(encoding="utf-8")
        n1 = len(PAT_I.findall(t))
        n2 = len(PAT_F.findall(t))
        if n1 or n2:
            if INCLUDE not in t:
                anchor = "#include <WebServer.h>\n"
                idx = t.index(anchor) + len(anchor)
                t = t[:idx] + "\n" + INCLUDE + "\n" + t[idx:]
            t = PAT_I.sub(r"getArgInt(server, \1)", t)
            t = PAT_F.sub(r"getArgFloat(server, \1)", t)
            p.write_text(t, encoding="utf-8")
        print(f"{rel}: toInt x{n1}, toFloat x{n2}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
