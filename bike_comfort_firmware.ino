#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include <math.h>

// ================= Настройки =================
const char* ssid = "Donut";
const char* password = "doughnut";

// Пины — ВРЕМЕННЫЕ, для теста на ESP32-CAM.
const int THROTTLE_ADC_PIN  = 34;
const int THROTTLE_DAC_PIN  = 25;
const int BTN_HEADLIGHT_PIN = 13;
const int BTN_TURN_LEFT_PIN  = 32;
const int BTN_TURN_RIGHT_PIN = 33;
const int BTN_HORN_PIN       = 5;
const int PAS_SENSOR_PIN    = 14;
const int BRAKE_PIN         = 27;
const int BUZZER_PIN        = 19; // "цыканье" поворотника

const int HEADLIGHT_PIN = 18; // НЕ GPIO12! Это strapping-пин (MTDI/VDD_SDIO) — если
                               // на нём HIGH при старте, чип может выбрать неверное
                               // напряжение флеша и уйти в ребут-луп. GPIO18 — обычный, безопасный.
const int DRL_PIN       = 15;
const int TURN_LEFT_PIN  = 2;
const int TURN_RIGHT_PIN = 4;
const int HORN_PIN       = 16;
const int REAR_LIGHT_PIN = 17;

const int PWM_FREQ = 5000;
const int PWM_RES  = 8;

const int HEADLIGHT_DEFAULT_BRIGHTNESS = 220;
const int DRL_DEFAULT_BRIGHTNESS       = 60;

const unsigned long WDT_TIMEOUT_S = 3;

WebServer server(80);
Preferences prefs;

// ================= Типы (объявлены здесь специально — до первой функции) =================
// Автогенератор прототипов в Arduino IDE вставляет прототипы функций перед
// самой первой функцией файла. Если тип объявлен позже — компилятор не видит
// его в этой точке и падает с "not declared in this scope". Поэтому все
// enum/typedef держим здесь, максимально рано.
enum class EventType {
  BTN_HEADLIGHT_PRESS,
  BTN_TURN_LEFT_PRESS,
  BTN_TURN_RIGHT_PRESS,
};
typedef void (*ActionFn)();
enum class MenuState { IDLE, CRUISE_LEVEL };
enum DisplayMode { MODE_NONE, MODE_PAS, MODE_THROTTLE, MODE_CRUISE };

// ВАЖНО ПО БЕЗОПАСНОСТИ: тормозные ручки ОБЯЗАТЕЛЬНО должны быть подключены
// напрямую в моторконтроллер (в обход этой платы). Без них у мотора не будет
// аппаратного отключения тяги при торможении.
bool webBrakeOverride = false;
bool isBrakePressed() {
  return digitalRead(BRAKE_PIN) == LOW || webBrakeOverride;
}

// ================= Событийная модель =================
bool headlightOn = false;
void actionToggleHeadlight() {
  headlightOn = !headlightOn;
  ledcWrite(HEADLIGHT_PIN, headlightOn ? HEADLIGHT_DEFAULT_BRIGHTNESS : 0);
  Serial.println(headlightOn ? "Фара: ВКЛ" : "Фара: ВЫКЛ");
}

bool turnLeftActive = false, turnRightActive = false;
void actionToggleTurnLeft() {
  turnLeftActive = !turnLeftActive;
  if (turnLeftActive) turnRightActive = false;
}
void actionToggleTurnRight() {
  turnRightActive = !turnRightActive;
  if (turnRightActive) turnLeftActive = false;
}

struct EventBinding { EventType event; ActionFn action; };
EventBinding bindings[] = {
  { EventType::BTN_HEADLIGHT_PRESS,  actionToggleHeadlight },
  { EventType::BTN_TURN_LEFT_PRESS,  actionToggleTurnLeft },
  { EventType::BTN_TURN_RIGHT_PRESS, actionToggleTurnRight },
};
const int bindingsCount = sizeof(bindings) / sizeof(bindings[0]);

void fireEvent(EventType e) {
  for (int i = 0; i < bindingsCount; i++) {
    if (bindings[i].event == e) bindings[i].action();
  }
}

// ================= Физические кнопки руля (дебаунс) =================
const unsigned long DEBOUNCE_MS = 50;

struct DebouncedButton {
  int pin; int lastReading; int stableState; unsigned long lastDebounceMs; EventType event;
};

DebouncedButton physicalButtons[] = {
  { BTN_HEADLIGHT_PIN,  HIGH, HIGH, 0, EventType::BTN_HEADLIGHT_PRESS },
  { BTN_TURN_LEFT_PIN,  HIGH, HIGH, 0, EventType::BTN_TURN_LEFT_PRESS },
  { BTN_TURN_RIGHT_PIN, HIGH, HIGH, 0, EventType::BTN_TURN_RIGHT_PRESS },
};
const int physicalButtonsCount = sizeof(physicalButtons) / sizeof(physicalButtons[0]);

void updatePhysicalButtons() {
  for (int i = 0; i < physicalButtonsCount; i++) {
    DebouncedButton &b = physicalButtons[i];
    int reading = digitalRead(b.pin);
    if (reading != b.lastReading) b.lastDebounceMs = millis();
    if (millis() - b.lastDebounceMs > DEBOUNCE_MS) {
      if (reading != b.stableState) {
        b.stableState = reading;
        if (b.stableState == LOW) fireEvent(b.event);
      }
    }
    b.lastReading = reading;
  }
}

// ================= Гудок =================
bool hornWebActive = false;
bool hornPhysicalActive = false;
void updateHorn() {
  hornPhysicalActive = (digitalRead(BTN_HORN_PIN) == LOW);
  digitalWrite(HORN_PIN, (hornPhysicalActive || hornWebActive) ? HIGH : LOW);
}

