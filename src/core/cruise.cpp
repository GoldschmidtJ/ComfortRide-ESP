#include "core/cruise.h"

// ================= Константы =================
const int CRUISE_MAX_LEVELS = 100;

// ================= Конфигурация уровней =================
int cruiseLevelsCount = 3;
float cruiseLevelPercent[100]; // CRUISE_MAX_LEVELS
float cruiseStartPercent = 20.0f;
float cruiseEndPercent = 100.0f;

// ================= Поведение круиза =================
bool cruiseConfirmThrottleAfterStart = false;
int cruiseAfterBrakingMode = 1;  // 0: сброс, 1: подтверждение газом (default), 2: восстановление
int cruiseAfterThrottleMode = 2; // 0: сброс, 1: подтверждение газом, 2: восстановление (default)

// ================= Мягкий старт/стоп =================
bool cruiseSoftStartEnabled = false;
bool cruiseSoftStopEnabled = false;
unsigned long cruiseSoftStartMs = 500;
unsigned long cruiseSoftStopMs = 800;
float cruiseSmoothOutV = 0;
unsigned long cruiseSmoothLastMs = 0;

// ================= Состояние FSM круиз-контроля =================
bool cruiseEnabled = false;
int cruiseCurrentLevel = 0;
bool cruiseEngaged = false;
bool cruisePendingResume = false;
bool cruiseConfirmRequired = false;
bool cruiseReleaseSeen = true;

// ================= Автораспределение уровней =================
void cruiseAutoDistribute() {
  if (cruiseLevelsCount <= 0) return;
  if (cruiseLevelsCount == 1) {
    cruiseLevelPercent[0] = cruiseEndPercent;
    return;
  }
  for (int i = 0; i < cruiseLevelsCount; i++) {
    float frac = (float)i / (float)(cruiseLevelsCount - 1);
    cruiseLevelPercent[i] = cruiseStartPercent + frac * (cruiseEndPercent - cruiseStartPercent);
  }
}

// ================= Мягкий старт/стоп =================
float applyCruiseSmoothing(float targetV) {
  unsigned long now = millis();
  unsigned long dt = now - cruiseSmoothLastMs;
  if (dt == 0) dt = 1;
  cruiseSmoothLastMs = now;

  float diff = targetV - cruiseSmoothOutV;
  bool rising = diff > 0;
  bool enabled = rising ? cruiseSoftStartEnabled : cruiseSoftStopEnabled;
  unsigned long tau = rising ? cruiseSoftStartMs : cruiseSoftStopMs;

  if (!enabled || tau == 0) {
    cruiseSmoothOutV = targetV;
    return targetV;
  }

  // Линейная рампа: за tau мс выход ГАРАНТИРОВАННО доходит до цели
  float step = diff * ((float)dt / (float)tau);
  cruiseSmoothOutV += step;
  if ((diff > 0 && cruiseSmoothOutV > targetV) || (diff < 0 && cruiseSmoothOutV < targetV)) {
    cruiseSmoothOutV = targetV;
  }
  return cruiseSmoothOutV;
}

// ================= Расчёт целевого напряжения =================
float getCruiseTargetV() {
  if (!cruiseEnabled || !cruiseEngaged) return 0;
  if (cruiseCurrentLevel <= 0 || cruiseCurrentLevel > cruiseLevelsCount) return 0;
  float pct = cruiseLevelPercent[cruiseCurrentLevel - 1];
  float outMin = throttleOutMinV;
  float outMax = (throttleOutMaxV > outMin + 0.01f) ? throttleOutMaxV : (outMin + 0.01f);
  return outMin + (pct / 100.0f) * (outMax - outMin);
}

// ================= Управление pending-режимом =================
void armCruisePending(bool confirmRequired, float throttlePct) {
  cruiseEngaged = false;
  cruisePendingResume = true;
  cruiseConfirmRequired = confirmRequired;
  cruiseReleaseSeen = (throttlePct <= 10.0f);
}
