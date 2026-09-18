#include "core/lights.h"
#include "system/peripherals.h"
#include "core/light_logic.h" // lightModeNext, lightModeFromOutputs, TurnSignalState, turnSignalAfterPress

// ================= Константы яркости =================
const int HEADLIGHT_DEFAULT_BRIGHTNESS = 220; // 0-255
const int DRL_DEFAULT_BRIGHTNESS       = 60;  // 0-255, горит всегда

// ================= Состояние освещения =================
bool headlightOn = false;
bool drlOn = true; // ДХО горит с включением платы
bool headlightBlink = false, drlBlink = false;
bool headlightBlinkState = false, drlBlinkState = false;
unsigned long headlightBlinkAtMs = 0, drlBlinkAtMs = 0;
unsigned long headlightBlinkMs = 500, drlBlinkMs = 500;

// Конечный автомат циклического режима света: 0=Выкл, 1=ДХО, 2=Ближний, 3=Ближний+ДХО
uint8_t lightCycleMode = LIGHT_DRL; // старт с ДХО (как было)

// ================= Поворотники =================
bool turnLeftActive = false, turnRightActive = false;
unsigned long turnLastBlinkMs = 0;
bool turnBlinkState = false;
const unsigned long TURN_BLINK_MS = 500;

// ================= Внутренние переменные пинов =================
static int _headlightPin = 18;
static int _drlPin = 19;
static int _turnLeftPin = 21;
static int _turnRightPin = 22;

// ================= Инициализация =================
void lightsInit(int headlightPin, int drlPin) {
  _headlightPin = headlightPin;
  _drlPin = drlPin;

  // Настройка PWM (LEDC) для фары и ДХО
  ledcSetup(0, 5000, 8); // канал 0, 5кГц, 8 бит
  ledcAttachPin(_headlightPin, 0);
  ledcSetup(1, 5000, 8); // канал 1, 5кГц, 8 бит
  ledcAttachPin(_drlPin, 1);

  // ДХО горит всегда, пока плата включена
  ledcWrite(1, DRL_DEFAULT_BRIGHTNESS);
}

// Обновить пины поворотников (вызывается при изменении распиновки)
void lightsSetTurnPins(int turnLeftPin, int turnRightPin) {
  _turnLeftPin = turnLeftPin;
  _turnRightPin = turnRightPin;
}

// ================= Управление фарой =================
void applyCycleLightMode() {
  switch (lightCycleMode) {
    case LIGHT_OFF: // Выкл
      headlightBlink = false; drlBlink = false;
      headlightOn = false; drlOn = false;
      ledcWrite(0, 0); ledcWrite(1, 0);
      Serial.println("Свет: ВЫКЛ");
      break;
    case LIGHT_DRL: // ДХО
      headlightBlink = false; drlBlink = false;
      headlightOn = false; drlOn = true;
      ledcWrite(0, 0); ledcWrite(1, DRL_DEFAULT_BRIGHTNESS);
      Serial.println("Свет: ДХО");
      break;
    case LIGHT_LOW: // Ближний
      headlightBlink = false; drlBlink = false;
      headlightOn = true; drlOn = false;
      ledcWrite(0, HEADLIGHT_DEFAULT_BRIGHTNESS); ledcWrite(1, 0);
      Serial.println("Свет: БЛИЖНИЙ");
      break;
    case LIGHT_LOW_DRL: // Ближний+ДХО
      headlightBlink = false; drlBlink = false;
      headlightOn = true; drlOn = true;
      ledcWrite(0, HEADLIGHT_DEFAULT_BRIGHTNESS); ledcWrite(1, DRL_DEFAULT_BRIGHTNESS);
      Serial.println("Свет: БЛИЖНИЙ+ДХО");
      break;
  }
}

void cycleLightMode() {
  lightCycleMode = lightModeNext(lightCycleMode);
  applyCycleLightMode();
}

void syncLightCycleMode() {
  lightCycleMode = lightModeFromOutputs(headlightOn, drlOn);
}

void toggleHeadlight() {
  headlightBlink = false;
  headlightOn = !headlightOn;
  ledcWrite(0, headlightOn ? HEADLIGHT_DEFAULT_BRIGHTNESS : 0);
  syncLightCycleMode();
  Serial.println(headlightOn ? "Фара: ВКЛ" : "Фара: ВЫКЛ");
}

