#include "nvs_flash.h"

bool pasInterruptAttached = false;
#include <Arduino.h>

#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <math.h>

// ================= НАСТРОЙКА WiFi =================
const char* ssid = "Donut";
const char* password = "doughnut";
const char* MDNS_HOST = "openbike";

// Статический IP для надежного подключения к точке доступа телефона (Android)
const IPAddress staticSTAIP(192, 168, 43, 88);
const IPAddress staticSTAGateway(192, 168, 43, 1);
const IPAddress staticSTASubnet(255, 255, 255, 0);
const IPAddress staticSTADNS(192, 168, 43, 1);

DNSServer dnsServer;
const byte DNS_PORT = 53;

// ================= ПИНЫ (распаять сюда) =================
// Газ: GND -> GND, +5В -> 5В, Сигнал -> GPIO34 (ВНИМАНИЕ: см. предупреждение по напряжению ниже)
const int THROTTLE_ADC_PIN = 34;
// Выход газа на моторконтроллер — настоящий ЦАП ESP32 + усилитель на ОУ
// (MCP6002, канал Б). ЦАП сам по себе уже даёт чистое напряжение 0-3.3В,
// ОУ просто поднимает его до нужных 0-4.2В.
const int THROTTLE_DAC_PIN = 25;
// Тормоз: один провод -> GND, второй -> GPIO27
const int BRAKE_PIN = 27;
// PAS-датчик: GND -> GND, +5В -> 5В, Сигнал -> GPIO14
const int PAS_SENSOR_PIN = 14;
// Кнопка переключения уровня PAS: один контакт -> GND, второй -> GPIO13
const int PAS_BUTTON_PIN = 13;

// ================= СВЕТ / ЗВУК (передняя группа, 12В) =================
// Все переключаются MOSFET AOD418 (низкая сторона — минус нагрузки на этот
// пин, плюс нагрузки — на общую 12В-шину напрямую). Задний блок (48В,
// неизвестной конструкции) пока НЕ трогаем — см. обсуждение в чате.
const int HEADLIGHT_PIN   = 18; // фара, ШИМ
const int DRL_PIN         = 19; // ДХО, ШИМ
const int TURN_LEFT_PIN   = 21; // поворотник левый
const int TURN_RIGHT_PIN  = 22; // поворотник правый
const int HORN_PIN        = 23; // гудок
const int BUZZER_PIN      = 4;  // пищалка (тик поворотника)

const int BTN_HEADLIGHT_PIN  = 16; // кнопка фары
const int BTN_TURN_LEFT_PIN  = 17; // кнопка поворотник влево
const int BTN_TURN_RIGHT_PIN = 32; // кнопка поворотник вправо
const int BTN_HORN_PIN       = 33; // кнопка гудка (отжимная)

const int HEADLIGHT_DEFAULT_BRIGHTNESS = 220; // 0-255
const int DRL_DEFAULT_BRIGHTNESS       = 60;  // 0-255, горит всегда

// Аппаратные пределы ESP32 — константы, не настройки. АЦП и ЦАП работают
// в диапазоне 0-3.3В. Если ручка газа реально выдаёт больше (многие выдают
// до 4.2В) — НАПРЯМУЮ подавать на ADC-пин нельзя, нужен делитель напряжения
// (два резистора), иначе есть риск спалить пин.
const float HW_MAX_VOLTAGE = 3.3f;

WebServer server(80);
Preferences prefs;
String storedApSsid = "BikeControllerAP";
String storedApPass = "";

// Forward declarations
void wifiCredsSave(const String &newSsid, const String &newPass);
void wifiConnect();
void updateWifiStateMachine();
void setThrottleOutputSafeZero();
void throttleSettingsSave();
void throttleSettingsLoad();
void pasSettingsSave();
void pasSettingsLoad();
void reattachPasInterrupt();
void handleHub();
void handleThrottlePage();
void handleThrottleCalMin();
void handleThrottleCalMax();
void handleThrottleSave();
void handlePasPage();
void handlePasSave();
void handlePasCalStart();
void handlePasCalStatus();
void handlePasCalStop();
void handleApiPasSetLevel();
void handleApiPasToggleMode();
void handleApiCruiseToggleMode();
void handleWifiPage();
void handleWifiScan();
void handleWifiSave();
void handleDebugPage();
void handleDebugData();
void handleSystemPage();
void handleSettingsExport();
void handleSettingsImport();
void handleUpdatePage();
void handleSystemStatus();
void criticalControlTask(void *pvParameters);
void nonCriticalTask(void *pvParameters);

// Forward declarations of state variables used in status endpoint
extern bool pasEnabled;
extern int pasCurrentLevel;
extern int pasLevelsCount;
extern bool cruiseEnabled;
extern int cruiseCurrentLevel;
extern int cruiseLevelsCount;
extern bool cruiseEngaged;


// ================= System status tracking ================
unsigned long cpuMeasureStartMs = 0;
unsigned long cpuBusyTimeMicros = 0;
int cpuUsagePercent = 0;

// ================= Real-time telemetry variables ================
volatile float hwThrottleInV = 0.0f;
volatile float hwThrottleOutV = 0.0f;
volatile float hwThrottlePct = 0.0f;
volatile float hwMotorOutPct = 0.0f;
volatile bool hwBrakeActive = false;
volatile bool hwPasActive = false;

// ================= Веб: статус системы (JSON endpoint) ================
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
  json += "\"cpu\":" + String(cpuUsagePercent) + ","; // Assuming cpuUsagePercent is globally available and updated
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
  json += "\"gas_pct\":" + String(hwThrottlePct, 2) + ",";
  json += "\"gas_in_v\":" + String(hwThrottleInV, 2) + ",";
  json += "\"gas_out_v\":" + String(hwThrottleOutV, 2) + ",";
  json += "\"motor_pct\":" + String(hwMotorOutPct, 2) + ",";
  json += "\"brake\":" + String(hwBrakeActive ? "true" : "false") + ",";
  json += "\"pas_active\":" + String(hwPasActive ? "true" : "false") + ",";
  json += "\"pas_lvl\":" + String(pasCurrentLevel) + ",";
  json += "\"pas_cnt\":" + String(pasLevelsCount) + ",";
  json += "\"cruise_en\":" + String(cruiseEnabled ? "true" : "false") + ",";
  json += "\"cruise_engaged\":" + String(cruiseEngaged ? "true" : "false") + ",";
  json += "\"cruise_lvl\":" + String(cruiseCurrentLevel) + ",";
  json += "\"cruise_cnt\":" + String(cruiseLevelsCount) + ",";
  json += "\"bt_active\":false,"; // Assuming this is a boolean
  json += "\"temp\":" + String(round(chipTemp)); // Add internal temperature, rounded
  json += "}";
  server.send(200, "application/json", json);
}
void cruiseSettingsSave();
void cruiseSettingsLoad();
void handleCruisePage();
void handleCruiseSave();

void handleUpdateResult();
void handleUpdateUpload();

// AP Settings forward declarations
void handleApSave();
void apSettingsSave();
void apSettingsLoad();

// ================= ТОРМОЗ =================
bool isBrakePressed() { return digitalRead(BRAKE_PIN) == LOW; }
bool ownBrakeCutoffEnabled = true; // дублировать отключение газа по тормозу (доп. к контроллеру)

// ================= ГАЗ: аппаратное согласование напряжений =================
// У ESP32 АЦП/ЦАП работают 0-3.3В, а ручка газа/контроллер — обычно 0-4.2В.
// На входе стоит резисторный делитель (понижает), на выходе — усилитель на
// ОУ (повышает). Эти коэффициенты описывают ЧТО РЕАЛЬНО СТОИТ в железе —
// дальше калибровка ниже работает уже в "настоящих" вольтах на проводах,
// а не на ножках ESP32.
float throttleInputDividerRatio = 20.0f / (10.0f + 20.0f); // R2/(R1+R2), R1=10к, R2=20к по умолчанию
float throttleOutputGain = 1.0f + 3.3f / 10.0f; // 1 + R4/R3 (MCP6002, канал Б), R3=10к, R4=3.3к по умолчанию

// ================= ГАЗ: калибровка (в РЕАЛЬНЫХ вольтах на проводах) =================
float throttleInMinV = 1.1f, throttleInMaxV = 4.1f;
float throttleOutMinV = 1.1f, throttleOutMaxV = 4.1f;
bool throttleExtendedRangeAllowed = false; // задел на будущее, сейчас ничего не меняет физически

float calibrateThrottleV(float rawV) {
  float inMin = throttleInMinV;
  float inMax = (throttleInMaxV > inMin + 0.01f) ? throttleInMaxV : (inMin + 0.01f);
  float outMin = throttleOutMinV;
  float outMax = (throttleOutMaxV > outMin + 0.01f) ? throttleOutMaxV : (outMin + 0.01f);
  float clamped = rawV;
  if (clamped < inMin) clamped = inMin;
  if (clamped > inMax) clamped = inMax;
  float norm = (clamped - inMin) / (inMax - inMin);
  float outV = outMin + norm * (outMax - outMin);
  return outV; // это ЦЕЛЕВОЕ напряжение на проводе к контроллеру, ещё не значение для ЦАП
}

// --- New Cruise Control configuration ---
const int CRUISE_MAX_LEVELS = 100;
int cruiseLevelsCount = 3;
float cruiseLevelPercent[CRUISE_MAX_LEVELS];
bool cruiseEnabled = false; // Cruise control выключен по умолчанию
int cruiseCurrentLevel = 0; // 0 = выключен
float cruiseStartPercent = 20.0f; // Начальное значение для автораспределения
float cruiseEndPercent = 100.0f;  // Конечное значение для автораспределения
bool cruiseConfirmThrottleAfterStart = false; // подтверждать газом после старта/восстановления
int cruiseAfterBrakingMode = 1;  // 0: сброс, 1: подтверждение газом (default), 2: восстановление к предыдущему
int cruiseAfterThrottleMode = 2; // 0: сброс, 1: подтверждение газом, 2: восстановление к предыдущему (default)
bool cruiseSoftStartEnabled = false;
bool cruiseSoftStopEnabled = false;
unsigned long cruiseSoftStartMs = 500;
unsigned long cruiseSoftStopMs = 800;

// Состояние круиз-контроля для конечного автомата
bool cruiseEngaged = false;         // Выдаётся ли тяга круиза на мотор
bool cruisePendingResume = false;   // Ожидает ли возобновления (уровень сохранён, но тяга отключена)
bool cruiseConfirmRequired = false; // Требуется ли подтверждение через газ для включения тяги
bool cruiseReleaseSeen = true;      // Защита: был ли газ отпущен перед новым нажатием для подтверждения

void armCruisePending(bool confirmRequired, float throttlePct) {
  cruiseEngaged = false;
  cruisePendingResume = true;
  cruiseConfirmRequired = confirmRequired;
  cruiseReleaseSeen = (throttlePct <= 10.0f);
}

float cruiseSmoothOutV = 0;
unsigned long cruiseSmoothLastMs = 0;

// Auto-distribution for cruise levels (между cruiseStartPercent и cruiseEndPercent)
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

float applyCruiseSmoothing(float targetV) {
  unsigned long now = millis();
  unsigned long dt = now - cruiseSmoothLastMs;
  if (dt == 0) dt = 1;
  cruiseSmoothLastMs = now;

  float diff = targetV - cruiseSmoothOutV;
  bool rising = diff > 0;
  bool enabled = rising ? cruiseSoftStartEnabled : cruiseSoftStopEnabled;
  unsigned long tau = rising ? cruiseSoftStartMs : cruiseSoftStopMs;

  if (!enabled || tau == 0) { cruiseSmoothOutV = targetV; return targetV; }

  // Линейная рампа: за tau мс выход ГАРАНТИРОВАННО доходит до цели
  // (экспонента за то же время давала лишь ~63%, ощущалось как лаг)
  float step = diff * ((float)dt / (float)tau);
  cruiseSmoothOutV += step;
  if ((diff > 0 && cruiseSmoothOutV > targetV) || (diff < 0 && cruiseSmoothOutV < targetV)) {
    cruiseSmoothOutV = targetV;
  }
  return cruiseSmoothOutV;
}

float getCruiseTargetV() {
  if (!cruiseEnabled || !cruiseEngaged) return 0;
  if (cruiseCurrentLevel <= 0 || cruiseCurrentLevel > cruiseLevelsCount) return 0;
  float pct = cruiseLevelPercent[cruiseCurrentLevel - 1];
  float outMin = throttleOutMinV;
  float outMax = (throttleOutMaxV > outMin + 0.01f) ? throttleOutMaxV : (outMin + 0.01f);
  return outMin + (pct / 100.0f) * (outMax - outMin);
}

// ================= ГАЗ: мягкий старт / мягкий стоп (в вольтах) =================
bool throttleSoftStartEnabled = false;
bool throttleSoftStopEnabled = false;
unsigned long throttleSoftStartMs = 500;
unsigned long throttleSoftStopMs  = 500;

float throttleSmoothOutV = 0;
unsigned long throttleSmoothLastMs = 0;

float applyThrottleSmoothing(float targetV) {
  unsigned long now = millis();
  unsigned long dt = now - throttleSmoothLastMs;
  if (dt == 0) dt = 1;
  throttleSmoothLastMs = now;

  float diff = targetV - throttleSmoothOutV;
  bool rising = diff > 0;
  bool enabled = rising ? throttleSoftStartEnabled : throttleSoftStopEnabled;
  unsigned long tau = rising ? throttleSoftStartMs : throttleSoftStopMs;

  if (!enabled || tau == 0) { throttleSmoothOutV = targetV; return targetV; }

  // Линейная рампа: за tau мс выход ГАРАНТИРОВАННО доходит до цели
  float step = diff * ((float)dt / (float)tau);
  throttleSmoothOutV += step;
  if ((diff > 0 && throttleSmoothOutV > targetV) || (diff < 0 && throttleSmoothOutV < targetV)) {
    throttleSmoothOutV = targetV;
  }
  return throttleSmoothOutV;
}

void throttleSettingsSave() {
  prefs.begin("throttle", false);
  prefs.putFloat("inMinV", throttleInMinV);
  prefs.putFloat("inMaxV", throttleInMaxV);
  prefs.putFloat("outMinV", throttleOutMinV);
  prefs.putFloat("outMaxV", throttleOutMaxV);
  prefs.putFloat("divRatio", throttleInputDividerRatio);
  prefs.putFloat("gain", throttleOutputGain);
  prefs.putInt("extRange", throttleExtendedRangeAllowed ? 1 : 0);
  prefs.putInt("ssEn", throttleSoftStartEnabled ? 1 : 0);
  prefs.putInt("spEn", throttleSoftStopEnabled ? 1 : 0);
  prefs.putULong("ssMs", throttleSoftStartMs);
  prefs.putULong("spMs", throttleSoftStopMs);
  prefs.putInt("brakeCut", ownBrakeCutoffEnabled ? 1 : 0);
  prefs.end();
}

void throttleSettingsLoad() {
  prefs.begin("throttle", true);
  throttleInMinV = prefs.getFloat("inMinV", 1.1f);
  throttleInMaxV = prefs.getFloat("inMaxV", 4.1f);
  throttleOutMinV = prefs.getFloat("outMinV", 1.1f);
  throttleOutMaxV = prefs.getFloat("outMaxV", 4.1f);
  throttleInputDividerRatio = prefs.getFloat("divRatio", 24.0f / 34.0f);
  throttleOutputGain = prefs.getFloat("gain", 1.27f);
  throttleExtendedRangeAllowed = prefs.getInt("extRange", 0) != 0;
  throttleSoftStartEnabled = prefs.getInt("ssEn", 0) != 0;
  throttleSoftStopEnabled = prefs.getInt("spEn", 0) != 0;
  throttleSoftStartMs = prefs.getULong("ssMs", 500);
  throttleSoftStopMs = prefs.getULong("spMs", 500);
  ownBrakeCutoffEnabled = prefs.getInt("brakeCut", 1) != 0;
  prefs.end();
}

// ================= PAS: настройки датчика =================
int pasMagnetCount = 12;
int pasEdgeMode = FALLING;
int pasActivationAngle = 180;
unsigned long pasTimeoutMs = 350;      // тайм-аут СБРОСА накопленных импульсов (долгий)
unsigned long pasStopTimeoutMs = 200;  // тайм-аут ОТКЛЮЧЕНИЯ при остановке педалей (быстрый)

volatile unsigned long pasLastPulseMicros = 0;
volatile unsigned long pasConsecutivePulses = 0;
bool pasConfirmedActive = false;

extern float pasSmoothOutV; // определён ниже, в блоке мягкого старта/стопа
extern volatile bool pasCalRunning;          // калибровка магнитов (определены ниже)
extern volatile unsigned long pasCalPulses;
extern volatile unsigned long pasCalLastPulseMicros;

void IRAM_ATTR onPasPulse() {
  pasLastPulseMicros = micros();
  pasConsecutivePulses++;
  if (pasCalRunning) {
    pasCalPulses++;
    pasCalLastPulseMicros = pasLastPulseMicros;
  }
}

int pasRequiredPulses() {
  int required = (int)round(pasActivationAngle * pasMagnetCount / 360.0f);
  if (required < 1) required = 1; // без max() — раньше тут была ошибка компиляции из-за смешения типов
  return required;
}

void updatePasDetection() {
  unsigned long now = micros();
  if (pasLastPulseMicros == 0) { pasConfirmedActive = false; return; }
  unsigned long sincePulseUs = now - pasLastPulseMicros;

  // После полной паузы начинаем новую последовательность импульсов.
  if (sincePulseUs >= pasTimeoutMs * 1000UL) {
    pasConsecutivePulses = 0;
    pasConfirmedActive = false;
    return;
  }

  // Остановка педалей: отключаем тягу быстро. Важно: пока действует этот
  // тайм-аут, нельзя повторно выполнить условие активации по старому счётчику.
  if (sincePulseUs >= pasStopTimeoutMs * 1000UL) {
    if (pasConfirmedActive) {
      pasConfirmedActive = false;
      pasSmoothOutV = 0;
    }
    return;
  }

  // Только свежий импульсный поток может поддерживать/включать PAS.
  if (pasConsecutivePulses >= (unsigned long)pasRequiredPulses()) {
    pasConfirmedActive = true;
  }
}

void reattachPasInterrupt() {
  if (pasInterruptAttached) {
    detachInterrupt(digitalPinToInterrupt(PAS_SENSOR_PIN));
  }
  attachInterrupt(digitalPinToInterrupt(PAS_SENSOR_PIN), onPasPulse, pasEdgeMode);
  pasInterruptAttached = true;
}

// ================= PAS: калибровка количества магнитов =================
// Пользователь проворачивает педали ровно на 2 полных оборота, считаются
// импульсы датчика; pasMagnetCount = импульсы / 2.
volatile bool pasCalRunning = false;
volatile unsigned long pasCalPulses = 0;
volatile unsigned long pasCalLastPulseMicros = 0;
unsigned long pasCalStartMs = 0;
const unsigned long PAS_CAL_TIMEOUT_MS = 60000; // авто-стоп калибровки через 60 с