// ================= Пищалка (тик поворотника) =================
bool buzzerOn = false;
unsigned long buzzerOffAtMs = 0;
void buzzerClick(unsigned long durationMs) {
  digitalWrite(BUZZER_PIN, HIGH);
  buzzerOn = true;
  buzzerOffAtMs = millis() + durationMs;
}
void updateBuzzer() {
  if (buzzerOn && millis() >= buzzerOffAtMs) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerOn = false;
  }
}

// ================= Поворотники =================
unsigned long turnLastBlinkMs = 0;
bool turnBlinkState = false;
const unsigned long TURN_BLINK_MS = 500;

void updateTurnSignals() {
  if (!turnLeftActive && !turnRightActive) {
    digitalWrite(TURN_LEFT_PIN, LOW);
    digitalWrite(TURN_RIGHT_PIN, LOW);
    return;
  }
  if (millis() - turnLastBlinkMs >= TURN_BLINK_MS) {
    turnLastBlinkMs = millis();
    turnBlinkState = !turnBlinkState;
    buzzerClick(50); // тик на каждой смене состояния мигания
  }
  digitalWrite(TURN_LEFT_PIN,  (turnLeftActive  && turnBlinkState) ? HIGH : LOW);
  digitalWrite(TURN_RIGHT_PIN, (turnRightActive && turnBlinkState) ? HIGH : LOW);
}

// ================= Задний габарит (по тормозу) =================
void updateRearLight() {
  digitalWrite(REAR_LIGHT_PIN, isBrakePressed() ? HIGH : LOW);
}

// ================= Модуль PAS (проценты) =================
const int PAS_MAX_LEVELS = 9;
const unsigned long PAS_TIMEOUT_MS = 400;

volatile unsigned long pasLastPulseMicros = 0;
volatile unsigned long pasPulseCount = 0;

int pasLevelsCount = 5;
int pasLevelPercent[PAS_MAX_LEVELS]; // 0-100%
int pasCurrentLevel = 0; // 0 = выкл (просто едем по ручке газа)

void IRAM_ATTR onPasPulse() {
  pasLastPulseMicros = micros();
  pasPulseCount++;
}

bool isPedaling() {
  if (pasLastPulseMicros == 0) return false;
  return (micros() - pasLastPulseMicros) < (PAS_TIMEOUT_MS * 1000UL);
}

int getPasOutput() {
  if (pasCurrentLevel <= 0 || pasCurrentLevel > pasLevelsCount) return 0;
  if (!isPedaling()) return 0;
  return (int)round(pasLevelPercent[pasCurrentLevel - 1] * 255.0f / 100.0f);
}

void pasAutoDistribute() {
  for (int i = 0; i < pasLevelsCount; i++) {
    pasLevelPercent[i] = (int)round((float)(i + 1) * 100.0f / (float)pasLevelsCount);
  }
}

void pasSave() {
  prefs.begin("pas", false);
  prefs.putInt("cnt", pasLevelsCount);
  prefs.putBytes("pct", pasLevelPercent, sizeof(pasLevelPercent));
  prefs.end();
}

void pasLoad() {
  prefs.begin("pas", true);
  pasLevelsCount = prefs.getInt("cnt", 5);
  size_t got = prefs.getBytes("pct", pasLevelPercent, sizeof(pasLevelPercent));
  prefs.end();
  if (got != sizeof(pasLevelPercent)) pasAutoDistribute();
}

// ================= Круиз-контроль (проценты, та же структура что PAS) =================
const int CRUISE_MAX_LEVELS = 20; // мельче шаги, чем у PAS — для скорости это уместнее

bool cruiseEngaged = false;
int cruiseLevelsCount = 10;
int cruiseLevelPercent[CRUISE_MAX_LEVELS];
int cruiseTargetLevel = 1;   // подтверждённый индекс уровня (1..cruiseLevelsCount)
int cruiseSelectLevel = 1;   // индекс, который сейчас крутим в меню

bool cruiseUseLastLevel = true;   // true: при включении — последний использованный уровень
int  cruiseDefaultLevel = 1;      // индекс уровня, если cruiseUseLastLevel == false
bool cruiseCancelOnThrottle = false; // сбрасывать круиз при касании газа (опция)
bool cruiseResumeAfterBrake = false; // после тормоза, при касании газа — вернуть круиз на прежний уровень
bool cruisePendingResume = false;    // внутренний флаг: круиз был отменён тормозом и ждёт "резюме"

void cruiseAutoDistribute() {
  for (int i = 0; i < cruiseLevelsCount; i++) {
    cruiseLevelPercent[i] = (int)round((float)(i + 1) * 100.0f / (float)cruiseLevelsCount);
  }
}

void cruiseSave() {
  prefs.begin("cruise", false);
  prefs.putInt("cnt", cruiseLevelsCount);
  prefs.putBytes("pct", cruiseLevelPercent, sizeof(cruiseLevelPercent));
  prefs.putInt("lastLvl", cruiseTargetLevel);
  prefs.putInt("useLast", cruiseUseLastLevel ? 1 : 0);
  prefs.putInt("defLvl", cruiseDefaultLevel);
  prefs.putInt("cancelThr", cruiseCancelOnThrottle ? 1 : 0);
  prefs.putInt("resumeBrk", cruiseResumeAfterBrake ? 1 : 0);
  prefs.end();
}

