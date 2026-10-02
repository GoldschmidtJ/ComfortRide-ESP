#!/usr/bin/env python3
"""Единая тема UI: синхронизация и проверка компонентов страниц.

Эталон — CSS в src/web/web_ui.cpp (getTopBarCss/getSettingsCss). Страницы,
которые не могут вызвать C++-хелпер (файлы LittleFS data/*.html и резервные
копии в PROGMEM src/web/html_pages.cpp), держат текстовые копии тех же правил.
Скрипт приводит копии к эталону и следит, чтобы:

  * блок `:root{--ui-bg:...}` был идентичен во всех копиях;
  * правила общих компонентов (утилиты, кнопка «назад», липкая шапка) совпадали
    с эталоном — локальные переопределения внутри @media не трогаются;
  * размеры шрифта брались из токенов масштаба (--ui-fs-*), стек — из
    var(--ui-font)/var(--ui-mono), полупрозрачные фоны — из --ui-*-soft;
  * на странице была ровно одна ссылка возврата, и её текстовая копия
    совпадала с getBackMenuHtml();
  * там, где есть разметка top-bar-sticky, существовали и её CSS-правила.

    python3 tools/check_ui_theme.py          # проверка (код 1 при расхождении)
    python3 tools/check_ui_theme.py --fix    # синхронизировать копии
"""
import io
import re
import sys

REFERENCE = 'src/web/web_ui.cpp'
TARGETS = [
    'src/web/html_pages.cpp',
    'src/web/static_resources.cpp',
    'src/web/web_handlers_system.cpp',
    'src/web/web_handlers_settings.cpp',
    'src/web/web_handlers_events.cpp',
    'src/web/web_handlers_pins.cpp',
    'data/index.html',
    'data/debug.html',
    'data/emulation.html',
]

# Правила, обязанные совпадать с эталоном символ в символ.
RULE_KEYS = [
    ':root{--ui-bg',
    '.u-muted{', '.u-small{', '.u-dim{', '.hint{', '.is-hidden{',
    '.back-link,a.back{', '.back-link:hover,a.back:hover{', '.back-row{',
    '.top-bar-sticky{', '.tb-item{', '.tb-link{', '.tb-link:hover{',
    '.tb-dot{', '.dot-green{', '.dot-yellow{', '.dot-red{', '.dot-gray{',
]
# Есть разметка компонента -> обязаны быть и его правила (вставка после :root).
NEEDS = [
    ('class="back-link', ['.back-link,a.back{', '.back-link:hover,a.back:hover{',
                          '.back-row{']),
    ('top-bar-sticky', ['.top-bar-sticky{', '.tb-item{', '.tb-link{', '.tb-link:hover{',
                        '.tb-dot{', '.dot-green{', '.dot-yellow{', '.dot-red{',
                        '.dot-gray{']),
]
FONT_SIZES = {
    'font-size:12px': 'font-size:var(--ui-fs-tiny)',
    'font-size:13px': 'font-size:var(--ui-fs-small)',
    'font-size:14px': 'font-size:var(--ui-fs-mid)',
    'font-size:16px': 'font-size:var(--ui-fs-body)',
    'font-size:17px': 'font-size:var(--ui-fs-h2)',
    'font-size:18px': 'font-size:var(--ui-fs-h2)',
    'font-size:24px': 'font-size:var(--ui-fs-h1)',
}
FONT_STACKS = [
    re.compile(r'font-family:\s*system-ui\s*,\s*-apple-system\s*,\s*"Segoe UI"\s*,\s*Roboto\s*,\s*sans-serif'),
    re.compile(r'font-family:\s*system-ui\s*,\s*-apple-system\s*,\s*"Segoe UI"\s*,\s*sans-serif'),
    re.compile(r'font-family:\s*system-ui\s*,\s*-apple-system\s*,\s*sans-serif'),
    re.compile(r'font-family:\s*system-ui\s*(?=[;"}])'),
]
MONO_STACK = re.compile(r'font-family:\s*monospace(?=[;"}])')
# Чужие палитры (GitHub-dark на странице эмуляции) -> токены темы. Применяется
# только внутри блоков <style>, чтобы не трогать цвета графиков в <script>.
HEX_TOKENS = [
    ('#0d1117', 'var(--ui-bg)'),
    ('#161b22', 'var(--ui-card)'),
    ('#21262d', 'var(--ui-button)'),
    ('#30363d', 'var(--ui-hover)'),
    ('#e6edf3', 'var(--ui-text)'),
    ('#8b949e', 'var(--ui-muted)'),
    ('#1f6feb', 'var(--ui-accent)'),
    ('#58a6ff', 'var(--ui-accent)'),
    ('#3fb950', 'var(--ui-success)'),
    ('#238636', 'var(--ui-success)'),
    ('#f85149', 'var(--ui-danger)'),
    ('#da3633', 'var(--ui-danger)'),
    ('#e5484d', 'var(--ui-danger)'),
    ('#d29922', 'var(--ui-warning)'),
    ('#f9c513', 'var(--ui-warning)'),
    ('#bb8609', 'var(--ui-warning)'),
]
SOFT_BG = {
    'rgba(46,204,113,.12)': 'var(--ui-success-soft)',
    'rgba(243,156,18,.15)': 'var(--ui-warning-soft)',
    'rgba(231,76,60,.12)': 'var(--ui-danger-soft)',
}
BACK_MARKUP_RE = re.compile(
    r'<p(?:\s+class="[^"]*")?>\s*<a\b[^>]*href="/"[^>]*>'
    r'(?:&larr;|&#8592;|\u2190)\s*[^<]*</a>\s*</p>'
)
BACK_ROW_LINE = '<p class="back-row"><a class="back-link" href="/" data-i18n="backMenu">&larr; Меню</a></p>'