// ================= PAS: уровни (усилие в %, до 20 штук) =================
const int PAS_MAX_LEVELS = 20;
int pasLevelsCount = 3;
int pasLevelPercent[PAS_MAX_LEVELS];
int pasCurrentLevel = 0; // 0 = выключен (edge case "PAS 0 = 0%" — заложено по умолчанию)

void pasAutoDistribute() {
  for (int i = 0; i < pasLevelsCount; i++) {
    pasLevelPercent[i] = (int)round((float)(i + 1) * 100.0f / (float)pasLevelsCount);
  }
}

// ================= PAS: мягкий старт / мягкий стоп (в вольтах) =================
bool pasSoftStartEnabled = false;
bool pasSoftStopEnabled = false;
bool pasEnabled = true; // PAS enabled by default
unsigned long pasSoftStartMs = 500;
unsigned long pasSoftStopMs = 800;

float pasSmoothOutV = 0;
unsigned long pasSmoothLastMs = 0;

float applyPasSmoothing(float targetV) {
  unsigned long now = millis();
  unsigned long dt = now - pasSmoothLastMs;
  if (dt == 0) dt = 1;
  pasSmoothLastMs = now;

  float diff = targetV - pasSmoothOutV;
  bool rising = diff > 0;
  bool enabled = rising ? pasSoftStartEnabled : pasSoftStopEnabled;
  unsigned long tau = rising ? pasSoftStartMs : pasSoftStopMs;

  if (!enabled || tau == 0) { pasSmoothOutV = targetV; return targetV; }

  // Мёртвая зона контроллера: пока выход ниже throttleOutMinV, мотор не крутится.
  // При старте сразу прыгаем на нижнюю границу, чтобы не тратить время рампы
  // на бесполезный участок 0В -> throttleOutMinV (раньше это давало лишнюю задержку).
  float outMin = throttleOutMinV;
  if (rising && pasSmoothOutV < outMin) pasSmoothOutV = outMin;

  float diff2 = targetV - pasSmoothOutV;
  if (diff2 <= 0) { pasSmoothOutV = targetV; return targetV; }

  // Линейная рампа: за tau мс выход ГАРАНТИРОВАННО доходит до цели
  float step = diff2 * ((float)dt / (float)tau);
  pasSmoothOutV += step;
  if (pasSmoothOutV > targetV) pasSmoothOutV = targetV;
  return pasSmoothOutV;
}

float getPasTargetV() {
  if (!pasEnabled) return 0; // PAS fully disabled
  if (pasCurrentLevel <= 0 || pasCurrentLevel > pasLevelsCount) return 0;
  if (!pasConfirmedActive) return 0;
  float pct = pasLevelPercent[pasCurrentLevel - 1];
  float outMin = throttleOutMinV;
  float outMax = (throttleOutMaxV > outMin + 0.01f) ? throttleOutMaxV : (outMin + 0.01f);
  return outMin + (pct / 100.0f) * (outMax - outMin);
}

void pasSettingsSave() {
  prefs.begin("pas", false);
  prefs.putInt("magnets", pasMagnetCount);
  prefs.putInt("edge", pasEdgeMode);
  prefs.putInt("angle", pasActivationAngle);
  prefs.putULong("timeout", pasTimeoutMs);
  prefs.putULong("stopTO", pasStopTimeoutMs);
  prefs.putInt("cnt", pasLevelsCount);
  prefs.putBytes("pct", pasLevelPercent, sizeof(pasLevelPercent));
  prefs.putInt("ssEn", pasSoftStartEnabled ? 1 : 0);
  prefs.putInt("spEn", pasSoftStopEnabled ? 1 : 0);
  prefs.putULong("ssMs", pasSoftStartMs);
  prefs.putULong("spMs", pasSoftStopMs);
  prefs.putInt("enabled", pasEnabled ? 1 : 0);
  prefs.end();
}

void pasSettingsLoad() {
  prefs.begin("pas", false);
  pasMagnetCount = prefs.getInt("magnets", 12);
  pasEdgeMode = prefs.getInt("edge", FALLING);
  pasActivationAngle = prefs.getInt("angle", 180);
  pasTimeoutMs = prefs.getULong("timeout", 350);
  pasStopTimeoutMs = prefs.getULong("stopTO", 200);
  if (pasStopTimeoutMs < 20) pasStopTimeoutMs = 20;
  pasLevelsCount = prefs.getInt("cnt", 3);
  size_t got = prefs.getBytes("pct", pasLevelPercent, sizeof(pasLevelPercent));
  pasSoftStartEnabled = prefs.getInt("ssEn", 0) != 0;
  pasSoftStopEnabled = prefs.getInt("spEn", 0) != 0;
  pasSoftStartMs = prefs.getULong("ssMs", 500);
  pasSoftStopMs = prefs.getULong("spMs", 800);
  pasEnabled = (prefs.getInt("enabled", 1) != 0);
  prefs.end();
  if (got != sizeof(pasLevelPercent)) {
    pasAutoDistribute();
  }
}

// ================= Кнопка переключения уровня PAS =================
int btnLastReading = HIGH, btnStable = HIGH;
unsigned long btnLastDebounce = 0;
const unsigned long DEBOUNCE_MS = 50;

void updatePasButton() {
  int reading = digitalRead(PAS_BUTTON_PIN);
  if (reading != btnLastReading) btnLastDebounce = millis();
  if (millis() - btnLastDebounce > DEBOUNCE_MS) {
    if (reading != btnStable) {
      btnStable = reading;
      if (btnStable == LOW) {
        pasCurrentLevel = (pasCurrentLevel + 1) % (pasLevelsCount + 1);
        pasEnabled = (pasCurrentLevel > 0);
        if (pasEnabled) {
          cruiseEnabled = false; // Отключаем круиз при физическом переключении PAS
          cruiseCurrentLevel = 0;
          cruiseEngaged = false;
          cruisePendingResume = false;
        }
        Serial.printf("PAS уровень: %d/%d\n", pasCurrentLevel, pasLevelsCount);
      }
    }
  }
  btnLastReading = reading;
}

// ================= Свет: фара, ДХО =================
bool headlightOn = false;

void toggleHeadlight() {
  headlightOn = !headlightOn;
  ledcWrite(0, headlightOn ? HEADLIGHT_DEFAULT_BRIGHTNESS : 0);
  Serial.println(headlightOn ? "Фара: ВКЛ" : "Фара: ВЫКЛ");
}

// ================= Пищалка (тик поворотника) =================
bool buzzerOn = false;
unsigned long buzzerOffAtMs = 0;

void buzzerClick(unsigned long durationMs) {
  digitalWrite(BUZZER_PIN, HIGH);
  buzzerOn = true;
  buzzerOffAtMs = millis() + durationMs;
}

void updateBuzzer() {
  if (buzzerOn && millis() >= buzzerOffAtMs) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerOn = false;
  }
}

// ================= Поворотники =================
bool turnLeftActive = false, turnRightActive = false;
unsigned long turnLastBlinkMs = 0;
bool turnBlinkState = false;
const unsigned long TURN_BLINK_MS = 500;

void toggleTurnLeft() {
  turnLeftActive = !turnLeftActive;
  if (turnLeftActive) turnRightActive = false;
}
void toggleTurnRight() {
  turnRightActive = !turnRightActive;
  if (turnRightActive) turnLeftActive = false;
}

void updateTurnSignals() {
  if (!turnLeftActive && !turnRightActive) {
    digitalWrite(TURN_LEFT_PIN, LOW);
    digitalWrite(TURN_RIGHT_PIN, LOW);
    return;
  }
  if (millis() - turnLastBlinkMs >= TURN_BLINK_MS) {
    turnLastBlinkMs = millis();
    turnBlinkState = !turnBlinkState;
    buzzerClick(50); // тик на каждой смене состояния мигания
  }
  digitalWrite(TURN_LEFT_PIN,  (turnLeftActive  && turnBlinkState) ? HIGH : LOW);
  digitalWrite(TURN_RIGHT_PIN, (turnRightActive && turnBlinkState) ? HIGH : LOW);
}

// ================= Гудок =================
void updateHorn() {
  bool hornPressed = (digitalRead(BTN_HORN_PIN) == LOW);
  digitalWrite(HORN_PIN, hornPressed ? HIGH : LOW);
}

// ================= Физические кнопки: фара, поворотники (дебаунс) =================
struct DebouncedButton {
  int pin; int lastReading; int stableState; unsigned long lastDebounceMs; void (*action)();
};

DebouncedButton lightButtons[] = {
  { BTN_HEADLIGHT_PIN,  HIGH, HIGH, 0, toggleHeadlight },
  { BTN_TURN_LEFT_PIN,  HIGH, HIGH, 0, toggleTurnLeft },
  { BTN_TURN_RIGHT_PIN, HIGH, HIGH, 0, toggleTurnRight },
};
const int lightButtonsCount = sizeof(lightButtons) / sizeof(lightButtons[0]);

void updateLightButtons() {
  for (int i = 0; i < lightButtonsCount; i++) {
    DebouncedButton &b = lightButtons[i];
    int reading = digitalRead(b.pin);
    if (reading != b.lastReading) b.lastDebounceMs = millis();
    if (millis() - b.lastDebounceMs > DEBOUNCE_MS) {
      if (reading != b.stableState) {
        b.stableState = reading;
        if (b.stableState == LOW) b.action();
      }
    }
    b.lastReading = reading;
  }
}

// ================= Отладочный буфер (для графиков в браузере) =================
// Копим последние несколько секунд значений — газ вход/выход, тормоз, PAS.
// Страница /debug рисует это как бегущий график, вроде мини-осциллографа.
const int DEBUG_BUFFER_SIZE = 200; // уменьшено для стабильности JSON-ответа (около 1-2 сек истории при семпле 20мс)

struct DebugSample {
  unsigned long tMs;
  float throttleInV;
  float throttleOutV;
  bool brake;
  bool pasActive;
  int pasLevel;
  bool btnPasPressed;
};

DebugSample debugBuffer[DEBUG_BUFFER_SIZE];
int debugBufferHead = 0;
unsigned long lastDebugSampleMs = 0;
const unsigned long DEBUG_SAMPLE_INTERVAL_MS = 100; // Sample every 100ms for oscilloscope

void updateDebugBuffer(float throttleInV, float throttleOutV) {
  unsigned long now = millis();
  if (now - lastDebugSampleMs < DEBUG_SAMPLE_INTERVAL_MS) return;
  lastDebugSampleMs = now;

  DebugSample &s = debugBuffer[debugBufferHead];
  s.tMs = now;
  s.throttleInV = throttleInV;
  s.throttleOutV = throttleOutV;
  s.brake = isBrakePressed();
  s.pasActive = pasConfirmedActive;
  s.pasLevel = pasCurrentLevel;
  s.btnPasPressed = (digitalRead(PAS_BUTTON_PIN) == LOW);
  debugBufferHead = (debugBufferHead + 1) % DEBUG_BUFFER_SIZE;
}

// ================= Throttle-by-wire =================
void setThrottleOutputSafeZero() {
  dacWrite(THROTTLE_DAC_PIN, 0);
}

void updateThrottle() {
  // Физический тормоз всегда имеет приоритет над выходом газа.
  // ownBrakeCutoffEnabled относится только к дополнительной программной
  // функции; удержание throttleOutMinV никогда не должно работать при тормозе.
  bool brakePressed = isBrakePressed();

  int raw = analogRead(THROTTLE_ADC_PIN);
  float adcPinV = raw * HW_MAX_VOLTAGE / 4095.0f;
  // На ножке ESP32 напряжение уже ослаблено делителем — пересчитываем
  // обратно в реальное напряжение на проводе ручки газа.
  float realGripV = adcPinV / throttleInputDividerRatio;

  float throttleTargetV = calibrateThrottleV(realGripV); // целевое напряжение на проводе К КОНТРОЛЛЕРУ
  float throttleOutV = applyThrottleSmoothing(throttleTargetV);

  // Расчет процента нажатия ручки газа (0-100%) для конечного автомата круиза
  float throttleSpan = (throttleInMaxV - throttleInMinV);
  float throttlePct = 0.0f;
  if (throttleSpan > 0.05f) {
    throttlePct = ((realGripV - throttleInMinV) / throttleSpan) * 100.0f;
  }
  if (throttlePct < 0.0f) throttlePct = 0.0f;
  if (throttlePct > 100.0f) throttlePct = 100.0f;

  // Конечный автомат круиз-контроля (State Machine)
  if (cruiseEnabled && cruiseCurrentLevel > 0) {
    if (brakePressed) {
      // Тормоз активен: переход в pending режим или сброс в зависимости от настроек
      if (cruiseEngaged || !cruisePendingResume) {
        if (cruiseAfterBrakingMode == 0) {
          cruiseEnabled = false;
          cruiseCurrentLevel = 0;
          cruiseEngaged = false;
          cruisePendingResume = false;
        } else if (cruiseAfterBrakingMode == 1) {
          armCruisePending(true, throttlePct);
        } else if (cruiseAfterBrakingMode == 2) {
          armCruisePending(false, throttlePct);
        }
      }
    } else {
      // Тормоз отпущен: обработка перегазовки и возобновления
      if (cruiseEngaged && throttlePct > 10.0f) {
        // Перегазовка: ручка газа нажата выше порога во время активного круиза
        if (cruiseAfterThrottleMode == 0) {
          cruiseEnabled = false;
          cruiseCurrentLevel = 0;
          cruiseEngaged = false;
          cruisePendingResume = false;
        } else if (cruiseAfterThrottleMode == 1) {
          armCruisePending(true, throttlePct);
        } else if (cruiseAfterThrottleMode == 2) {
          armCruisePending(false, throttlePct);
        }
      } else if (!cruiseEngaged && cruisePendingResume) {
        if (cruiseConfirmRequired) {
          if (throttlePct <= 10.0f) {
            cruiseReleaseSeen = true;
          } else if (cruiseReleaseSeen && throttlePct > 10.0f) {
            cruiseEngaged = true;
            cruisePendingResume = false;
          }
        } else {
          // Авто-возобновление: как только газ отпущен ниже порога, включаем круиз обратно
          if (throttlePct <= 10.0f) {
            cruiseEngaged = true;
            cruisePendingResume = false;
          }
        }
      }
    }
  }

  float pasTargetV = getPasTargetV();
  float pasOutV = applyPasSmoothing(pasTargetV);

  float cruiseTargetV = getCruiseTargetV();
  float cruiseOutV = applyCruiseSmoothing(cruiseTargetV);

  float combinedV = throttleOutV;
  if (pasOutV > combinedV) combinedV = pasOutV;
  if (cruiseOutV > combinedV) combinedV = cruiseOutV;

  // Если контроллер включён, но ни один режим не дал команды выше нуля,
  // мы всё равно удерживаем выход на уровне throttleOutMinV (готовность),
  // чтобы убрать задержку реакции контроллера на старт.
  if (combinedV < throttleOutMinV) {
    combinedV = throttleOutMinV;
  }

  // Обновляем глобальные переменные телеметрии
  hwThrottleInV = realGripV;
  hwThrottleOutV = brakePressed ? 0.0f : combinedV;
  hwThrottlePct = throttlePct;
  // Процент мощности считаем относительно РАБОЧЕГО диапазона (outMin..outMax),
  // иначе из-за удержания outMinV в простое телеметрия показывала бы ~27%.
  {
    float outSpan = throttleOutMaxV - throttleOutMinV;
    float motorPct = (outSpan > 0.05f) ? ((combinedV - throttleOutMinV) / outSpan) * 100.0f : 0.0f;
    if (motorPct < 0.0f) motorPct = 0.0f;
    if (motorPct > 100.0f) motorPct = 100.0f;
    hwMotorOutPct = brakePressed ? 0.0f : motorPct;
  }
  hwBrakeActive = brakePressed;
  hwPasActive = pasConfirmedActive;

  if (brakePressed) {
    setThrottleOutputSafeZero();
    throttleSmoothOutV = 0;
    pasSmoothOutV = 0;
    cruiseSmoothOutV = 0;
    updateDebugBuffer(0, 0);
    return;
  }

  // ОУ (канал Б) поднимает напряжение в throttleOutputGain раз — значит на
  // сам ЦАП нужно подать МЕНЬШЕ, чтобы после усиления получить цель.
  float dacTargetV = combinedV / throttleOutputGain;
  if (dacTargetV > HW_MAX_VOLTAGE) dacTargetV = HW_MAX_VOLTAGE; // физический предел ЦАП — не обойти
  if (dacTargetV < 0) dacTargetV = 0;
  int dacVal = (int)round(dacTargetV / HW_MAX_VOLTAGE * 255.0f);
  if (dacVal < 0) dacVal = 0;
  if (dacVal > 255) dacVal = 255;
  dacWrite(THROTTLE_DAC_PIN, dacVal);

  updateDebugBuffer(realGripV, combinedV);
}