void cruiseLoad() {
  prefs.begin("cruise", true);
  cruiseLevelsCount = prefs.getInt("cnt", 10);
  size_t got = prefs.getBytes("pct", cruiseLevelPercent, sizeof(cruiseLevelPercent));
  cruiseTargetLevel = prefs.getInt("lastLvl", 1);
  cruiseUseLastLevel = prefs.getInt("useLast", 1) != 0;
  cruiseDefaultLevel = prefs.getInt("defLvl", 1);
  cruiseCancelOnThrottle = prefs.getInt("cancelThr", 0) != 0;
  cruiseResumeAfterBrake = prefs.getInt("resumeBrk", 0) != 0;
  prefs.end();
  if (got != sizeof(cruiseLevelPercent)) cruiseAutoDistribute();
  cruiseTargetLevel = constrain(cruiseTargetLevel, 1, max(cruiseLevelsCount,1));
  cruiseDefaultLevel = constrain(cruiseDefaultLevel, 1, max(cruiseLevelsCount,1));
  cruiseSelectLevel = cruiseTargetLevel;
}

int getCruiseOutput() {
  if (!cruiseEngaged || cruiseTargetLevel < 1 || cruiseTargetLevel > cruiseLevelsCount) return 0;
  return (int)round(cruiseLevelPercent[cruiseTargetLevel - 1] * 255.0f / 100.0f);
}

// ================= Меню (джойстик 5 кнопок) =================
// Упростил: только два состояния. IDLE — тут же вверх/вниз крутят уровень
// PAS (0 = просто едем). OK — вход в выбор уровня круиза.
MenuState menuState = MenuState::IDLE;

void onJoystick(String dir) {
  switch (menuState) {
    case MenuState::IDLE:
      if (dir == "ok") {
        if (cruiseEngaged) {
          cruiseEngaged = false;
          cruisePendingResume = false; // ручное выключение отменяет и ожидание автовосстановления
          Serial.println("Круиз: ВЫКЛ (через ОК)");
        } else if (cruiseLevelsCount > 0) {
          cruiseSelectLevel = cruiseUseLastLevel ? cruiseTargetLevel : cruiseDefaultLevel;
          cruiseSelectLevel = constrain(cruiseSelectLevel, 1, cruiseLevelsCount);
          menuState = MenuState::CRUISE_LEVEL;
        } else {
          Serial.println("Круиз: уровни не настроены (см. /settings/cruise)");
        }
      } else if (dir == "up") {
        pasCurrentLevel = min(pasCurrentLevel + 1, pasLevelsCount);
        Serial.printf("PAS уровень: %d\n", pasCurrentLevel);
      } else if (dir == "down") {
        pasCurrentLevel = max(pasCurrentLevel - 1, 0);
        Serial.printf("PAS уровень: %d\n", pasCurrentLevel);
      }
      break;

    case MenuState::CRUISE_LEVEL:
      if (dir == "up") {
        cruiseSelectLevel = min(cruiseSelectLevel + 1, cruiseLevelsCount);
      } else if (dir == "down") {
        cruiseSelectLevel = max(cruiseSelectLevel - 1, 1);
      } else if (dir == "ok") {
        cruiseTargetLevel = cruiseSelectLevel;
        cruiseSave();
        cruiseEngaged = true;
        cruisePendingResume = false; // ручной выбор уровня — точно не "автовосстановление"
        menuState = MenuState::IDLE;
        Serial.printf("Круиз: ВКЛ, уровень %d (%d%%)\n", cruiseTargetLevel, cruiseLevelPercent[cruiseTargetLevel-1]);
      } else if (dir == "left") {
        menuState = MenuState::IDLE;
      }
      break;
  }
}

// ================= Throttle-by-wire с fail-safe =================
// Приоритеты (важно!): 1) тормоз — идёт напрямую в моторконтроллер, в обход
// нас, это аппаратный приоритет №1, мы его не трогаем и не дублируем.
// 2) газ — читаем и передаём КАЖДЫЙ цикл, PAS/круиз только ДОБАВЛЯЮТ (max),
// никогда не блокируют и не уменьшают то, что дал реальный газ.

void setThrottleOutputSafeZero() {
  dacWrite(THROTTLE_DAC_PIN, 0);
}

void updateSafety() {
  if (isBrakePressed() && cruiseEngaged) {
    cruiseEngaged = false;
    if (cruiseResumeAfterBrake) {
      cruisePendingResume = true; // уровень (cruiseTargetLevel) уже сохранён, просто ждём газа
    }
    Serial.println("Круиз: ВЫКЛ (тормоз)");
  }
}

// ---- Сглаживание разгона/торможения (гибрид) ----
// Экспоненциальный фильтр, привязанный к реальному времени (мс) — а не к
// номеру цикла, поэтому не "плывёт" от джиттера основного loop() (веб-сервер,
// OTA и т.п.). Плюс "доводка": как только сигнал подошёл близко к цели —
// ставим точное значение, а не бесконечно приближаемся (в этом была слабость
// обычного экспоненциального фильтра).
bool throttleSmoothingEnabled = false;
int accelSmoothLevel = 0; // 0-99, слайдер: 0 = мгновенно, 99 = максимально плавно
int decelSmoothLevel = 0; // 0-99
const unsigned long SMOOTH_MAX_TAU_MS = 3000; // при 99 разгон/торможение "чувствуется" за ~3с
const float SMOOTH_SNAP_THRESHOLD = 1.0f;     // единиц ЦАП (0-255)

float throttleSmoothOutput = 0.0f;
unsigned long lastSmoothMs = 0;

