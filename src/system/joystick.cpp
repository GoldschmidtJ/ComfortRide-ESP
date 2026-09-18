#include "system/joystick.h"
#include "core/pas.h"
#include "core/cruise.h"

// ================= Пины физического джойстика =================
// VRx/VRy переведены с GPIO32/33 (в дефолтной распиновке прошивки там
// BTN_TURN_RIGHT и BTN_HORN) на свободные входы ADC1. GPIO35/36/39 —
// входы ADC1: работают одновременно с WiFi, в отличие от ADC2.
// SW — GPIO15: страп-пин, после загрузки безопасен как обычный вход.
int JOYSTICK_VRX_PIN = 35; // ось X — резерв
int JOYSTICK_VRY_PIN = 36; // ось Y — изменение уровня внутри режима
int JOYSTICK_SW_PIN  = 15; // кнопка нажатия — переключение режима

// ================= Настройки опроса =================
static const unsigned long JOY_AXIS_SAMPLE_MS   = 10;  // период опроса оси (цикл управления 1 кГц)
static const int           JOY_CENTER_LOW       = 1248; // мёртвая зона центра: 2048 ± 800
static const int           JOY_CENTER_HIGH      = 2848; // (12-бит АЦП 0..4095, покой ≈ 2048)
static const unsigned long JOY_REPEAT_MS        = 400; // автоповтор при удержании оси
static const unsigned long JOY_SW_DEBOUNCE_MS   = 40;  // антидребезг кнопки SW

static unsigned long lastAxisSampleMs = 0;
static unsigned long lastLevelStepMs  = 0;
static int lastDir = 0;

static bool swRawPressed    = false;
static bool swStablePressed = false;
static unsigned long swLastChangeMs = 0;

// ================= Применение режима/уровня =================
// Логика 1:1 с веб-эмуляцией /api/joystick/apply (web_handlers_emulation.cpp):
// PAS и круиз взаимоисключающие, уровень 0 = режим выключен.

static void joyApplyOff() {
  pasEnabled = false;
  pasCurrentLevel = 0;
  cruiseEnabled = false;
  cruiseCurrentLevel = 0;
  cruiseEngaged = false;
  cruisePendingResume = false;
}

static void joyApplyPas(int level) {
  if (level < 1) { joyApplyOff(); return; }
  if (level > pasLevelsCount) level = pasLevelsCount;
  pasCurrentLevel = level;
  pasEnabled = true;
  cruiseEnabled = false; // Взаимное исключение
  cruiseCurrentLevel = 0;
  cruiseEngaged = false;
  cruisePendingResume = false;
}

static void joyApplyCruise(int level) {
  if (level < 1) { joyApplyOff(); return; }
  if (level > cruiseLevelsCount) level = cruiseLevelsCount;
  cruiseCurrentLevel = level;
  cruiseEnabled = true;
  pasEnabled = false; // Взаимное исключение
  pasCurrentLevel = 0;
  if (cruiseConfirmThrottleAfterStart) {
    armCruisePending(true, 0.0f);
  } else {
    cruiseEngaged = true;
    cruisePendingResume = false;
  }
}

// SW: цикл OFF → PAS → КРУИЗ → OFF
static void joyCycleMode() {
  if (pasCurrentLevel > 0) {
    joyApplyCruise(cruiseCurrentLevel > 0 ? cruiseCurrentLevel : 1);
  } else if (cruiseEnabled) {
    joyApplyOff();
  } else {
    joyApplyPas(1);
  }
}

// Ось Y: шаг уровня внутри текущего режима
static void joyStepLevel(int dir) {
  if (pasCurrentLevel > 0) {
    int lvl = pasCurrentLevel + dir;
    if (lvl > pasLevelsCount) lvl = pasLevelsCount;
    if (lvl <= 0) joyApplyOff();
    else pasCurrentLevel = lvl;
    Serial.printf("Джойстик: PAS %d/%d\n", pasCurrentLevel, pasLevelsCount);
  } else if (cruiseEnabled) {
    int lvl = cruiseCurrentLevel + dir;
    if (lvl > cruiseLevelsCount) lvl = cruiseLevelsCount;
    if (lvl <= 0) joyApplyOff();
    else cruiseCurrentLevel = lvl;
    Serial.printf("Джойстик: КРУИЗ %d/%d\n", cruiseCurrentLevel, cruiseLevelsCount);
  }
}

static void joyUpdateSwitch(unsigned long now) {
  bool raw = (digitalRead(JOYSTICK_SW_PIN) == LOW); // нажатие прижимает к GND (pull-up)
  if (raw != swRawPressed) {
    swRawPressed = raw;
    swLastChangeMs = now;
  }
  if (raw == swStablePressed) return;
  if (now - swLastChangeMs < JOY_SW_DEBOUNCE_MS) return;
  swStablePressed = raw;
  if (swStablePressed) joyCycleMode(); // срабатывание по нажатию
}

static void joyUpdateAxis(unsigned long now) {
  if (now - lastAxisSampleMs < JOY_AXIS_SAMPLE_MS) return;
  lastAxisSampleMs = now;

  int y = analogRead(JOYSTICK_VRY_PIN);
  int dir = 0;
  if (y < JOY_CENTER_LOW) dir = -1;        // вниз — уровень меньше
  else if (y > JOY_CENTER_HIGH) dir = +1;  // вверх — уровень больше

  if (dir != lastDir) {          // смена направления или возврат в центр
    lastDir = dir;
    lastLevelStepMs = now - JOY_REPEAT_MS; // первый шаг — сразу (без стража «!= 0»:
  }                                 // корректно при переполнении millis())
  if (dir == 0) return;
  if (now - lastLevelStepMs < JOY_REPEAT_MS) return;
  lastLevelStepMs = now;
  joyStepLevel(dir);
}

void joystickInit() {
  pinMode(JOYSTICK_SW_PIN, INPUT_PULLUP);
  // Оси — входы ADC1 (GPIO35/36): читаем один раз для инициализации АЦП
  analogRead(JOYSTICK_VRX_PIN);
  analogRead(JOYSTICK_VRY_PIN);
  Serial.printf("Джойстик: VRx=GPIO%d VRy=GPIO%d SW=GPIO%d\n",
                JOYSTICK_VRX_PIN, JOYSTICK_VRY_PIN, JOYSTICK_SW_PIN);
}

void updateJoystick() {
  unsigned long now = millis();
  joyUpdateSwitch(now);
  joyUpdateAxis(now);
}
