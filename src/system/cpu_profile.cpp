#include "system/cpu_profile.h"

// ================= Состояние профайлера =================
static unsigned long cpuMeasureStartMs = 0;
static unsigned long cpuBusyTimeMicros = 0;
static unsigned long cpuUs[CPU_SEC_COUNT] = {0};
static unsigned long secStartUs = 0;
static unsigned long loopStartUs = 0;
static CpuSection currentSection = CPU_SEC_PAS;

// Результаты (читаются телеметрией)
int cpuUsagePercent = 0;
int cpuPasPct = 0;
int cpuPasBtnPct = 0;
int cpuThrottlePct = 0;
int cpuLightPct = 0;
int cpuSoundPct = 0;

void cpuProfileLoopBegin() {
  loopStartUs = micros();
}

void cpuProfileBegin(CpuSection section) {
  currentSection = section;
  secStartUs = micros();
}

void cpuProfileEnd() {
  cpuUs[currentSection] += micros() - secStartUs;
}

void cpuProfileLoopEnd() {
  cpuBusyTimeMicros += micros() - loopStartUs;

  if (millis() - cpuMeasureStartMs >= 1000) {
    unsigned long totalElapsedMs = millis() - cpuMeasureStartMs;
    if (totalElapsedMs > 0) {
      cpuUsagePercent = (int)constrain((cpuBusyTimeMicros * 100ULL) / (totalElapsedMs * 1000ULL), 0ULL, 100ULL);
      cpuPasPct       = (int)constrain((cpuUs[CPU_SEC_PAS] * 100ULL) / (totalElapsedMs * 1000ULL), 0ULL, 100ULL);
      cpuPasBtnPct    = (int)constrain((cpuUs[CPU_SEC_PAS_BTN] * 100ULL) / (totalElapsedMs * 1000ULL), 0ULL, 100ULL);
      cpuThrottlePct  = (int)constrain((cpuUs[CPU_SEC_THROTTLE] * 100ULL) / (totalElapsedMs * 1000ULL), 0ULL, 100ULL);
      cpuLightPct     = (int)constrain((cpuUs[CPU_SEC_LIGHT] * 100ULL) / (totalElapsedMs * 1000ULL), 0ULL, 100ULL);
      cpuSoundPct     = (int)constrain((cpuUs[CPU_SEC_SOUND] * 100ULL) / (totalElapsedMs * 1000ULL), 0ULL, 100ULL);
    }
    cpuBusyTimeMicros = 0;
    for (int i = 0; i < CPU_SEC_COUNT; i++) cpuUs[i] = 0;
    cpuMeasureStartMs = millis();
  }
}
