#!/usr/bin/env python3
"""Normalize the "back to menu" control on every UI page.

Canonical markup (must stay byte-identical to web_ui.cpp getBackMenuHtml()):
    <p class="back-row"><a class="back-link" href="/">&larr; Меню</a></p>
Static pages (data/*.html and the PROGMEM fallbacks in html_pages.cpp) cannot
call the C++ helper, so they carry a literal copy; this script repairs any
variant label/class to the canonical one. Idempotent.
"""
import io
import re
import sys

BACK = '<p class="back-row"><a class="back-link" href="/">&larr; Меню</a></p>'
# Any <p> (or <p class=...>) wrapping a link to "/" with a back arrow and any label.
PATTERN = re.compile(
    r'<p(?:\s+class="[^"]*")?>\s*<a\b[^>]*href="/"[^>]*>'
    r'(?:&larr;|&#8592;|\u2190)\s*[^<]*</a>\s*</p>'
)

FILES = [
    'src/web/web_handlers_pins.cpp',
    'src/web/web_handlers_settings.cpp',
    'src/web/web_handlers_system.cpp',
    'src/web/web_handlers_events.cpp',
    'src/web/html_pages.cpp',
    'src/web/static_resources.cpp',
    'data/index.html',
    'data/debug.html',
    'data/emulation.html',
]


def main():
    changed = []
    for path in FILES:
        try:
            src = io.open(path, encoding='utf-8').read()
        except IOError:
            continue
        # Only touch links that are not already canonical.
        fixed = re.sub(PATTERN, lambda m: BACK if m.group(0) != BACK else m.group(0), src)
        if fixed != src:
            io.open(path, 'w', encoding='utf-8').write(fixed)
            changed.append(path)
    print('rewritten:', ', '.join(changed) if changed else 'nothing (already canonical)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
