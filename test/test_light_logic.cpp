// Юнит-тесты чистой логики света и поворотников (src/light_logic.h).
// Запуск на хосте (без железа): pio test -e native
#include <unity.h>
#include <stdio.h>
#include <initializer_list>

#include "core/light_logic.h"

void setUp(void) {}
void tearDown(void) {}

// ---------- Циклический режим света ----------

void test_light_mode_next_cycles_in_order(void) {
  TEST_ASSERT_EQUAL_UINT8(LIGHT_DRL, lightModeNext(LIGHT_OFF));
  TEST_ASSERT_EQUAL_UINT8(LIGHT_LOW, lightModeNext(LIGHT_DRL));
  TEST_ASSERT_EQUAL_UINT8(LIGHT_LOW_DRL, lightModeNext(LIGHT_LOW));
  TEST_ASSERT_EQUAL_UINT8(LIGHT_OFF, lightModeNext(LIGHT_LOW_DRL)); // 3 → 0
}

// Полный цикл из 4 нажатий должен вернуть исходный режим, а не «уехать».
void test_light_mode_next_full_cycle_returns_to_start(void) {
  uint8_t mode = LIGHT_OFF;
  for (int i = 0; i < 4; i++) mode = lightModeNext(mode);
  TEST_ASSERT_EQUAL_UINT8(LIGHT_OFF, mode);

  mode = LIGHT_DRL;
  for (int i = 0; i < 4; i++) mode = lightModeNext(mode);
  TEST_ASSERT_EQUAL_UINT8(LIGHT_DRL, mode);
}

// Мусорное значение не должно давать режим вне 0..3 (иначе switch в applyCycleLightMode
// не сработает и ledcWrite останется в прежнем состоянии).
void test_light_mode_next_handles_out_of_range(void) {
  uint8_t m = lightModeNext(200);
  TEST_ASSERT_EQUAL_UINT8(LIGHT_OFF, m);
  TEST_ASSERT_TRUE(m <= LIGHT_LOW_DRL);
}

// ---------- Синхронизация FSM по фактическим выходам ----------

void test_light_mode_from_outputs_mapping(void) {
  TEST_ASSERT_EQUAL_UINT8(LIGHT_OFF, lightModeFromOutputs(false, false));
  TEST_ASSERT_EQUAL_UINT8(LIGHT_DRL, lightModeFromOutputs(false, true));
  TEST_ASSERT_EQUAL_UINT8(LIGHT_LOW, lightModeFromOutputs(true, false));
  TEST_ASSERT_EQUAL_UINT8(LIGHT_LOW_DRL, lightModeFromOutputs(true, true));
}

// Инвариант: режим из lightModeFromOutputs не меняется после применения
// (тумблеры фары/ДХО синхронизируют FSM без «переключения» на другой режим).
void test_light_mode_from_outputs_is_stable(void) {
  for (uint8_t mode = LIGHT_OFF; mode <= LIGHT_LOW_DRL; mode++) {
    bool headlight = (mode == LIGHT_LOW || mode == LIGHT_LOW_DRL);
    bool drl = (mode == LIGHT_DRL || mode == LIGHT_LOW_DRL);
    TEST_ASSERT_EQUAL_UINT8(mode, lightModeFromOutputs(headlight, drl));
  }
}

// ---------- Поворотники ----------

void test_turn_off_press_left_turns_left_on(void) {
  TurnSignalState s = turnSignalAfterPress({ false, false }, true);
  TEST_ASSERT_TRUE(s.left);
  TEST_ASSERT_FALSE(s.right);
}

void test_turn_off_press_right_turns_right_on(void) {
  TurnSignalState s = turnSignalAfterPress({ false, false }, false);
  TEST_ASSERT_FALSE(s.left);
  TEST_ASSERT_TRUE(s.right);
}

// Повторное нажатие той же стороны гасит поворотник.
void test_turn_repeat_press_same_side_turns_off(void) {
  TurnSignalState s = turnSignalAfterPress({ true, false }, true);
  TEST_ASSERT_FALSE(s.left);
  TEST_ASSERT_FALSE(s.right);

  s = turnSignalAfterPress({ false, true }, false);
  TEST_ASSERT_FALSE(s.left);
  TEST_ASSERT_FALSE(s.right);
}

// Нажатие противоположной кнопки при активном поворотнике включает аварийку.
void test_turn_opposite_press_creates_hazard(void) {
  TurnSignalState s = turnSignalAfterPress({ true, false }, false); // горит левый, жмём правый
  TEST_ASSERT_TRUE(s.left);
  TEST_ASSERT_TRUE(s.right);

  s = turnSignalAfterPress({ false, true }, true); // горит правый, жмём левый
  TEST_ASSERT_TRUE(s.left);
  TEST_ASSERT_TRUE(s.right);
}

// Любая кнопка в режиме аварийки гасит оба поворотника.
void test_turn_any_press_cancels_hazard(void) {
  TurnSignalState s = turnSignalAfterPress({ true, true }, true);
  TEST_ASSERT_FALSE(s.left);
  TEST_ASSERT_FALSE(s.right);

  s = turnSignalAfterPress({ true, true }, false);
  TEST_ASSERT_FALSE(s.left);
  TEST_ASSERT_FALSE(s.right);
}

// Серия кликов: L, R (аварийка), L (сброс), R (только правый).
void test_turn_sequence(void) {
  TurnSignalState s = { false, false };
  s = turnSignalAfterPress(s, true);
  TEST_ASSERT_TRUE(s.left && !s.right);

  s = turnSignalAfterPress(s, false);
  TEST_ASSERT_TRUE(s.left && s.right);

  s = turnSignalAfterPress(s, true);
  TEST_ASSERT_FALSE(s.left || s.right);

  s = turnSignalAfterPress(s, false);
  TEST_ASSERT_TRUE(!s.left && s.right);
}

