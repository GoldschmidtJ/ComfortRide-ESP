#include "web/web_handlers_telemetry.h"
#include "system/debug_capture.h" // DEBUG_BUFFER_SIZE, debugBuffer, busCapture* (P3)
#include "web/html_pages.h"
#include "core/pas.h"
#include "core/cruise.h"
#include "core/lights.h"
#include "system/battery_sag.h" // batterySagGetStateData()
#include <WiFi.h>
#include <driver/gpio.h>
#include <math.h>
#include "system/version.h"             // FIRMWARE_VERSION — единая точка версии
#include "web/web_routes.h"
#include "web/param_utils.h"          // getArgInt
#include "core/throttle.h" // hwThrottle*, serviceThrottleLimitPct, serviceModeActive
#include "system/events_engine.h" // serviceModeActive, serviceThrottleLimitPct
#include "core/throttle.h" // hwThrottle*, serviceThrottleLimitPct
#include "system/cpu_profile.h" // cpuUsagePercent, cpuPasPct, ...
#include "system/network.h"     // MDNS_HOST
#include "system/storage.h"     // storedApSsid
#include "utils/utils.h"        // jsonEscape
// busCapture*/clearBusCapture/snapshotBusCapture/onBusCaptureEdge — из system/debug_capture.h (P3)
void handleDebugPage() { sendDebugPage(server); }