int applySmoothing(int target) {
  unsigned long now = millis();
  unsigned long dt = now - lastSmoothMs;
  lastSmoothMs = now;
  if (dt == 0) dt = 1;

  if (!throttleSmoothingEnabled) {
    throttleSmoothOutput = target;
    return target;
  }

  float diff = (float)target - throttleSmoothOutput;
  int level = (diff > 0) ? accelSmoothLevel : decelSmoothLevel;
  unsigned long tau = map(level, 0, 99, 0, SMOOTH_MAX_TAU_MS);

  if (tau == 0) {
    throttleSmoothOutput = target;
  } else {
    float alpha = 1.0f - expf(-(float)dt / (float)tau);
    throttleSmoothOutput += diff * alpha;
    if (fabs((float)target - throttleSmoothOutput) < SMOOTH_SNAP_THRESHOLD) {
      throttleSmoothOutput = target; // доводка — реально долетаем до цели
    }
  }
  return (int)round(throttleSmoothOutput);
}

void throttleSettingsSave() {
  prefs.begin("throttle", false);
  prefs.putInt("enabled", throttleSmoothingEnabled ? 1 : 0);
  prefs.putInt("accel", accelSmoothLevel);
  prefs.putInt("decel", decelSmoothLevel);
  prefs.end();
}

void throttleSettingsLoad() {
  prefs.begin("throttle", true);
  throttleSmoothingEnabled = prefs.getInt("enabled", 0) != 0;
  accelSmoothLevel = prefs.getInt("accel", 0);
  decelSmoothLevel = prefs.getInt("decel", 0);
  prefs.end();
}

int lastThrottleOut = 0; // для опции "сброс круиза при касании газа"

void updateThrottle() {
  int raw = analogRead(THROTTLE_ADC_PIN);
  int throttleOut = map(raw, 0, 4095, 0, 255);
  lastThrottleOut = throttleOut;

  if (cruiseEngaged && cruiseCancelOnThrottle && throttleOut > 10) {
    cruiseEngaged = false;
    Serial.println("Круиз: ВЫКЛ (касание газа, опция включена)");
  }

  if (cruisePendingResume && !isBrakePressed() && throttleOut > 10) {
    cruiseEngaged = true; // cruiseTargetLevel не трогали — восстанавливаем прежний уровень
    cruisePendingResume = false;
    Serial.printf("Круиз: ВОССТАНОВЛЕН после тормоза, уровень %d\n", cruiseTargetLevel);
  }

  int pasOut = getPasOutput();
  int cruiseOut = getCruiseOutput();

  int target = max(throttleOut, max(pasOut, cruiseOut));
  int out = applySmoothing(target);
  dacWrite(THROTTLE_DAC_PIN, out);
}

// ================= Модуль дисплея (виртуальный, 8x32) =================
bool displayFB[8][32];
void fbClear() { memset(displayFB, 0, sizeof(displayFB)); }

const uint8_t FONT_P[7] = {0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000};
const uint8_t FONT_T[7] = {0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100};
const uint8_t FONT_C[7] = {0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110};
const uint8_t FONT_B[7] = {0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110};
const uint8_t FONT_W[7] = {0b10001,0b10001,0b10001,0b10101,0b10101,0b11011,0b10001};
const uint8_t FONT_E[7] = {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111};
const uint8_t FONT_L[7] = {0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111};
const uint8_t FONT_U[7] = {0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110};
const uint8_t FONT_M[7] = {0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001};
const uint8_t ARROW_LEFT[7]  = {0b00001,0b00011,0b00111,0b01111,0b00111,0b00011,0b00001};
const uint8_t ARROW_RIGHT[7] = {0b10000,0b11000,0b11100,0b11110,0b11100,0b11000,0b10000};

const uint8_t DIGIT_0[7] = {0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110};
const uint8_t DIGIT_1[7] = {0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110};
const uint8_t DIGIT_2[7] = {0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111};
const uint8_t DIGIT_3[7] = {0b11111,0b00010,0b00100,0b00010,0b00001,0b10001,0b01110};
const uint8_t DIGIT_4[7] = {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010};
const uint8_t DIGIT_5[7] = {0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110};
const uint8_t DIGIT_6[7] = {0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110};
const uint8_t DIGIT_7[7] = {0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000};
const uint8_t DIGIT_8[7] = {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110};
const uint8_t DIGIT_9[7] = {0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100};

const uint8_t* getGlyph(char c) {
  switch (toupper(c)) {
    case 'P': return FONT_P; case 'T': return FONT_T; case 'C': return FONT_C;
    case 'B': return FONT_B; case 'W': return FONT_W; case 'E': return FONT_E;
    case 'L': return FONT_L; case 'U': return FONT_U; case 'M': return FONT_M;
    default:  return nullptr;
  }
}

const uint8_t* getDigitGlyph(int d) {
  static const uint8_t* digits[10] = {DIGIT_0,DIGIT_1,DIGIT_2,DIGIT_3,DIGIT_4,DIGIT_5,DIGIT_6,DIGIT_7,DIGIT_8,DIGIT_9};
  if (d < 0 || d > 9) return nullptr;
  return digits[d];
}

void fbDrawGlyph(const uint8_t* glyph, int xOffset) {
  if (!glyph) return;
  for (int col = 0; col < 5; col++) {
    for (int row = 0; row < 7; row++) {
      bool on = (glyph[row] >> (4 - col)) & 1;
      int x = xOffset + col;
      if (x >= 0 && x < 32) displayFB[row][x] = on;
    }
  }
}

void fbDrawNumber(int num, int xOffset) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", num);
  int len = strlen(buf);
  int x = xOffset;
  for (int i = 0; i < len; i++) { fbDrawGlyph(getDigitGlyph(buf[i]-'0'), x); x += 6; }
}

