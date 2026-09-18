#ifndef WEB_HANDLERS_TELEMETRY_H
#define WEB_HANDLERS_TELEMETRY_H

#include <Arduino.h>
#include <WebServer.h>

struct DebugSample {
  unsigned long tMs;
  float throttleInV;
  float throttleOutV;
  bool brake;
  bool pasActive;
  int pasLevel;
  bool btnPasPressed;
};

struct BusEdge {
  uint32_t tUs;
  uint8_t level;
};

void handleSystemStatus();
void handleDebugPage();
void handleDebugData();
void handleBusCaptureControl();
void handleBusCaptureStatus();
void handleBusCaptureData();
void handleBusCaptureCsv();

#endif // WEB_HANDLERS_TELEMETRY_H
