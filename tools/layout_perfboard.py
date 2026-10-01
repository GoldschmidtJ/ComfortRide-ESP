#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Генератор макетки perfboard 60x80 мм (22x27 отверстий, шаг 2.54 мм)
для KiCad 10 (pcbnew Python API).

Запуск:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 layout_perfboard.py

Результат: perfboard_layout.kicad_pcb — открыть в PCB Editor (KiCad).
Сетка: 2 колонки шин (питание/земля) по краям + поле перфорации 22x27.
"""
import os
import sys

KIAD_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_FILE = os.path.join(KIAD_DIR, "perfboard_layout.kicad_pcb")

# Ищем pcbnew от KiCad (системный python его не видит)
def _load_pcbnew():
    try:
        import pcbnew  # noqa
        return pcbnew
    except ImportError:
        candidates = [
            "/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3",
        ]
        for c in candidates:
            if os.path.exists(c):
                os.execv(c, [c] + sys.argv)  # перезапуск под kicad-python
        raise

pcbnew = _load_pcbnew()

# ------------------ Геометрия ------------------
PITCH = 2.54  # мм, стандартный шаг макетки
COLS = 22     # отверстий по горизонтали (60 мм)
ROWS = 27     # отверстий по вертикали (80 мм)
MARGIN = 6.0  # отступ рамки от края платы

BOARD_W = 60.0
BOARD_H = 80.0

# Сетка отверстий начинается с отступа 3 мм от края
X0 = 3.0
Y0 = 3.0

# ------------------ Слои ------------------
L_EDGE = pcbnew.Edge_Cuts
L_FSilk = pcbnew.F_SilkS
L_FCu = pcbnew.F_Cu
L_FSilk = pcbnew.F_SilkS

# ------------------ Создание платы ------------------
board = pcbnew.NewBoard(OUT_FILE)

# Виртуальный footprint-контейнер для всех пятачков сетки
holes_fp = pcbnew.FOOTPRINT(board)  # без библиотеки, чистый контейнер
holes_fp.SetReference("GRID")
board.Add(holes_fp)

def mm(v):
    return pcbnew.FromMM(v)

def add_hole(x_mm, y_mm, pad_size=1.7, drill=1.0, net=0):
    """Одиночное отверстие-пятачок (THT pad без footprint)."""
    p = pcbnew.PAD(holes_fp)
    p.SetPosition(pcbnew.VECTOR2I(mm(x_mm), mm(y_mm)))
    p.SetSize(pcbnew.VECTOR2I(mm(pad_size), mm(pad_size)))
    p.SetDrillSize(pcbnew.VECTOR2I(mm(drill), mm(drill)))
    p.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
    p.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
    p.SetPinFunction("hole")
    holes_fp.Add(p)
    return p

def add_label(x_mm, y_mm, text, size=1.0, thickness=0.15, layer=L_FSilk):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pcbnew.VECTOR2I(mm(x_mm), mm(y_mm)))
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(thickness))
    t.SetLayer(layer)
    board.Add(t)
    return t

def add_line(x1, y1, x2, y2, layer=L_EDGE, width=0.15):
    seg = pcbnew.PCB_SHAPE(board)
    seg.SetShape(pcbnew.S_SEGMENT)
    seg.SetStart(pcbnew.VECTOR2I(mm(x1), mm(y1)))
    seg.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(y2)))
    Edge = pcbnew.Edge_Cuts
    seg.SetLayer(layer)
    seg.SetWidth(mm(width))
    board.Add(seg)
    return seg

# ------------------ Рамка платы ------------------
add_line(0, 0, BOARD_W, 0, L_EDGE)
add_line(0, 0, 0, BOARD_H, L_EDGE)
add_line(0, BOARD_H, BOARD_W, BOARD_H, L_EDGE)
add_line(BOARD_W, 0, BOARD_W, BOARD_H, L_EDGE)

# ------------------ Сетка отверстий ------------------
# Крайние 2 колонки слева и справа = шины питания/земли (как на perfboard с шинами)
for r in range(ROWS):
    y = Y0 + r * PITCH
    for c in range(COLS):
        x = X0 + c * PITCH
        add_hole(x, y)

# ------------------ Подписи шин ------------------
# Левая шина (колонки 0-1): GND
for r in range(0, ROWS, 4):
    y = Y0 + r * PITCH
    add_label(X0 - 2.2, y, "GND", size=0.8, thickness=0.12)

# Правая шина (колонки 20-21): +5V
for r in range(0, ROWS, 4):
    y = Y0 + r * PITCH
    add_label(X0 + (COLS - 1) * PITCH + 2.2, y, "+5V", size=0.8, thickness=0.12)

# ------------------ Компоненты ------------------
# Все позиции задаются в координатах сетки (col,row), 0-based
# col: 0..21, row: 0..26

COMPONENTS = [
    # (имя, col, row, ширина в отверстиях, высота в отверстиях, описание)
    ("ESP32",   2,  2, 10, 12, "ESP32 DevKit (широкий, 38 pin)"),
    ("MCP6002", 14, 3, 4,  2,  "ОУ MCP6002 DIP-8"),
    ("R1-R5",   14, 8, 5,  1,  "Резисторы 1/4W (стоя)"),
    ("C1-C2",   14, 10, 4, 1,  "Конденсаторы керамика"),
    ("PWR",     2,  16, 4, 2,  "Разъём питания 5V/GND"),
    ("SENSOR",  14, 14, 6, 3,  "Клеммник датчиков (газ/тормоз/PAS)"),
    ("DISPLAY", 2,  20, 10, 6, "Дисплей 8x32 x2 (MAX7219) — разъём 5pin x2"),
    ("JOY",     14, 20, 4, 3,  "Джойстик 5 pin (VCC GND VRx VRy SW)"),
]

for name, c0, r0, w, h, desc in COMPONENTS:
    x = X0 + c0 * PITCH
    y = Y0 + r0 * PITCH
    # Прямоугольник шелкографии зоны
    x2 = X0 + (c0 + w - 1) * PITCH
    y2 = Y0 + (r0 + h - 1) * PITCH
    add_line(x, y, x2, y, L_FSilk, 0.2)
    add_line(x2, y, x2, y2, L_FSilk, 0.2)
    add_line(x2, y2, x, y2, L_FSilk, 0.2)
    add_line(x, y2, x, y, L_FSilk, 0.2)
    add_label(x, y - 2.0, name, size=1.2, thickness=0.2)

# ------------------ Сохранение ------------------
board.Save(OUT_FILE)
print("OK:", OUT_FILE)
