// Матрица: шрифты, стрелки и иконки эмулятора (вынесено из html_pages.cpp).
// Синхронизировано с src/light_logic.h (lightIconRow / turnArrowRow / hornIconRow).

const DIGIT_GLYPHS = {
  '0': [0x1F, 0x11, 0x1F],
  '1': [0x00, 0x1F, 0x00],
  '2': [0x1D, 0x15, 0x17],
  '3': [0x15, 0x15, 0x1F],
  '4': [0x07, 0x04, 0x1F],
  '5': [0x17, 0x15, 0x1D],
  '6': [0x1F, 0x15, 0x1D],
  '7': [0x01, 0x19, 0x07],
  '8': [0x1F, 0x15, 0x1F],
  '9': [0x17, 0x15, 0x1F]
};

const ARROW_UP = [
  [0,0,1,0,0],
  [0,1,1,1,0]
];
const ARROW_DOWN = [
  [0,1,1,1,0],
  [0,0,1,0,0]
];

const ICON_GEAR_7X7 = [
  [0,1,0,1,0,1,0],
  [1,1,1,1,1,1,1],
  [0,1,0,0,0,1,0],
  [1,1,0,0,0,1,1],
  [0,1,0,0,0,1,0],
  [1,1,1,1,1,1,1],
  [0,1,0,1,0,1,0]
];

const ICON_CHECK_5X5 = [
  0b00000,
  0b00001,
  0b00010,
  0b10100,
  0b01000
];

// Тормоз: квадратные скобки с точкой внутри.
const ICON_BRAKE = [0b10001, 0b10001, 0b10101, 0b10001, 0b10001];
// Гудок (бибика): динамик со звуковыми волнами.
const ICON_HORN = [0b00100, 0b01110, 0b11111, 0b01110, 0b00100];
// Единая иконка света 5x5: одна «фара» показывает все 4 режима цикла
// (0 ВЫКЛ — пустой корпус, 1 ДХО — центральная полоса,
//  2 БЛИЖНИЙ — корпус с заливкой, 3 БЛ+ДХО — полностью залитая).
// Битмапы синхронизированы с src/light_logic.h (lightIconRow) — покрыты юнит-тестами.
const ICON_LIGHT_MODES = [
  [0b00000, 0b00000, 0b00100, 0b00000, 0b00000],
  [0b00000, 0b00100, 0b01110, 0b00100, 0b00000],
  [0b00000, 0b01110, 0b11111, 0b01110, 0b00000],
  [0b00100, 0b01110, 0b11111, 0b01110, 0b00100]
];
// Стрелки поворотников 5x5 — зеркальная пара (остриё в сторону поворота).
// Синхронизированы с src/light_logic.h (turnArrowRow).
const TURN_ARROW_5X5_L = [0b00100, 0b01100, 0b11111, 0b01100, 0b00100];
const TURN_ARROW_5X5_R = [0b00100, 0b00110, 0b11111, 0b00110, 0b00100];
const ICON_PEDAL_FRAMES = [
  [0b11000, 0b01000, 0b00100, 0b00010, 0b00011],
  [0b00000, 0b11000, 0b01110, 0b00011, 0b00000],
  [0b00000, 0b00000, 0b11111, 0b00000, 0b00000],
  [0b00000, 0b00011, 0b01110, 0b11000, 0b00000],
  [0b00011, 0b00010, 0b00100, 0b01000, 0b11000],
  [0b00000, 0b00011, 0b01110, 0b11000, 0b00000],
  [0b00000, 0b00000, 0b11111, 0b00000, 0b00000],
  [0b00000, 0b11000, 0b01110, 0b00011, 0b00000]
];

// 3x5 font bit patterns (columns 0..2)
const FONT_3X5 = {
  'A': [0x1E, 0x05, 0x1E],
  'B': [0x1F, 0x15, 0x0A],
  'C': [0x0E, 0x11, 0x11],
  'D': [0x1F, 0x11, 0x0E],
  'E': [0x1F, 0x15, 0x11],
  'F': [0x1F, 0x05, 0x01],
  'G': [0x0E, 0x11, 0x1D],
  'H': [0x1F, 0x04, 0x1F],
  'I': [0x11, 0x1F, 0x11],
  'J': [0x08, 0x10, 0x0F],
  'K': [0x1F, 0x04, 0x1B],
  'L': [0x1F, 0x10, 0x10],
  'M': [0x1F, 0x02, 0x1F],
  'N': [0x1F, 0x06, 0x1F],
  'O': [0x0E, 0x11, 0x0E],
  'P': [0x1F, 0x05, 0x02],
  'Q': [0x0E, 0x11, 0x1E],
  'R': [0x1F, 0x05, 0x1A],
  'S': [0x12, 0x15, 0x09],
  'T': [0x01, 0x1F, 0x01],
  'U': [0x0F, 0x10, 0x0F],
  'V': [0x07, 0x18, 0x07],
  'W': [0x1F, 0x08, 0x1F],
  'X': [0x1B, 0x04, 0x1B],
  'Y': [0x03, 0x1C, 0x03],
  'Z': [0x19, 0x15, 0x13],
  '0': [0x1F, 0x11, 0x1F],
  '1': [0x00, 0x1F, 0x00],
  '2': [0x1D, 0x15, 0x17],
  '3': [0x15, 0x15, 0x1F],
  '4': [0x07, 0x04, 0x1F],
  '5': [0x17, 0x15, 0x1D],
  '6': [0x1F, 0x15, 0x1D],
  '7': [0x01, 0x19, 0x07],
  '8': [0x1F, 0x15, 0x1F],
  '9': [0x17, 0x15, 0x1F],
  '.': [0x00, 0x10, 0x00],
  ':': [0x00, 0x0A, 0x00],
  '-': [0x04, 0x04, 0x04],
  '_': [0x10, 0x10, 0x10],
  '/': [0x18, 0x06, 0x01],
  ' ': [0x00, 0x00, 0x00]
};

function drawChar3x5(ch, sr, sc, clipMinR = 0, clipMaxR = 15, clipMinC = 0, clipMaxC = 31) {
  const g = FONT_3X5[ch.toUpperCase()] || FONT_3X5[' '];
  for (let c = 0; c < 3; c++) {
    const colBits = g[c];
    for (let r = 0; r < 5; r++) {
      if ((colBits >> r) & 1) {
        const tr = sr + r;
        const tc = sc + c;
        if (tr >= clipMinR && tr <= clipMaxR && tc >= clipMinC && tc <= clipMaxC) {
          setMatrixPixel(tr, tc, 1);
        }
      }
    }
  }
}

function drawText3x5(str, sr, sc, clipMinR = 0, clipMaxR = 15, clipMinC = 0, clipMaxC = 31) {
  let currC = sc;
  for (let i = 0; i < str.length; i++) {
    const ch = str[i];
    if (ch === '.') {
      // Точка прилегает к предыдущей цифре и к следующей цифре.
      drawChar3x5('.', sr, currC - 2, clipMinR, clipMaxR, clipMinC, clipMaxC);
    } else {
      drawChar3x5(ch, sr, currC, clipMinR, clipMaxR, clipMinC, clipMaxC);
      currC += 4;
    }
  }
}

