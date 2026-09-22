#include "core/throttle.h"
#include "system/events_engine.h" // serviceModeActive, serviceThrottleLimitPct
#include "core/pas.h"             // getPasTargetV(), applyPasSmoothing(), pasConfirmedActive
#include "core/cruise.h"          // getCruiseTargetV(), applyCruiseSmoothing()
#include "system/inputs.h"        // isBrakePressed()
#include "system/debug_capture.h" // updateDebugBuffer() (P3)

// ================= Флаг заводского сброса =================
// Пишется веб-обработчиком factory reset (web_handlers_system.cpp),
// читается в контуре управления (updateThrottle) для принудительного нуля.
bool factoryResetInProgress = false;

// ================= Константы =================
const float HW_MAX_VOLTAGE = 3.3f; // Максимальное напряжение ESP32

// ================= Аппаратное согласование напряжений =================
float throttleInputDividerRatio = 20.0f / (10.0f + 20.0f); // R2/(R1+R2), R1=10к, R2=20к по умолчанию
float throttleOutputGain = 1.0f + 3.3f / 10.0f; // 1 + R4/R3 (MCP6002, канал Б), R3=10к, R4=3.3к по умолчанию

// ================= Калибровка (в реальных вольтах) =================
float throttleInMinV = 1.1f, throttleInMaxV = 4.1f;
float throttleOutMinV = 1.1f, throttleOutMaxV = 4.1f;
bool throttleExtendedRangeAllowed = false; // задел на будущее

// ================= Мягкий старт/стоп =================
bool throttleSoftStartEnabled = false;
bool throttleSoftStopEnabled = false;
unsigned long throttleSoftStartMs = 500;
unsigned long throttleSoftStopMs = 500;
float throttleSmoothOutV = 0;
unsigned long throttleSmoothLastMs = 0;

// ================= Real-time телеметрия (определения) =================
volatile float hwThrottleInV = 0.0f;
volatile float hwThrottleOutV = 0.0f;
volatile float hwThrottlePct = 0.0f;
volatile float hwMotorOutPct = 0.0f;
volatile bool hwBrakeActive = false;
volatile bool hwPasActive = false;

// ================= Калибровка напряжения =================
float calibrateThrottleV(float rawV) {
  float inMin = throttleInMinV;
  float inMax = (throttleInMaxV > inMin + 0.01f) ? throttleInMaxV : (inMin + 0.01f);
  float outMin = throttleOutMinV;
  float outMax = (throttleOutMaxV > outMin + 0.01f) ? throttleOutMaxV : (outMin + 0.01f);
  float clamped = rawV;
  if (clamped < inMin) clamped = inMin;
  if (clamped > inMax) clamped = inMax;
  float normalized = (clamped - inMin) / (inMax - inMin);
  return outMin + normalized * (outMax - outMin);
}

// ================= Мягкий старт/стоп =================
float applyThrottleSmoothing(float targetV) {
  unsigned long now = millis();
  unsigned long dt = now - throttleSmoothLastMs;
  if (dt == 0) dt = 1;
  throttleSmoothLastMs = now;

  float diff = targetV - throttleSmoothOutV;
  bool rising = diff > 0;
  bool enabled = rising ? throttleSoftStartEnabled : throttleSoftStopEnabled;
  unsigned long tau = rising ? throttleSoftStartMs : throttleSoftStopMs;

  if (!enabled || tau == 0) { 
    throttleSmoothOutV = targetV; 
    return targetV; 
  }

  // Линейная рампа: за tau мс выход ГАРАНТИРОВАННО доходит до цели
  float step = diff * ((float)dt / (float)tau);
  throttleSmoothOutV += step;
  if ((diff > 0 && throttleSmoothOutV > targetV) || (diff < 0 && throttleSmoothOutV < targetV)) {
    throttleSmoothOutV = targetV;
  }
  return throttleSmoothOutV;
}

// ================= Безопасный ноль =================
void setThrottleOutputSafeZero() {
  dacWrite(THROTTLE_DAC_PIN, 0);
}

