// ================= Чистая логика света и поворотников =================
// Файл без зависимостей от Arduino/ESP32: используется прошивкой и юнит-тестами
// (tests/test_light_logic.cpp), собранными из одного и того же заголовка.
#pragma once
#include <stdint.h>

// Режимы циклического света: ВЫКЛ → ДХО → Ближний → Ближний+ДХО → ВЫКЛ…
enum LightMode : uint8_t {
  LIGHT_OFF = 0,
  LIGHT_DRL = 1,
  LIGHT_LOW = 2,
  LIGHT_LOW_DRL = 3,
};

// Следующий режим цикла с переходом через последний (3 → 0).
inline uint8_t lightModeNext(uint8_t mode) {
  return (mode >= LIGHT_LOW_DRL) ? LIGHT_OFF : (uint8_t)(mode + 1);
}

// Обратная синхронизация FSM по фактическим выходам (события/веб-тумблеры
// могут переключать фару и ДХО напрямую, минуя циклическую кнопку).
inline uint8_t lightModeFromOutputs(bool headlightOn, bool drlOn) {
  if (headlightOn && drlOn) return LIGHT_LOW_DRL;
  if (headlightOn) return LIGHT_LOW;
  if (drlOn) return LIGHT_DRL;
  return LIGHT_OFF;
}

// Состояние поворотников: обе стороны могут гореть одновременно (аварийка).
struct TurnSignalState {
  bool left;
  bool right;
};

// Один клик кнопки поворотника:
//  - обе стороны уже мигают            → всё выключается (сброс аварийки);
//  - мигает противоположная            → включается аварийка (обе стороны);
//  - иначе                             → инверт своей стороны (повторный клик гасит).
inline TurnSignalState turnSignalAfterPress(TurnSignalState s, bool pressLeft) {
  if (s.left && s.right) return { false, false };
  if (pressLeft) {
    if (s.right) return { true, true }; // аварийка
    return { !s.left, false };
  }
  if (s.left) return { true, true }; // аварийка
  return { false, !s.right };
}

// ---------- Иконки дисплея 5x5 ----------
// Битмапы используются JS-эмулятором матрицы в main.cpp (синхронизированы вручную)
// и покрыты юнит-тестами. Бит 4 в строке — левый столбец иконки.

// Единая иконка режима света: одна «фара» показывает все 4 состояния цикла
// (0 ВЫКЛ — пустой корпус, 1 ДХО — центральная полоса,
//  2 БЛИЖНИЙ — корпус с заливкой, 3 БЛ+ДХО — полностью залитая).
inline uint8_t lightIconRow(uint8_t mode, uint8_t row) {
  static const uint8_t ICONS[4][5] = {
    {0b00000, 0b00000, 0b00100, 0b00000, 0b00000}, // ВЫКЛ — 1 px (потухшая точка)
    {0b00000, 0b00100, 0b01110, 0b00100, 0b00000}, // ДХО — 5 px (тонкий крест)
    {0b00000, 0b01110, 0b11111, 0b01110, 0b00000}, // БЛИЖНИЙ — 12 px (залитая фара)
    {0b00100, 0b01110, 0b11111, 0b01110, 0b00100}, // БЛ+ДХО — 14 px (фара+лучи вверх-вниз)
  };
  if (mode > LIGHT_LOW_DRL || row > 4) return 0;
  return ICONS[mode][row];
}

// Стрелки поворотников 5x5 — зеркальная пара (остриё в сторону поворота).
inline uint8_t turnArrowRow(bool left, uint8_t row) {
  static const uint8_t ARROW_L[5] = {0b00100, 0b01100, 0b11111, 0b01100, 0b00100};
  static const uint8_t ARROW_R[5] = {0b00100, 0b00110, 0b11111, 0b00110, 0b00100};
  if (row > 4) return 0;
  return left ? ARROW_L[row] : ARROW_R[row];
}

// Иконка гудка (бибика) 5x5 — динамик со звуковыми волнами.
inline uint8_t hornIconRow(uint8_t row) {
  static const uint8_t ICON[5] = {0b00100, 0b01110, 0b11111, 0b01110, 0b00100};
  if (row > 4) return 0;
  return ICON[row];
}
