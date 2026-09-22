#ifndef CPU_PROFILE_H
#define CPU_PROFILE_H

#include <Arduino.h>

// ================= Секционный CPU-профайлер =================
// Замеряет время (мкс) секций контура управления в criticalControlTask,
// раз в 1000 мс пересчитывает проценты загрузки для телеметрии.

enum CpuSection {
  CPU_SEC_PAS,       // updatePasDetection()
  CPU_SEC_PAS_BTN,   // handlePasButtonPress()
  CPU_SEC_THROTTLE,  // updateThrottle(), updateEventEngine(), updateDisplayIcons()
  CPU_SEC_LIGHT,     // updateVirtualButtons(), updateJoystick()
  CPU_SEC_SOUND,     // updateLightBlink(), updateTurnSignals(), updateHorn(), updateBuzzer()
  CPU_SEC_COUNT
};

// Результаты (обновляются в cpuProfileLoopEnd(), читаются телеметрией)
extern int cpuUsagePercent;
extern int cpuPasPct;
extern int cpuPasBtnPct;
extern int cpuThrottlePct;
extern int cpuLightPct;
extern int cpuSoundPct;

// Начало измерения общей длительности итерации контура (вызывать 1 раз в начале цикла)
void cpuProfileLoopBegin();

// Начало замера секции
void cpuProfileBegin(CpuSection section);

// Конец замера секции (накапливает время в счётчик секции)
void cpuProfileEnd();

// Конец итерации: накапливает busy-время, раз в 1000 мс пересчитывает проценты
void cpuProfileLoopEnd();

#endif // CPU_PROFILE_H