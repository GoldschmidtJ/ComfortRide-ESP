#include "core/pas.h"
#include "system/events_engine.h" // serviceModeActive, serviceThrottleLimitPct
#include "system/inputs.h"        // updatePasButton()
#include "core/cruise.h"          // cruiseEnabled, cruiseCurrentLevel, ...

// ================= Константы =================
const int PAS_MAX_LEVELS = 20;
const unsigned long PAS_CAL_TIMEOUT_MS = 60000; // авто-стоп калибровки через 60 с

// ================= Настройки датчика =================
int pasMagnetCount = 12;
int pasEdgeMode = FALLING;
int pasActivationAngle = 180;
unsigned long pasTimeoutMs = 350;      // тайм-аут СБРОСА накопленных импульсов (долгий)
unsigned long pasStopTimeoutMs = 200;  // тайм-аут ОТКЛЮЧЕНИЯ при остановке педалей (быстрый)

// ================= Состояние PAS =================
volatile unsigned long pasLastPulseMicros = 0;
volatile unsigned long pasConsecutivePulses = 0;
bool pasConfirmedActive = false;
portMUX_TYPE pasPulseMux = portMUX_INITIALIZER_UNLOCKED;
bool pasInterruptAttached = false;

// ================= Калибровка магнитов =================
volatile bool pasCalRunning = false;
volatile unsigned long pasCalPulses = 0;
volatile unsigned long pasCalLastPulseMicros = 0;
unsigned long pasCalStartMs = 0;

// ================= Уровни PAS =================
int pasLevelsCount = 3;
int pasLevelPercent[20]; // PAS_MAX_LEVELS
int pasCurrentLevel = 0; // 0 = выключен

// ================= Мягкий старт/стоп =================
bool pasSoftStartEnabled = false;
bool pasSoftStopEnabled = false;
bool pasEnabled = true; // PAS enabled by default
unsigned long pasSoftStartMs = 500;
unsigned long pasSoftStopMs = 800;
float pasSmoothOutV = 0;
unsigned long pasSmoothLastMs = 0;

// ================= Обработчик прерывания =================
void IRAM_ATTR onPasPulse() {
  unsigned long now = micros();
  portENTER_CRITICAL_ISR(&pasPulseMux);
  pasLastPulseMicros = now;
  pasConsecutivePulses++;
  if (pasCalRunning) {
    pasCalPulses++;
    pasCalLastPulseMicros = now;
  }
  portEXIT_CRITICAL_ISR(&pasPulseMux);
}

// ================= Инициализация =================
void pasInit(int sensorPin) {
  PAS_SENSOR_PIN = sensorPin;
  pinMode(PAS_SENSOR_PIN, INPUT_PULLUP);
  reattachPasInterrupt();
}

// ================= Вычисление требуемых импульсов =================
int pasRequiredPulses() {
  int required = (int)round(pasActivationAngle * pasMagnetCount / 360.0f);
  if (required < 1) required = 1;
  return required;
}

// ================= Обновление состояния PAS =================
void updatePasDetection() {
  unsigned long now = micros();
  unsigned long lastPulse;
  unsigned long consecutivePulses;
  portENTER_CRITICAL(&pasPulseMux);
  lastPulse = pasLastPulseMicros;
  consecutivePulses = pasConsecutivePulses;
  portEXIT_CRITICAL(&pasPulseMux);
  
  if (lastPulse == 0) { 
    pasConfirmedActive = false; 
    return; 
  }
  
  unsigned long sincePulseUs = now - lastPulse;

  // После полной паузы начинаем новую последовательность импульсов.
  if (sincePulseUs >= pasTimeoutMs * 1000UL) {
    portENTER_CRITICAL(&pasPulseMux);
    // Не стираем импульс, который мог прийти после сделанного выше снимка.
    if (pasLastPulseMicros == lastPulse) pasConsecutivePulses = 0;
    portEXIT_CRITICAL(&pasPulseMux);
    pasConfirmedActive = false;
    return;
  }

  // Остановка педалей: отключаем тягу быстро. Важно: пока действует этот
  // тайм-аут, нельзя повторно выполнить условие активации по старому счётчику.
  if (sincePulseUs >= pasStopTimeoutMs * 1000UL) {
    if (pasConfirmedActive) {
      pasConfirmedActive = false;
      pasSmoothOutV = 0;
    }
    return;
  }

  // Только свежий импульсный поток может поддерживать/включать PAS.
  if (consecutivePulses >= (unsigned long)pasRequiredPulses()) {
    pasConfirmedActive = true;
  }
}