void handleSystemStatus() {
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  int ramPct = totalHeap > 0 ? (int)(((totalHeap - freeHeap) * 100) / totalHeap) : 0;

  String wifiMode = "OFF";
  int rssi = 0;
  String ssidName = "";
  String ipStr = "";
  if (WiFi.status() == WL_CONNECTED) {
    wifiMode = "STA";
    rssi = WiFi.RSSI();
    ssidName = WiFi.SSID();
    ipStr = WiFi.localIP().toString();
  } else if ((WiFi.getMode() & WIFI_MODE_AP) != 0) {
    wifiMode = "AP";
    ssidName = storedApSsid;
    ipStr = WiFi.softAPIP().toString();
  }

  float chipTemp = temperatureRead(); // Read internal temperature sensor

  String json = "{";
  json += "\"firmware_version\":\"" FIRMWARE_VERSION "\",";
  json += "\"cpu\":" + String(cpuUsagePercent) + ",";
  json += "\"cpu_pas\":" + String(cpuPasPct) + ",";
  json += "\"cpu_pas_btn\":" + String(cpuPasBtnPct) + ",";
  json += "\"cpu_throttle\":" + String(cpuThrottlePct) + ",";
  json += "\"cpu_light\":" + String(cpuLightPct) + ",";
  json += "\"cpu_sound\":" + String(cpuSoundPct) + ",";
  json += "\"ram_pct\":" + String(ramPct) + ",";
  json += "\"ram_free_kb\":" + String(freeHeap / 1024) + ",";
  json += "\"ram_total_kb\":" + String(totalHeap / 1024) + ",";
  json += "\"rom_sketch_kb\":" + String(ESP.getSketchSize() / 1024) + ",";
  json += "\"rom_total_kb\":" + String(ESP.getFlashChipSize() / 1024) + ",";
  json += "\"wifi_mode\":\"" + wifiMode + "\",";
  json += "\"wifi_ssid\":\"" + ssidName + "\",";
  json += "\"wifi_ip\":\"" + ipStr + "\",";
  json += "\"mdns_host\":\"" + String(MDNS_HOST) + ".local\",";
  json += "\"wifi_rssi\":" + String(rssi) + ",";
  json += "\"pas_en\":" + String(pasEnabled ? "true" : "false") + ",";
  json += "\"service\":" + String(serviceModeActive ? "true" : "false") + ",";
  json += "\"service_limit_pct\":" + String(serviceThrottleLimitPct) + ",";
  json += "\"gas_pct\":" + String(hwThrottlePct, 2) + ",";
  json += "\"gas_in_v\":" + String(hwThrottleInV, 2) + ",";
  json += "\"gas_out_v\":" + String(hwThrottleOutV, 2) + ",";
  json += "\"motor_pct\":" + String(hwMotorOutPct, 2) + ",";
  json += "\"brake\":" + String(hwBrakeActive ? "true" : "false") + ",";
  json += "\"headlight\":" + String(headlightOn ? "true" : "false") + ",";
  json += "\"drl\":" + String(drlOn ? "true" : "false") + ",";
  json += "\"light_mode\":" + String(lightCycleMode) + ",";
  json += "\"turn_left_act\":" + String(turnLeftActive ? "true" : "false") + ",";
  json += "\"turn_right_act\":" + String(turnRightActive ? "true" : "false") + ",";
  json += "\"turn_left\":" + String((turnLeftActive && turnBlinkState) ? "true" : "false") + ",";
  json += "\"turn_right\":" + String((turnRightActive && turnBlinkState) ? "true" : "false") + ",";
  json += "\"pas_active\":" + String(hwPasActive ? "true" : "false") + ",";
  json += "\"pas_lvl\":" + String(pasCurrentLevel) + ",";
  json += "\"pas_cnt\":" + String(pasLevelsCount) + ",";
  json += "\"cruise_en\":" + String(cruiseEnabled ? "true" : "false") + ",";
  json += "\"cruise_engaged\":" + String(cruiseEngaged ? "true" : "false") + ",";
  json += "\"cruise_pending\":" + String(cruisePendingResume ? "true" : "false") + ",";
  json += "\"cruise_conf_req\":" + String(cruiseConfirmRequired ? "true" : "false") + ",";
  json += "\"cruise_lvl\":" + String(cruiseCurrentLevel) + ",";
  json += "\"cruise_cnt\":" + String(cruiseLevelsCount) + ",";
  // Конфигурация поведения круиза для синхронизации FSM в эмуляторе
  json += "\"cruise_conf_thr\":" + String(cruiseConfirmThrottleAfterStart ? "true" : "false") + ",";
  json += "\"cruise_brk_mode\":" + String(cruiseAfterBrakingMode) + ",";
  json += "\"cruise_thr_mode\":" + String(cruiseAfterThrottleMode) + ",";
  json += "\"cruise_pcts\":[";
  {
    int safeCruiseCnt = constrain(cruiseLevelsCount, 0, 100); // CRUISE_MAX_LEVELS
    for (int i = 0; i < safeCruiseCnt; i++) {
      json += String(cruiseLevelPercent[i]);
      if (i < safeCruiseCnt - 1) json += ",";
    }
  }
  json += "],";
  json += "\"bt_active\":false,"; // Assuming this is a boolean
  json += "\"temp\":" + String(round(chipTemp)); // Add internal temperature, rounded
  // Батарейный саг-гард
  json += ",\"sag_state\":" + String(static_cast<int>(batterySagGetStateData().state));
  json += ",\"battery_mv\":" + String(batterySagGetStateData().battery_mv);
  json += ",\"battery_v_raw\":" + String(batterySagGetStateData().voltage_raw, 2);
  json += "}";
  server.send(200, "application/json", json);
}
void handleDebugData() {
  server.sendHeader("X-Firmware-Version", FIRMWARE_VERSION);
  String json = "[";
  bool first = true;
  for (int i = 0; i < DEBUG_BUFFER_SIZE; i++) {
    DebugSample s;
    portENTER_CRITICAL(&debugBufferMux);
    int idx = (debugBufferHead + i) % DEBUG_BUFFER_SIZE;
    s = debugBuffer[idx];
    portEXIT_CRITICAL(&debugBufferMux);
    if (s.tMs == 0) continue; // пропускаем неинициализированные сэмплы
    if (!first) json += ",";
    first = false;
    json += "{\"t\":" + String(s.tMs) +
            ",\"in\":" + String(s.throttleInV, 2) +
            ",\"out\":" + String(s.throttleOutV, 2) +
            ",\"brake\":" + (s.brake ? "1" : "0") +
            ",\"pas\":" + (s.pasActive ? "1" : "0") +
            ",\"btn\":" + (s.btnPasPressed ? "1" : "0") +
            ",\"lvl\":" + String(s.pasLevel) + "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleBusCaptureControl() {
  String command = server.arg("cmd");
  if (command == "pin") {
    // Выбор пина разведки: только input-only 35/36/39, захват должен быть остановлен
    int pin = getArgInt(server, "pin");
    if (!busCapturePinValid(pin)) { server.send(400, "text/plain", "Допустимы GPIO 35/36/39"); return; }
    if (busCaptureRunning) { server.send(409, "text/plain", "Сначала остановите захват"); return; }
    String reason;
    if (busCapturePinBusy(pin, &reason)) { server.send(409, "text/plain", reason); return; }
    busCapturePin = pin;
    server.send(200, "text/plain", "OK");
    return;
  }
  if (command == "start") {
    String reason;
    if (busCapturePinBusy(busCapturePin, &reason)) { server.send(409, "text/plain", reason); return; }
    if (!busCaptureRunning) {
      pinMode(busCapturePin, INPUT);
      clearBusCapture();
      attachInterrupt(digitalPinToInterrupt(busCapturePin), onBusCaptureEdge, CHANGE);
      portENTER_CRITICAL(&busCaptureMux);
      busCaptureRunning = true;
      busCaptureStartedUs = micros();
      busCaptureLastUs = busCaptureStartedUs;
      portEXIT_CRITICAL(&busCaptureMux);
    }
  } else if (command == "stop") {
    portENTER_CRITICAL(&busCaptureMux);
    busCaptureRunning = false;
    portEXIT_CRITICAL(&busCaptureMux);
    detachInterrupt(digitalPinToInterrupt(busCapturePin));
  } else if (command == "clear") {
    clearBusCapture();
  } else {
    server.send(400, "text/plain", "Неизвестная команда");
    return;
  }
  server.send(200, "text/plain", "OK");
}

void handleBusCaptureStatus() {
  uint32_t total, overwritten, startedUs, lastUs, count;
  bool running;
  portENTER_CRITICAL(&busCaptureMux);
  count = busCaptureCount; total = busCaptureTotal; overwritten = busCaptureOverwritten;
  startedUs = busCaptureStartedUs; lastUs = busCaptureLastUs; running = busCaptureRunning;
  portEXIT_CRITICAL(&busCaptureMux);
  String reason;
  bool busy = busCapturePinBusy(busCapturePin, &reason);
  uint32_t durationUs = total ? (lastUs - startedUs) : 0;
  String json = "{\"running\":" + String(running ? "true" : "false") +
                ",\"pin\":" + String(busCapturePin) +
                ",\"busy\":" + String(busy ? "true" : "false") +
                ",\"reason\":\"" + jsonEscape(reason) + "\"" +
                ",\"count\":" + String(count) +
                ",\"capacity\":" + String(BUS_CAPTURE_CAPACITY) +
                ",\"total\":" + String(total) +
                ",\"overwritten\":" + String(overwritten) +
                ",\"duration_us\":" + String(durationUs) + "}";
  server.send(200, "application/json", json);
}

void handleBusCaptureData() {
  uint32_t total, overwritten, startedUs, lastUs;
  bool running;
  uint32_t count = snapshotBusCapture(total, overwritten, startedUs, lastUs, running);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{\"running\":" + String(running ? "true" : "false") + ",\"overwritten\":" + String(overwritten) + ",\"edges\":[");
  String chunk;
  chunk.reserve(1024);
  for (uint32_t i = 0; i < count; i++) {
    if (i) chunk += ",";
    chunk += "[" + String(busCaptureSnapshot[i].tUs - startedUs) + "," + String(busCaptureSnapshot[i].level) + "]";
    if (chunk.length() >= 900) { server.sendContent(chunk); chunk = ""; }
  }
  if (chunk.length()) server.sendContent(chunk);
  server.sendContent("]}");
  server.sendContent("");
}

void handleBusCaptureCsv() {
  uint32_t total, overwritten, startedUs, lastUs;
  bool running;
  uint32_t count = snapshotBusCapture(total, overwritten, startedUs, lastUs, running);
  server.sendHeader("Content-Disposition", "attachment; filename=bus_gpio36.csv");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv; charset=utf-8", "");
  server.sendContent("time_us,delta_us,level\r\n");
  String chunk;
  chunk.reserve(1024);
  for (uint32_t i = 0; i < count; i++) {
    uint32_t timeUs = busCaptureSnapshot[i].tUs - startedUs;
    uint32_t deltaUs = i ? (busCaptureSnapshot[i].tUs - busCaptureSnapshot[i - 1].tUs) : timeUs;
    chunk += String(timeUs) + "," + String(deltaUs) + "," + String(busCaptureSnapshot[i].level) + "\r\n";
    if (chunk.length() >= 900) { server.sendContent(chunk); chunk = ""; }
  }
  if (chunk.length()) server.sendContent(chunk);
  server.sendContent("");
}
