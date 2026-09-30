#ifndef DEBUG_CAPTURE_H
#define DEBUG_CAPTURE_H

#include <Arduino.h>
#include "web/web_handlers_telemetry.h" // DebugSample, BusEdge — единые определения

// ================= Отладочный буфер (график газа на /debug) =================
extern const int DEBUG_BUFFER_SIZE;                 // 200
extern DebugSample debugBuffer[];
extern int debugBufferHead;
extern portMUX_TYPE debugBufferMux;
extern const unsigned long DEBUG_SAMPLE_INTERVAL_MS; // 100 мс

// Запись сэмпла (вызывается из core/throttle.cpp)
void updateDebugBuffer(float throttleInV, float throttleOutV);

// ================= Пассивная запись шины дисплея (сниффер /debug) =================
extern int busCapturePin;                           // переключаемый пин (35/36/39, input-only)
extern const uint32_t BUS_CAPTURE_CAPACITY;         // 4096 фронтов
extern BusEdge busCapture[];
extern BusEdge busCaptureSnapshot[];
extern volatile uint32_t busCaptureHead;
extern volatile uint32_t busCaptureCount;
extern volatile uint32_t busCaptureTotal;
extern volatile uint32_t busCaptureOverwritten;
extern volatile uint32_t busCaptureStartedUs;
extern volatile uint32_t busCaptureLastUs;
extern volatile bool busCaptureRunning;
extern portMUX_TYPE busCaptureMux;

// Проверка, не занят ли пин захвата системной/пользовательской ролью
bool busCapturePinValid(int pin);
bool busCapturePinBusy(int pin, String *reason);

// ISR захвата фронтов (подключается через attachInterrupt)
void IRAM_ATTR onBusCaptureEdge();

// Очистка буфера захвата
void clearBusCapture();

// Снимок буфера: останавливает захват на время копирования, возвращает количество
uint32_t snapshotBusCapture(uint32_t &total, uint32_t &overwritten, uint32_t &startedUs, uint32_t &lastUs, bool &running);

#endif // DEBUG_CAPTURE_H