void toggleDrl() {
  drlBlink = false;
  drlOn = !drlOn;
  ledcWrite(1, drlOn ? DRL_DEFAULT_BRIGHTNESS : 0);
  syncLightCycleMode();
  Serial.println(drlOn ? "ДХО: ВКЛ" : "ДХО: ВЫКЛ");
}

// Получить текущий режим света (0=ВЫКЛ, 1=ДХО, 2=БЛИЖНИЙ, 3=ПОЛНЫЙ)
uint8_t currentLightMode() {
  return lightModeFromOutputs(headlightOn, drlOn);
}

// Установить режим света (используется в событиях)
void setLightMode(uint8_t mode) {
  headlightBlink = drlBlink = false;
  switch(mode) {
    case LIGHT_OFF:     headlightOn = false; drlOn = false; break;
    case LIGHT_DRL:     headlightOn = false; drlOn = true;  break;
    case LIGHT_LOW:     headlightOn = true;  drlOn = false; break;
    case LIGHT_LOW_DRL: headlightOn = true;  drlOn = true;  break;
    default:            headlightOn = false; drlOn = false; break;
  }
  ledcWrite(0, headlightOn ? HEADLIGHT_DEFAULT_BRIGHTNESS : 0);
  ledcWrite(1, drlOn ? DRL_DEFAULT_BRIGHTNESS : 0);
  syncLightCycleMode();
  Serial.print("Режим света: "); Serial.println(mode);
}

// Переключить режим света вверх (цикл: 0→1→2→3→0)
void cycleLightModeUp() {
  setLightMode(lightModeNext(currentLightMode()));
}

// ================= Мигание фары/ДХО =================
void toggleLightBlink(bool headlight, unsigned long intervalMs) {
  intervalMs = constrain(intervalMs, 100UL, 5000UL);
  bool &enabled = headlight ? headlightBlink : drlBlink;
  bool &state = headlight ? headlightBlinkState : drlBlinkState;
  unsigned long &blinkMs = headlight ? headlightBlinkMs : drlBlinkMs;
  unsigned long &changedAt = headlight ? headlightBlinkAtMs : drlBlinkAtMs;
  enabled = !enabled;
  state = false;
  blinkMs = intervalMs;
  changedAt = millis();
  if (enabled) {
    ledcWrite(headlight ? 0 : 1, headlight ? HEADLIGHT_DEFAULT_BRIGHTNESS : DRL_DEFAULT_BRIGHTNESS);
  } else {
    ledcWrite(headlight ? 0 : 1, (headlight ? headlightOn : drlOn) ?
              (headlight ? HEADLIGHT_DEFAULT_BRIGHTNESS : DRL_DEFAULT_BRIGHTNESS) : 0);
  }
}

void updateLightBlink() {
  unsigned long now = millis();
  if (headlightBlink && now - headlightBlinkAtMs >= headlightBlinkMs) {
    headlightBlinkAtMs = now;
    headlightBlinkState = !headlightBlinkState;
    ledcWrite(0, headlightBlinkState ? HEADLIGHT_DEFAULT_BRIGHTNESS : 0);
  }
  if (drlBlink && now - drlBlinkAtMs >= drlBlinkMs) {
    drlBlinkAtMs = now;
    drlBlinkState = !drlBlinkState;
    ledcWrite(1, drlBlinkState ? DRL_DEFAULT_BRIGHTNESS : 0);
  }
}

// ================= Поворотники =================
void toggleTurnLeft() {
  TurnSignalState s = turnSignalAfterPress({ turnLeftActive, turnRightActive }, true);
  turnLeftActive = s.left;
  turnRightActive = s.right;
}

void toggleTurnRight() {
  TurnSignalState s = turnSignalAfterPress({ turnLeftActive, turnRightActive }, false);
  turnLeftActive = s.left;
  turnRightActive = s.right;
}

void updateTurnSignals() {
  if (!turnLeftActive && !turnRightActive) {
    digitalWrite(_turnLeftPin, LOW);
    digitalWrite(_turnRightPin, LOW);
    return;
  }
  if (millis() - turnLastBlinkMs >= TURN_BLINK_MS) {
    turnLastBlinkMs = millis();
    turnBlinkState = !turnBlinkState;
    buzzerClick(50); // тик на каждой смене состояния мигания
  }
  digitalWrite(_turnLeftPin,  (turnLeftActive  && turnBlinkState) ? HIGH : LOW);
  digitalWrite(_turnRightPin, (turnRightActive && turnBlinkState) ? HIGH : LOW);
}