// ---------- Иконки дисплея ----------

// Битмапы света: все 4 режима должны быть разными (визуально различимы).
void test_light_icon_modes_are_unique(void) {
  // Прямое побайтовое сравнение всех пар режимов (XOR не подходит для симметричных паттернов).
  for (uint8_t i = 0; i <= LIGHT_LOW_DRL; i++) {
    for (uint8_t j = i + 1; j <= LIGHT_LOW_DRL; j++) {
      bool different = false;
      for (uint8_t row = 0; row < 5; row++) {
        if (lightIconRow(i, row) != lightIconRow(j, row)) {
          different = true;
          break;
        }
      }
      TEST_ASSERT_TRUE(different);
    }
  }
}

// Монотонность заполнения: каждый следующий режим должен быть «ярче» (≥ пикселей).
void test_light_icon_monotonic_brightness(void) {
  uint8_t pixelCount[4] = {0};
  for (uint8_t mode = 0; mode <= LIGHT_LOW_DRL; mode++) {
    for (uint8_t row = 0; row < 5; row++) {
      uint8_t bits = lightIconRow(mode, row);
      // Подсчёт установленных бит (битмап: бит 4 = левая колонка, бит 0 = правая).
      for (uint8_t b = 0; b < 5; b++) {
        if ((bits >> b) & 1) pixelCount[mode]++;
      }
    }
  }
  // Отладочный вывод для диагностики
  char msg[200];
  snprintf(msg, sizeof(msg), "Pixels: OFF=%d, DRL=%d, LOW=%d, LOW_DRL=%d",
           pixelCount[LIGHT_OFF], pixelCount[LIGHT_DRL],
           pixelCount[LIGHT_LOW], pixelCount[LIGHT_LOW_DRL]);
  TEST_MESSAGE(msg);
  
  // ВЫКЛ < ДХО < БЛИЖНИЙ < БЛ+ДХО (монотонное возрастание яркости).
  // TEST_ASSERT_LESS_THAN(threshold, actual) проверяет actual < threshold.
  TEST_ASSERT_LESS_THAN_UINT8(pixelCount[LIGHT_DRL], pixelCount[LIGHT_OFF]);
  TEST_ASSERT_LESS_THAN_UINT8(pixelCount[LIGHT_LOW], pixelCount[LIGHT_DRL]);
  TEST_ASSERT_LESS_THAN_UINT8(pixelCount[LIGHT_LOW_DRL], pixelCount[LIGHT_LOW]);
}

// Стрелки поворотников: правая = зеркальное отражение левой по вертикальной оси.
void test_turn_arrows_are_mirrored(void) {
  for (uint8_t row = 0; row < 5; row++) {
    uint8_t left = turnArrowRow(true, row);
    uint8_t right = turnArrowRow(false, row);
    // Каждая строка правой стрелки — зеркало левой (обратный порядок бит 0..4).
    uint8_t leftMirror = 0;
    for (uint8_t b = 0; b < 5; b++) {
      if (left & (1 << b)) leftMirror |= (1 << (4 - b));
    }
    TEST_ASSERT_EQUAL_UINT8(leftMirror, right);
  }
}

// Стрелки симметричны относительно горизонтальной оси (row 2 — центр).
void test_turn_arrows_vertical_symmetry(void) {
  for (bool left : {true, false}) {
    TEST_ASSERT_EQUAL_UINT8(turnArrowRow(left, 0), turnArrowRow(left, 4));
    TEST_ASSERT_EQUAL_UINT8(turnArrowRow(left, 1), turnArrowRow(left, 3));
    // Центральная строка 2 — самая широкая (0b11111 — полная).
    TEST_ASSERT_EQUAL_UINT8(0b11111, turnArrowRow(left, 2));
  }
}

// Обе стрелки помещаются в 5×5 (не выходят за границы).
void test_turn_arrows_within_bounds(void) {
  for (bool left : {true, false}) {
    for (uint8_t row = 0; row < 5; row++) {
      uint8_t bits = turnArrowRow(left, row);
      TEST_ASSERT_LESS_OR_EQUAL_UINT8(0b11111, bits); // не больше 5 бит
    }
    // За пределами 5 строк — только нули.
    TEST_ASSERT_EQUAL_UINT8(0, turnArrowRow(left, 5));
    TEST_ASSERT_EQUAL_UINT8(0, turnArrowRow(left, 255));
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_light_mode_next_cycles_in_order);
  RUN_TEST(test_light_mode_next_full_cycle_returns_to_start);
  RUN_TEST(test_light_mode_next_handles_out_of_range);
  RUN_TEST(test_light_mode_from_outputs_mapping);
  RUN_TEST(test_light_mode_from_outputs_is_stable);
  RUN_TEST(test_turn_off_press_left_turns_left_on);
  RUN_TEST(test_turn_off_press_right_turns_right_on);
  RUN_TEST(test_turn_repeat_press_same_side_turns_off);
  RUN_TEST(test_turn_opposite_press_creates_hazard);
  RUN_TEST(test_turn_any_press_cancels_hazard);
  RUN_TEST(test_turn_sequence);
  RUN_TEST(test_light_icon_modes_are_unique);
  RUN_TEST(test_light_icon_monotonic_brightness);
  RUN_TEST(test_turn_arrows_are_mirrored);
  RUN_TEST(test_turn_arrows_vertical_symmetry);
  RUN_TEST(test_turn_arrows_within_bounds);
  return UNITY_END();
}