// ---- Бегущая строка при загрузке ----
const char* BOOT_TEXT = "wellcum";
bool bootWideFB[7][240];
int bootWideWidth = 0;
int bootScrollOffset = -32;
bool bootAnimationDone = false;
unsigned long bootLastStepMs = 0;
const unsigned long BOOT_STEP_MS = 80;

void buildBootWideFB() {
  memset(bootWideFB, 0, sizeof(bootWideFB));
  bootWideWidth = 0;
  for (int i = 0; BOOT_TEXT[i] != '\0'; i++) {
    const uint8_t* glyph = getGlyph(BOOT_TEXT[i]);
    if (glyph) {
      for (int col = 0; col < 5; col++)
        for (int row = 0; row < 7; row++)
          bootWideFB[row][bootWideWidth + col] = (glyph[row] >> (4 - col)) & 1;
      bootWideWidth += 6;
    }
  }
}

void displayBootStep() {
  if (millis() - bootLastStepMs < BOOT_STEP_MS) return;
  bootLastStepMs = millis();
  fbClear();
  for (int col = 0; col < 32; col++) {
    int srcCol = bootScrollOffset + col;
    if (srcCol >= 0 && srcCol < bootWideWidth)
      for (int row = 0; row < 7; row++) displayFB[row][col] = bootWideFB[row][srcCol];
  }
  bootScrollOffset++;
  if (bootScrollOffset > bootWideWidth) bootAnimationDone = true;
}

DisplayMode getCurrentMode() {
  if (cruiseEngaged) return MODE_CRUISE;
  if (isPedaling() && pasCurrentLevel > 0) return MODE_PAS;
  if (lastThrottleOut > 10) return MODE_THROTTLE;
  return MODE_NONE;
}

void updateMatrixHardware() {
  // TODO: когда придёт MAX7219 x4 — сюда вызовы библиотеки, которая
  // перегонит displayFB на реальные светодиоды.
}

void displayLoop() {
  if (!bootAnimationDone) {
    displayBootStep();
    updateMatrixHardware();
    return;
  }

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < 100) return;
  lastUpdate = millis();

  fbClear();

  if (menuState == MenuState::CRUISE_LEVEL) {
    int pct = cruiseLevelPercent[cruiseSelectLevel - 1];
    fbDrawNumber(pct, 10);
  } else {
    DisplayMode mode = getCurrentMode();
    const uint8_t* modeGlyph = nullptr;
    switch (mode) {
      case MODE_PAS:      modeGlyph = FONT_P; break;
      case MODE_THROTTLE: modeGlyph = FONT_T; break;
      case MODE_CRUISE:   modeGlyph = FONT_C; break;
      default: break;
    }
    if (modeGlyph) fbDrawGlyph(modeGlyph, 13);

    // Стрелки поворотников по краям — не пересекаются с буквой режима в центре
    if (turnLeftActive  && turnBlinkState) fbDrawGlyph(ARROW_LEFT, 0);
    if (turnRightActive && turnBlinkState) fbDrawGlyph(ARROW_RIGHT, 27);

    // Тормоз — отдельная строка снизу (ряд 7), не конфликтует ни с чем выше
    if (isBrakePressed()) {
      for (int x = 0; x < 32; x++) displayFB[7][x] = true;
    }
  }

  updateMatrixHardware();
}

// ================= Веб: хаб настроек =================
void handleSettingsHub() {
  server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Настройки</title>
<style>
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto}
.warn{background:#5c1a1a;color:#fff;padding:12px;border-radius:8px;margin-bottom:20px;font-weight:bold}
a.card{display:block;background:#333;color:#fff;padding:15px;border-radius:8px;margin-bottom:10px;text-decoration:none}
</style></head><body>
<h1>Настройки</h1>
<div class="warn">&#9888; Тормозные ручки должны быть ОБЯЗАТЕЛЬНО подключены напрямую в моторконтроллер для безопасности!</div>
<a class="card" href="/settings/throttle">Газ &rarr;</a>
<a class="card" href="/settings/pas">PAS &rarr;</a>
<a class="card" href="/settings/cruise">Круиз-контроль &rarr;</a>
<p><a href="/panel">&larr; Панель управления</a></p>
</body></html>
)rawliteral");
}

// ================= Веб: Газ (сглаживание) =================
void handleThrottlePage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Газ</title>
<style>
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto}
label{display:block;margin-top:15px}
input[type=range]{width:100%}
.val{font-weight:bold}
button{margin-top:20px;padding:10px;width:100%;font-size:16px}
.chk{display:flex;align-items:center;gap:8px;margin-top:15px}
.chk input{width:auto}
</style></head><body>
<h1>Настройка газа</h1>

<div class="chk"><input type="checkbox" id="enabled" )rawliteral";
  html += throttleSmoothingEnabled ? "checked" : "";
  html += R"rawliteral(><label for="enabled">Сглаживание включено (по умолчанию — выключено, газ повторяет ручку 1:1)</label></div>

<label>Плавность разгона: <span class="val" id="accelVal">)rawliteral";
  html += String(accelSmoothLevel);
  html += R"rawliteral(</span></label>
<input type="range" min="0" max="99" id="accel" value=")rawliteral";
  html += String(accelSmoothLevel);
  html += R"rawliteral(" oninput="document.getElementById('accelVal').innerText=this.value">

<label>Плавность торможения (отпускания газа): <span class="val" id="decelVal">)rawliteral";
  html += String(decelSmoothLevel);
  html += R"rawliteral(</span></label>
