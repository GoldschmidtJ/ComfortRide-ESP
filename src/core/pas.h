#ifndef PAS_H
#define PAS_H

#include <Arduino.h>

// ================= Константы =================
extern const int PAS_MAX_LEVELS;
extern const unsigned long PAS_CAL_TIMEOUT_MS;

// ================= Настройки датчика =================
extern int pasMagnetCount;
extern int pasEdgeMode;
extern int pasActivationAngle;
extern unsigned long pasTimeoutMs;
extern unsigned long pasStopTimeoutMs;

// ================= Состояние PAS =================
extern volatile unsigned long pasLastPulseMicros;
extern volatile unsigned long pasConsecutivePulses;
extern bool pasConfirmedActive;
extern portMUX_TYPE pasPulseMux;
extern bool pasInterruptAttached;

// ================= Калибровка магнитов =================
extern volatile bool pasCalRunning;
extern volatile unsigned long pasCalPulses;
extern volatile unsigned long pasCalLastPulseMicros;
extern unsigned long pasCalStartMs;

// ================= Уровни PAS =================
extern int pasLevelsCount;
extern int pasLevelPercent[20]; // PAS_MAX_LEVELS
extern int pasCurrentLevel;

// ================= Мягкий старт/стоп =================
extern bool pasSoftStartEnabled;
extern bool pasSoftStopEnabled;
extern bool pasEnabled;
extern unsigned long pasSoftStartMs;
extern unsigned long pasSoftStopMs;
extern float pasSmoothOutV;
extern unsigned long pasSmoothLastMs;

// ================= Зависимости (из main.cpp) =================
extern int PAS_SENSOR_PIN;
extern float throttleOutMinV;
extern float throttleOutMaxV;
extern bool serviceModeActive;

// ================= API управления =================

// Инициализация прерывания PAS
void pasInit(int sensorPin);

// Обработчик прерывания (IRAM)
void IRAM_ATTR onPasPulse();

// Обновление состояния PAS (вызывается в loop на высокой частоте)
void updatePasDetection();

// Переподключение прерывания (после изменения режима)
void reattachPasInterrupt();

// Вычислить требуемое количество импульсов для активации
int pasRequiredPulses();

// Автоматическое распределение уровней PAS (равномерно 0-100%)
void pasAutoDistribute();

// Применить мягкий старт/стоп к целевому напряжению PAS
float applyPasSmoothing(float targetV);

// Получить целевое напряжение PAS (с учётом уровня, сервисного режима и пр.)
float getPasTargetV();

#endif // PAS_H
