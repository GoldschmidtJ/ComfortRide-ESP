#include "system/debug_capture.h"
#include "system/hardware_config.h" // pinConfig, pinField, pinRoleName, PIN_ROLE_COUNT, CUSTOM_PIN_MAX
#include "system/inputs.h"          // isBrakePressed()
#include "core/pas.h"               // pasConfirmedActive, pasCurrentLevel, PAS_BUTTON_PIN
#include <Arduino.h>

// ================= Отладочный буфер (график газа на /debug) =================
const int DEBUG_BUFFER_SIZE = 200;
DebugSample debugBuffer[DEBUG_BUFFER_SIZE];
int debugBufferHead = 0;
unsigned long lastDebugSampleMs = 0;
portMUX_TYPE debugBufferMux = portMUX_INITIALIZER_UNLOCKED;
const unsigned long DEBUG_SAMPLE_INTERVAL_MS = 100;

// ================= Пассивная запись шины дисплея =================
int busCapturePin = 36; // кандидаты 35/36/39 (input-only), переключается с /debug
bool busCapturePinValid(int pin) {
  return pin == 35 || pin == 36 || pin == 39;
}
const uint32_t BUS_CAPTURE_CAPACITY = 4096;
BusEdge busCapture[BUS_CAPTURE_CAPACITY];
BusEdge busCaptureSnapshot[BUS_CAPTURE_CAPACITY];
volatile uint32_t busCaptureHead = 0;
volatile uint32_t busCaptureCount = 0;
volatile uint32_t busCaptureTotal = 0;
volatile uint32_t busCaptureOverwritten = 0;
volatile uint32_t busCaptureStartedUs = 0;
volatile uint32_t busCaptureLastUs = 0;
volatile bool busCaptureRunning = false;
portMUX_TYPE busCaptureMux = portMUX_INITIALIZER_UNLOCKED;

bool busCapturePinBusy(int pin, String *reason) {
  for (int i = 0; i < PIN_ROLE_COUNT; i++) {
    if (pinField(pinConfig, i) == pin) {
      if (reason) *reason = "GPIO" + String(pin) + " занят системной ролью «" + pinRoleName(i) + "»";
      return true;
    }
  }
  for (int i = 0; i < CUSTOM_PIN_MAX; i++) {
    if (customPins[i].used && customPins[i].gpio == pin) {
      if (reason) *reason = "GPIO" + String(pin) + " занят дополнительной ролью «" + String(customPins[i].name) + "»";
      return true;
    }
  }
  return false;
}

void IRAM_ATTR onBusCaptureEdge() {
  if (!busCaptureRunning) return;
  uint32_t now = micros();
  uint8_t level = (uint8_t)gpio_get_level((gpio_num_t)busCapturePin);
  portENTER_CRITICAL_ISR(&busCaptureMux);
  if (!busCaptureRunning) { portEXIT_CRITICAL_ISR(&busCaptureMux); return; }
  uint32_t idx = busCaptureHead;
  busCapture[idx] = {now, level};
  busCaptureHead = (idx + 1) % BUS_CAPTURE_CAPACITY;
  if (busCaptureCount < BUS_CAPTURE_CAPACITY) busCaptureCount++;
  else busCaptureOverwritten++;
  busCaptureTotal++;
  busCaptureLastUs = now;
  portEXIT_CRITICAL_ISR(&busCaptureMux);
}

void clearBusCapture() {
  portENTER_CRITICAL(&busCaptureMux);
  busCaptureHead = busCaptureCount = busCaptureTotal = busCaptureOverwritten = 0;
  busCaptureStartedUs = micros();
  busCaptureLastUs = busCaptureStartedUs;
  portEXIT_CRITICAL(&busCaptureMux);
}

uint32_t snapshotBusCapture(uint32_t &total, uint32_t &overwritten, uint32_t &startedUs, uint32_t &lastUs, bool &running) {
  portENTER_CRITICAL(&busCaptureMux);
  running = busCaptureRunning;
  busCaptureRunning = false;
  uint32_t count = busCaptureCount;
  uint32_t first = (busCaptureHead + BUS_CAPTURE_CAPACITY - count) % BUS_CAPTURE_CAPACITY;
  total = busCaptureTotal; overwritten = busCaptureOverwritten;
  startedUs = busCaptureStartedUs; lastUs = busCaptureLastUs;
  portEXIT_CRITICAL(&busCaptureMux);
  for (uint32_t i = 0; i < count; i++) busCaptureSnapshot[i] = busCapture[(first + i) % BUS_CAPTURE_CAPACITY];
  portENTER_CRITICAL(&busCaptureMux);
  if (running) busCaptureRunning = true;
  portEXIT_CRITICAL(&busCaptureMux);
  return count;
}

void updateDebugBuffer(float throttleInV, float throttleOutV) {
  unsigned long now = millis();
  if (now - lastDebugSampleMs < DEBUG_SAMPLE_INTERVAL_MS) return;
  lastDebugSampleMs = now;

  DebugSample sample;
  sample.tMs = now;
  sample.throttleInV = throttleInV;
  sample.throttleOutV = throttleOutV;
  sample.brake = isBrakePressed();
  sample.pasActive = pasConfirmedActive;
  sample.pasLevel = pasCurrentLevel;
  sample.btnPasPressed = (digitalRead(PAS_BUTTON_PIN) == LOW);
  portENTER_CRITICAL(&debugBufferMux);
  debugBuffer[debugBufferHead] = sample;
  debugBufferHead = (debugBufferHead + 1) % DEBUG_BUFFER_SIZE;
  portEXIT_CRITICAL(&debugBufferMux);
}