<input type="range" min="0" max="99" id="decel" value=")rawliteral";
  html += String(decelSmoothLevel);
  html += R"rawliteral(" oninput="document.getElementById('decelVal').innerText=this.value">

<p style="color:#888;font-size:14px">0 — мгновенный отклик (как есть сейчас). 99 — самый плавный, реакция "растягивается" примерно на )rawliteral";
  html += String(SMOOTH_MAX_TAU_MS / 1000);
  html += R"rawliteral( сек.</p>

<button onclick="save()">Сохранить</button>
<p><a href="/settings">&larr; Настройки</a></p>
<script>
function save(){
  const data = new URLSearchParams();
  data.set('enabled', document.getElementById('enabled').checked ? '1' : '0');
  data.set('accel', document.getElementById('accel').value);
  data.set('decel', document.getElementById('decel').value);
  fetch('/settings/throttle/save', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:data.toString()})
    .then(() => alert('Сохранено'));
}
</script>
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleThrottleSave() {
  throttleSmoothingEnabled = server.arg("enabled") == "1";
  accelSmoothLevel = constrain(server.arg("accel").toInt(), 0, 99);
  decelSmoothLevel = constrain(server.arg("decel").toInt(), 0, 99);
  throttleSettingsSave();
  server.send(200, "text/plain", "OK");
}

// ================= Веб: PAS =================
void handlePasPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PAS</title>
<style>
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto}
label{display:block;margin-top:10px}
input{width:100%;padding:6px;box-sizing:border-box}
button{margin-top:15px;padding:10px;width:100%;font-size:16px}
</style></head><body>
<h1>Настройка PAS</h1>
<form id="f">
<label>Количество уровней (0-9)</label>
<input type="number" min="0" max="9" id="count" name="count" value=")rawliteral";
  html += String(pasLevelsCount);
  html += R"rawliteral(">
<div id="levels"></div>
<button type="button" onclick="autoDistribute()">Автораспределение</button>
<button type="submit">Сохранить</button>
</form>
<p><a href="/settings">&larr; Настройки</a></p>
<script>
const saved = [)rawliteral";
  for (int i = 0; i < PAS_MAX_LEVELS; i++) { html += String(pasLevelPercent[i]); if (i < PAS_MAX_LEVELS-1) html += ","; }
  html += R"rawliteral(];
function renderLevels() {
  const count = parseInt(document.getElementById('count').value) || 0;
  const div = document.getElementById('levels'); div.innerHTML = '';
  for (let i = 0; i < count; i++) {
    const val = saved[i] !== undefined ? saved[i] : 0;
    div.innerHTML += '<label>Уровень ' + (i+1) + ' — усилие (0-100%)</label>' +
      '<input type="number" min="0" max="100" name="lvl' + i + '" value="' + val + '">';
  }
}
function autoDistribute() {
  const count = parseInt(document.getElementById('count').value) || 0;
  for (let i = 0; i < count; i++) saved[i] = Math.round((i+1) * 100 / count);
  renderLevels();
}
document.getElementById('count').addEventListener('input', renderLevels);
renderLevels();
document.getElementById('f').addEventListener('submit', function(e) {
  e.preventDefault();
  fetch('/settings/pas/save', { method: 'POST', body: new FormData(this) }).then(() => alert('Сохранено'));
});
</script>
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handlePasSave() {
  int count = constrain(server.arg("count").toInt(), 0, PAS_MAX_LEVELS);
  pasLevelsCount = count;
  for (int i = 0; i < count; i++) {
    String key = "lvl" + String(i);
    if (server.hasArg(key)) pasLevelPercent[i] = constrain(server.arg(key).toInt(), 0, 100);
  }
  pasSave();
  server.send(200, "text/plain", "OK");
}

// ================= Веб: Круиз-контроль =================
void handleCruisePage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Круиз</title>
<style>
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto}
label{display:block;margin-top:10px}
input[type=number]{width:100%;padding:6px;box-sizing:border-box}
button{margin-top:15px;padding:10px;width:100%;font-size:16px}
.chk{display:flex;align-items:center;gap:8px;margin-top:12px}
.chk input{width:auto}
</style></head><body>
<h1>Настройка круиз-контроля</h1>
<form id="f">
<label>Количество уровней (0-)rawliteral";
  html += String(CRUISE_MAX_LEVELS);
  html += R"rawliteral()</label>
<input type="number" min="0" max=")rawliteral";
  html += String(CRUISE_MAX_LEVELS);
  html += R"rawliteral(" id="count" name="count" value=")rawliteral";
  html += String(cruiseLevelsCount);
  html += R"rawliteral(">
<div id="levels"></div>
<button type="button" onclick="autoDistribute()">Автораспределение</button>

<div class="chk"><input type="checkbox" id="useLast" name="useLast" )rawliteral";
  html += cruiseUseLastLevel ? "checked" : "";
  html += R"rawliteral(><label for="useLast">При включении — последний использованный уровень</label></div>

<label>Уровень по умолчанию (если галочка выше снята)</label>
<input type="number" min="1" max=")rawliteral";
  html += String(CRUISE_MAX_LEVELS);
  html += R"rawliteral(" id="defLvl" name="defLvl" value=")rawliteral";
  html += String(cruiseDefaultLevel);
  html += R"rawliteral(">

<div class="chk"><input type="checkbox" id="cancelThr" name="cancelThr" )rawliteral";
  html += cruiseCancelOnThrottle ? "checked" : "";
  html += R"rawliteral(><label for="cancelThr">Сбрасывать круиз при касании газа</label></div>