// ================= Главная функция обновления газа =================
void updateThrottle() {
  // Во время заводского сброса фоновая задача не должна повторно поднять ЦАП
  if (factoryResetInProgress) {
    setThrottleOutputSafeZero();
    throttleSmoothOutV = 0;
    pasSmoothOutV = 0;
    cruiseSmoothOutV = 0;
    hwThrottleOutV = 0.0f;
    hwMotorOutPct = 0.0f;
    return;
  }

  bool brakePressed = isBrakePressed();

  // АЦП на ESP32 дорогой (~50-100 мкс на чтение): на 1 кГц это до ~10% CPU.
  // Децимируем выборку до 200 Гц (раз в 5 мс)
  static int cachedRaw = 0;
  static unsigned long lastAdcSampleMs = 0;
  unsigned long adcNowMs = millis();
  if (adcNowMs - lastAdcSampleMs >= 5) {
    cachedRaw = analogRead(THROTTLE_ADC_PIN);
    lastAdcSampleMs = adcNowMs;
  }
  int raw = cachedRaw;
  float adcPinV = raw * HW_MAX_VOLTAGE / 4095.0f;
  float realGripV = adcPinV / throttleInputDividerRatio;

  float throttleTargetV = calibrateThrottleV(realGripV);
  float throttleOutV = applyThrottleSmoothing(throttleTargetV);

  // Расчет процента нажатия ручки газа (0-100%) для круиза
  float throttleSpan = (throttleInMaxV - throttleInMinV);
  float throttlePct = 0.0f;
  if (throttleSpan > 0.05f) {
    throttlePct = ((realGripV - throttleInMinV) / throttleSpan) * 100.0f;
  }
  if (throttlePct < 0.0f) throttlePct = 0.0f;
  if (throttlePct > 100.0f) throttlePct = 100.0f;

  // Конечный автомат круиз-контроля
  if (cruiseEnabled && cruiseCurrentLevel > 0 && !serviceModeActive) {
    if (brakePressed) {
      if (cruiseEngaged || !cruisePendingResume) {
        if (cruiseAfterBrakingMode == 0) {
          cruiseEnabled = false;
          cruiseCurrentLevel = 0;
          cruiseEngaged = false;
          cruisePendingResume = false;
        } else if (cruiseAfterBrakingMode == 1) {
          armCruisePending(true, throttlePct);
        } else if (cruiseAfterBrakingMode == 2) {
          armCruisePending(false, throttlePct);
        }
      }
    } else {
      if (cruiseEngaged && throttlePct > 10.0f) {
        if (cruiseAfterThrottleMode == 0) {
          cruiseEnabled = false;
          cruiseCurrentLevel = 0;
          cruiseEngaged = false;
          cruisePendingResume = false;
        } else if (cruiseAfterThrottleMode == 1) {
          armCruisePending(true, throttlePct);
        } else if (cruiseAfterThrottleMode == 2) {
          armCruisePending(false, throttlePct);
        }
      } else if (!cruiseEngaged && cruisePendingResume) {
        if (cruiseConfirmRequired) {
          if (throttlePct <= 10.0f) {
            cruiseReleaseSeen = true;
          } else if (cruiseReleaseSeen && throttlePct > 10.0f) {
            cruiseEngaged = true;
            cruisePendingResume = false;
          }
        } else {
          if (throttlePct <= 10.0f) {
            cruiseEngaged = true;
            cruisePendingResume = false;
          }
        }
      }
    }
  }

  float pasTargetV = getPasTargetV();
  float pasOutV = applyPasSmoothing(pasTargetV);

  float cruiseTargetV = getCruiseTargetV();
  float cruiseOutV = applyCruiseSmoothing(cruiseTargetV);

  float combinedV = throttleOutV;
  if (pasOutV > combinedV) combinedV = pasOutV;
  if (cruiseOutV > combinedV) combinedV = cruiseOutV;

  // Удержание throttleOutMinV для мгновенного отклика
  if (combinedV < throttleOutMinV) {
    combinedV = throttleOutMinV;
  }

  // Сервисный режим: жёсткий потолок мощности
  if (serviceModeActive) {
    float svcOutMin = throttleOutMinV;
    float svcOutMax = (throttleOutMaxV > svcOutMin + 0.01f) ? throttleOutMaxV : (svcOutMin + 0.01f);
    float limitV = svcOutMin + (constrain(serviceThrottleLimitPct, 0, 100) / 100.0f) * (svcOutMax - svcOutMin);
    if (combinedV > limitV) combinedV = limitV;
  }

  // Обновляем телеметрию
  hwThrottleInV = realGripV;
  hwThrottleOutV = brakePressed ? 0.0f : combinedV;
  hwThrottlePct = throttlePct;
  {
    float outSpan = throttleOutMaxV - throttleOutMinV;
    float motorPct = (outSpan > 0.05f) ? ((combinedV - throttleOutMinV) / outSpan) * 100.0f : 0.0f;
    if (motorPct < 0.0f) motorPct = 0.0f;
    if (motorPct > 100.0f) motorPct = 100.0f;
    hwMotorOutPct = brakePressed ? 0.0f : motorPct;
  }
  hwBrakeActive = brakePressed;
  hwPasActive = pasConfirmedActive;

  if (brakePressed) {
    setThrottleOutputSafeZero();
    throttleSmoothOutV = 0;
    pasSmoothOutV = 0;
    cruiseSmoothOutV = 0;
    updateDebugBuffer(0, 0);
    return;
  }

  // ОУ поднимает напряжение в throttleOutputGain раз
  float dacTargetV = combinedV / throttleOutputGain;
  if (dacTargetV > HW_MAX_VOLTAGE) dacTargetV = HW_MAX_VOLTAGE;
  if (dacTargetV < 0) dacTargetV = 0;
  int dacVal = (int)round(dacTargetV / HW_MAX_VOLTAGE * 255.0f);
  if (dacVal < 0) dacVal = 0;
  if (dacVal > 255) dacVal = 255;
  dacWrite(THROTTLE_DAC_PIN, dacVal);

  updateDebugBuffer(realGripV, combinedV);
}