// ================= Переподключение прерывания =================
void reattachPasInterrupt() {
  if (pasInterruptAttached) {
    detachInterrupt(digitalPinToInterrupt(PAS_SENSOR_PIN));
  }
  attachInterrupt(digitalPinToInterrupt(PAS_SENSOR_PIN), onPasPulse, pasEdgeMode);
  pasInterruptAttached = true;
}

// ================= Автоматическое распределение уровней =================
void pasAutoDistribute() {
  for (int i = 0; i < pasLevelsCount; i++) {
    pasLevelPercent[i] = (int)round((float)(i + 1) * 100.0f / (float)pasLevelsCount);
  }
}

// ================= Мягкий старт/стоп =================
float applyPasSmoothing(float targetV) {
  unsigned long now = millis();
  unsigned long dt = now - pasSmoothLastMs;
  if (dt == 0) dt = 1;
  pasSmoothLastMs = now;

  float diff = targetV - pasSmoothOutV;
  bool rising = diff > 0;
  bool enabled = rising ? pasSoftStartEnabled : pasSoftStopEnabled;
  unsigned long tau = rising ? pasSoftStartMs : pasSoftStopMs;

  if (!enabled || tau == 0) { 
    pasSmoothOutV = targetV; 
    return targetV; 
  }

  // Мёртвая зона контроллера: пока выход ниже throttleOutMinV, мотор не крутится.
  // При старте сразу прыгаем на нижнюю границу, чтобы не тратить время рампы
  // на бесполезный участок 0В -> throttleOutMinV (раньше это давало лишнюю задержку).
  float outMin = throttleOutMinV;
  if (rising && pasSmoothOutV < outMin) pasSmoothOutV = outMin;

  float diff2 = targetV - pasSmoothOutV;
  if (diff2 <= 0) { 
    pasSmoothOutV = targetV; 
    return targetV; 
  }

  // Линейная рампа: за tau мс выход ГАРАНТИРОВАННО доходит до цели
  float step = diff2 * ((float)dt / (float)tau);
  pasSmoothOutV += step;
  if (pasSmoothOutV > targetV) pasSmoothOutV = targetV;
  return pasSmoothOutV;
}

// ================= Получение целевого напряжения PAS =================
float getPasTargetV() {
  if (!pasEnabled) return 0; // PAS fully disabled
  
  // Сервисный режим: PAS не выше 1 уровня, каким бы путём его ни raised
  int effLevel = pasCurrentLevel;
  if (serviceModeActive && effLevel > 1) effLevel = 1;
  
  if (effLevel <= 0 || effLevel > pasLevelsCount) return 0;
  if (!pasConfirmedActive) return 0;
  
  float pct = pasLevelPercent[effLevel - 1];
  float outMin = throttleOutMinV;
  float outMax = (throttleOutMaxV > outMin + 0.01f) ? throttleOutMaxV : (outMin + 0.01f);
  return outMin + (pct / 100.0f) * (outMax - outMin);
}

// ================= Обработка кнопки переключения уровня PAS =================
void handlePasButtonPress() {
  if (updatePasButton()) {
    pasCurrentLevel = (pasCurrentLevel + 1) % (pasLevelsCount + 1);
    pasEnabled = (pasCurrentLevel > 0);
    if (pasEnabled) {
      cruiseEnabled = false; // Отключаем круиз при физическом переключении PAS
      cruiseCurrentLevel = 0;
      cruiseEngaged = false;
      cruisePendingResume = false;
    }
    Serial.printf("PAS уровень: %d/%d\n", pasCurrentLevel, pasLevelsCount);
  }
}
