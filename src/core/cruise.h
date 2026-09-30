#ifndef CRUISE_H
#define CRUISE_H

#include <Arduino.h>

// ================= Константы =================
extern const int CRUISE_MAX_LEVELS; // Максимальное количество уровней круиза

// ================= Конфигурация уровней =================
extern int cruiseLevelsCount;
extern float cruiseLevelPercent[100]; // CRUISE_MAX_LEVELS
extern float cruiseStartPercent;      // Начальное значение для автораспределения
extern float cruiseEndPercent;        // Конечное значение для автораспределения

// ================= Поведение круиза =================
extern bool cruiseConfirmThrottleAfterStart; // Подтверждать газом после старта/восстановления
extern int cruiseAfterBrakingMode;           // 0: сброс, 1: подтверждение газом, 2: восстановление
extern int cruiseAfterThrottleMode;          // 0: сброс, 1: подтверждение газом, 2: восстановление

// ================= Мягкий старт/стоп =================
// cruiseSmoothMode: 0 = по умолчанию (из Газа), 1 = свои вкл+тайминги, 2 = выкл (см. SmoothMode в pas.h)
extern uint8_t cruiseSmoothMode;
extern bool cruiseSoftStartEnabled;
extern bool cruiseSoftStopEnabled;
extern unsigned long cruiseSoftStartMs;
extern unsigned long cruiseSoftStopMs;
extern float cruiseSmoothOutV;
extern unsigned long cruiseSmoothLastMs;

// ================= Состояние FSM круиз-контроля =================
extern bool cruiseEnabled;           // Включен ли круиз
extern int cruiseCurrentLevel;       // Текущий уровень (0 = выключен)
extern bool cruiseEngaged;           // Выдаётся ли тяга круиза на мотор
extern bool cruisePendingResume;     // Ожидает ли возобновления (уровень сохранён, но тяга отключена)
extern bool cruiseConfirmRequired;   // Требуется ли подтверждение через газ для включения тяги
extern bool cruiseReleaseSeen;       // Защита: был ли газ отпущен перед новым нажатием

// ================= Зависимости (из throttle.h) =================
extern float throttleOutMinV;
extern float throttleOutMaxV;

// ================= API управления =================

// Автораспределение уровней между cruiseStartPercent и cruiseEndPercent
void cruiseAutoDistribute();

// Применить мягкий старт/стоп к целевому напряжению
float applyCruiseSmoothing(float targetV);

// Получить целевое напряжение круиза (0 если неактивен)
float getCruiseTargetV();

// Перевести круиз в pending-режим (ожидание подтверждения или автовозобновление)
void armCruisePending(bool confirmRequired, float throttlePct);

#endif // CRUISE_H