<div class="chk"><input type="checkbox" id="resumeBrk" name="resumeBrk" )rawliteral";
  html += cruiseResumeAfterBrake ? "checked" : "";
  html += R"rawliteral(><label for="resumeBrk">После тормоза — восстанавливать круиз при касании газа (по умолчанию выкл.)</label></div>

<button type="submit">Сохранить</button>
</form>
<p><a href="/settings">&larr; Настройки</a></p>
<script>
const saved = [)rawliteral";
  for (int i = 0; i < CRUISE_MAX_LEVELS; i++) { html += String(cruiseLevelPercent[i]); if (i < CRUISE_MAX_LEVELS-1) html += ","; }
  html += R"rawliteral(];
function renderLevels() {
  const count = parseInt(document.getElementById('count').value) || 0;
  const div = document.getElementById('levels'); div.innerHTML = '';
  for (let i = 0; i < count; i++) {
    const val = saved[i] !== undefined ? saved[i] : 0;
    div.innerHTML += '<label>Уровень ' + (i+1) + ' — цель (0-100%)</label>' +
      '<input type="number" min="0" max="100" name="lvl' + i + '" value="' + val + '">';
  }
}
function autoDistribute() {
  const count = parseInt(document.getElementById('count').value) || 0;
  for (let i = 0; i < count; i++) saved[i] = Math.round((i+1) * 100 / count);
  renderLevels();
}
document.getElementById('count').addEventListener('input', renderLevels);
renderLevels();
document.getElementById('f').addEventListener('submit', function(e) {
  e.preventDefault();
  const data = new FormData(this);
  if (!document.getElementById('useLast').checked) data.delete('useLast');
  if (!document.getElementById('cancelThr').checked) data.delete('cancelThr');
  if (!document.getElementById('resumeBrk').checked) data.delete('resumeBrk');
  fetch('/settings/cruise/save', { method: 'POST', body: data }).then(() => alert('Сохранено'));
});
</script>
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleCruiseSave() {
  int count = constrain(server.arg("count").toInt(), 0, CRUISE_MAX_LEVELS);
  cruiseLevelsCount = count;
  for (int i = 0; i < count; i++) {
    String key = "lvl" + String(i);
    if (server.hasArg(key)) cruiseLevelPercent[i] = constrain(server.arg(key).toInt(), 0, 100);
  }
  cruiseUseLastLevel = server.hasArg("useLast");
  cruiseCancelOnThrottle = server.hasArg("cancelThr");
  cruiseResumeAfterBrake = server.hasArg("resumeBrk");
  if (server.hasArg("defLvl")) {
    cruiseDefaultLevel = constrain(server.arg("defLvl").toInt(), 1, max(cruiseLevelsCount,1));
  }
  cruiseSave();
  server.send(200, "text/plain", "OK");
}