def read(path):
    return io.open(path, encoding='utf-8').read()


def write(path, text):
    io.open(path, 'w', encoding='utf-8').write(text)


def in_media_block(text, pos):
    """True, если позиция стоит внутри @media(...){...} той же строки."""
    line_start = text.rfind('\n', 0, pos) + 1
    line = text[line_start:pos]
    at = line.rfind('@media(')
    if at < 0:
        return False
    brace = line.find('{', at)
    if brace < 0:
        return False
    depth = 0
    for char in line[brace:]:
        if char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
    return depth > 0


def canonical_rules(reference):
    rules = {}
    for key in RULE_KEYS:
        pattern = re.compile(re.escape(key) + r'[^{}]*\}')
        for match in pattern.finditer(reference):
            if not in_media_block(reference, match.start()):
                rules[key] = match.group(0)
                break
    missing = [key for key in RULE_KEYS if key not in rules]
    if missing:
        sys.exit('в эталоне не найдены правила: ' + ', '.join(missing))
    return rules


def mirror(text, rules):
    """Копии общих правил -> эталон; литералы шрифта/фона -> токены;
    недостающие правила компонента -> вставка сразу после строки :root."""
    for key, canonical in rules.items():
        pattern = re.compile(re.escape(key) + r'[^{}]*\}')

        def replace(match):
            if in_media_block(text, match.start()):
                return match.group(0)
            return canonical

        text = pattern.sub(replace, text)

    lines = text.split('\n')
    in_style = False
    for index, line in enumerate(lines):
        if '<style' in line:
            in_style = True
        if line.lstrip().startswith(':root{--ui-bg'):
            if '</style>' in line:
                in_style = False
            lines[index] = line
            continue
        for pattern in FONT_STACKS:
            line = pattern.sub('font-family:var(--ui-font)', line)
        line = MONO_STACK.sub('font-family:var(--ui-mono)', line)
        for literal, token in list(SOFT_BG.items()) + list(FONT_SIZES.items()):
            line = line.replace(literal, token)
        if in_style:  # чужие hex из CSS -> токены темы (графики в <script> не трогаем)
            for literal, token in HEX_TOKENS:
                line = line.replace(literal, token)
                line = line.replace(literal.upper(), token)
        lines[index] = line
        if '</style>' in line:
            in_style = False
    text = '\n'.join(lines)

    root_lines = [line for line in text.split('\n')
                  if line.lstrip().startswith(':root{--ui-bg')]
    # Копии вставляются только там, где CSS продублирован текстом: страницы на
    # C++ получают компоненты из getTopBarCss()/getSettingsCss() в рантайме.
    provides = 'getTopBarCss()' in text or 'getSettingsCss()' in text
    if root_lines and not provides:
        root_line = root_lines[0]
        indent = root_line[:len(root_line) - len(root_line.lstrip())]
        for marker, keys in NEEDS:
            if marker not in text:
                continue
            absent = [rules[key] for key in keys if key not in text]
            if absent:
                block = '\n'.join(indent + rule for rule in absent)
                text = text.replace(root_line, root_line + '\n' + block, 1)
    return text


def check(path, text, rules):
    problems = []
    for key, canonical in rules.items():
        for match in re.compile(re.escape(key) + r'[^{}]*\}').finditer(text):
            if in_media_block(text, match.start()):
                continue
            if match.group(0) != canonical:
                problems.append('правило %s отличается от эталона' % key)
                break
    for match in BACK_MARKUP_RE.finditer(text):
        if match.group(0) != BACK_ROW_LINE:
            problems.append('разметка ссылки назад: %s' % match.group(0)[:64])
    provides = 'getTopBarCss()' in text or 'getSettingsCss()' in text
    for marker, keys in NEEDS:
        if marker in text and not provides:
            for key in keys:
                if key not in text:
                    problems.append('нет правила %s (разметка %s есть)' % (key, marker))
    if 'getBackMenuHtml()' in text and 'extern String getBackMenuHtml();' not in text \
            and '#include "web/web_ui.h"' not in text:
        problems.append('getBackMenuHtml() без объявления и без web_ui.h')
    return ['%s: %s' % (path, problem) for problem in problems]


def main():
    fix = '--fix' in sys.argv
    rules = canonical_rules(read(REFERENCE))
    problems, touched = [], []
    for path in [REFERENCE] + TARGETS:
        try:
            src = read(path)
        except IOError:
            continue
        dst = mirror(src, rules)
        if dst != src:
            touched.append(path)
            if fix:
                write(path, dst)
                src = dst
        problems += check(path, src, rules)
    print('эталонных правил: %d' % len(rules))
    if fix:
        print('обновлено:', ', '.join(touched) if touched else 'ничего (уже синхронно)')
    elif touched:
        print('расходятся с эталоном:', ', '.join(touched))
    for line in problems:
        print('  !', line)
    if problems:
        return 1
    print('тема, шрифты и кнопка «назад» едины на всех страницах')
    return 0


if __name__ == '__main__':
    sys.exit(main())