// ================= Общий компонент: Статус-бар в шапке (Функции-генераторы) =================
String getTopBarCss() {
  return String(R"rawliteral(
.top-bar-sticky{position:sticky;top:0;left:0;right:0;z-index:9999;background:#181818;border-bottom:1px solid #333;padding:8px 12px;margin:-20px -20px 15px -20px;font-size:12px;color:#bbb;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;box-shadow:0 2px 8px rgba(0,0,0,0.5)}
.tb-item{display:inline-flex;align-items:center;gap:4px;white-space:nowrap}
.tb-link{color:#4a90d9;text-decoration:none;padding:2px 6px;border-radius:4px;background:#242424;border:1px solid #3a3a3a;transition:background .2s, border-color .2s}
.tb-link:hover{background:#303030;border-color:#555;color:#70b0ff}
.tb-dot{width:8px;height:8px;border-radius:50%;display:inline-block}
.dot-green{background:#2ecc71;box-shadow:0 0 5px #2ecc71}
.dot-yellow{background:#f1c40f;box-shadow:0 0 5px #f1c40f}
.dot-red{background:#e74c3c}
.dot-gray{background:#666}
)rawliteral");
}

String getTopBarHtml() {
  return String(R"rawliteral(
<div class="top-bar-sticky">
  <div class="tb-item" title="Нагрузка процессора ESP32">
    <span>CPU:</span> <b id="tbCpu">0%</b>
  </div>
  <div class="tb-item" title="Оперативная память (занято / свободно)">
    <span>RAM:</span> <b id="tbRam">0%</b> <span id="tbRamKb" style="color:#888;font-size:11px">(0k)</span>
  </div>
  <div class="tb-item" title="Flash память (прошивка / всего)">
    <span>ROM:</span> <span id="tbRom" style="color:#bbb">0k</span>
  </div>
  <a href="/wifi" class="tb-item tb-link" title="Настройки Wi-Fi">
    <span>WiFi:</span>
    <span class="tb-dot dot-gray" id="tbWifiDot"></span>
    <span id="tbWifiTxt">...</span>
  </a>
  <div class="tb-item" title="Температура процессора">
    <span>Temp:</span> <b id="tbTemp">--°C</b>
  </div>
  <div class="tb-item" title="Bluetooth (не используется)" style="opacity:0.7">
    <span>BT:</span>
    <span class="tb-dot dot-gray"></span>
    <span style="color:#777">Выкл</span>
  </div>
</div>
)rawliteral");
}

String getTopBarJs() {
  return String(R"rawliteral(
<script>
let statusErrCount = 0;
function updateSysStatus(){
  if (localStorage.getItem('ui_show_topbar') === 'false') {
    const tb = document.querySelector('.top-bar-sticky');
    if (tb) tb.style.display = 'none';
  } else {
    const tb = document.querySelector('.top-bar-sticky');
    if (tb) tb.style.display = 'flex';
  }

  fetch("/status/sys").then(r=>r.json()).then(d=>{
    const cpuEl = document.getElementById("tbCpu");
    if(cpuEl) cpuEl.innerText = (d.cpu || 0) + "%";

    const ramEl = document.getElementById("tbRam");
    if(ramEl) ramEl.innerText = (d.ram_pct || 0) + "%";
    const ramKbEl = document.getElementById("tbRamKb");
    if(ramKbEl && d.ram_free_kb !== undefined) ramKbEl.innerText = "(" + d.ram_free_kb + "k)";

    const romEl = document.getElementById("tbRom");
    if(romEl && d.rom_sketch_kb !== undefined) romEl.innerText = d.rom_sketch_kb + "k/" + d.rom_total_kb + "k";

    const dot = document.getElementById("tbWifiDot");
    const txt = document.getElementById("tbWifiTxt");
    if(dot && txt){
      dot.className = "tb-dot ";
      if(d.wifi_mode === "STA"){
        dot.className += "dot-green";
        let display = d.wifi_ssid || "WiFi";
        if(d.wifi_ip) display += " (" + d.wifi_ip + ")";
        txt.innerText = display;
        txt.title = "Доступен по http://" + (d.mdns_host || "openbike.local") + " или http://" + (d.wifi_ip || "");
      } else if(d.wifi_mode === "AP"){
        dot.className += "dot-yellow";
        txt.innerText = "AP: " + (d.wifi_ssid || "Bike") + " (" + (d.wifi_ip || "192.168.4.1") + ")";
      } else {
        dot.className += "dot-red";
        txt.innerText = "Подключение...";
      }
    }
    
    // Update temperature display
    const tempEl = document.getElementById("tbTemp");
    if(tempEl && d.temp !== undefined) {
      tempEl.innerText = d.temp + "°C";
      if (d.temp > 75) {
        tempEl.style.color = "#e74c3c";
      } else if (d.temp > 60) {
        tempEl.style.color = "#f39c12";
      } else {
        tempEl.style.color = "#eee";
      }
    } else if (tempEl) {
      tempEl.innerText = "--°C";
    }

    statusErrCount = 0;
  }).catch(e=>{
    statusErrCount++;
    const cpu = document.getElementById("tbCpu"); if(cpu) cpu.innerText = "ERR";
    const ram = document.getElementById("tbRam"); if(ram) ram.innerText = "ERR";
    const temp = document.getElementById("tbTemp"); if(temp) temp.innerText = "ERR";
    const wifi = document.getElementById("tbWifiTxt"); if(wifi) wifi.innerText = "ERR";
    if (statusErrCount >= 3) {
      location.reload();
    }
  });
}
setInterval(updateSysStatus, 2000);
updateSysStatus();
</script>
)rawliteral");
}

// ================= Веб: отладочный график =================
void handleDebugPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Отладка</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(

body{background:#111;color:#eee;font-family:sans-serif;padding:15px}
canvas{background:#000;border-radius:6px;width:100%;max-width:700px;display:block}
.tbtn{background:#333;color:#eee;border:1px solid #555;padding:5px 12px;border-radius:4px;cursor:pointer;margin-right:5px;font-size:13px;transition:background .2s}
.tbtn:hover{background:#444}
.legend{display:flex;gap:15px;flex-wrap:wrap;margin:10px 0;font-size:13px}
.legend span{display:inline-flex;align-items:center;gap:5px}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block}
#vals{font-size:14px;margin-top:10px;line-height:1.6}
a{color:#4a90d9}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a href="/">&larr; Настройки</a></p>
<h1>Отладка (мини-осциллограф)</h1>
<div style="margin-bottom:10px;display:flex;align-items:center;flex-wrap:wrap;gap:8px;">
  <span style="font-size:14px;">Масштаб времени:</span>
  <button type="button" class="tbtn" onclick="setTimeScale(1)" id="tb1">1с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(3)" id="tb3">3с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(5)" id="tb5" style="background:#4a90d9;color:#fff;">5с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(10)" id="tb10">10с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(30)" id="tb30">30с</button>
  <label style="margin-left:10px;font-weight:bold;font-size:14px;">
    <input type="checkbox" id="chkOscilloscope" onchange="updateOscilloscopeState()"> Запускать осциллограф
  </label>
</div>
<canvas id="chart" width="700" height="320"></canvas>
<div class="legend">
<span><span class="dot" style="background:#4a90d9"></span>Газ вход, В</span>
<span><span class="dot" style="background:#e74c3c"></span>Газ выход, В</span>
<span><span class="dot" style="background:#f39c12"></span>Тормоз</span>
<span><span class="dot" style="background:#2ecc71"></span>PAS активен</span>
<span><span class="dot" style="background:#9b59b6"></span>Кнопка PAS</span>
</div>
<div id="vals">Осциллограф отключен. Включите галочку для запуска.</div>

<script>
const canvas = document.getElementById('chart');
const ctx = canvas.getContext('2d');
const W = canvas.width, H = canvas.height;
const VMAX = 5.0; // шкала по напряжению, В
let currentTimeScaleSec = 5;
let isOscRunning = false;

function setTimeScale(sec) {
  currentTimeScaleSec = sec;
  [1, 3, 5, 10, 30].forEach(s => {
    const el = document.getElementById('tb' + s);
    if (el) {
      if (s === sec) { el.style.background = '#4a90d9'; el.style.color = '#fff'; }
      else { el.style.background = '#333'; el.style.color = '#eee'; }
    }
  });
}

function updateOscilloscopeState() {
  const checkbox = document.getElementById('chkOscilloscope');
  isOscRunning = checkbox.checked;
  if (isOscRunning) {
    refresh();
  } else {
    document.getElementById('vals').innerText = 'Осциллограф отключен.';
    ctx.clearRect(0,0,W,H);
  }
}

function draw(rawHistory) {
  ctx.clearRect(0,0,W,H);
  if (!rawHistory || !rawHistory.length) return;
  const now = rawHistory[rawHistory.length - 1].t;
  const cutoff = now - currentTimeScaleSec * 1000;
  let data = rawHistory.filter(d => d.t >= cutoff);
  if (data.length < 2) data = rawHistory.slice(-2);
  if (data.length < 2) return;

  // сетка
  ctx.strokeStyle = '#222';
  ctx.lineWidth = 1;
  for (let v = 0; v <= VMAX; v++) {
    const y = H - 60 - (v/VMAX)*(H-80);
    ctx.beginPath(); ctx.moveTo(0,y); ctx.lineTo(W,y); ctx.stroke();
    ctx.fillStyle = '#555'; ctx.font = '10px sans-serif';
    ctx.fillText(v.toFixed(1)+'В', 2, y-2);
  }

  const stepX = W / (data.length - 1);
  function plotV(key, color) {
    ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.beginPath();
    data.forEach((d,i) => {
      const x = i*stepX;
      const y = H - 60 - (d[key]/VMAX)*(H-80);
      if (i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    });
    ctx.stroke();
  }
  function plotBool(key, color, baseY) {
    ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.beginPath();
    data.forEach((d,i) => {
      const x = i*stepX;
      const y = d[key] ? baseY - 15 : baseY;
      if (i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    });
    ctx.stroke();
  }

  plotV('in', '#4a90d9');
  plotV('out', '#e74c3c');
  plotBool('brake', '#f39c12', H-25);
  plotBool('pas', '#2ecc71', H-45);
  plotBool('btn', '#9b59b6', H-65);
}

function refresh() {
  if (!isOscRunning) return;
  fetch('/debug/data').then(r=>r.json()).then(data=>{
    if (!isOscRunning) return;
    draw(data);
    if (data.length) {
      const last = data[data.length-1];
      document.getElementById('vals').innerHTML =
        'Газ вход: <b>' + last.in.toFixed(2) + 'В</b> &nbsp;|&nbsp; ' +
        'Газ выход: <b>' + last.out.toFixed(2) + 'В</b> &nbsp;|&nbsp; ' +
        'Тормоз: <b>' + (last.brake ? 'НАЖАТ' : 'отпущен') + '</b> &nbsp;|&nbsp; ' +
        'PAS: <b>' + (last.pas ? ('активен, уровень '+last.lvl) : 'неактивен') + '</b> &nbsp;|&nbsp; ' +
        'Кнопка: <b>' + (last.btn ? 'НАЖАТА' : 'отпущена') + '</b>';
    }
  }).catch(e=>{});
}
setInterval(refresh, 200);
</script>
)rawliteral" + getTopBarJs() + R"rawliteral(
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleDebugData() {
  String json = "[";
  bool first = true;
  for (int i = 0; i < DEBUG_BUFFER_SIZE; i++) {
    int idx = (debugBufferHead + i) % DEBUG_BUFFER_SIZE;
    DebugSample &s = debugBuffer[idx];
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

// ================= Веб: WiFi и Точка Доступа =================
void handleWifiPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WiFi и Точка доступа</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(

body{background:#111;color:#eee;font-family:sans-serif;padding:15px;max-width:420px;margin:auto}
.card{background:#222;border:1px solid #333;border-radius:8px;padding:15px;margin-bottom:20px}
h2{font-size:18px;margin-top:0;color:#4a90d9;border-bottom:1px solid #333;padding-bottom:8px}
button{padding:10px;width:100%;font-size:15px;border-radius:8px;border:none;background:#333;color:#eee;margin-top:10px;cursor:pointer}
button:hover{background:#444}
button.primary{background:#2980b9;color:#fff}
button.primary:hover{background:#3498db}
input{width:100%;padding:9px;box-sizing:border-box;background:#181818;color:#eee;border:1px solid #444;border-radius:4px;margin-top:6px;margin-bottom:10px;font-size:14px}
.net{padding:10px;background:#1a1a1a;border-radius:6px;margin-top:6px;cursor:pointer;display:flex;justify-content:space-between;border:1px solid #2a2a2a}
.net:active{background:#333}
a{color:#4a90d9;text-decoration:none}
.status{color:#aaa;font-size:13px;margin:8px 0;line-height:1.4}
.hint{color:#888;font-size:12px;margin-top:-6px;margin-bottom:10px;display:block}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a href="/">&larr; Меню</a></p>
<h1>Связь и сеть</h1>

<div class="card">
  <h2>Подключение к Wi-Fi (Клиент)</h2>
  <div class="status" id="wifi_status">Текущий статус: )rawliteral";
  html += (WiFi.status() == WL_CONNECTED) ? ("<b>Подключено к " + WiFi.SSID() + "</b> (IP: " + WiFi.localIP().toString() + ")") : "<i>Не подключено к внешней сети</i>";
  html += R"rawliteral(</div>

  <button type="button" onclick="scan()">Найти доступные сети</button>
  <div id="nets" style="margin-top:10px"></div>

  <form id="f_wifi" style="margin-top:15px">
    <label>Имя сети (SSID):</label>
    <input type="text" id="ssid" name="ssid" placeholder="Выберите сеть или введите вручную">
    <label>Пароль сети:</label>
    <input type="password" id="pass" name="pass" placeholder="Пароль Wi-Fi">
    <button type="submit" class="primary">Подключиться к Wi-Fi</button>
  </form>
</div>

<div class="card">
  <h2>Настройки точки доступа (AP)</h2>
  <div class="status">
    Режим точки доступа: <b>активен</b><br>
    IP-адрес точки: <b>)rawliteral";
  html += WiFi.softAPIP().toString();
  html += R"rawliteral(</b>
  </div>

  <form id="f_ap" style="margin-top:15px">
    <label>Имя точки доступа (SSID):</label>
    <input type="text" id="ap_ssid" name="ap_ssid" value=")rawliteral";
  html += storedApSsid;
  html += R"rawliteral(">

    <label>Пароль точки доступа (открыто):</label>
    <input type="text" id="ap_pass" name="ap_pass" value=")rawliteral";
  html += storedApPass;
  html += R"rawliteral(" placeholder="Оставьте пустым для открытой сети">
    <span class="hint">Пароль отображается открыто. Оставьте пустым для открытой точки (без пароля). Для WPA2 нужно минимум 8 символов.</span>

    <button type="submit" class="primary">Сохранить настройки точки доступа</button>
  </form>
  <div id="ap_status" class="status" style="margin-top:8px"></div>
</div>

<script>
function scan() {
  document.getElementById('nets').innerHTML = '<i>Поиск сетей...</i>';
  fetch('/wifi/scan').then(r=>r.json()).then(list=>{
    if (!list || !list.length) { document.getElementById('nets').innerHTML = '<i>Сети не найдены</i>'; return; }
    document.getElementById('nets').innerHTML = list.map(n =>
      '<div class="net" onclick="pick(\''+n.ssid.replace(/'/g,"")+'\')"><span>'+n.ssid+'</span><span style="color:#888">'+n.rssi+' dBm</span></div>'
    ).join('');
  }).catch(() => {
    document.getElementById('nets').innerHTML = '<span style="color:#e74c3c">Ошибка сканирования</span>';
  });
}

function pick(ssid) {
  document.getElementById('ssid').value = ssid;
  document.getElementById('pass').focus();
}

document.getElementById('f_wifi').addEventListener('submit', function(e){
  e.preventDefault();
  const d = new FormData(this);
  document.getElementById('wifi_status').innerHTML = '<i>Подключаюсь к сети...</i>';
  fetch('/wifi/save', {method:'POST', body:d}).then(()=>{
    setTimeout(()=>location.reload(), 4000);
  }).catch(e=>{
    alert('Ошибка: ' + e.message);
  });
});

document.getElementById('f_ap').addEventListener('submit', function(e){
  e.preventDefault();
  const d = new FormData(this);
  const st = document.getElementById('ap_status');
  st.innerHTML = '<i>Сохранение точки доступа...</i>';
  fetch('/wifi/ap/save', {method:'POST', body:d}).then(r=>r.text()).then(txt=>{
    st.innerHTML = '<b style="color:#2ecc71">Настройки точки доступа сохранены и применены!</b>';
  }).catch(e=>{
    st.innerHTML = '<span style="color:#e74c3c">Ошибка: ' + e.message + '</span>';
  });
});
</script>
)rawliteral" + getTopBarJs() + R"rawliteral(
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleWifiScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    String s = WiFi.SSID(i);
    s.replace("\"", "");
    json += "{\"ssid\":\"" + s + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  json += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void handleWifiSave() {
  String newSsid = server.arg("ssid");
  String newPass = server.arg("pass");
  if (newSsid.length() > 0) {
    wifiCredsSave(newSsid, newPass);
    wifiConnect();
  }
  server.send(200, "text/plain", "OK");
}

void handleApSave() {
  if (server.hasArg("ap_ssid")) {
    storedApSsid = server.arg("ap_ssid");
  }
  if (server.hasArg("ap_pass")) {
    storedApPass = server.arg("ap_pass");
  }
  apSettingsSave();
  WiFi.softAP(storedApSsid.c_str(), storedApPass.c_str());
  server.send(200, "text/plain", "OK");
}

// ================= Веб: хаб =================
void handleHub() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenBike Controller v0.1.6-alpha</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
.header{text-align:center;margin-bottom:16px;}
.header h1{margin:0;font-size:24px;}
.header .version{color:#888;font-size:14px;}
a.card{display:block;background:#333;color:#fff;padding:15px;border-radius:8px;margin-bottom:10px;text-decoration:none;transition:background .2s}
a.card:active{background:#444}
.warn{background:#5c1a1a;padding:12px;border-radius:8px;margin-bottom:16px;font-weight:bold;font-size:13px}

/* LED Matrix Simulator */
.matrix-card{background:#161616;border:2px solid #2a2a2a;border-radius:14px;padding:12px;margin-bottom:14px;text-align:center;box-shadow:0 6px 18px rgba(0,0,0,0.7);transition:all .2s}
.matrix-title{font-size:11px;font-weight:bold;color:#777;letter-spacing:1px;text-transform:uppercase;margin-bottom:10px}
.sim-matrix-area{display:flex;justify-content:center;align-items:center;margin-bottom:10px}
#ledMatrixCanvas{background:#000;border:3px solid #222;border-radius:8px;box-shadow:inset 0 0 10px rgba(0,0,0,0.8);display:block;max-width:100%;height:auto}

.sim-controls-panel{display:flex;flex-direction:row;justify-content:space-between;align-items:center;gap:12px;background:#1a1a1a;border:1px solid #2d2d2d;border-radius:10px;padding:10px}
.sim-btns-group{display:flex;flex-direction:column;gap:8px;flex:1}
.sim-side-btn{display:flex;align-items:center;justify-content:center;gap:6px;padding:10px 14px;font-size:13px;font-weight:bold;border-radius:8px;border:2px solid #383838;background:#242424;color:#eee;cursor:pointer;user-select:none;-webkit-user-select:none;touch-action:manipulation;transition:all .1s}
.sim-side-btn:active, .sim-side-btn.active{background:#e74c3c;border-color:#c0392b;color:#fff}
.sim-side-btn.pedal-btn:active, .sim-side-btn.pedal-btn.active{background:#27ae60;border-color:#2ecc71;color:#fff}

.sim-throttle-group{display:flex;flex-direction:column;align-items:center;gap:6px;background:#202020;border:1px solid #333;border-radius:8px;padding:8px 10px}
.sim-throttle-label{font-size:11px;font-weight:900;color:#888;text-transform:uppercase}
.sim-slider-vert{writing-mode:bt-lr;-webkit-appearance:slider-vertical;width:24px;height:75px;cursor:pointer;accent-color:#e67e22}
.sim-throttle-val{font-size:12px;font-weight:bold;color:#e67e22;min-width:36px;text-align:center}

/* D-Pad Джойстик */
.joystick-panel{background:#181818;border:2px solid #333;border-radius:18px;padding:16px;margin-bottom:16px;box-shadow:0 8px 24px rgba(0,0,0,0.6)}
.joy-screen{background:#0a0f0d;border:2px solid #1e3a29;border-radius:10px;padding:10px 14px;margin-bottom:16px;text-align:center;font-family:monospace}
.screen-mode{font-size:13px;font-weight:bold;letter-spacing:1px;color:#888;text-transform:uppercase}
.screen-mode.mode-pas{color:#2ecc71}
.screen-mode.mode-cruise{color:#3498db}
.screen-mode.mode-off{color:#e74c3c}
.screen-val{font-size:26px;font-weight:900;color:#fff;margin:4px 0}
.screen-status{font-size:11px;color:#888;font-weight:bold;text-transform:uppercase}
.screen-status.dirty{color:#f39c12;animation:blink 1s infinite}
@keyframes blink{50%{opacity:0.4}}

.dpad-container{display:grid;grid-template-columns:80px 80px 80px;grid-template-rows:60px 60px 60px;gap:10px;justify-content:center;margin:10px auto}
.dpad-btn{background:#282828;color:#eee;border:2px solid #444;border-bottom-width:5px;border-radius:14px;display:flex;align-items:center;justify-content:center;font-size:24px;font-weight:900;cursor:pointer;user-select:none;-webkit-user-select:none;touch-action:manipulation;transition:all .08s}
.dpad-btn:active{transform:translateY(3px);border-bottom-width:2px;background:#383838}
.btn-up{grid-column:2;grid-row:1}
.btn-left{grid-column:1;grid-row:2}
.btn-ok{grid-column:2;grid-row:2;background:#1b442b;border-color:#2ecc71;border-bottom-color:#1e7e44;color:#2ecc71;font-size:18px}
.btn-ok:active{background:#235838}
.btn-ok.dirty-pulse{background:#633e08;border-color:#f39c12;border-bottom-color:#a86708;color:#f39c12;animation:pulse 1s infinite}
@keyframes pulse{50%{box-shadow:0 0 14px rgba(243,156,18,0.7)}}
.btn-right{grid-column:3;grid-row:2}
.btn-down{grid-column:2;grid-row:3}
.joy-legend{display:flex;justify-content:space-around;font-size:11px;color:#777;margin-top:10px;text-align:center}
</style>
</head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(
<div class="header">
  <h1>OpenBike Controller</h1>
  <div class="version">v0.1.6-alpha &bull; 16&times;32 LED Matrix</div>
</div>

<div class="matrix-card" id="simMatrixCard">
  <div class="matrix-title">16&times;32 LED Display Simulator (2&times; MAX7219)</div>
  <div class="sim-matrix-area">
    <canvas id="ledMatrixCanvas" width="320" height="160"></canvas>
  </div>
  <div class="sim-controls-panel" id="simControlsPanel">
    <div class="sim-btns-group">
      <button type="button" class="sim-side-btn" id="btnSimBrake">
        <span>🛑</span> <span>Тормоз</span>
      </button>
      <button type="button" class="sim-side-btn pedal-btn" id="btnSimPedal">
        <span>🔄</span> <span>Педали</span>
      </button>
    </div>
    <div class="sim-throttle-group">
      <span class="sim-throttle-label">Газ</span>
      <input type="range" min="0" max="100" value="0" orient="vertical" class="sim-slider-vert" id="simGas">
      <span class="sim-throttle-val" id="lblGas">0%</span>
    </div>
  </div>
</div>

<div class="joystick-panel" id="joystickPanel">
  <div class="joy-screen" id="joyScreen">
    <div class="screen-mode" id="joyMode">PAS</div>
    <div class="screen-val" id="joyVal">УРОВЕНЬ 1</div>
    <div class="screen-status" id="joyStatus">ПОДТВЕРЖДЕНО</div>
  </div>

  <div class="dpad-container" id="dpadContainer">
    <button type="button" class="dpad-btn btn-up" id="btnUp" title="Увеличить">&#9650;</button>
    <button type="button" class="dpad-btn btn-left" id="btnLeft" title="Режим влево">&#9664;</button>
    <button type="button" class="dpad-btn btn-ok" id="btnOk" title="Применить">OK</button>
    <button type="button" class="dpad-btn btn-right" id="btnRight" title="Режим вправо">&#9654;</button>
    <button type="button" class="dpad-btn btn-down" id="btnDown" title="Уменьшить">&#9660;</button>
  </div>

  <div class="joy-legend">
    <span>&#9664; &#9654; Режим (PAS/Круиз)</span>
    <span>&#9650; &#9660; Уровень</span>
    <span><b>OK</b> Применить</span>
  </div>
</div>

<div class="warn">&#9888; Тормоз продублирован сюда как приоритет 0 (можно отключить в настройках газа).</div>
<a class="card" href="/settings/throttle">Газ &rarr;</a>
<a class="card" href="/settings/pas">PAS &rarr;</a>
<a class="card" href="/settings/cruise">Круиз-контроль &rarr;</a>
<a class="card" href="/wifi">Связь и сеть (WiFi / AP) &rarr;</a>
<a class="card" href="/debug">Отладка (график) &rarr;</a>
<a class="card" href="/system">Система &rarr;</a>
)rawliteral" + getTopBarJs() + R"rawliteral(
<script>
let activeMode = )rawliteral" + String(pasEnabled ? "\"pas\"" : (cruiseEnabled ? "\"cruise\"" : "\"off\"")) + R"rawliteral(;
let activePasLvl = )rawliteral" + String(pasCurrentLevel) + R"rawliteral(;
let activeCruiseLvl = )rawliteral" + String(cruiseCurrentLevel) + R"rawliteral(;
const pasMax = )rawliteral" + String(pasLevelsCount) + R"rawliteral(;
const cruiseMax = )rawliteral" + String(cruiseLevelsCount) + R"rawliteral(;

const cfgThrottleInMin = )rawliteral" + String(throttleInMinV, 2) + R"rawliteral(;
const cfgThrottleInMax = )rawliteral" + String(throttleInMaxV, 2) + R"rawliteral(;
const cfgThrottleOutMin = )rawliteral" + String(throttleOutMinV, 2) + R"rawliteral(;
const cfgThrottleOutMax = )rawliteral" + String(throttleOutMaxV, 2) + R"rawliteral(;

const cfgCruiseConfirmThrottle = )rawliteral" + String(cruiseConfirmThrottleAfterStart ? "true" : "false") + R"rawliteral(;
const cfgCruiseAfterBraking = )rawliteral" + String(cruiseAfterBrakingMode) + R"rawliteral(;
const cfgCruiseAfterThrottle = )rawliteral" + String(cruiseAfterThrottleMode) + R"rawliteral(;

let draftMode = (activeMode === "off") ? "pas" : activeMode;
let draftPasLvl = activePasLvl;
let draftCruiseLvl = activeCruiseLvl;
let isDirty = false;
let userInteractingUntil = 0;
let applyInProgressUntil = 0;
let simCruiseEngaged = (activeMode === "cruise" && activeCruiseLvl > 0);
let simCruisePendingResume = false;
let simCruiseConfirmRequired = false;
let simCruiseReleaseSeen = false;
let arrowBounceUntil = 0;
let arrowBounceDir = 0;

// Screen Wake Lock API
let wakeLock = null;
async function requestWakeLock() {
  if ('wakeLock' in navigator) {
    try {
      wakeLock = await navigator.wakeLock.request('screen');
      wakeLock.addEventListener('release', () => { wakeLock = null; });
    } catch (err) {
      console.log('WakeLock error:', err);
    }
  }
}
requestWakeLock();
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible') requestWakeLock();
});

function vib(pattern = 20) {
  if (navigator.vibrate) {
    try { navigator.vibrate(pattern); } catch (e) {}
  }
}

function checkUiVisibility() {
  const showMatrix = localStorage.getItem("ui_show_matrix") !== "false";
  const showControls = localStorage.getItem("ui_show_controls") !== "false";
  const showScreen = localStorage.getItem("ui_show_screen") !== "false";
  const showDpad = localStorage.getItem("ui_show_dpad") !== "false";

  const matrixEl = document.getElementById("simMatrixCard");
  const controlsEl = document.getElementById("simControlsPanel");
  const screenEl = document.getElementById("joyScreen");
  const dpadEl = document.getElementById("dpadContainer");
  const joyPanel = document.getElementById("joystickPanel");

  if (matrixEl) matrixEl.style.display = showMatrix ? "block" : "none";
  if (controlsEl) controlsEl.style.display = showControls ? "flex" : "none";
  if (screenEl) screenEl.style.display = showScreen ? "block" : "none";
  if (dpadEl) dpadEl.style.display = showDpad ? "grid" : "none";
  if (joyPanel) {
    joyPanel.style.display = (!showScreen && !showDpad) ? "none" : "block";
  }
}
document.addEventListener("DOMContentLoaded", checkUiVisibility);
checkUiVisibility();

// ================= 16x32 LED MATRIX RENDERING ENGINE =================
const canvas = document.getElementById("ledMatrixCanvas");
const ctx = canvas ? canvas.getContext("2d") : null;
const MATRIX_ROWS = 16, MATRIX_COLS = 32;
let matrixGrid = [];
for (let r = 0; r < MATRIX_ROWS; r++) matrixGrid[r] = new Uint8Array(MATRIX_COLS);

const BIG_GLYPHS = {
  'P': [
    [1,1,1,1,1,1,0],
    [1,1,1,1,1,1,1],
    [1,1,0,0,0,1,1],
    [1,1,0,0,0,1,1],
    [1,1,1,1,1,1,1],
    [1,1,1,1,1,1,0],
    [1,1,0,0,0,0,0],
    [1,1,0,0,0,0,0],
    [1,1,0,0,0,0,0],
    [1,1,0,0,0,0,0],
    [1,1,0,0,0,0,0]
  ],
  'C': [
    [0,1,1,1,1,1,0],
    [1,1,1,1,1,1,1],
    [1,1,0,0,0,1,1],
    [1,1,0,0,0,0,0],
    [1,1,0,0,0,0,0],
    [1,1,0,0,0,0,0],
    [1,1,0,0,0,0,0],
    [1,1,0,0,0,0,0],
    [1,1,0,0,0,1,1],
    [1,1,1,1,1,1,1],
    [0,1,1,1,1,1,0]
  ]
};

const DIGIT_GLYPHS = {
  '0': [0x1F, 0x11, 0x11, 0x11, 0x1F],
  '1': [0x00, 0x12, 0x1F, 0x10, 0x00],
  '2': [0x1D, 0x15, 0x15, 0x15, 0x17],
  '3': [0x15, 0x15, 0x15, 0x15, 0x1F],
  '4': [0x07, 0x04, 0x04, 0x1F, 0x04],
  '5': [0x17, 0x15, 0x15, 0x15, 0x1D],
  '6': [0x1F, 0x15, 0x15, 0x15, 0x1D],
  '7': [0x01, 0x01, 0x19, 0x05, 0x03],
  '8': [0x1F, 0x15, 0x15, 0x15, 0x1F],
  '9': [0x17, 0x15, 0x15, 0x15, 0x1F]
};

const ARROW_UP = [
  [0,0,1,0,0],
  [0,1,1,1,0]
];
const ARROW_DOWN = [
  [0,1,1,1,0],
  [0,0,1,0,0]
];

const ICON_BRAKE = [0b11111, 0b10001, 0b10101, 0b10001, 0b11111];
const ICON_PEDAL_FRAMES = [
  [0b11000, 0b01000, 0b00100, 0b00010, 0b00011],
  [0b00000, 0b11000, 0b01110, 0b00011, 0b00000],
  [0b00000, 0b00000, 0b11111, 0b00000, 0b00000],
  [0b00000, 0b00011, 0b01110, 0b11000, 0b00000],
  [0b00011, 0b00010, 0b00100, 0b01000, 0b11000],
  [0b00000, 0b00011, 0b01110, 0b11000, 0b00000],
  [0b00000, 0b00000, 0b11111, 0b00000, 0b00000],
  [0b00000, 0b11000, 0b01110, 0b00011, 0b00000]
];

function clearMatrix() {
  for (let r = 0; r < MATRIX_ROWS; r++) matrixGrid[r].fill(0);
}
function setMatrixPixel(r, c, val) {
  if (r >= 0 && r < MATRIX_ROWS && c >= 0 && c < MATRIX_COLS) matrixGrid[r][c] = val ? 1 : 0;
}
function drawBigLetter(k, sr, sc) {
  const g = BIG_GLYPHS[k]; if (!g) return;
  for (let r = 0; r < 11; r++) {
    for (let c = 0; c < 7; c++) {
      if (g[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}
function drawDigit5x7ToBuffer(dChar, buf) {
  const g = DIGIT_GLYPHS[dChar];
  if (!g) return;
  for (let c = 0; c < 5; c++) {
    for (let r = 0; r < 7; r++) {
      if ((g[c] >> r) & 1) buf[r][c] = 1;
    }
  }
}
function drawArrow5x2(arrowMatrix, sr, sc) {
  for (let r = 0; r < 2; r++) {
    for (let c = 0; c < 5; c++) {
      if (arrowMatrix[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}
function drawIcon5x5(b, sr, sc) {
  for (let r = 0; r < 5; r++) {
    for (let c = 0; c < 5; c++) {
      if ((b[r] >> (4 - c)) & 1) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}

// Elevator animation state
let animStartLvl = 0;
let animTargetLvl = 0;
let animStartTime = 0;
const ANIM_DURATION_MS = 250;

function updateMatrixDisplay() {
  clearMatrix();
  const now = Date.now();
  const blinkOn = Math.floor(now / 400) % 2 === 0;

  let modeToDraw = draftMode;
  let lvlToDraw = (modeToDraw === "pas") ? draftPasLvl : ((modeToDraw === "cruise") ? draftCruiseLvl : 0);
  let maxLvl = (modeToDraw === "pas") ? pasMax : cruiseMax;

  if (lvlToDraw !== animTargetLvl) {
    animStartLvl = animTargetLvl;
    animTargetLvl = lvlToDraw;
    animStartTime = now;
  }
  let animProgress = 1;
  if (now - animStartTime < ANIM_DURATION_MS) {
    let t = (now - animStartTime) / ANIM_DURATION_MS;
    animProgress = 1 - Math.pow(1 - t, 3);
  }

  // 1. Draw Big Letter P or C (height 11px, rows 2..12) - blink when draft mode differs from active mode
  let modeSwitchPending = isDirty && (draftMode !== activeMode);
  if (!modeSwitchPending || blinkOn) {
    if (modeToDraw === "pas") {
      drawBigLetter('P', 2, 1);
    } else if (modeToDraw === "cruise") {
      drawBigLetter('C', 2, 1);
    }
  }

  // 2. Arrows (Up: row 2..3, Down: row 11..12) with 500ms ±1px bounce
  let isTwoDigits = (lvlToDraw >= 10);
  let arrowCol = isTwoDigits ? 13 : 12;
  let arrowUpVOffset = 0;
  let arrowDownVOffset = 0;
  if (now < arrowBounceUntil) {
    let remaining = arrowBounceUntil - now;
    let bPhase = Math.sin((500 - remaining) / 500 * Math.PI);
    if (arrowBounceDir > 0) {
      arrowUpVOffset = -Math.round(bPhase * 1.2);
    } else if (arrowBounceDir < 0) {
      arrowDownVOffset = Math.round(bPhase * 1.2);
    }
  }

  if (lvlToDraw < maxLvl) {
    drawArrow5x2(ARROW_UP, 2 + arrowUpVOffset, arrowCol);
  }
  if (lvlToDraw > 0) {
    drawArrow5x2(ARROW_DOWN, 11 + arrowDownVOffset, arrowCol);
  }

  // 3. Elevator Animation inside digit window: rows 5..11 (height 7px), strictly clipped
  let isCruiseWaiting = (modeToDraw === "cruise" && activeCruiseLvl > 0 && !simCruiseEngaged && !isDirty);
  let showDigits = true;
  if ((isDirty || isCruiseWaiting) && !blinkOn) showDigits = false;

  if (showDigits) {
    let fromLvl = animStartLvl;
    let toLvl = animTargetLvl;
    let dir = (toLvl >= fromLvl) ? 1 : -1;

    const renderLevelToMatrix = (lvlVal, rowOffset) => {
      let tD = Math.floor(lvlVal / 10);
      let oD = lvlVal % 10;
      let twoD = (lvlVal >= 10);
      let startC = twoD ? 10 : 12;

      let buf1 = Array.from({length: 7}, () => new Uint8Array(5));
      let buf2 = Array.from({length: 7}, () => new Uint8Array(5));

      if (twoD) {
        drawDigit5x7ToBuffer(tD.toString(), buf1);
        drawDigit5x7ToBuffer(oD.toString(), buf2);
      } else {
        drawDigit5x7ToBuffer(oD.toString(), buf1);
      }

      for (let r = 0; r < 7; r++) {
        let targetRow = Math.round(5 + r + rowOffset);
        if (targetRow >= 5 && targetRow <= 11) {
          for (let c = 0; c < 5; c++) {
            if (buf1[r][c]) setMatrixPixel(targetRow, startC + c, 1);
            if (twoD && buf2[r][c]) setMatrixPixel(targetRow, startC + 6 + c, 1);
          }
        }
      }
    };

    if (animProgress < 1 && fromLvl !== toLvl) {
      renderLevelToMatrix(Math.round(fromLvl), Math.round(-dir * animProgress * 7));
      renderLevelToMatrix(Math.round(toLvl), Math.round(dir * (1 - animProgress) * 7));
    } else {
      renderLevelToMatrix(lvlToDraw, 0);
    }
  }

  // 4. Scales calculation
  let inMin = cfgThrottleInMin, inMax = Math.max(inMin + 0.01, cfgThrottleInMax);
  let outMin = cfgThrottleOutMin, outMax = Math.max(outMin + 0.01, cfgThrottleOutMax);
  let rawGripV = inMin + (simGasPct / 100) * (inMax - inMin);
  let clampedV = Math.max(inMin, Math.min(inMax, rawGripV));
  let calibOutV = outMin + ((clampedV - inMin) / (inMax - inMin)) * (outMax - outMin);
  let calibOutPct = Math.max(0, Math.min(100, ((calibOutV - outMin) / (outMax - outMin)) * 100));

  let inGasLeds = Math.round((simGasPct / 100) * 16);
  for (let r = 0; r < inGasLeds; r++) setMatrixPixel(15 - r, 30, 1);

  let effectiveOutPct = 0;
  if (!effectiveBrake) {
    let baseMotorPct = calibOutPct;
    if (activeMode === "cruise" && activeCruiseLvl > 0 && simCruiseEngaged) {
      let targetCruisePct = Math.min(100, activeCruiseLvl * (100 / Math.max(1, cruiseMax)));
      baseMotorPct = Math.max(baseMotorPct, targetCruisePct);
    } else if (activeMode === "pas" && activePasLvl > 0 && effectivePedal) {
      let targetPasPct = Math.min(100, activePasLvl * (100 / Math.max(1, pasMax)));
      baseMotorPct = Math.max(baseMotorPct, targetPasPct);
    }
    effectiveOutPct = baseMotorPct;
  }
  let outGasLeds = Math.round((effectiveOutPct / 100) * 16);
  for (let r = 0; r < outGasLeds; r++) setMatrixPixel(15 - r, 31, 1);

  // Bottom-right 5x5 Indicator area: cols 24..28, rows 10..14
  if (effectiveBrake) {
    const brakeBlink = Math.floor(now / 90) % 2 === 0;
    if (brakeBlink) drawIcon5x5(ICON_BRAKE, 10, 24);
  } else if (effectivePedal) {
    let frame = Math.floor((now - simPedalStartMs) / 75) % 8;
    drawIcon5x5(ICON_PEDAL_FRAMES[frame], 10, 24);
  }

  if (!canvas || !ctx) return;
  const cellW = canvas.width / MATRIX_COLS;
  const cellH = canvas.height / MATRIX_ROWS;
  const radius = Math.min(cellW, cellH) * 0.42;

  ctx.fillStyle = "#0c0c0c";
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  ctx.strokeStyle = "#1a1a1a";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(0, 8 * cellH);
  ctx.lineTo(canvas.width, 8 * cellH);
  ctx.stroke();

  for (let r = 0; r < MATRIX_ROWS; r++) {
    for (let c = 0; c < MATRIX_COLS; c++) {
      const cx = c * cellW + cellW / 2;
      const cy = r * cellH + cellH / 2;
      ctx.beginPath();
      ctx.arc(cx, cy, radius, 0, Math.PI * 2);
      if (matrixGrid[r][c] === 1) {
        ctx.fillStyle = "#ff8c00";
        ctx.shadowColor = "#ff7700";
        ctx.shadowBlur = 8;
        ctx.fill();
        ctx.shadowBlur = 0;
      } else {
        ctx.fillStyle = "#1e140a";
        ctx.fill();
      }
    }
  }
}
setInterval(updateMatrixDisplay, 40);

// Virtual simulation inputs
let simGasPct = 0; // This will now reflect the physical throttle's value
let simBrakeActive = false; // Virtual brake button state
let simPedalActive = false; // Virtual pedal button state
let simPedalStartMs = 0;
const sliderGas = document.getElementById("simGas");
const lblGas = document.getElementById("lblGas");

// Real-time data from hardware
let hwBrakeActive = false;
let hwPasActive = false;

// Combined effective states
let effectiveBrake = false;
let effectivePedal = false;

// Variables for combined state update
let currentMode = "";
let currentPasLvl = 0;
let currentCruiseLvl = 0;
let currentCruiseEngaged = false;

// Function to update effective states based on simulation and hardware inputs
function updateEffectiveStates() {
  effectiveBrake = simBrakeActive || hwBrakeActive;
  effectivePedal = simPedalActive || hwPasActive;
}

function refreshHubData(forceSync = false) {
  if (!forceSync && (isDirty || Date.now() < applyInProgressUntil)) return;
  fetch("/status/sys")
    .then(r => r.json())
    .then(d => {
      // Update hardware states
      hwBrakeActive = d.brake || false;
      hwPasActive = d.pas_active || false; // Use 'pas_active' field

      // Update throttle slider and label from physical sensor
      simGasPct = d.gas_pct || 0;
      if (sliderGas) {
        sliderGas.value = simGasPct; // Update slider visually
      }
      if (lblGas) {
        lblGas.innerText = simGasPct + "%";
      }

      // Update effective states
      updateEffectiveStates();

      // State machine for cruise confirmation & engagement in UI
      if (activeMode === "cruise" && activeCruiseLvl > 0) {
        if (effectiveBrake) {
          if (simCruiseEngaged || !simCruisePendingResume) {
            if (cfgCruiseAfterBraking === 0) {
              simCruiseEngaged = false;
              simCruisePendingResume = false;
            } else if (cfgCruiseAfterBraking === 1) {
              simCruiseEngaged = false;
              simCruisePendingResume = true;
              simCruiseConfirmRequired = true;
              simCruiseReleaseSeen = (simGasPct <= 10);
            } else if (cfgCruiseAfterBraking === 2) {
              simCruiseEngaged = false;
              simCruisePendingResume = true;
              simCruiseConfirmRequired = false;
            }
          }
        } else {
          if (simCruiseEngaged && simGasPct > 10) {
            if (cfgCruiseAfterThrottle === 0) {
              simCruiseEngaged = false;
              simCruisePendingResume = false;
            } else if (cfgCruiseAfterThrottle === 1) {
              simCruiseEngaged = false;
              simCruisePendingResume = true;
              simCruiseConfirmRequired = true;
              simCruiseReleaseSeen = false;
            } else if (cfgCruiseAfterThrottle === 2) {
              simCruiseEngaged = false;
              simCruisePendingResume = true;
              simCruiseConfirmRequired = false;
            }
          } else if (!simCruiseEngaged && simCruisePendingResume) {
            if (simCruiseConfirmRequired) {
              if (simGasPct <= 10) {
                simCruiseReleaseSeen = true;
              } else if (simCruiseReleaseSeen && simGasPct > 10) {
                simCruiseEngaged = true;
                simCruisePendingResume = false;
                simCruiseConfirmRequired = false;
              }
            } else {
              if (simGasPct <= 10) {
                simCruiseEngaged = true;
                simCruisePendingResume = false;
              }
            }
          }
        }
      }

      // Update joystick mode and level display
      currentMode = d.pas_en ? "pas" : (d.cruise_en ? "cruise" : "off");
      currentPasLvl = d.pas_lvl || 0;
      currentCruiseLvl = d.cruise_lvl || 0;
      currentCruiseEngaged = d.cruise_engaged || false;

      let serverMode = currentMode;
      let serverPasLvl = currentPasLvl;
      let serverCruiseLvl = currentCruiseLvl;
      let serverCruiseEngaged = currentCruiseEngaged;

      if (serverMode === "cruise") {
        if (serverCruiseEngaged) {
          simCruiseEngaged = true;
          simCruisePendingResume = false;
          simCruiseConfirmRequired = false;
        }
      }

      if (serverMode !== activeMode || serverPasLvl !== activePasLvl || serverCruiseLvl !== activeCruiseLvl) {
        activeMode = serverMode;
        activePasLvl = serverPasLvl;
        activeCruiseLvl = serverCruiseLvl;
        let isUserBusy = isDirty || (Date.now() < userInteractingUntil) || (Date.now() < applyInProgressUntil);
        if (!isUserBusy || forceSync) {
          draftMode = (activeMode === "off") ? "pas" : activeMode;
          draftPasLvl = activePasLvl;
          draftCruiseLvl = activeCruiseLvl;
          if (forceSync) isDirty = false;
        }
        renderJoystick();
      }

      // Update visual feedback for simulated buttons based on effective states
      const btnBrake = document.getElementById("btnSimBrake");
      if (btnBrake) {
        if (effectiveBrake) btnBrake.classList.add("active");
        else btnBrake.classList.remove("active");
      }
      const btnPedal = document.getElementById("btnSimPedal");
      if (btnPedal) {
        if (effectivePedal) btnPedal.classList.add("active");
        else btnPedal.classList.remove("active");
      }

    })
    .catch(e => {
      console.error("Failed to fetch hub data:", e);
    });
}
setInterval(refreshHubData, 100);

// Update effective states whenever sim states change
document.getElementById("btnSimBrake").addEventListener("click", () => {
  simBrakeActive = !simBrakeActive;
  updateEffectiveStates();
  renderJoystick(); // Re-render to reflect state changes visually
});

document.getElementById("btnSimPedal").addEventListener("click", () => {
  simPedalActive = !simPedalActive;
  updateEffectiveStates();
  renderJoystick(); // Re-render to reflect state changes visually
});

// Initial setup and interval
// Set initial effective states
updateEffectiveStates();
// Set initial slider value and label
if (sliderGas && lblGas) {
  lblGas.innerText = simGasPct + "%";
  sliderGas.value = simGasPct;
}

function renderJoystick() {
  const modeEl = document.getElementById("joyMode");
  const valEl = document.getElementById("joyVal");
  const statusEl = document.getElementById("joyStatus");
  const okBtn = document.getElementById("btnOk");

  modeEl.className = "screen-mode mode-" + draftMode;
  if (draftMode === "pas") {
    modeEl.innerText = "РЕЖИМ: PAS АССИСТЕНТ";
    if (draftPasLvl === 0) {
      valEl.innerText = "ВЫКЛ (0 / " + pasMax + ")";
    } else {
      valEl.innerText = "УРОВЕНЬ " + draftPasLvl + " / " + pasMax;
    }
  } else if (draftMode === "cruise") {
    modeEl.innerText = "РЕЖИМ: КРУИЗ-КОНТРОЛЬ";
    if (draftCruiseLvl === 0) {
      valEl.innerText = "ВЫКЛ (0 / " + cruiseMax + ")";
    } else {
      valEl.innerText = "УРОВЕНЬ " + draftCruiseLvl + " / " + cruiseMax;
    }
  }

  let modified = false;
  if (activeMode === "off") {
    // When nothing is active, only the currently selected draft level matters —
    // switching draftMode alone (PAS <-> Cruise) with level 0 should NOT be dirty.
    let draftLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
    modified = (draftLvl !== 0);
  } else if (draftMode !== activeMode) {
    modified = true;
  } else if (draftMode === "pas" && draftPasLvl !== activePasLvl) {
    modified = true;
  } else if (draftMode === "cruise" && draftCruiseLvl !== activeCruiseLvl) {
    modified = true;
  }

  isDirty = modified;
  if (isDirty) {
    statusEl.innerText = "НАЖМИТЕ OK ДЛЯ ПРИМЕНЕНИЯ";
    statusEl.className = "screen-status dirty";
    okBtn.className = "dpad-btn btn-ok dirty-pulse";
  } else {
    let curLvl = (activeMode === "pas") ? activePasLvl : ((activeMode === "cruise") ? activeCruiseLvl : 0);
    statusEl.innerText = (activeMode === "off" || curLvl === 0) ? "ОБЫЧНАЯ ЕЗДА (БЕЗ МОТОРА)" : "АКТИВНО И ПРИМЕНЕНО";
    statusEl.className = "screen-status";
    okBtn.className = "dpad-btn btn-ok";
  }
}

function toggleMode() {
  vib();
  userInteractingUntil = Date.now() + 4000;
  draftMode = (draftMode === "pas") ? "cruise" : "pas";
  renderJoystick();
}

document.getElementById("btnLeft").addEventListener("click", toggleMode);
document.getElementById("btnRight").addEventListener("click", toggleMode);

document.getElementById("btnUp").addEventListener("click", () => {
  vib();
  userInteractingUntil = Date.now() + 4000;
  let curLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
  let maxLvl = (draftMode === "pas") ? pasMax : cruiseMax;
  if (curLvl < maxLvl) {
    if (draftMode === "pas") draftPasLvl++;
    else if (draftMode === "cruise") draftCruiseLvl++;
    arrowBounceUntil = Date.now() + 500;
    arrowBounceDir = 1;
  }
  renderJoystick();
});

document.getElementById("btnDown").addEventListener("click", () => {
  vib();
  userInteractingUntil = Date.now() + 4000;
  let curLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
  if (curLvl > 0) {
    if (draftMode === "pas") draftPasLvl--;
    else if (draftMode === "cruise") draftCruiseLvl--;
    arrowBounceUntil = Date.now() + 500;
    arrowBounceDir = -1;
  }
  renderJoystick();
});

document.getElementById("btnOk").addEventListener("click", () => {
  vib();
  if (navigator.vibrate) navigator.vibrate([40, 30, 40]);
  let targetMode = draftMode;
  let targetLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;

  if (targetLvl === 0) {
    activeMode = "off";
    activePasLvl = 0;
    activeCruiseLvl = 0;
  } else {
    activeMode = targetMode;
    if (targetMode === "pas") {
      activePasLvl = targetLvl;
      activeCruiseLvl = 0;
    } else if (targetMode === "cruise") {
      activeCruiseLvl = targetLvl;
      activePasLvl = 0;
    }
  }

  if (targetMode === "cruise" && targetLvl > 0) {
    // Starting/changing cruise level via OK: if confirmation is required, wait for
    // a fresh throttle press before engaging the motor output.
    if (cfgCruiseConfirmThrottle) {
      simCruiseEngaged = false;
      simCruisePendingResume = true;
      simCruiseConfirmRequired = true;
      simCruiseReleaseSeen = (simGasPct <= 10);
    } else {
      simCruiseEngaged = false;
      simCruisePendingResume = false;
    }
  }

  draftMode = (activeMode === "off") ? targetMode : activeMode;
  draftPasLvl = activePasLvl;
  draftCruiseLvl = activeCruiseLvl;
  applyInProgressUntil = Date.now() + 1500;
  renderJoystick();

  fetch("/api/joystick/apply?mode=" + encodeURIComponent(targetMode) + "&level=" + targetLvl)
    .then(res => {
      if (res.ok) {
        setTimeout(refreshHubData, 200);
      }
    })
    .catch(err => {
      console.warn("Apply mode error:", err);
    });

  setTimeout(refreshHubData, 500);
});

// Remove the duplicate function definition

document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible") {
    applyInProgressUntil = 0;
    refreshHubData(true);
    if (typeof updateSysStatus === "function") updateSysStatus();
  }
});

window.addEventListener("focus", () => {
  applyInProgressUntil = 0;
  refreshHubData(true);
  if (typeof updateSysStatus === "function") updateSysStatus();
});

renderJoystick();
</script>
</body>
</html>)rawliteral";
  server.send(200, "text/html", html);
}

void handleApiJoystickApply() {
  if (!server.hasArg("mode")) {
    server.send(400, "text/plain", "Missing mode");
    return;
  }
  String mode = server.arg("mode");
  int level = server.hasArg("level") ? server.arg("level").toInt() : 0;

  if (mode == "pas") {
    if (level >= 0 && level <= pasLevelsCount) {
      pasCurrentLevel = level;
      pasEnabled = (level > 0);
      if (pasEnabled) {
        cruiseEnabled = false; // Взаимное исключение
        cruiseCurrentLevel = 0;
        cruiseEngaged = false;
        cruisePendingResume = false;
      }
      server.send(200, "text/plain", "OK");
      return;
    }
  } else if (mode == "cruise") {
    if (level >= 0 && level <= cruiseLevelsCount) {
      cruiseCurrentLevel = level;
      cruiseEnabled = (level > 0);
      if (cruiseEnabled) {
        pasEnabled = false; // Взаимное исключение
        pasCurrentLevel = 0;
        if (cruiseConfirmThrottleAfterStart) {
          armCruisePending(true, 0.0f);
        } else {
          cruiseEngaged = true;
          cruisePendingResume = false;
        }
      } else {
        cruiseEngaged = false;
        cruisePendingResume = false;
      }
      server.send(200, "text/plain", "OK");
      return;
    }
  } else if (mode == "off") {
    pasEnabled = false;
    pasCurrentLevel = 0;
    cruiseEnabled = false;
    cruiseCurrentLevel = 0;
    cruiseEngaged = false;
    cruisePendingResume = false;
    server.send(200, "text/plain", "OK");
    return;
  }
  server.send(400, "text/plain", "Bad Request");
}
void handleApiPasSetLevel() {
  if (server.hasArg("level")) {
    int newLevel = server.arg("level").toInt();
    // Уровень 0 means PAS off, levels 1..pasLevelsCount are valid
    if (newLevel >= 0 && newLevel <= pasLevelsCount) {
      pasCurrentLevel = newLevel;
      pasEnabled = (newLevel > 0);
      // Взаимное исключение: если PAS включен, круиз отключается
      if (pasEnabled) {
        cruiseEnabled = false;
        cruiseCurrentLevel = 0;
        cruiseEngaged = false;
        cruisePendingResume = false;
      }
      server.send(200, "text/plain", "OK");
      return;
    }
  }
  server.send(400, "text/plain", "Bad Request");
}

void handleApiPasToggleMode() {
  pasEnabled = !pasEnabled;
  if (!pasEnabled) {
    pasCurrentLevel = 0;
  } else {
    if (pasCurrentLevel == 0) pasCurrentLevel = 1;
    // Взаимное исключение: если PAS включен, круиз отключается
    cruiseEnabled = false;
    cruiseCurrentLevel = 0;
    cruiseEngaged = false;
    cruisePendingResume = false;
  }
  server.send(200, "text/plain", "OK");
}

void handleApiCruiseToggleMode() {
  cruiseEnabled = !cruiseEnabled;
  // Взаимное исключение: если круиз включен, PAS отключается
  if (cruiseEnabled) {
    pasEnabled = false;
    pasCurrentLevel = 0;
    if (cruiseCurrentLevel == 0) cruiseCurrentLevel = 1;
    if (cruiseConfirmThrottleAfterStart) {
      armCruisePending(true, 0.0f);
    } else {
      cruiseEngaged = true;
      cruisePendingResume = false;
    }
  } else {
    cruiseCurrentLevel = 0;
    cruiseEngaged = false;
    cruisePendingResume = false;
  }
  server.send(200, "text/plain", "OK");
}

// ================= Веб: Газ =================
void handleThrottlePage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Газ</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
label{display:block;margin-top:12px}input{width:100%;padding:6px;box-sizing:border-box;background:#222;color:#eee;border:1px solid #444}
button{margin-top:15px;padding:10px;width:100%;font-size:16px}
.cal-btn{background:#2c3e50;color:#fff;border:1px solid #34495e;padding:8px;margin-top:4px}
.chk{display:flex;gap:8px;align-items:center;margin-top:12px}.chk input{width:auto}
fieldset{border:1px solid #333;border-radius:8px;margin-top:15px;padding:10px}
.warn{color:#ff8888;font-size:13px;margin-top:6px}
#liveV{font-size:18px;font-weight:bold;color:#2ecc71;margin-bottom:10px;display:inline-block;padding:4px 8px;background:#222;border-radius:4px;border:1px solid #444}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a href="/" style="color:#4a90d9">&larr; Настройки</a></p>
<h1>Газ</h1>
<form id="f">
<fieldset><legend>Калибровка (в реальных вольтах на проводах)</legend>
<p style="color:#888;font-size:13px">Это напряжение на самих проводах (ручка газа / вход контроллера), не на ножках ESP32 — делитель и усилитель уже всё пересчитывают сами.</p>
<div>Текущее напряжение ручки газа: <span id="liveV">-- В</span></div>
<p style="color:#888;font-size:13px;margin-top:0;">* В будущем планируется добавить автокалибровку выходного порога старта по датчику скорости (чтобы определять реальный вольтаж трогания велосипеда).</p>

<label>Вход мин, В (ручка газа в покое)</label>
<input type="number" step="0.01" min="0" max="5" id="inMinV" name="inMinV" value=")rawliteral"; html += String(throttleInMinV, 2);
  html += R"rawliteral(">
<button type="button" class="cal-btn" onclick="calMin()">Захватить текущее как МИН (Отпусти ручку)</button>

<label>Вход макс, В (ручка на полном газу)</label>
<input type="number" step="0.01" min="0" max="5" id="inMaxV" name="inMaxV" value=")rawliteral"; html += String(throttleInMaxV, 2);
  html += R"rawliteral(">
<button type="button" class="cal-btn" onclick="calMax()">Захватить текущее как МАКС (Выжми газ до упора)</button>

<label>Выход мин, В (контроллер, холостой ход)</label><input type="number" step="0.05" min="0" max="5" name="outMinV" value=")rawliteral"; html += String(throttleOutMinV, 2);
  html += R"rawliteral(">
<label>Выход макс, В (контроллер, полный газ)</label><input type="number" step="0.05" min="0" max="5" name="outMaxV" value=")rawliteral"; html += String(throttleOutMaxV, 2);
  html += R"rawliteral(">
</fieldset>
<fieldset><legend>Согласующие цепи (подстроить под фактические резисторы)</legend>
<label>Коэффициент делителя на входе (R2/(R1+R2))</label><input type="number" step="0.001" min="0.1" max="1" name="divRatio" value=")rawliteral"; html += String(throttleInputDividerRatio, 3);
  html += R"rawliteral(">
<p style="color:#888;font-size:13px">По умолчанию для R1=10к, R2=24к: 24/(10+24) &#8776; 0.706</p>
<label>Коэффициент усиления ОУ (1 + R4/R3)</label><input type="number" step="0.01" min="1" max="3" name="gain" value=")rawliteral"; html += String(throttleOutputGain, 2);
  html += R"rawliteral(">
<p style="color:#888;font-size:13px">По умолчанию для R3=10к, R4=2.7к: 1 + 2.7/10 = 1.27 (ОУ MCP6002, питание +5В).</p>
<p class="warn">Выше 3.3В на самом ЦАП ESP32 не поднимется — это аппаратный предел чипа. ОУ после ЦАП компенсирует это усилением, но выше напряжения питания ОУ (обычно 5В) выход тоже не поднимется физически.</p>
</fieldset>
<fieldset><legend>Мягкий старт/стоп</legend>
<div class="chk"><input type="checkbox" name="ssEn" )rawliteral"; html += throttleSoftStartEnabled?"checked":"";
  html += R"rawliteral(><label>Мягкий старт</label></div>
<label>Время разгона (мс)</label><input type="number" name="ssMs" value=")rawliteral"; html += String(throttleSoftStartMs);
  html += R"rawliteral(">
<div class="chk"><input type="checkbox" name="spEn" )rawliteral"; html += throttleSoftStopEnabled?"checked":"";
  html += R"rawliteral(><label>Мягкий стоп</label></div>
<label>Время торможения (мс)</label><input type="number" name="spMs" value=")rawliteral"; html += String(throttleSoftStopMs);
  html += R"rawliteral(">
</fieldset>
<fieldset><legend>Безопасность</legend>
<div class="chk"><input type="checkbox" name="brakeCut" )rawliteral"; html += ownBrakeCutoffEnabled?"checked":"";
  html += R"rawliteral(><label>Дублировать отключение газа по тормозу (доп. к моторконтроллеру)</label></div>
</fieldset>
<button type="submit" style="background:#27ae60;color:#fff;border:none;font-weight:bold">Сохранить</button>
</form>
<script>
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  const d=new FormData(this);
  fetch('/settings/throttle/save',{method:'POST',body:d}).then(()=>alert('Сохранено'));
});
function pollV(){
  fetch('/status/sys').then(r=>r.json()).then(d=>{
    if(d.gas_in_v!==undefined) document.getElementById('liveV').textContent = d.gas_in_v.toFixed(2)+' В';
  }).catch(()=>{});
}
setInterval(pollV, 200);
pollV();

function calMin(){
  fetch('/settings/throttle/cal_min',{method:'POST'}).then(r=>r.json()).then(d=>{
    document.getElementById('inMinV').value = d.val.toFixed(2);
  });
}
function calMax(){
  fetch('/settings/throttle/cal_max',{method:'POST'}).then(r=>r.json()).then(d=>{
    document.getElementById('inMaxV').value = d.val.toFixed(2);
  });
}
</script>
)rawliteral" + getTopBarJs() + R"rawliteral(
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleThrottleCalMin() {
  throttleInMinV = hwThrottleInV;
  // Защита: МИН не должен подниматься выше МАКС (иначе калибровка сломается)
  if (throttleInMaxV > 0.2f && throttleInMinV > throttleInMaxV - 0.1f)
    throttleInMinV = throttleInMaxV - 0.1f;
  throttleSettingsSave();
  server.send(200, "application/json", "{\"val\":" + String(throttleInMinV, 2) + "}");
}

void handleThrottleCalMax() {
  throttleInMaxV = hwThrottleInV;
  if (throttleInMaxV < throttleInMinV + 0.1f) throttleInMaxV = throttleInMinV + 0.1f;
  throttleSettingsSave();
  server.send(200, "application/json", "{\"val\":" + String(throttleInMaxV, 2) + "}");
}

void handleThrottleSave() {
  throttleInMinV = constrain(server.arg("inMinV").toFloat(), 0.0f, 5.0f);
  throttleInMaxV = constrain(server.arg("inMaxV").toFloat(), 0.0f, 5.0f);
  if (throttleInMaxV < throttleInMinV + 0.1f) throttleInMaxV = throttleInMinV + 0.1f;
  if (throttleInMaxV > 5.0f) {
    throttleInMaxV = 5.0f;
    throttleInMinV = min(throttleInMinV, throttleInMaxV - 0.1f);
  }
  throttleOutMinV = constrain(server.arg("outMinV").toFloat(), 0.0f, 5.0f);
  throttleOutMaxV = constrain(server.arg("outMaxV").toFloat(), 0.0f, 5.0f);
  if (throttleOutMaxV < throttleOutMinV + 0.1f) throttleOutMaxV = throttleOutMinV + 0.1f;
  if (throttleOutMaxV > 5.0f) {
    throttleOutMaxV = 5.0f;
    throttleOutMinV = min(throttleOutMinV, throttleOutMaxV - 0.1f);
  }
  float newDivRatio = server.arg("divRatio").toFloat();
  if (newDivRatio > 0.05f && newDivRatio <= 1.0f) throttleInputDividerRatio = newDivRatio;
  float newGain = server.arg("gain").toFloat();
  if (newGain >= 1.0f) throttleOutputGain = newGain;
  throttleSoftStartEnabled = server.hasArg("ssEn");
  throttleSoftStopEnabled = server.hasArg("spEn");
  throttleSoftStartMs = server.arg("ssMs").toInt();
  throttleSoftStopMs = server.arg("spMs").toInt();
  ownBrakeCutoffEnabled = server.hasArg("brakeCut");
  throttleSettingsSave();
  server.send(200, "text/plain", "OK");
}

// ================= Веб: PAS =================
void handlePasPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PAS</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
label{display:block;margin-top:12px}input,select{width:100%;padding:6px;box-sizing:border-box;background:#222;color:#eee;border:1px solid #444}
button{margin-top:15px;padding:10px;width:100%;font-size:16px}
.chk{display:flex;gap:8px;align-items:center;margin-top:12px}.chk input{width:auto}
fieldset{border:1px solid #333;border-radius:8px;margin-top:15px;padding:10px}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a href="/" style="color:#4a90d9">&larr; Настройки</a></p>
<h1>PAS</h1>
<form id="f">
<fieldset><legend>Ассистент PAS</legend>
<div class="chk"><input type="checkbox" name="en" )rawliteral"; html += pasEnabled?"checked":"";
  html += R"rawliteral(><label>Включить PAS</label></div>
</fieldset>
<fieldset><legend>Датчик</legend>
<label>Количество магнитов</label><input type="number" name="magnets" value=")rawliteral"; html += String(pasMagnetCount);
  html += R"rawliteral(">
<label>Направление срабатывания</label>
<select name="edge">
<option value="2")rawliteral"; html += (pasEdgeMode==FALLING?" selected":"");
  html += R"rawliteral(>При уходе магнита (FALLING)</option>
<option value="3")rawliteral"; html += (pasEdgeMode==RISING?" selected":"");
  html += R"rawliteral(>При появлении магнита (RISING)</option>
<option value="1")rawliteral"; html += (pasEdgeMode==CHANGE?" selected":"");
  html += R"rawliteral(>При любом изменении (CHANGE)</option>
</select>
<label>Угол активации</label>
<select name="angle">
<option value="90")rawliteral"; html += (pasActivationAngle==90?" selected":"");
  html += R"rawliteral(>90&deg;</option>
<option value="180")rawliteral"; html += (pasActivationAngle==180?" selected":"");
  html += R"rawliteral(>180&deg;</option>
<option value="270")rawliteral"; html += (pasActivationAngle==270?" selected":"");
  html += R"rawliteral(>270&deg;</option>
<option value="360")rawliteral"; html += (pasActivationAngle==360?" selected":"");
  html += R"rawliteral(>360&deg;</option>
</select>
<label>Тайм-аут импульса (мс)</label><input type="number" name="timeout" value=")rawliteral"; html += String(pasTimeoutMs);
  html += R"rawliteral(">
<label>Тайм-аут отключения при остановке педалей (мс)</label><input type="number" name="stopTO" value=")rawliteral"; html += String(pasStopTimeoutMs);
  html += R"rawliteral(">
<p style="color:#888;font-size:13px">Первый — как долго счётчик импульсов «помнит» вращение (медленное педалирование не сбрасывает). Второй — как быстро тяга отключается, когда педали остановились.</p>
</fieldset>

<fieldset><legend>Калибровка магнитов</legend>
<p style="color:#888;font-size:13px">Нажми «Старт», проверни педали ровно на 2 полных оборота, затем нажми «Готово» (или подожди 3 с после остановки — калибровка завершится сама). Количество магнитов будет посчитано и сохранено.</p>
<button type="button" onclick="calStart()">Старт</button>
<button type="button" onclick="calStop()">Готово</button>
<div id="calStat" style="margin-top:8px;font-weight:bold">—</div>
</fieldset>

<fieldset><legend>Уровни усилия (0-)rawliteral"; html += String(PAS_MAX_LEVELS); html += R"rawliteral()</legend>
<label>Количество уровней</label><input type="number" id="count" name="count" min="0" max=")rawliteral"; html += String(PAS_MAX_LEVELS);
  html += R"rawliteral(" value=")rawliteral"; html += String(pasLevelsCount);
  html += R"rawliteral(" oninput="renderLevels()">
<div id="levels"></div>
<button type="button" onclick="autoDistribute()">Автораспределение</button>
</fieldset>

<fieldset><legend>Мягкий старт/стоп ассиста</legend>
<div class="chk"><input type="checkbox" name="ssEn" )rawliteral"; html += pasSoftStartEnabled?"checked":"";
  html += R"rawliteral(><label>Мягкий старт</label></div>
<label>Время разгона (мс)</label><input type="number" name="ssMs" value=")rawliteral"; html += String(pasSoftStartMs);
  html += R"rawliteral(">
<div class="chk"><input type="checkbox" name="spEn" )rawliteral"; html += pasSoftStopEnabled?"checked":"";
  html += R"rawliteral(><label>Мягкий стоп</label></div>
<label>Время затухания при остановке педалей (мс)</label><input type="number" name="spMs" value=")rawliteral"; html += String(pasSoftStopMs);
  html += R"rawliteral(">
</fieldset>

<button type="submit">Сохранить</button>
</form>
<script>
const saved = [)rawliteral";
  for (int i = 0; i < PAS_MAX_LEVELS; i++) { html += String(pasLevelPercent[i]); if (i<PAS_MAX_LEVELS-1) html += ","; }
  html += R"rawliteral(];
function renderLevels(){
  const count = parseInt(document.getElementById('count').value)||0;
  const div = document.getElementById('levels'); div.innerHTML='';
  for(let i=0;i<count;i++){
    const val = saved[i]!==undefined?saved[i]:0;
    div.innerHTML += '<label>Уровень '+(i+1)+' — усилие (%)</label><input type="number" name="lvl'+i+'" value="'+val+'">';
  }
}
function autoDistribute(){
  const count = parseInt(document.getElementById('count').value)||0;
  for(let i=0;i<count;i++) saved[i]=Math.round((i+1)*100/count);
  renderLevels();
}
renderLevels();
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  const d=new FormData(this);
  fetch('/settings/pas/save',{method:'POST',body:d}).then(()=>alert('Сохранено'));
});
let calTimer=null;
function calStart(){
  fetch('/settings/pas/cal_start').then(()=>{
    calTimer=setInterval(calPoll,250);
    document.getElementById('calStat').textContent='Крути педали ровно 2 оборота...';
  });
}
function calStop(){
  fetch('/settings/pas/cal_stop').then(r=>r.json()).then(d=>{
    if(calTimer){clearInterval(calTimer);calTimer=null;}
    if(d.finished){
      document.getElementById('calStat').textContent='Импульсов: '+d.pulses+' → магнитов: '+d.magnets+' (сохранено)';
      const m=document.getElementsByName('magnets')[0]; if(m) m.value=d.magnets;
    } else {
      document.getElementById('calStat').textContent='Калибровка не запущена';
    }
  });
}
function calPoll(){
  fetch('/settings/pas/cal_status').then(r=>r.json()).then(d=>{
    if(d.finished){
      if(calTimer){clearInterval(calTimer);calTimer=null;}
      document.getElementById('calStat').textContent='Импульсов: '+d.pulses+' → магнитов: '+d.magnets+' (сохранено)';
      const m=document.getElementsByName('magnets')[0]; if(m) m.value=d.magnets;
    } else if(d.running){
      const estM = (d.pulses/2).toFixed(1);
      document.getElementById('calStat').textContent='Прошло магнитов/импульсов: '+d.pulses+' → оценка магнитов после 2 оборотов: ~'+estM+' — продолжай до 2 оборотов';
    }
  });
}
</script>
)rawliteral" + getTopBarJs() + R"rawliteral(
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handlePasSave() {
  pasMagnetCount = server.arg("magnets").toInt();
  if (pasMagnetCount < 1) pasMagnetCount = 1;
  int newEdge = server.arg("edge").toInt();
  bool edgeChanged = (newEdge != pasEdgeMode);
  pasEdgeMode = newEdge;
  pasActivationAngle = server.arg("angle").toInt();
  pasTimeoutMs = server.arg("timeout").toInt();
  pasStopTimeoutMs = server.arg("stopTO").toInt();
  if (pasStopTimeoutMs < 20) pasStopTimeoutMs = 20;

  int count = server.arg("count").toInt();
  if (count < 0) count = 0;
  if (count > PAS_MAX_LEVELS) count = PAS_MAX_LEVELS;
  pasLevelsCount = count;
  for (int i = 0; i < count; i++) {
    String key = "lvl" + String(i);
    if (server.hasArg(key)) {
      int v = server.arg(key).toInt();
      if (v < 0) v = 0;
      if (v > 100) v = 100;
      pasLevelPercent[i] = v;
    }
  }

  pasSoftStartEnabled = server.hasArg("ssEn");
  pasSoftStopEnabled = server.hasArg("spEn");
  pasSoftStartMs = server.arg("ssMs").toInt();
  pasSoftStopMs = server.arg("spMs").toInt();
  pasEnabled = server.hasArg("en");

  pasSettingsSave();
  if (edgeChanged) reattachPasInterrupt();
  server.send(200, "text/plain", "OK");
}

// ================= Веб: калибровка магнитов PAS =================
// Завершает калибровку: magnets = импульсы / 2 (2 полных оборота педалей).
String pasCalFinishAndJson() {
  pasCalRunning = false;
  unsigned long pulses = pasCalPulses;
  int magnets = (int)(pulses / 2);
  if (magnets < 1) magnets = 1;
  pasMagnetCount = magnets;
  pasSettingsSave();
  String json = "{\"finished\":true,\"pulses\":" + String(pulses) +
                ",\"magnets\":" + String(magnets) + "}";
  return json;
}

void handlePasCalStart() {
  pasCalPulses = 0;
  pasCalLastPulseMicros = 0;
  pasCalStartMs = millis();
  pasCalRunning = true;
  server.send(200, "text/plain", "OK");
}

void handlePasCalStatus() {
  if (pasCalRunning) {
    // Авто-завершение: 3 с без импульсов (педали встали) или общий тайм-аут 60 с
    bool idleDone = pasCalPulses > 0 && pasCalLastPulseMicros != 0 &&
                    (micros() - pasCalLastPulseMicros) > 3000000UL;
    bool timeoutDone = (millis() - pasCalStartMs) > PAS_CAL_TIMEOUT_MS;
    if (idleDone || timeoutDone) {
      server.send(200, "application/json", pasCalFinishAndJson());
      return;
    }
    String json = "{\"running\":true,\"pulses\":" + String(pasCalPulses) +
                  ",\"estimatedMagnets\":" + String((float)pasCalPulses / 2.0f, 1) + "}";
    server.send(200, "application/json", json);
  } else {
    server.send(200, "application/json", "{\"running\":false}");
  }
}

void handlePasCalStop() {
  if (pasCalRunning) {
    server.send(200, "application/json", pasCalFinishAndJson());
  } else {
    server.send(200, "application/json", "{\"finished\":false,\"running\":false}");
  }
}

// ================= Веб: заливка прошивки прямо через браузер =================
void handleUpdatePage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Обновление прошивки</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
button{margin-top:15px;padding:10px;width:100%;font-size:16px}</style>
</head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a href="/" style="color:#4a90d9">&larr; Настройки</a></p>
<h1>Загрузить прошивку (.bin)</h1>
<p style="color:#888;font-size:13px">В Arduino IDE: Sketch &rarr; Export Compiled Binary — появится .bin рядом со скетчем. Выбери его тут и жми "Залить". Займёт секунд 20-30, плата сама перезагрузится.</p>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="update" accept=".bin">
<button type="submit">Залить</button>
</form>
)rawliteral" + getTopBarJs() + R"rawliteral(
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("Обновление: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("Обновление успешно: %u байт\n", upload.totalSize);
    else Update.printError(Serial);
  }
}

void handleUpdateResult() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", Update.hasError() ? "ОШИБКА обновления" : "OK, перезагружаюсь...");
  delay(1000);
  ESP.restart();
}

// ================= Setup / Loop =================
// ================= WiFi: сохранённая сеть (сверх дефолтной из кода) =================
// ssid/password вверху файла — это дефолт "из коробки". Если через веб выберешь
// другую сеть — она сохранится в NVS и будет использоваться вместо дефолтной.
String storedSsid = "";
String storedPass = "";
bool mdnsStarted = false;
unsigned long wifiConnectStartMs = 0;
bool wifiApActive = false;

void wifiCredsLoad() {
  prefs.begin("wifi", false);
  storedSsid = prefs.getString("ssid", "");
  storedPass = prefs.getString("pass", "");
  prefs.end();
}

void wifiCredsSave(const String &newSsid, const String &newPass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", newSsid);
  prefs.putString("pass", newPass);
  prefs.end();
  storedSsid = newSsid;
  storedPass = newPass;
}

void apSettingsSave() {
  prefs.begin("ap", false);
  prefs.putString("ssid", storedApSsid);
  prefs.putString("pass", storedApPass);
  prefs.end();
}

void apSettingsLoad() {
  prefs.begin("ap", false);
  storedApSsid = prefs.getString("ssid", "BikeControllerAP");
  storedApPass = prefs.getString("pass", "");
  prefs.end();
}

void wifiConnect() {
  String useSsid = (storedSsid.length() > 0) ? storedSsid : String(ssid);
  String usePass = (storedSsid.length() > 0) ? storedPass : String(password);

  WiFi.mode(WIFI_STA);
  // Если подключение к хотспоту/внешней сети, настраиваем статический IP (если хотспот)
  if (useSsid.indexOf("Android") != -1 || useSsid.indexOf("Hotspot") != -1 || useSsid == "Redmi" || useSsid == "Donut") {
    WiFi.config(staticSTAIP, staticSTAGateway, staticSTASubnet, staticSTADNS);
  }

  Serial.printf("WiFi Connect -> SSID: '%s', Pass: '%s' (len=%d), Source: %s\n",
                useSsid.c_str(), usePass.c_str(), usePass.length(),
                (storedSsid.length() > 0) ? "NVS" : "DEFAULT");
  WiFi.begin(useSsid.c_str(), usePass.c_str());
}

// Автомат состояний WiFi:
// Режим только STA (подключение к хотспоту без постоянного зависания при потере сети)
unsigned long lastWifiRetryMs = 0;

void updateWifiStateMachine() {
  if (!wifiApActive) {
    if (WiFi.status() != WL_CONNECTED && millis() - lastWifiRetryMs > 10000) {
      lastWifiRetryMs = millis();
      if (storedSsid.length() > 0) {
        Serial.printf("WiFi Connect attempt: SSID='%s'\n", storedSsid.c_str());
        WiFi.begin(storedSsid.c_str(), storedPass.c_str());
      } else {
        Serial.println("Нет сохраненной WiFi сети. Запуск точки доступа AP...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(storedApSsid.c_str(), storedApPass.c_str());
        wifiApActive = true;
        #ifdef ENABLE_CAPTIVE_PORTAL
        dnsServer.setErrorReplyCode(DNS_RCODE_NOERROR);
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
        #endif
      }
    } else if (WiFi.status() == WL_CONNECTED) {
      if (!mdnsStarted) {
        if (MDNS.begin(MDNS_HOST)) {
          MDNS.addService("http", "tcp", 80);
          mdnsStarted = true;
          Serial.printf("mDNS запущен: http://%s.local\n", MDNS_HOST);
        }
      }
    }
  }
}

void setup() {
  // Инициализация NVS для работы с настройками
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND || err == ESP_ERR_NVS_NOT_FOUND) {
      nvs_flash_erase();
      err = nvs_flash_init();
  }
  if (err != ESP_OK) {
      Serial.printf("NVS Init Error: 0x%x (%s)\n", err, esp_err_to_name(err));
  }
  Serial.begin(115200);
  Serial.println(F("--- OpenBike Controller v0.1.6-alpha ---"));

  pinMode(BRAKE_PIN, INPUT_PULLUP);
  pinMode(PAS_SENSOR_PIN, INPUT_PULLUP);
  pinMode(PAS_BUTTON_PIN, INPUT_PULLUP);

  pinMode(BTN_HEADLIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_TURN_LEFT_PIN, INPUT_PULLUP);
  pinMode(BTN_TURN_RIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_HORN_PIN, INPUT_PULLUP);
  pinMode(TURN_LEFT_PIN, OUTPUT);
  pinMode(TURN_RIGHT_PIN, OUTPUT);
  pinMode(HORN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(HEADLIGHT_PIN, 0);
  ledcSetup(1, 5000, 8);
  ledcAttachPin(DRL_PIN, 1);
  ledcWrite(1, DRL_DEFAULT_BRIGHTNESS); // ДХО горит всегда, пока плата включена

  // Настоящий ЦАП ESP32 — ledcAttach больше не нужен, dacWrite() работает сразу

  throttleSettingsLoad();
  pasSettingsLoad();
  apSettingsLoad();

  setThrottleOutputSafeZero();

  reattachPasInterrupt();

  // Инициализация WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  wifiCredsLoad();
  wifiConnectStartMs = millis();
  wifiConnect();

  Serial.print("Ожидание подключения к WiFi");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 8000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Открой в браузере: http://"); Serial.println(WiFi.localIP());
    if (MDNS.begin(MDNS_HOST)) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
      Serial.printf("mDNS запущен: http://%s.local\n", MDNS_HOST);
    }
  } else {
    Serial.printf("WiFi статус при старте: %d (1=NoSSID, 4=Failed, 6=WrongPass, 7=Disconnected)\n", WiFi.status());
  }

  cruiseSettingsLoad();

  server.on("/", handleHub);
  server.on("/api/joystick/apply", HTTP_GET, handleApiJoystickApply);
  server.on("/api/pas/set_level", HTTP_GET, handleApiPasSetLevel);
  server.on("/api/pas/toggle_mode", HTTP_GET, handleApiPasToggleMode);
  server.on("/api/cruise/toggle", HTTP_GET, handleApiCruiseToggleMode);
  server.on("/settings/throttle", handleThrottlePage);
  server.on("/settings/throttle/save", HTTP_POST, handleThrottleSave);
  server.on("/settings/throttle/cal_min", HTTP_POST, handleThrottleCalMin);
  server.on("/settings/throttle/cal_max", HTTP_POST, handleThrottleCalMax);
  server.on("/settings/pas", handlePasPage);
  server.on("/settings/pas/save", HTTP_POST, handlePasSave);
  server.on("/settings/pas/cal_start", HTTP_GET, handlePasCalStart);
  server.on("/settings/pas/cal_status", HTTP_GET, handlePasCalStatus);
  server.on("/settings/pas/cal_stop", HTTP_GET, handlePasCalStop);
  server.on("/wifi/ap/save", HTTP_POST, handleApSave);
  server.on("/wifi", handleWifiPage);
  server.on("/wifi/scan", handleWifiScan);
  server.on("/wifi/save", HTTP_POST, handleWifiSave);
server.on("/settings/cruise", handleCruisePage);
server.on("/settings/cruise/save", HTTP_POST, handleCruiseSave);
  server.on("/debug", handleDebugPage);
  server.on("/debug/data", handleDebugData);
  server.on("/system", handleSystemPage);
  server.on("/system/export", handleSettingsExport);
  server.on("/system/import", HTTP_POST, handleSettingsImport);
  server.on("/status/sys", HTTP_GET, handleSystemStatus);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);
  server.begin();

  // Создаем высокоприоритетную задачу реального времени для газа/тормоза/PAS (Ядро 1, высокий приоритет)
  xTaskCreatePinnedToCore(
    criticalControlTask,
    "CritCtrlTask",
    4096,
    NULL,
    5, // Высокий приоритет (выше базовых и фоновых задач)
    NULL,
    1  // Ядро 1 (изолировано от системного WiFi на Core 0)
  );

  // Создаем фоновую задачу для веб-сервера и Wi-Fi (Ядро 0, низкий приоритет)
  xTaskCreatePinnedToCore(
    nonCriticalTask,
    "NonCritTask",
    4096,
    NULL,
    1, // Низкий приоритет
    NULL,
    0  // Ядро 0 (совместно с WiFi стеком ESP32)
  );

  Serial.println("Готово. Упрощённый прототип запущен.");
}

void criticalControlTask(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(1); // 1 kHz цикл управления

  for (;;) {
    unsigned long startMicros = micros();

    // 1. Наивысший приоритет: Тормоз, Газ, PAS
    updatePasDetection();
    updatePasButton();
    updateThrottle();

    // 2. Вспомогательное управление освещением и звуком
    updateLightButtons();
    updateTurnSignals();
    updateHorn();
    updateBuzzer();

    unsigned long elapsed = micros() - startMicros;
    cpuBusyTimeMicros += elapsed;

    if (millis() - cpuMeasureStartMs >= 1000) {
      unsigned long totalElapsedMs = millis() - cpuMeasureStartMs;
      if (totalElapsedMs > 0) {
        cpuUsagePercent = (int)constrain((cpuBusyTimeMicros * 100ULL) / (totalElapsedMs * 1000ULL), 0ULL, 100ULL);
      }
      cpuBusyTimeMicros = 0;
      cpuMeasureStartMs = millis();
    }

    // Точный интервал 1 мс без джиттера
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void nonCriticalTask(void *pvParameters) {
  for (;;) {
    #ifdef ENABLE_CAPTIVE_PORTAL
    if (wifiApActive) {
      dnsServer.processNextRequest();
    }
    #endif

    server.handleClient();
    updateWifiStateMachine();
    vTaskDelay(pdMS_TO_TICKS(1)); // 1 мс пауза для мгновенной обработки запросов
  }
}

void loop() {
  // Основной цикл свободен, управление разделено по RTOS-задачам
  vTaskDelay(pdMS_TO_TICKS(1000));
}

// --- Cruise Control Module Implementation ---

void cruiseSettingsSave() {
  prefs.begin("cruise", false);
  prefs.putInt("cnt", cruiseLevelsCount);
  prefs.putBytes("pct", cruiseLevelPercent, sizeof(cruiseLevelPercent));
  prefs.putFloat("stPct", cruiseStartPercent);
  prefs.putFloat("endPct", cruiseEndPercent);
  prefs.putBool("confThr", cruiseConfirmThrottleAfterStart);
  prefs.putInt("brkMode", cruiseAfterBrakingMode);
  prefs.putInt("thrMode", cruiseAfterThrottleMode);
  prefs.putInt("ssEn", cruiseSoftStartEnabled ? 1 : 0);
  prefs.putInt("spEn", cruiseSoftStopEnabled ? 1 : 0);
  prefs.putULong("ssMs", cruiseSoftStartMs);
  prefs.putULong("spMs", cruiseSoftStopMs);
  prefs.putBool("cen", cruiseEnabled); // Save cruiseEnabled state
  prefs.end();
}

void cruiseSettingsLoad() {
  prefs.begin("cruise", false);
  cruiseLevelsCount = prefs.getInt("cnt", 3);
  size_t got = prefs.getBytes("pct", cruiseLevelPercent, sizeof(cruiseLevelPercent));
  cruiseSoftStartEnabled = prefs.getInt("ssEn", 0) != 0;
  cruiseSoftStopEnabled = prefs.getInt("spEn", 0) != 0;
  cruiseEnabled = prefs.getBool("cen", false); // Load cruiseEnabled state
  cruiseSoftStartMs = prefs.getULong("ssMs", 500);
  cruiseSoftStopMs = prefs.getULong("spMs", 800);
  cruiseConfirmThrottleAfterStart = prefs.getBool("confThr", false);
  cruiseAfterBrakingMode = prefs.getInt("brkMode", 1);
  cruiseAfterThrottleMode = prefs.getInt("thrMode", 2);
  prefs.end();
  if (got != sizeof(cruiseLevelPercent)) {
    cruiseAutoDistribute();
  }
}

// ================= Веб: Cruise Control =================
void handleCruisePage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Cruise Control</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(

body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
label{display:block;margin-top:12px}
input,select{width:100%;padding:6px;box-sizing:border-box;background:#222;color:#eee;border:1px solid #444}
button{margin-top:15px;padding:10px;width:100%;font-size:16px}
.chk{display:flex;gap:8px;align-items:center;margin-top:12px}
.chk input{width:auto}
fieldset{border:1px solid #333;border-radius:8px;margin-top:15px;padding:10px}
.flex-row{display:flex;gap:10px}
.flex-row > div{flex:1}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a href="/" style="color:#4a90d9">&larr; Главный экран</a></p>
<h1>Cruise Control</h1>
<form id="f">
<fieldset><legend>Настройки ступеней Cruise Control</legend>
<label>Количество уровней (1-100)</label>
<input type="number" id="count" name="count" min="1" max="100" value=")rawliteral"; 
  html += String(cruiseLevelsCount);
  html += R"rawliteral(" oninput="renderLevels()">

<div class="flex-row">
  <div>
    <label>Начало диап. (%)</label>
    <input type="number" id="stPct" name="stPct" step="0.1" min="0" max="100" value=")rawliteral";
  html += String(cruiseStartPercent);
  html += R"rawliteral(">
  </div>
  <div>
    <label>Конец диап. (%)</label>
    <input type="number" id="endPct" name="endPct" step="0.1" min="0" max="100" value=")rawliteral";
  html += String(cruiseEndPercent);
  html += R"rawliteral(">
  </div>
</div>

<button type="button" onclick="autoDistribute()" style="background:#2c3e50;color:#fff;border:1px solid #34495e">Автораспределение (интерполяция)</button>
<div id="levels"></div>
</fieldset>
<fieldset><legend>Мягкий старт/стоп</legend>
<div class="chk"><input type="checkbox" name="ssEn" )rawliteral";
  html += cruiseSoftStartEnabled ? "checked" : "";
  html += R"rawliteral(><label>Мягкий старт</label></div>
<label>Время разгона (мс)</label><input type="number" name="ssMs" value=")rawliteral";
  html += String(cruiseSoftStartMs);
  html += R"rawliteral(">
<div class="chk"><input type="checkbox" name="spEn" )rawliteral";
  html += cruiseSoftStopEnabled ? "checked" : "";
  html += R"rawliteral(><label>Мягкий стоп</label></div>
<label>Время торможения (мс)</label><input type="number" name="spMs" value=")rawliteral";
  html += String(cruiseSoftStopMs);
  html += R"rawliteral(">
</fieldset>
<fieldset><legend>Поведение круиз-контроля</legend>
<div class="chk"><input type="checkbox" name="confThr" )rawliteral";
  html += cruiseConfirmThrottleAfterStart ? "checked" : "";
  html += R"rawliteral(><label>Подтверждать газом после старта</label></div>
<label>После торможения</label>
<select name="brkMode">
  <option value="0")rawliteral"; html += (cruiseAfterBrakingMode == 0 ? " selected" : ""); html += R"rawliteral(>Сбрасывать круиз, требуется нажатие ОК и газ</option>
  <option value="1")rawliteral"; html += (cruiseAfterBrakingMode == 1 ? " selected" : ""); html += R"rawliteral(>Требуется подтверждение через газ</option>
  <option value="2")rawliteral"; html += (cruiseAfterBrakingMode == 2 ? " selected" : ""); html += R"rawliteral(>Восстанавливать к предыдущему значению (ИСПОЛЬЗОВАТЬ С КРАЙНЕЙ ОСТОРОЖНОСТЬЮ!!!)</option>
</select>
<label>После перегазовки</label>
<select name="thrMode">
  <option value="0")rawliteral"; html += (cruiseAfterThrottleMode == 0 ? " selected" : ""); html += R"rawliteral(>Сбрасывать круиз, требуется нажатие ОК и газ</option>
  <option value="1")rawliteral"; html += (cruiseAfterThrottleMode == 1 ? " selected" : ""); html += R"rawliteral(>Требуется подтверждение через газ</option>
  <option value="2")rawliteral"; html += (cruiseAfterThrottleMode == 2 ? " selected" : ""); html += R"rawliteral(>Восстанавливать к предыдущему значению (ИСПОЛЬЗОВАТЬ С КРАЙНЕЙ ОСТОРОЖНОСТЬЮ!!!)</option>
</select>
</fieldset>
<button type="submit" style="background:#27ae60;color:#fff;border:none;font-weight:bold">Сохранить</button>
</form>
<script>
const saved = [)rawliteral";
  for (int i = 0; i < CRUISE_MAX_LEVELS; i++) {
    html += String(cruiseLevelPercent[i]);
    if (i < CRUISE_MAX_LEVELS - 1) html += ",";
  }
  html += R"rawliteral(];
function renderLevels(){
  const count = parseInt(document.getElementById('count').value)||0;
  const div = document.getElementById('levels'); div.innerHTML='';
  for(let i=0;i<count;i++){
    const val = saved[i]!==undefined?saved[i]:0;
    div.innerHTML += '<label>Уровень '+(i+1)+' — цель (%)</label><input type="number" step="0.1" min="0" max="100" class="lvl-input" data-idx="'+i+'" value="'+val+'" onchange="saved['+i+']=parseFloat(this.value)||0">';
  }
}
function autoDistribute(){
  const count = parseInt(document.getElementById('count').value)||0;
  const st = parseFloat(document.getElementById('stPct').value)||0;
  const end = parseFloat(document.getElementById('endPct').value)||100;
  if(count <= 0) return;
  if(count === 1){
    saved[0] = Math.round(end * 10) / 10;
  } else {
    for(let i=0; i<count; i++){
      const frac = i / (count - 1);
      saved[i] = Math.round((st + frac * (end - st)) * 10) / 10;
    }
  }
  renderLevels();
}
renderLevels();
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  const d=new FormData(this);
  const count = parseInt(document.getElementById('count').value)||0;
  const lvls = [];
  document.querySelectorAll('.lvl-input').forEach(inp => {
    lvls.push(parseFloat(inp.value)||0);
  });
  d.append('levelsJson', JSON.stringify(lvls));
  fetch('/settings/cruise/save',{method:'POST',body:d}).then(()=>alert('Сохранено'));
});
</script>
)rawliteral" + getTopBarJs() + R"rawliteral(
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleCruiseSave() {
  cruiseLevelsCount = server.arg("count").toInt();
  if (cruiseLevelsCount < 0) cruiseLevelsCount = 0;
  if (cruiseLevelsCount > CRUISE_MAX_LEVELS) cruiseLevelsCount = CRUISE_MAX_LEVELS;
  
  if (server.hasArg("stPct")) cruiseStartPercent = server.arg("stPct").toFloat();
  if (server.hasArg("endPct")) cruiseEndPercent = server.arg("endPct").toFloat();

  if (server.hasArg("levelsJson")) {
    String json = server.arg("levelsJson");
    int startIdx = json.indexOf('[');
    int endIdx = json.lastIndexOf(']');
    if (startIdx != -1 && endIdx != -1 && endIdx > startIdx) {
      String content = json.substring(startIdx + 1, endIdx);
      int currentPos = 0;
      int idx = 0;
      while (currentPos < content.length() && idx < CRUISE_MAX_LEVELS) {
        int commaPos = content.indexOf(',', currentPos);
        if (commaPos == -1) commaPos = content.length();
        String item = content.substring(currentPos, commaPos);
        item.trim();
        if (item.length() > 0) {
          float v = item.toFloat();
          if (v < 0) v = 0;
          if (v > 100) v = 100;
          cruiseLevelPercent[idx++] = v;
        }
        currentPos = commaPos + 1;
      }
    }
  } else {
    for (int i = 0; i < cruiseLevelsCount; i++) {
      String key = "lvl" + String(i);
      if (server.hasArg(key)) {
        float v = server.arg(key).toFloat();
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        cruiseLevelPercent[i] = v;
      }
    }
  }

  cruiseSoftStartEnabled = server.hasArg("ssEn");
  cruiseSoftStopEnabled = server.hasArg("spEn");
  cruiseSoftStartMs = server.arg("ssMs").toInt();
  cruiseSoftStopMs = server.arg("spMs").toInt();
  cruiseConfirmThrottleAfterStart = server.hasArg("confThr");
  if (server.hasArg("brkMode")) cruiseAfterBrakingMode = server.arg("brkMode").toInt();
  if (server.hasArg("thrMode")) cruiseAfterThrottleMode = server.arg("thrMode").toInt();
  cruiseSettingsSave();
  server.send(200, "text/plain", "OK");
}

// ================= Веб: Система (экспорт/импорт настроек, OTA) =================
void handleSystemPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Система</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
a.card, label.card{display:block;background:#333;color:#fff;padding:15px;border-radius:8px;margin-bottom:10px;text-decoration:none;box-sizing:border-box;text-align:center;cursor:pointer}
a.card:hover, label.card:hover{background:#444}
.warn{background:#5c1a1a;padding:12px;border-radius:8px;margin-bottom:20px;font-weight:bold;font-size:14px}
a.back{color:#4a90d9;text-decoration:none;display:inline-block;margin-top:20px;}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a class="back" href="/">&larr; Меню</a></p>
<h1>Система</h1>
<div style="background:#222;border:1px solid #333;border-radius:8px;padding:12px;margin-bottom:15px">
  <div style="font-weight:bold;margin-bottom:8px;color:#4a90d9">Интерфейс и элементы управления</div>
  <label style="display:flex;align-items:center;gap:10px;cursor:pointer;margin-bottom:10px">
    <input type="checkbox" id="chkShowTopbar" onchange="toggleTopbar(this.checked)" style="width:auto;cursor:pointer">
    <span>Верхняя панель статуса (Top Bar)</span>
  </label>
  <label style="display:flex;align-items:center;gap:10px;cursor:pointer;margin-bottom:10px">
    <input type="checkbox" id="chkShowMatrix" onchange="toggleUiItem('ui_show_matrix', this.checked)" style="width:auto;cursor:pointer">
    <span>LED Matrix дисплей (16&times;32)</span>
  </label>
  <label style="display:flex;align-items:center;gap:10px;cursor:pointer;margin-bottom:10px">
    <input type="checkbox" id="chkShowControls" onchange="toggleUiItem('ui_show_controls', this.checked)" style="width:auto;cursor:pointer">
    <span>Кнопки симуляции (Тормоз, Педали, Газ)</span>
  </label>
  <label style="display:flex;align-items:center;gap:10px;cursor:pointer;margin-bottom:10px">
    <input type="checkbox" id="chkShowScreen" onchange="toggleUiItem('ui_show_screen', this.checked)" style="width:auto;cursor:pointer">
    <span>Экранчик джойстика</span>
  </label>
  <label style="display:flex;align-items:center;gap:10px;cursor:pointer">
    <input type="checkbox" id="chkShowDpad" onchange="toggleUiItem('ui_show_dpad', this.checked)" style="width:auto;cursor:pointer">
    <span>Кнопки джойстика (D-Pad)</span>
  </label>
</div>
<a class="card" href="/update">Обновить прошивку (.bin) &rarr;</a>
<a class="card" href="#" onclick="exportSettings(); return false;">Экспортировать настройки (.json) &rarr;</a>
<form id="importForm" enctype="multipart/form-data" method="post" action="/system/import" style="margin-top:10px">
  <label for="settingsFile" class="card">Импортировать настройки (.json) &rarr;</label>
  <input type="file" id="settingsFile" name="settingsFile" accept=".json" onchange="importSettings(this)" style="display:none;">
</form>

<script>
document.addEventListener("DOMContentLoaded", () => {
  const chk = document.getElementById("chkShowTopbar");
  if (chk) chk.checked = localStorage.getItem("ui_show_topbar") !== "false";
  const chkM = document.getElementById("chkShowMatrix");
  if (chkM) chkM.checked = localStorage.getItem("ui_show_matrix") !== "false";
  const chkC = document.getElementById("chkShowControls");
  if (chkC) chkC.checked = localStorage.getItem("ui_show_controls") !== "false";
  const chkS = document.getElementById("chkShowScreen");
  if (chkS) chkS.checked = localStorage.getItem("ui_show_screen") !== "false";
  const chkD = document.getElementById("chkShowDpad");
  if (chkD) chkD.checked = localStorage.getItem("ui_show_dpad") !== "false";
});

function toggleTopbar(show) {
  localStorage.setItem("ui_show_topbar", show ? "true" : "false");
  if (typeof updateSysStatus === "function") {
    updateSysStatus();
  } else {
    const tb = document.querySelector(".top-bar-sticky");
    if (tb) tb.style.display = show ? "flex" : "none";
  }
}

function toggleUiItem(key, show) {
  localStorage.setItem(key, show ? "true" : "false");
}
function exportSettings() {
  fetch('/system/export').then(r => {
    if (!r.ok) throw new Error('Export failed');
    return r.json();
  }).then(data => {
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'bike_settings.json';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }).catch(err => {
    alert('Ошибка экспорта: ' + err);
  });
}

function importSettings(input) {
  if (!input.files || !input.files[0]) return;
  const file = input.files[0];
  const reader = new FileReader();
  reader.onload = function(e) {
    const content = e.target.result;
    const form = new FormData();
    form.append('settingsFile', content);
    fetch('/system/import', { method: 'POST', body: form })
      .then(r => r.text())
      .then(msg => {
        alert(msg);
        setTimeout(() => location.href = '/', 2000);
      })
      .catch(err => alert('Ошибка импорта: ' + err));
  };
  reader.readAsText(file);
}
</script>
)rawliteral" + getTopBarJs() + R"rawliteral(
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleSettingsExport() {
  String json = "{";
  json += "\"throttle\":{";
  json += "\"inMinV\":" + String(throttleInMinV, 2) + ",";
  json += "\"inMaxV\":" + String(throttleInMaxV, 2) + ",";
  json += "\"outMinV\":" + String(throttleOutMinV, 2) + ",";
  json += "\"outMaxV\":" + String(throttleOutMaxV, 2) + ",";
  json += "\"divRatio\":" + String(throttleInputDividerRatio, 3) + ",";
  json += "\"gain\":" + String(throttleOutputGain, 2) + ",";
  json += "\"ssEn\":" + String(throttleSoftStartEnabled ? 1 : 0) + ",";
  json += "\"spEn\":" + String(throttleSoftStopEnabled ? 1 : 0) + ",";
  json += "\"ssMs\":" + String(throttleSoftStartMs) + ",";
  json += "\"spMs\":" + String(throttleSoftStopMs) + ",";
  json += "\"brakeCut\":" + String(ownBrakeCutoffEnabled ? 1 : 0);
  json += "},";
  json += "\"pas\":{";
  json += "\"magnets\":" + String(pasMagnetCount) + ",";
  json += "\"edge\":" + String(pasEdgeMode) + ",";
  json += "\"angle\":" + String(pasActivationAngle) + ",";
  json += "\"timeout\":" + String(pasTimeoutMs) + ",";
  json += "\"stopTO\":" + String(pasStopTimeoutMs) + ",";
  json += "\"cnt\":" + String(pasLevelsCount) + ",";
  json += "\"curLvl\":" + String(pasCurrentLevel) + ",";
  json += "\"ssEn\":" + String(pasSoftStartEnabled ? 1 : 0) + ",";
  json += "\"spEn\":" + String(pasSoftStopEnabled ? 1 : 0) + ",";
  json += "\"ssMs\":" + String(pasSoftStartMs) + ",";
  json += "\"spMs\":" + String(pasSoftStopMs) + ",";
  json += "\"enabled\":" + String(pasEnabled ? 1 : 0) + ",";
  json += "\"pct\":[";
  for (int i = 0; i < pasLevelsCount; i++) {
    json += String(pasLevelPercent[i]);
    if (i < pasLevelsCount - 1) json += ",";
  }
  json += "]";
  json += "},";
  json += "\"wifi\":{";
  json += "\"ssid\":\"" + storedSsid + "\",";
  json += "\"pass\":\"" + storedPass + "\""; // Add storedPass
  json += "},";
  json += "\"ap\":{"; // New AP settings section
  json += "\"ssid\":\"" + storedApSsid + "\",";
  json += "\"pass\":\"" + storedApPass + "\"";
  json += "},";
  json += "\"cruise\":{"; // New Cruise Control settings section
  json += "\"cnt\":" + String(cruiseLevelsCount) + ",";
  json += "\"stPct\":" + String(cruiseStartPercent) + ",";
  json += "\"endPct\":" + String(cruiseEndPercent) + ",";
  json += "\"pct\":[";
  for (int i = 0; i < cruiseLevelsCount; i++) {
    json += String(cruiseLevelPercent[i]);
    if (i < cruiseLevelsCount - 1) json += ",";
  }
  json += "],";
  json += "\"ssEn\":" + String(cruiseSoftStartEnabled ? 1 : 0) + ",";
  json += "\"spEn\":" + String(cruiseSoftStopEnabled ? 1 : 0) + ",";
  json += "\"ssMs\":" + String(cruiseSoftStartMs) + ",";
  json += "\"spMs\":" + String(cruiseSoftStopMs);
  json += "}";
  json += "}";
  server.send(200, "application/json", json);
}

void handleSettingsImport() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  if (!server.hasArg("settingsFile")) {
    server.send(400, "text/plain", "No settings file uploaded");
    return;
  }
  String fileContent = server.arg("settingsFile");
  int idx = fileContent.indexOf("\"throttle\":{");
  if (idx != -1) {
    String throttleJson = fileContent.substring(fileContent.indexOf('{', idx) + 1, fileContent.indexOf('}', idx));
    int inMinVPos = throttleJson.indexOf("\"inMinV\":");
    if (inMinVPos != -1)
      throttleInMinV = throttleJson.substring(inMinVPos + 9, throttleJson.indexOf(',', inMinVPos)).toFloat();
    int inMaxVPos = throttleJson.indexOf("\"inMaxV\":");
    if (inMaxVPos != -1)
      throttleInMaxV = throttleJson.substring(inMaxVPos + 9, throttleJson.indexOf(',', inMaxVPos)).toFloat();
    int outMinVPos = throttleJson.indexOf("\"outMinV\":");
    if (outMinVPos != -1)
      throttleOutMinV = throttleJson.substring(outMinVPos + 10, throttleJson.indexOf(',', outMinVPos)).toFloat();
    int outMaxVPos = throttleJson.indexOf("\"outMaxV\":");
    if (outMaxVPos != -1)
      throttleOutMaxV = throttleJson.substring(outMaxVPos + 10, throttleJson.indexOf(',', outMaxVPos)).toFloat();
    int divRatioPos = throttleJson.indexOf("\"divRatio\":");
    if (divRatioPos != -1)
      throttleInputDividerRatio = throttleJson.substring(divRatioPos + 11, throttleJson.indexOf(',', divRatioPos)).toFloat();
    int gainPos = throttleJson.indexOf("\"gain\":");
    if (gainPos != -1)
      throttleOutputGain = throttleJson.substring(gainPos + 7, throttleJson.indexOf(',', gainPos)).toFloat();
    int ssEnPos = throttleJson.indexOf("\"ssEn\":");
    if (ssEnPos != -1)
      throttleSoftStartEnabled = throttleJson.substring(ssEnPos + 7, throttleJson.indexOf(',', ssEnPos)).toInt() != 0;
    int spEnPos = throttleJson.indexOf("\"spEn\":");
    if (spEnPos != -1)
      throttleSoftStopEnabled = throttleJson.substring(spEnPos + 7, throttleJson.indexOf(',', spEnPos)).toInt() != 0;
    int ssMsPos = throttleJson.indexOf("\"ssMs\":");
    if (ssMsPos != -1)
      throttleSoftStartMs = throttleJson.substring(ssMsPos + 7, throttleJson.indexOf(',', ssMsPos)).toInt();
    int spMsPos = throttleJson.indexOf("\"spMs\":");
    if (spMsPos != -1)
      throttleSoftStopMs = throttleJson.substring(spMsPos + 7, throttleJson.indexOf(',', spMsPos)).toInt();
    int brakeCutPos = throttleJson.indexOf("\"brakeCut\":");
    if (brakeCutPos != -1) {
      int bcIdx = throttleJson.indexOf('{', brakeCutPos) + 1;
      int bcEnd = throttleJson.indexOf('}', bcIdx);
      if (bcEnd != -1) {
        ownBrakeCutoffEnabled = throttleJson.substring(bcIdx, bcEnd).toInt() != 0;
      }
    }
    throttleSettingsSave();
  }
  idx = fileContent.indexOf("\"pas\":{");
  if (idx != -1) {
    String pasJson = fileContent.substring(fileContent.indexOf('{', idx) + 1, fileContent.indexOf('}', idx));
    int magnetsPos = pasJson.indexOf("\"magnets\":");
    if (magnetsPos != -1)
      pasMagnetCount = pasJson.substring(magnetsPos + 10, pasJson.indexOf(',', magnetsPos)).toInt();
    int edgePos = pasJson.indexOf("\"edge\":");
    if (edgePos != -1)
      pasEdgeMode = pasJson.substring(edgePos + 7, pasJson.indexOf(',', edgePos)).toInt();
    int anglePos = pasJson.indexOf("\"angle\":");
    if (anglePos != -1)
      pasActivationAngle = pasJson.substring(anglePos + 8, pasJson.indexOf(',', anglePos)).toInt();
    if (pasJson.indexOf("\"timeout\":") != -1)
      pasTimeoutMs = pasJson.substring(pasJson.indexOf("\"timeout\":") + 10, pasJson.indexOf(',', pasJson.indexOf("\"timeout\":"))).toInt();
    if (pasJson.indexOf("\"stopTO\":") != -1) {
      pasStopTimeoutMs = pasJson.substring(pasJson.indexOf("\"stopTO\":") + 9, pasJson.indexOf(',', pasJson.indexOf("\"stopTO\":"))).toInt();
      if (pasStopTimeoutMs < 20) pasStopTimeoutMs = 20;
    }
    if (pasJson.indexOf("\"cnt\":") != -1)
      pasLevelsCount = pasJson.substring(pasJson.indexOf("\"cnt\":") + 6, pasJson.indexOf(',', pasJson.indexOf("\"cnt\":"))).toInt();
    if (pasJson.indexOf("\"curLvl\":") != -1)
      pasCurrentLevel = pasJson.substring(pasJson.indexOf("\"curLvl\":") + 9, pasJson.indexOf(',', pasJson.indexOf("\"curLvl\":"))).toInt();
    if (pasJson.indexOf("\"ssEn\":") != -1)
      pasSoftStartEnabled = pasJson.substring(pasJson.indexOf("\"ssEn\":") + 7, pasJson.indexOf(',', pasJson.indexOf("\"ssEn\":"))).toInt() != 0;
    if (pasJson.indexOf("\"spEn\":") != -1)
      pasSoftStopEnabled = pasJson.substring(pasJson.indexOf("\"spEn\":") + 7, pasJson.indexOf(',', pasJson.indexOf("\"spEn\":"))).toInt() != 0;
    if (pasJson.indexOf("\"ssMs\":") != -1)
      pasSoftStartMs = pasJson.substring(pasJson.indexOf("\"ssMs\":") + 7, pasJson.indexOf(',', pasJson.indexOf("\"ssMs\":"))).toInt();
    if (pasJson.indexOf("\"spMs\":") != -1)
      pasSoftStopMs = pasJson.substring(pasJson.indexOf("\"spMs\":") + 7, pasJson.indexOf(',', pasJson.indexOf("\"spMs\":"))).toInt();
    int pctStart = pasJson.indexOf("\"pct\":[");
    if (pctStart != -1) {
      pctStart += 7;
      int pctEnd = pasJson.indexOf("]", pctStart);
      String pctJson = pasJson.substring(pctStart, pctEnd);
      int currentPct = 0;
      int commaPos = -1;
      for (int i = 0; i < pasLevelsCount; i++) {
        commaPos = pctJson.indexOf(',', currentPct);
        if (commaPos == -1) commaPos = pctJson.length();
        pasLevelPercent[i] = pctJson.substring(currentPct, commaPos).toInt();
        currentPct = commaPos + 1;
      }
    }
    pasSettingsSave();
    reattachPasInterrupt();
  }
  idx = fileContent.indexOf("\"wifi\":{");
  if (idx != -1) {
    String wifiJson = fileContent.substring(fileContent.indexOf('{', idx) + 1, fileContent.indexOf('}', idx));
    int ssidPos = wifiJson.indexOf("\"ssid\":\"");
    if (ssidPos != -1) {
      String importedSsid = wifiJson.substring(ssidPos + 8);
      int commaPos = importedSsid.indexOf("\",");
      if (commaPos != -1) {
        importedSsid = importedSsid.substring(0, commaPos);
        if (importedSsid.length() > 0) {
          wifiCredsSave(importedSsid, "");
        }
      }
    }
  }

  // --- AP Settings ---
  idx = fileContent.indexOf("\"ap\":{");
  if (idx != -1) {
    String apJson = fileContent.substring(fileContent.indexOf('{', idx) + 1, fileContent.indexOf('}', idx));
    int apSsidPos = apJson.indexOf("\"ssid\":\"");
    if (apSsidPos != -1) {
      String importedApSsid = apJson.substring(apSsidPos + 8);
      int commaPos = importedApSsid.indexOf("\",");
      if (commaPos != -1) {
        importedApSsid = importedApSsid.substring(0, commaPos);
        if (importedApSsid.length() > 0) {
          storedApSsid = importedApSsid;
          storedApPass = ""; // Clear pass if SSID is changed
          int apPassPos = apJson.indexOf("\"pass\":\"");
          if (apPassPos != -1) {
            String importedApPass = apJson.substring(apPassPos + 8);
            commaPos = importedApPass.indexOf("\",");
            if (commaPos != -1) {
              importedApPass = importedApPass.substring(0, commaPos);
              storedApPass = importedApPass;
            }
          }
          apSettingsSave(); // Save AP settings
        }
      }
    }
  }

  // --- Cruise Control Settings ---
  idx = fileContent.indexOf("\"cruise\":{");
  if (idx != -1) {
    String cruiseJson = fileContent.substring(fileContent.indexOf('{', idx) + 1, fileContent.indexOf('}', idx));
    int cntPos = cruiseJson.indexOf("\"cnt\":");
    if (cntPos != -1) cruiseLevelsCount = cruiseJson.substring(cntPos + 6, cruiseJson.indexOf(',', cntPos)).toInt();
    int stPctPos = cruiseJson.indexOf("\"stPct\":");
    if (stPctPos != -1) cruiseStartPercent = cruiseJson.substring(stPctPos + 8, cruiseJson.indexOf(',', stPctPos)).toFloat();
    int endPctPos = cruiseJson.indexOf("\"endPct\":");
    if (endPctPos != -1) cruiseEndPercent = cruiseJson.substring(endPctPos + 9, cruiseJson.indexOf(',', endPctPos)).toFloat();
    int ssEnPos = cruiseJson.indexOf("\"ssEn\":");
    if (ssEnPos != -1) cruiseSoftStartEnabled = cruiseJson.substring(ssEnPos + 7, cruiseJson.indexOf(',', ssEnPos)).toInt() != 0;
    int spEnPos = cruiseJson.indexOf("\"spEn\":");
    if (spEnPos != -1) cruiseSoftStopEnabled = cruiseJson.substring(spEnPos + 7, cruiseJson.indexOf(',', spEnPos)).toInt() != 0;
    int ssMsPos = cruiseJson.indexOf("\"ssMs\":");
    if (ssMsPos != -1) cruiseSoftStartMs = cruiseJson.substring(ssMsPos + 7, cruiseJson.indexOf(',', ssMsPos)).toInt();
    int spMsPos = cruiseJson.indexOf("\"spMs\":");
    if (spMsPos != -1) cruiseSoftStopMs = cruiseJson.substring(spMsPos + 7, cruiseJson.indexOf(',', spMsPos)).toInt();
    
      int pctStart = cruiseJson.indexOf("\"pct\":[");
      if (pctStart != -1) {
        pctStart += 7;
        int pctEnd = cruiseJson.indexOf("]", pctStart);
        if (pctEnd != -1) {
          String pctJson = cruiseJson.substring(pctStart, pctEnd);
          int currentPct = 0;
          int commaPos = -1;
          for (int i = 0; i < cruiseLevelsCount; i++) {
            commaPos = pctJson.indexOf(',', currentPct);
            if (commaPos == -1) commaPos = pctJson.length();
            cruiseLevelPercent[i] = pctJson.substring(currentPct, commaPos).toFloat();
            currentPct = commaPos + 1;
          }
        }
      }
    cruiseSettingsSave();
  }

  server.send(200, "text/plain", "Настройки успешно импортированы. Перезагрузка...");
  delay(1500);
  ESP.restart();
}