// ================= Веб: панель управления + зеркало дисплея =================
void handlePanelPage() {
  server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Панель</title>
<style>
body{background:#111;color:#eee;font-family:sans-serif;text-align:center;padding:15px}
#grid{display:inline-grid;grid-template-columns:repeat(32,9px);grid-template-rows:repeat(8,9px);gap:1px;background:#000;padding:8px;margin-bottom:20px}
.px{width:9px;height:9px;background:#222}
.on{background:#fff}
h3{margin:15px 0 5px}
.dpad{display:grid;grid-template-columns:60px 60px 60px;grid-template-rows:60px 60px 60px;gap:5px;justify-content:center;margin:10px auto}
.dpad button{font-size:18px}
.row{display:flex;gap:8px;justify-content:center;margin:8px 0;flex-wrap:wrap}
button{padding:12px 16px;font-size:15px;border-radius:8px;border:none;background:#333;color:#eee}
button:active{background:#555}
button.active{background:#4a90d9}
a{color:#4a90d9}
</style></head><body>
<h2>Матрица 8x32 (превью)</h2>
<div id="grid"></div>

<h3>Джойстик (5 кнопок)</h3>
<div class="dpad">
<div></div><button onclick="joy('up')">&uarr;</button><div></div>
<button onclick="joy('left')">&larr;</button><button onclick="joy('ok')">OK</button><button onclick="joy('right')">&rarr;</button>
<div></div><button onclick="joy('down')">&darr;</button><div></div>
</div>

<h3>Кнопки на руле (пока эмуляция)</h3>
<div class="row">
<button onclick="btn('headlight')">Фара</button>
<button onclick="btn('turnleft')">Поворот &larr;</button>
<button onclick="btn('turnright')">Поворот &rarr;</button>
</div>
<div class="row">
<button id="hornBtn" onmousedown="hornState(true)" onmouseup="hornState(false)" ontouchstart="hornState(true)" ontouchend="hornState(false)">Гудок (держать)</button>
<button id="brakeBtn" onclick="toggleBrake()">Тормоз (тест)</button>
</div>

<p><a href="/settings">Настройки (PAS / Круиз) &rarr;</a></p>

<script>
const grid = document.getElementById('grid');
const cells = [];
for (let r=0;r<8;r++){cells.push([]);for(let c=0;c<32;c++){const d=document.createElement('div');d.className='px';grid.appendChild(d);cells[r].push(d);}}
function refresh(){
  fetch('/display/fb').then(r=>r.text()).then(text=>{
    const rows = text.split('\n');
    for (let r=0;r<8;r++){ const row = rows[r] || ''; for (let c=0;c<32;c++){ cells[r][c].className = row[c]==='1' ? 'px on' : 'px'; } }
  });
}
setInterval(refresh, 150);
refresh();
function joy(dir){ fetch('/joy', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'dir='+dir}); }
function btn(name){ fetch('/btn', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'name='+name}); }
function hornState(on){
  fetch('/btn', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'name=horn&state=' + (on?'on':'off')});
  document.getElementById('hornBtn').className = on ? 'active' : '';
}
let brakeOn = false;
function toggleBrake(){
  brakeOn = !brakeOn;
  fetch('/btn', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'name=brake&state=' + (brakeOn?'on':'off')});
  document.getElementById('brakeBtn').className = brakeOn ? 'active' : '';
}
</script>
</body></html>
)rawliteral");
}

void handleDisplayFB() {
  String out;
  for (int r = 0; r < 8; r++) { for (int c = 0; c < 32; c++) out += displayFB[r][c] ? '1' : '0'; out += '\n'; }
  server.send(200, "text/plain", out);
}

void handleJoy() { onJoystick(server.arg("dir")); server.send(200, "text/plain", "OK"); }

void handleBtn() {
  String name = server.arg("name");
  String state = server.hasArg("state") ? server.arg("state") : "";
  if (name == "headlight")       fireEvent(EventType::BTN_HEADLIGHT_PRESS);
  else if (name == "turnleft")   fireEvent(EventType::BTN_TURN_LEFT_PRESS);
  else if (name == "turnright")  fireEvent(EventType::BTN_TURN_RIGHT_PRESS);
  else if (name == "horn")       hornWebActive = (state == "on");
  else if (name == "brake")      webBrakeOverride = (state == "on");
  server.send(200, "text/plain", "OK");
}

void feedWatchdog() { esp_task_wdt_reset(); }

unsigned long lastWifiRetryMs = 0;
const unsigned long WIFI_RETRY_INTERVAL_MS = 30000;
void maintainWifi() {
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiRetryMs > WIFI_RETRY_INTERVAL_MS) {
    lastWifiRetryMs = millis();
    WiFi.reconnect();
  }
}

// ================= Setup / Loop =================
void setup() {
  Serial.begin(115200);

  pinMode(BTN_HEADLIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_TURN_LEFT_PIN, INPUT_PULLUP);
  pinMode(BTN_TURN_RIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_HORN_PIN, INPUT_PULLUP);
  pinMode(PAS_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PAS_SENSOR_PIN), onPasPulse, FALLING);
  pinMode(BRAKE_PIN, INPUT_PULLUP);

  pinMode(TURN_LEFT_PIN, OUTPUT);
  pinMode(TURN_RIGHT_PIN, OUTPUT);
  pinMode(HORN_PIN, OUTPUT);
  pinMode(REAR_LIGHT_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  ledcAttach(HEADLIGHT_PIN, PWM_FREQ, PWM_RES);
  ledcAttach(DRL_PIN, PWM_FREQ, PWM_RES);
  ledcWrite(DRL_PIN, DRL_DEFAULT_BRIGHTNESS);

  pasLoad();
  cruiseLoad();
  throttleSettingsLoad();
  buildBootWideFB();

  setThrottleOutputSafeZero();

  // В ESP32-ядре 3.x watchdog уже поднят фреймворком ДО setup(). Повторный
  // esp_task_wdt_init() на уже инициализированном TWDT приводил к крашу
  // (LoadProhibited) — вместо этого пробуем init, а если он уже есть,
  // просто перенастраиваем существующий через reconfigure.
  esp_task_wdt_config_t twdtConfig = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_err_t wdtInitResult = esp_task_wdt_init(&twdtConfig);
  if (wdtInitResult == ESP_ERR_INVALID_STATE) {
    esp_task_wdt_reconfigure(&twdtConfig);
  }
  esp_task_wdt_add(NULL);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Подключение к WiFi");
  unsigned long wifiStartMs = millis();
  const unsigned long WIFI_CONNECT_TIMEOUT_MS = 8000;
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartMs < WIFI_CONNECT_TIMEOUT_MS) {
    delay(300); Serial.print("."); esp_task_wdt_reset();
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Открой в браузере: http://"); Serial.println(WiFi.localIP());
    Serial.println("  /panel    — джойстик, кнопки, превью дисплея");
    Serial.println("  /settings — PAS и круиз-контроль");
  } else {
    Serial.println("WiFi не найден — едем без веб-панели и OTA. Газ/свет/тормоз работают штатно.");
  }

  ArduinoOTA.setHostname("bike-comfort-esp32");
  ArduinoOTA.onStart([]() { Serial.println("OTA: начало обновления"); });
  ArduinoOTA.onEnd([]()   { Serial.println("OTA: обновление завершено"); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA ошибка [%u]\n", error); });
  ArduinoOTA.begin();

  server.on("/settings", handleSettingsHub);
  server.on("/settings/throttle", handleThrottlePage);
  server.on("/settings/throttle/save", HTTP_POST, handleThrottleSave);
  server.on("/settings/pas", handlePasPage);
  server.on("/settings/pas/save", HTTP_POST, handlePasSave);
  server.on("/settings/cruise", handleCruisePage);
  server.on("/settings/cruise/save", HTTP_POST, handleCruiseSave);
  server.on("/panel", handlePanelPage);
  server.on("/display", handlePanelPage);
  server.on("/display/fb", handleDisplayFB);
  server.on("/joy", HTTP_POST, handleJoy);
  server.on("/btn", HTTP_POST, handleBtn);
  server.begin();

  Serial.println("Готово. Каркас прошивки запущен.");
}

void loop() {
  feedWatchdog();
  ArduinoOTA.handle();
  server.handleClient();
  maintainWifi();

  updatePhysicalButtons();
  updateSafety();
  updateThrottle();
  updateTurnSignals();
  updateHorn();
  updateBuzzer();
  updateRearLight();
  displayLoop();

  delay(5);
}
