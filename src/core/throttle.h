#ifndef THROTTLE_H
#define THROTTLE_H

#include <Arduino.h>

// ================= Константы =================
extern const float HW_MAX_VOLTAGE; // 3.3V для ESP32

// ================= Аппаратное согласование напряжений =================
extern float throttleInputDividerRatio;  // делитель на входе (понижает 4.2В -> 3.3В)
extern float throttleOutputGain;         // усиление на выходе (поднимает 3.3В -> 4.2В)

// ================= Калибровка (в реальных вольтах) =================
extern float throttleInMinV, throttleInMaxV;   // вход от ручки газа
extern float throttleOutMinV, throttleOutMaxV; // выход на контроллер
extern bool throttleExtendedRangeAllowed;

// ================= Мягкий старт/стоп =================
extern bool throttleSoftStartEnabled;
extern bool throttleSoftStopEnabled;
extern unsigned long throttleSoftStartMs;
extern unsigned long throttleSoftStopMs;
extern float throttleSmoothOutV;
extern unsigned long throttleSmoothLastMs;

// ================= Зависимости (из main.cpp) =================
extern int THROTTLE_ADC_PIN;
extern int THROTTLE_DAC_PIN;
extern bool serviceModeActive;
extern int serviceThrottleLimitPct;
extern volatile bool factoryResetInProgress;

// ================= API управления =================

// Калибровка: преобразует напряжение ручки в целевое напряжение контроллера
float calibrateThrottleV(float rawV);

// Применить мягкий старт/стоп к целевому напряжению
float applyThrottleSmoothing(float targetV);

// Установить безопасный ноль на выходе ЦАП
void setThrottleOutputSafeZero();

// Обновление газа: чтение АЦП, калибровка, сглаживание, запись ЦАП
void updateThrottle();

#endif // THROTTLE_H
