#!/usr/bin/env python3
"""Верификация этапа C: консистентность замен getArgInt/getArgFloat.

Проверки:
1. Ни одного остатка server.arg(...).toInt()/.toFloat() в целевых файлах.
2. Каждый файл с getArgInt/getArgFloat включает web/param_utils.h.
3. Подсчёт замен.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FILES = [
    "src/web/web_handlers_settings_throttle.cpp",
    "src/web/web_handlers_settings_pas.cpp",
    "src/web/web_handlers_settings_cruise.cpp",
    "src/web/web_handlers_settings_wifi.cpp",
    "src/web/web_handlers_pins.cpp",
    "src/web/web_handlers_emulation.cpp",
]

PAT_LEFT = re.compile(r'server\.arg\([^)]*\)\.(toInt|toFloat)\(\)')
PAT_USED = re.compile(r'getArg(Int|Float)\(server,')
INCLUDE = '#include "web/param_utils.h"'


def main() -> int:
    fails = 0
    total = 0
    for rel in FILES:
        t = (ROOT / rel).read_text(encoding="utf-8")
        left = PAT_LEFT.findall(t)
        used = len(PAT_USED.findall(t))
        has_inc = INCLUDE in t
        if used and not has_inc:
            print(f"FAIL: {rel}: getArg* без param_utils.h")
            fails += 1
        if left:
            print(f"FAIL: {rel}: остатки server.arg().toInt/toFloat: {len(left)}")
            fails += 1
        total += used
        print(f"{rel}: getArg* x{used}, include={'да' if has_inc else 'нет'}, остатков={len(left)}")
    print(f"Итого замен: {total}")
    print("RESULT:", "OK" if fails == 0 else f"FAILURES: {fails}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
