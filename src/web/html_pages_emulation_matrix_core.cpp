#include "web/html_pages_emulation.h"

// JS: движок 16x32 LED-матрицы (примитивы, шрифты, спрайты, boot-анимация, renderCanvas, updateMatrixDisplay)
// Часть страницы эмулятора (этап D: разбиение html_pages_emulation.cpp).
static const char part[] PROGMEM = R"rawliteral(<script>
const SETTINGS_MENU = [
  { id: 'in_min', name: 'THROTTLE IN MIN', unit: 'V', step: 0.05, min: 0.0, max: 4.5, val: cfgThrottleInMin },
  { id: 'in_max', name: 'THROTTLE IN MAX', unit: 'V', step: 0.05, min: 0.5, max: 5.0, val: cfgThrottleInMax },
  { id: 'out_min', name: 'THROTTLE OUT MIN', unit: 'V', step: 0.05, min: 0.0, max: 4.5, val: cfgThrottleOutMin },
  { id: 'out_max', name: 'THROTTLE OUT MAX', unit: 'V', step: 0.05, min: 0.5, max: 5.0, val: cfgThrottleOutMax }
];

// Битмапы иконок (ICON_BRAKE, ICON_HORN, ICON_LIGHT_MODES, TURN_ARROW_5X5_L/R,
// ICON_PEDAL_FRAMES) вынесены в /matrix_graphics.js.
function drawTurnArrow5x5(left, sr, sc) {
  const g = left ? TURN_ARROW_5X5_L : TURN_ARROW_5X5_R;
  for (let r = 0; r < 5; r++) {
    for (let c = 0; c < 5; c++) {
      if ((g[r] >> (4 - c)) & 1) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}

function clearMatrix() {
  for (let r = 0; r < MATRIX_ROWS; r++) matrixGrid[r].fill(0);
}
function setMatrixPixel(r, c, val) {
  if (r >= 0 && r < MATRIX_ROWS && c >= 0 && c < MATRIX_COLS) matrixGrid[r][c] = val ? 1 : 0;
}
function drawBigLetter(k, sr, sc) {
  const g = BIG_GLYPHS[k]; if (!g) return;
  for (let r = 0; r < g.length; r++) {
    for (let c = 0; c < g[r].length; c++) {
      if (g[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}
function drawDigit3x5ToBuffer(dChar, buf) {
  const g = DIGIT_GLYPHS[dChar];
  if (!g) return;
  for (let c = 0; c < 3; c++) {
    for (let r = 0; r < 5; r++) {
      if ((g[c] >> r) & 1) buf[r][c] = 1;
    }
  }
}

function drawArrow5x2(arrowMatrix, sr, sc, hollowCenter = false) {
  for (let r = 0; r < arrowMatrix.length; r++) {
    for (let c = 0; c < arrowMatrix[r].length; c++) {
      if (hollowCenter && r === 0 && c === 2) continue;
      if (arrowMatrix[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}
function drawIcon5x5(b, sr, sc) {
  for (let r = 0; r < 5; r++) {
    for (let c = 0; c < 5; c++) {
      if ((b[r] >> (4 - c)) & 1) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}
function drawGear7x7(sr, sc) {
  for (let r = 0; r < 7; r++) {
    for (let c = 0; c < 7; c++) {
      if (ICON_GEAR_7X7[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}

// ================= BOOT ANIMATION: PAC-MAN INTRO =================
let bootAnimationActive = false;
let bootAnimationComplete = false;
let bootAnimationTimeoutId = null;

// Pac-Man 16x16 (БОЛЬШОЙ, рот вправо →, с глазом)
const PACMAN_OPEN = [
  [0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0],
  [0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0],
  [0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,0,0,1,1,1,1,1,1,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0],
  [0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0],
  [0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0]
];

const PACMAN_CLOSED = [
  [0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0],
  [0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0],
  [0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0],
  [0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0],
  [0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0]
];

// Велосипедист 7x7 (палочковый человечек с колёсами)
const BIKER_FRAME1 = [
  [0,0,0,1,0,0,0],
  [0,0,1,1,1,0,0],
  [0,0,0,1,0,0,0],
  [0,1,0,1,0,1,0],
  [0,0,1,0,1,0,0],
  [0,0,0,0,0,0,0],
  [0,1,0,0,0,1,0]
];

const BIKER_FRAME2 = [
  [0,0,0,1,0,0,0],
  [0,0,1,1,1,0,0],
  [0,0,0,1,0,0,0],
  [0,1,0,1,0,1,0],
  [0,0,1,0,1,0,0],
  [0,0,0,0,0,0,0],
  [1,0,0,0,0,0,1]
];

// Italic/Skewed font для "BIKE" (наклон вправо, читаемый 3x5 с uniform shear)
const FONT_ITALIC = {
  'B': [
    [0,0,1,1,1],
    [0,1,0,1,0],
    [1,1,1,1,0],
    [1,0,1,0,1],
    [1,1,1,1,0]
  ],
  'I': [
    [0,0,1],
    [0,1,1],
    [1,1,1],
    [1,0,1],
    [1,0,0]
  ],
  'K': [
    [0,0,1,0,0],
    [0,1,0,1,0],
    [1,1,1,0,0],
    [1,0,1,0,1],
    [1,0,0,1,0]
  ],
  'E': [
    [0,0,1,1,1],
    [0,1,0,1,0],
    [1,1,1,1,0],
    [1,0,1,0,1],
    [1,1,1,1,1]
  ]
};

// Font 4x5 для "OPEN" (компактный, читаемый)
const FONT_4X5 = {
  'O': [
    [0,1,1,0],
    [1,0,0,1],
    [1,0,0,1],
    [1,0,0,1],
    [0,1,1,0]
  ],
  'P': [
    [1,1,1,0],
    [1,0,0,1],
    [1,1,1,0],
    [1,0,0,0],
    [1,0,0,0]
  ],
  'E': [
    [1,1,1,1],
    [1,0,0,0],
    [1,1,1,0],
    [1,0,0,0],
    [1,1,1,1]
  ],
  'N': [
    [1,0,0,1],
    [1,1,0,1],
    [1,0,1,1],
    [1,0,0,1],
    [1,0,0,1]
  ]
};

// Font 7x10 для "BIKE" (ОГРОМНЫЙ, жирный)
const FONT_7X10 = {
  'B': [
    [1,1,1,1,1,0,0],
    [1,1,1,1,1,0,0],
    [1,1,0,0,1,1,0],
    [1,1,0,0,1,1,0],
    [1,1,1,1,1,0,0],
    [1,1,1,1,1,0,0],
    [1,1,0,0,1,1,0],
    [1,1,0,0,1,1,0],
    [1,1,1,1,1,0,0],
    [1,1,1,1,1,0,0]
  ],
  'I': [
    [1,1,1],
    [1,1,1],
    [0,1,0],
    [0,1,0],
    [0,1,0],
    [0,1,0],
    [0,1,0],
    [0,1,0],
    [1,1,1],
    [1,1,1]
  ],
  'K': [
    [1,1,0,0,1,1,0],
    [1,1,0,0,1,1,0],
    [1,1,0,1,1,0,0],
    [1,1,1,1,0,0,0],
    [1,1,1,0,0,0,0],
    [1,1,1,0,0,0,0],
    [1,1,1,1,0,0,0],
    [1,1,0,1,1,0,0],
    [1,1,0,0,1,1,0],
    [1,1,0,0,1,1,0]
  ],
  'E': [
    [1,1,1,1,1,1],
    [1,1,1,1,1,1],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,1,1,1,0],
    [1,1,1,1,1,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,1,1,1,1],
    [1,1,1,1,1,1]
  ]
};

function drawText4x5(text, sr, sc) {
  let offsetC = sc;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    const glyph = FONT_4X5[ch];
    if (glyph) {
      for (let r = 0; r < glyph.length; r++) {
        for (let c = 0; c < glyph[r].length; c++) {
          if (glyph[r][c]) setMatrixPixel(sr + r, offsetC + c, 1);
        }
      }
      offsetC += 5; // 4px буква + 1px gap
    }
  }
}

function drawText7x10(text, sr, sc) {
  const ROW_SHIFTS = [0, 0, 0, 1, 1, 2, 2, 3, 3, 3]; // Italic: верхние строки ровные, нижние сдвинуты вправо
  let offsetC = sc;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    const glyph = FONT_7X10[ch];
    if (glyph) {
      for (let r = 0; r < glyph.length; r++) {
        const shift = ROW_SHIFTS[r] || 0;
        for (let c = 0; c < glyph[r].length; c++) {
          if (glyph[r][c]) setMatrixPixel(sr + r, offsetC + c + shift, 1);
        }
      }
      offsetC += glyph[0].length + 1; // ширина буквы + 1px gap
    }
  }
}

// Elevator animation state
let animStartLvl = 0;
let animTargetLvl = 0;
let animStartTime = 0;
const ANIM_DURATION_MS = 250;

// Функции для рисования элементов загрузочной анимации
function drawPacman(sr, sc, mouthOpen) {
  const sprite = mouthOpen ? PACMAN_OPEN : PACMAN_CLOSED;
  for (let r = 0; r < sprite.length; r++) {
    for (let c = 0; c < sprite[r].length; c++) {
      if (sprite[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}

function drawBiker(sr, sc, frame) {
  const sprite = (frame % 2 === 0) ? BIKER_FRAME1 : BIKER_FRAME2;
  for (let r = 0; r < sprite.length; r++) {
    for (let c = 0; c < sprite[r].length; c++) {
      if (sprite[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}

function drawItalicChar(ch, sr, sc) {
  const g = FONT_ITALIC[ch];
  if (!g) return;
  for (let r = 0; r < g.length; r++) {
    for (let c = 0; c < g[r].length; c++) {
      if (g[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}

function fillRandomDots(density) {
  for (let r = 0; r < MATRIX_ROWS; r++) {
    for (let c = 0; c < MATRIX_COLS; c++) {
      if (Math.random() < density) setMatrixPixel(r, c, 1);
    }
  }
}

function renderCanvas() {
  if (!canvas || !ctx) return;
  const cellW = canvas.width / MATRIX_COLS;
  const cellH = canvas.height / MATRIX_ROWS;
  const radius = Math.min(cellW, cellH) * 0.42;

  ctx.fillStyle = "#0c0c0c";
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  ctx.strokeStyle = "#1a1a1a";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(0, 8 * cellH);
  ctx.lineTo(canvas.width, 8 * cellH);
  ctx.stroke();

  for (let r = 0; r < MATRIX_ROWS; r++) {
    for (let c = 0; c < MATRIX_COLS; c++) {
      const cx = c * cellW + cellW / 2;
      const cy = r * cellH + cellH / 2;
      ctx.beginPath();
      ctx.arc(cx, cy, radius, 0, Math.PI * 2);
      if (matrixGrid[r][c] === 1) {
        ctx.fillStyle = "#ff8c00";
        ctx.shadowColor = "#ff7700";
        ctx.shadowBlur = 8;
        ctx.fill();
        ctx.shadowBlur = 0;
      } else {
        ctx.fillStyle = "#1e140a";
        ctx.fill();
      }
    }
  }
}

)rawliteral";

PGM_P getEmulationMatrixCore() { return part; }
size_t getEmulationMatrixCoreLen() { return sizeof(part) - 1; }
