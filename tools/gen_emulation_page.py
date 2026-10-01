#!/usr/bin/env python3
"""Генерация data/emulation.html из PROGMEM-частей страницы эмулятора.

Единый источник правды — PROGMEM-части src/web/html_pages_emulation_*.cpp
(этап D). Скрипт собирает их в data/emulation.html, которая раздаётся с
LittleFS; встроенная копия в прошивке (sendEmulationPage) — фолбэк.

Проверка консистентности: cmp data/emulation.html <реконструкция> == 0.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PARTS = [
    "src/web/html_pages_emulation_head.cpp",
    "src/web/html_pages_emulation_status.cpp",
    "src/web/html_pages_emulation_state.cpp",
    "src/web/html_pages_emulation_matrix_core.cpp",
    "src/web/html_pages_emulation_matrix_boot.cpp",
    "src/web/html_pages_emulation_matrix_display.cpp",
    "src/web/html_pages_emulation_app.cpp",
]

RAW_OPEN = 'R"rawliteral('
RAW_CLOSE = ')rawliteral";'


def extract_part(path: Path) -> str:
    """Достаёт контент raw-литерала из PROGMEM-части (этап D).

    Контент начинается сразу после R"rawliteral( в первой такой строке
    и заканчивается перед )rawliteral"; — закрывающая скобка литерала.
    """
    text = path.read_text(encoding="utf-8")
    start = text.index(RAW_OPEN) + len(RAW_OPEN)
    end = text.index(RAW_CLOSE, start)
    return text[start:end]


def main() -> int:
    chunks = []
    for rel in PARTS:
        chunk = extract_part(ROOT / rel)
        if not chunk:
            print(f"ERROR: empty part: {rel}", file=sys.stderr)
            return 1
        chunks.append(chunk)
    page = "".join(chunks)
    out = ROOT / "data/emulation.html"
    out.write_text(page, encoding="utf-8")
    print(f"OK: {out} ({len(page.encode('utf-8'))} bytes from {len(chunks)} parts)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
