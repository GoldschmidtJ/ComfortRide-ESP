#include "nvs_flash.h"

bool pasInterruptAttached = false;
#include <Arduino.h>
#include <driver/gpio.h>

#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <math.h>

#define FIRMWARE_VERSION "0.3.1"


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

// ================= ПИНЫ (конструктор распиновки в веб-интерфейсе) =================
// Заводская распиновка проекта. Её можно переназначить через веб-интерфейс
// (/settings/pins): конфиг хранится в NVS и применяется при старте в setup().
// Газ: GND -> GND, +5В -> 5В, Сигнал -> GPIO34 (см. предупреждение по напряжению ниже).
// Выход газа — настоящий ЦАП ESP32 + усилитель на ОУ (MCP6002, канал Б):
// ЦАП даёт чистые 0-3.3В, ОУ поднимает их до нужных 0-4.2В.
// Свет/звук: MOSFET AOD418 (низкая сторона — минус нагрузки на пин, плюс — на 12В-шину).

struct PinConfig {
  int16_t throttleAdc;   // вход ручки газа (АЦП)
  int16_t throttleDac;   // выход газа на мотор-контроллер (ЦАП, только GPIO25/26)
  int16_t brake;         // тормоз (вход с подтяжкой)
  int16_t pasSensor;     // PAS-датчик (прерывание, подтяжка)
  int16_t pasButton;     // кнопка уровня PAS
  int16_t headlight;     // фара (ШИМ)
  int16_t drl;           // ДХО (ШИМ)
  int16_t turnLeft;      // поворотник левый
  int16_t turnRight;     // поворотник правый
  int16_t horn;          // гудок
  int16_t buzzer;        // пищалка (тик поворотника)
  int16_t btnHeadlight;  // кнопка фары
  int16_t btnTurnLeft;   // кнопка поворотника влево
  int16_t btnTurnRight;  // кнопка поворотника вправо
  int16_t btnHorn;       // кнопка гудка (отжимная)
};

static const PinConfig PIN_CONFIG_DEFAULTS = {34, 25, 27, 14, 13, 18, 19, 21, 22, 23, 4, 16, 17, 32, 33};
PinConfig pinConfig = PIN_CONFIG_DEFAULTS;
bool pinConfigCustom = false; // true — пользователь менял распиновку через веб

// Рабочие переменные пинов — их использует вся остальная прошивка.
// Заполняются из pinConfig при старте (applyPinConfig()).
int THROTTLE_ADC_PIN   = 34;
int THROTTLE_DAC_PIN   = 25;
int BRAKE_PIN          = 27;
int PAS_SENSOR_PIN     = 14;
int PAS_BUTTON_PIN     = 13;
int HEADLIGHT_PIN      = 18;
int DRL_PIN            = 19;
int TURN_LEFT_PIN      = 21;
int TURN_RIGHT_PIN     = 22;
int HORN_PIN           = 23;
int BUZZER_PIN         = 4;
int BTN_HEADLIGHT_PIN  = 16;
int BTN_TURN_LEFT_PIN  = 17;
int BTN_TURN_RIGHT_PIN = 32;
int BTN_HORN_PIN       = 33;

// --- Возможности GPIO (ESP32-WROOM) ---
bool gpioExists(int g)    { return (g >= 0 && g <= 19) || (g >= 21 && g <= 23) || (g >= 25 && g <= 27) || (g >= 32 && g <= 39); }
bool gpioInputOnly(int g) { return g >= 34 && g <= 39; }  // 34-39: только вход, внутренней подтяжки нет
bool gpioHasAdc(int g)    { return g == 0 || g == 2 || g == 4 || (g >= 12 && g <= 15) || (g >= 25 && g <= 27) || (g >= 32 && g <= 39); }
bool gpioIsAdc2(int g)    { return g == 0 || g == 2 || g == 4 || (g >= 12 && g <= 15) || (g >= 25 && g <= 27); } // ADC2 конфликтует с Wi-Fi
bool gpioHasDac(int g)    { return g == 25 || g == 26; }
bool gpioIsFlash(int g)   { return g >= 6 && g <= 11; }   // подключены к SPI Flash
bool gpioIsStrap(int g)   { return g == 0 || g == 2 || g == 5 || g == 12 || g == 15; } // страппинг-пины
bool gpioIsUart(int g)    { return g == 1 || g == 3; }    // UART0 (USB/отладка)

// --- Роли пинов ---
const int PIN_ROLE_COUNT = 15;
struct PinRole {
  const char *key;    // имя в HTTP/NVS
  const char *title;  // название в интерфейсе
  bool output;        // выход (иначе вход)
  bool adc;           // требуется АЦП
  bool dac;           // требуется ЦАП
  bool pullup;        // нужна внутренняя подтяжка
  bool pwm;           // ШИМ (LEDC)
};

const PinRole pinRoles[PIN_ROLE_COUNT] = {
  {"tAdc",  "Вход газа",           false, true,  false, false, false},
  {"tDac",  "Выход газа (ЦАП)",    true,  false, true,  false, false},
  {"brake", "Тормоз",              false, false, false, true,  false},
  {"pas",   "PAS-датчик",          false, false, false, true,  false},
  {"pasB",  "Кнопка PAS",          false, false, false, true,  false},
  {"light", "Фара",                true,  false, false, false, true},
  {"drl",   "ДХО",                 true,  false, false, false, true},
  {"turnL", "Поворотник левый",    true,  false, false, false, false},
  {"turnR", "Поворотник правый",   true,  false, false, false, false},
  {"horn",  "Гудок",               true,  false, false, false, false},
  {"buzz",  "Пищалка",             true,  false, false, false, false},
  {"bLgt",  "Кнопка фары",         false, false, false, true,  false},
  {"bTL",   "Кнопка пов. влево",   false, false, false, true,  false},
  {"bTR",   "Кнопка пов. вправо",  false, false, false, true,  false},
  {"bHrn",  "Кнопка гудка",        false, false, false, true,  false},
};

#define USER_LABEL_SIZE 65
char pinRoleNames[PIN_ROLE_COUNT][USER_LABEL_SIZE] = {};

// Дополнительные GPIO резервируют и инициализируют пин, но не привязаны к
// функциям контроллера. Фиксированный массив сохраняет бинарную совместимость NVS.
#define CUSTOM_PIN_MAX 8
enum CustomPinMode : uint8_t { CUSTOM_PIN_INPUT=0, CUSTOM_PIN_INPUT_PULLUP=1, CUSTOM_PIN_OUTPUT=2 };
struct CustomPinRole { uint8_t used, mode; int16_t gpio; char name[USER_LABEL_SIZE]; };
CustomPinRole customPins[CUSTOM_PIN_MAX] = {};

String customPinModeName(uint8_t mode) {
  if (mode == CUSTOM_PIN_INPUT_PULLUP) return "вход + подтяжка";
  if (mode == CUSTOM_PIN_OUTPUT) return "выход";
  return "вход";
}

String htmlEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '\"') out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else out += c;
  }
  return out;
}

String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '\\' || c == '\"') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if ((uint8_t)c >= 0x20) out += c;
  }
  return out;
}

bool normalizeUserLabel(String &value) {
  value.trim();
  if (!value.length() || value.length() >= USER_LABEL_SIZE) return false;
  for (size_t i = 0; i < value.length(); i++) if ((uint8_t)value[i] < 0x20 || value[i] == '<' || value[i] == '>') return false;
  return true;
}

String pinRoleName(int i) {
  return pinRoleNames[i][0] ? String(pinRoleNames[i]) : String(pinRoles[i].title);
}

void setUserLabel(char *dest, const String &value) {
  memset(dest, 0, USER_LABEL_SIZE);
  value.substring(0, USER_LABEL_SIZE - 1).toCharArray(dest, USER_LABEL_SIZE);
}

int16_t& pinField(PinConfig &c, int i) {
  switch (i) {
    case 0:  return c.throttleAdc;
    case 1:  return c.throttleDac;
    case 2:  return c.brake;
    case 3:  return c.pasSensor;
    case 4:  return c.pasButton;
    case 5:  return c.headlight;
    case 6:  return c.drl;
    case 7:  return c.turnLeft;
    case 8:  return c.turnRight;
    case 9:  return c.horn;
    case 10: return c.buzzer;
    case 11: return c.btnHeadlight;
    case 12: return c.btnTurnLeft;
    case 13: return c.btnTurnRight;
    default: return c.btnHorn;
  }
}

// Валидация конфигурации: ошибки (запрет сохранения) и предупреждения.
String validatePinConfig(PinConfig &c, String *warnings) {
  String errors = "";
  if (warnings) *warnings = "";
  for (int i = 0; i < PIN_ROLE_COUNT; i++) {
    int g = pinField(c, i);
    const PinRole &r = pinRoles[i];
    String where = pinRoleName(i) + " (GPIO" + String(g) + "): ";
    if (!gpioExists(g)) { errors += where + "пин не существует\n"; continue; }
    if (gpioIsFlash(g)) { errors += where + "занят SPI Flash\n"; continue; }
    if (gpioIsUart(g))  { errors += where + "занят UART0 (USB/отладка)\n"; continue; }
    if (r.output && gpioInputOnly(g)) { errors += where + "пин работает только на вход\n"; continue; }
    if (r.pullup && gpioInputOnly(g)) { errors += where + "нет внутренней подтяжки (нужен внешний резистор)\n"; continue; }
    if (r.adc && !gpioHasAdc(g)) { errors += where + "на пине нет АЦП\n"; continue; }
    if (r.dac && !gpioHasDac(g)) { errors += where + "ЦАП есть только на GPIO25/GPIO26\n"; continue; }
    if (warnings) {
      if (gpioIsStrap(g)) *warnings += where + "страппинг-пин — влияет на режим загрузки платы\n";
      if (r.adc && gpioIsAdc2(g)) *warnings += where + "ADC2 — нестабилен при активном Wi-Fi\n";
    }
  }
  for (int i = 0; i < PIN_ROLE_COUNT; i++)
    for (int j = i + 1; j < PIN_ROLE_COUNT; j++)
      if (pinField(c, i) == pinField(c, j))
        errors += pinRoleName(j) + " и " + pinRoleName(i) + " на одном пине (GPIO" + String(pinField(c, i)) + ")\n";
  return errors;
}

void applyPinConfig() {
  THROTTLE_ADC_PIN   = pinConfig.throttleAdc;
  THROTTLE_DAC_PIN   = pinConfig.throttleDac;
  BRAKE_PIN          = pinConfig.brake;
  PAS_SENSOR_PIN     = pinConfig.pasSensor;
  PAS_BUTTON_PIN     = pinConfig.pasButton;
  HEADLIGHT_PIN      = pinConfig.headlight;
  DRL_PIN            = pinConfig.drl;
  TURN_LEFT_PIN      = pinConfig.turnLeft;
  TURN_RIGHT_PIN     = pinConfig.turnRight;
  HORN_PIN           = pinConfig.horn;
  BUZZER_PIN         = pinConfig.buzzer;
  BTN_HEADLIGHT_PIN  = pinConfig.btnHeadlight;
  BTN_TURN_LEFT_PIN  = pinConfig.btnTurnLeft;
  BTN_TURN_RIGHT_PIN = pinConfig.btnTurnRight;
  BTN_HORN_PIN       = pinConfig.btnHorn;
}

void pinSettingsSave() {
  Preferences p;
  p.begin("pins", false);
  p.putBytes("cfg", &pinConfig, sizeof(pinConfig));
  p.putBool("custom", pinConfigCustom);
  p.putBytes("names", pinRoleNames, sizeof(pinRoleNames));
  p.putBytes("extra", customPins, sizeof(customPins));
  p.end();
}

void pinSettingsLoad() {
  Preferences p;
  // read-only begin() падает с "nvs_open failed: NOT_FOUND", если namespace ещё
  // не создан (первый запуск). Это норма, а не ошибка — используем заводскую распиновку.
  if (!p.begin("pins", true)) return;
  PinConfig c = PIN_CONFIG_DEFAULTS;
  size_t got = p.isKey("cfg") ? p.getBytes("cfg", &c, sizeof(c)) : 0;
  bool custom = p.getBool("custom", false);
  if (p.isKey("names") && p.getBytesLength("names") == sizeof(pinRoleNames)) p.getBytes("names", pinRoleNames, sizeof(pinRoleNames));
  if (p.isKey("extra") && p.getBytesLength("extra") == sizeof(customPins)) p.getBytes("extra", customPins, sizeof(customPins));
  p.end();
  if (got == sizeof(c)) {
    pinConfig = c;
    pinConfigCustom = custom;
  } else {
    pinConfig = PIN_CONFIG_DEFAULTS;
    pinConfigCustom = false;
  }
}

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
void handleEmulationPage();
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
void handleBusCaptureControl();
void handleBusCaptureStatus();
void handleBusCaptureData();
void handleBusCaptureCsv();
void handleSystemPage();
void handleSettingsExport();
void handleSettingsImport();
void handleSystemFactoryReset();
void handlePinsPage();
void handlePinsSave();
void handlePinRowSave();
String customPinRowHtml(int i, const CustomPinRole &c);
void handleCustomPinSave();
void handleCustomPinDelete();
void handlePinsReset();
void handleEventsPage();
void handleEventsSave();
void handleEventRuleSave();
void handleEventRuleDelete();
void handleEventsReset();
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
extern bool serviceModeActive;
extern int serviceThrottleLimitPct;
extern bool headlightOn;
extern bool drlOn;
extern bool turnLeftActive;
extern bool turnRightActive;
extern bool turnBlinkState;

// ================= System status tracking ================
unsigned long cpuMeasureStartMs = 0;
unsigned long cpuBusyTimeMicros = 0;
int cpuUsagePercent = 0;
// Секционный профайлер: накопители времени (мкс) по секциям контура за окно
// измерения (1 с) и их проценты — позволяют увидеть, какая функция ест CPU.
unsigned long cpuUsPas = 0;
unsigned long cpuUsPasBtn = 0;
unsigned long cpuUsThrottle = 0;
unsigned long cpuUsLight = 0;
unsigned long cpuUsSound = 0;
int cpuPasPct = 0;
int cpuPasBtnPct = 0;
int cpuThrottlePct = 0;
int cpuLightPct = 0;
int cpuSoundPct = 0;

// ================= Real-time telemetry variables ================
volatile float hwThrottleInV = 0.0f;
volatile float hwThrottleOutV = 0.0f;
volatile float hwThrottlePct = 0.0f;
volatile float hwMotorOutPct = 0.0f;
volatile bool factoryResetInProgress = false;
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
  json += "\"turn_left\":" + String((turnLeftActive && turnBlinkState) ? "true" : "false") + ",";
  json += "\"turn_right\":" + String((turnRightActive && turnBlinkState) ? "true" : "false") + ",";
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
bool ownBrakeCutoffEnabled = true; // всегда включено; настройка скрыта из веб-интерфейса

// ================= Сервисный режим (конструктор событий, «проблема 5») =================
// Включается/выключается событиями (см. updateEventEngine): ограничение газа,
// PAS не выше 1 уровня, круиз запрещён. Аппаратный тормоз и fail-safe нуля
// газа на старте не затрагиваются — они приоритетнее любых событий.
bool serviceModeActive = false;
int serviceThrottleLimitPct = 30; // % от рабочего диапазона выхода газа

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
  ownBrakeCutoffEnabled = true; // всегда включено, настройка пользователю не показывается
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
portMUX_TYPE pasPulseMux = portMUX_INITIALIZER_UNLOCKED;

extern float pasSmoothOutV; // определён ниже, в блоке мягкого старта/стопа
extern volatile bool pasCalRunning;          // калибровка магнитов (определены ниже)
extern volatile unsigned long pasCalPulses;
extern volatile unsigned long pasCalLastPulseMicros;

void IRAM_ATTR onPasPulse() {
  unsigned long now = micros();
  portENTER_CRITICAL_ISR(&pasPulseMux);
  pasLastPulseMicros = now;
  pasConsecutivePulses++;
  if (pasCalRunning) {
    pasCalPulses++;
    pasCalLastPulseMicros = now;
  }
  portEXIT_CRITICAL_ISR(&pasPulseMux);
}

int pasRequiredPulses() {
  int required = (int)round(pasActivationAngle * pasMagnetCount / 360.0f);
  if (required < 1) required = 1; // без max() — раньше тут была ошибка компиляции из-за смешения типов
  return required;
}

void updatePasDetection() {
  unsigned long now = micros();
  unsigned long lastPulse;
  unsigned long consecutivePulses;
  portENTER_CRITICAL(&pasPulseMux);
  lastPulse = pasLastPulseMicros;
  consecutivePulses = pasConsecutivePulses;
  portEXIT_CRITICAL(&pasPulseMux);
  if (lastPulse == 0) { pasConfirmedActive = false; return; }
  unsigned long sincePulseUs = now - lastPulse;

  // После полной паузы начинаем новую последовательность импульсов.
  if (sincePulseUs >= pasTimeoutMs * 1000UL) {
    portENTER_CRITICAL(&pasPulseMux);
    // Не стираем импульс, который мог прийти после сделанного выше снимка.
    if (pasLastPulseMicros == lastPulse) pasConsecutivePulses = 0;
    portEXIT_CRITICAL(&pasPulseMux);
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
  if (consecutivePulses >= (unsigned long)pasRequiredPulses()) {
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
  // Сервисный режим: PAS не выше 1 уровня, каким бы путём его ни raised
  int effLevel = pasCurrentLevel;
  if (serviceModeActive && effLevel > 1) effLevel = 1;
  if (effLevel <= 0 || effLevel > pasLevelsCount) return 0;
  if (!pasConfirmedActive) return 0;
  float pct = pasLevelPercent[effLevel - 1];
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
  pasLevelsCount = constrain(prefs.getInt("cnt", 3), 0, PAS_MAX_LEVELS);
  size_t got = prefs.isKey("pct") && prefs.getBytesLength("pct") == sizeof(pasLevelPercent)
                 ? prefs.getBytes("pct", pasLevelPercent, sizeof(pasLevelPercent)) : 0;
  pasSoftStartEnabled = prefs.getInt("ssEn", 0) != 0;
  pasSoftStopEnabled = prefs.getInt("spEn", 0) != 0;
  pasSoftStartMs = prefs.getULong("ssMs", 500);
  pasSoftStopMs = prefs.getULong("spMs", 800);
  pasEnabled = (prefs.getInt("enabled", 1) != 0);
  prefs.end();
  if (got != sizeof(pasLevelPercent)) {
    pasAutoDistribute();
  }
  for (int i = 0; i < PAS_MAX_LEVELS; i++) pasLevelPercent[i] = constrain(pasLevelPercent[i], 0, 100);
  pasCurrentLevel = constrain(pasCurrentLevel, 0, pasLevelsCount);
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
bool drlOn = true; // ДХО горит с включением платы (см. setup)
bool headlightBlink = false, drlBlink = false;
bool headlightBlinkState = false, drlBlinkState = false;
unsigned long headlightBlinkAtMs = 0, drlBlinkAtMs = 0;
unsigned long headlightBlinkMs = 500, drlBlinkMs = 500;

void toggleHeadlight() {
  headlightBlink = false;
  headlightOn = !headlightOn;
  ledcWrite(0, headlightOn ? HEADLIGHT_DEFAULT_BRIGHTNESS : 0);
  Serial.println(headlightOn ? "Фара: ВКЛ" : "Фара: ВЫКЛ");
}

void toggleDrl() {
  drlBlink = false;
  drlOn = !drlOn;
  ledcWrite(1, drlOn ? DRL_DEFAULT_BRIGHTNESS : 0);
  Serial.println(drlOn ? "ДХО: ВКЛ" : "ДХО: ВЫКЛ");
}

void toggleLightBlink(bool headlight, unsigned long intervalMs) {
  intervalMs = constrain(intervalMs, 100UL, 5000UL);
  bool &enabled = headlight ? headlightBlink : drlBlink;
  bool &state = headlight ? headlightBlinkState : drlBlinkState;
  unsigned long &period = headlight ? headlightBlinkMs : drlBlinkMs;
  unsigned long &changedAt = headlight ? headlightBlinkAtMs : drlBlinkAtMs;
  enabled = !enabled;
  period = intervalMs;
  state = enabled;
  changedAt = millis();
  if (enabled) {
    ledcWrite(headlight ? 0 : 1, headlight ? HEADLIGHT_DEFAULT_BRIGHTNESS : DRL_DEFAULT_BRIGHTNESS);
  } else {
    ledcWrite(headlight ? 0 : 1, (headlight ? headlightOn : drlOn) ?
              (headlight ? HEADLIGHT_DEFAULT_BRIGHTNESS : DRL_DEFAULT_BRIGHTNESS) : 0);
  }
}

void updateLightBlink() {
  unsigned long now = millis();
  if (headlightBlink && now - headlightBlinkAtMs >= headlightBlinkMs) {
    headlightBlinkAtMs = now;
    headlightBlinkState = !headlightBlinkState;
    ledcWrite(0, headlightBlinkState ? HEADLIGHT_DEFAULT_BRIGHTNESS : 0);
  }
  if (drlBlink && now - drlBlinkAtMs >= drlBlinkMs) {
    drlBlinkAtMs = now;
    drlBlinkState = !drlBlinkState;
    ledcWrite(1, drlBlinkState ? DRL_DEFAULT_BRIGHTNESS : 0);
  }
}

// ================= Пищалка (тик поворотника) =================
bool buzzerOn = false;
unsigned long buzzerOffAtMs = 0;
// Серия коротких писков (подтверждение входа/выхода сервисного режима)
uint8_t buzzerPatternRemaining = 0;
unsigned long buzzerPatternNextMs = 0;

void buzzerClick(unsigned long durationMs) {
  digitalWrite(BUZZER_PIN, HIGH);
  buzzerOn = true;
  buzzerOffAtMs = millis() + durationMs;
}

void buzzerPattern(uint8_t beeps) {
  buzzerPatternRemaining = beeps;
  buzzerPatternNextMs = 0; // первый писк сразу
}

void updateBuzzer() {
  unsigned long now = millis();
  if (buzzerOn && now >= buzzerOffAtMs) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerOn = false;
    if (buzzerPatternRemaining > 0) {
      buzzerPatternRemaining--;
      if (buzzerPatternRemaining > 0) buzzerPatternNextMs = now + 120;
    }
  }
  if (!buzzerOn && buzzerPatternRemaining > 0 &&
      (buzzerPatternNextMs == 0 || now >= buzzerPatternNextMs)) {
    buzzerClick(120);
    buzzerPatternNextMs = 1; // следующий — только по расписанию
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
// Гудок обычно отжимной (пока держишь кнопку), но событиям нужен сигнал
// фиксированной длительности — поверх кнопочного состояния действует override.
unsigned long hornOverrideUntilMs = 0;

void hornBeep(unsigned long durationMs) {
  hornOverrideUntilMs = millis() + durationMs;
}

void updateHorn() {
  if (millis() < hornOverrideUntilMs) {
    digitalWrite(HORN_PIN, HIGH);
    return;
  }
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

// ================= Конструктор событий =================
#define EVENT_MAX_RULES 8
enum EventTrigger { EV_BRAKE_PRESS=1, EV_BRAKE_RELEASE, EV_BRAKE_HOLD, EV_BTN_HEADLIGHT, EV_BTN_TURN_L, EV_BTN_TURN_R, EV_BTN_HORN, EV_BTN_PAS, EV_PAS_LEVEL, EV_PAS_ON, EV_PAS_OFF, EV_CRUISE_ON, EV_CRUISE_OFF, EV_BOOT };
#define EV_TRIGGER_MAX 14
enum EventCondition { EV_NONE=0, EV_PRESS_COUNT, EV_HOLD_MS };
enum EventAction { EV_NO_ACTION=0, EV_SERVICE_TOGGLE, EV_SERVICE_ON, EV_SERVICE_OFF, EV_LIGHT_TOGGLE, EV_DRL_TOGGLE, EV_TURN_L_TOGGLE, EV_TURN_R_TOGGLE, EV_HORN_BEEP, EV_BUZZER_BEEP, EV_PAS_SET_LEVEL, EV_PAS_TOGGLE, EV_LIGHT_BLINK, EV_DRL_BLINK };
#define EV_ACTION_MAX EV_DRL_BLINK
#define EVENT_MAX_ACTIONS 3 // до 3 результатов на правило
struct EventRule { uint8_t enabled, trigger, condition, priority; uint16_t count; uint32_t intervalMs; uint8_t actions[EVENT_MAX_ACTIONS]; int16_t actionValues[EVENT_MAX_ACTIONS]; };
char eventRuleNames[EVENT_MAX_RULES][USER_LABEL_SIZE] = {};
String eventRuleName(int i) { return eventRuleNames[i][0] ? String(eventRuleNames[i]) : "Правило " + String(i + 1); }
// Старый формат (одно действие на правило) — только для миграции сохранённых правил.
struct EventRuleV1 { uint8_t enabled, trigger, condition, priority; uint16_t count; uint32_t intervalMs; uint8_t action; int16_t actionValue; };
const EventRule EVENT_DEFAULTS[EVENT_MAX_RULES] = {{1,EV_BRAKE_PRESS,EV_PRESS_COUNT,0,5,700,{EV_SERVICE_TOGGLE},{30}}};
EventRule eventRules[EVENT_MAX_RULES];
struct EventRuntime { uint16_t count; unsigned long lastPress, lastFire; bool holdFired; } eventRuntime[EVENT_MAX_RULES];
struct EventLog { unsigned long at; uint8_t rule; } eventLog[10];
int eventLogHead=0,eventLogCount=0;
void eventLogAdd(uint8_t i){eventLog[eventLogHead]={(unsigned long)millis(),i};eventLogHead=(eventLogHead+1)%10;if(eventLogCount<10)eventLogCount++;}
bool eventSettingsSave(){
  Preferences p;
  if(!p.begin("events",false)) return false;
  size_t rulesWritten=p.putBytes("rules",eventRules,sizeof(eventRules));
  size_t namesWritten=p.putBytes("names",eventRuleNames,sizeof(eventRuleNames));
  bool ok=rulesWritten==sizeof(eventRules)&&namesWritten==sizeof(eventRuleNames)&&
          p.getBytesLength("rules")==sizeof(eventRules)&&p.getBytesLength("names")==sizeof(eventRuleNames);
  p.end();
  return ok;
}
void eventSettingsLoad(){
  Preferences p;
  // read-only begin() падает с "NOT_FOUND", если namespace ещё не создан — это норма
  // при первом запуске, просто применяем правила по умолчанию без шума в логе.
  if(!p.begin("events",true)){memcpy(eventRules,EVENT_DEFAULTS,sizeof(eventRules));return;}
  size_t n=p.isKey("rules")?p.getBytesLength("rules"):0;
  if(p.isKey("names")&&p.getBytesLength("names")==sizeof(eventRuleNames))p.getBytes("names",eventRuleNames,sizeof(eventRuleNames));
  if(n==sizeof(eventRules)){p.getBytes("rules",eventRules,sizeof(eventRules));p.end();return;}
  if(n==sizeof(EventRuleV1)*EVENT_MAX_RULES){
    // Миграция старого формата: единственное действие переносится в слот 0.
    EventRuleV1 old[EVENT_MAX_RULES];
    p.getBytes("rules",old,sizeof(old));p.end();
    memset(eventRules,0,sizeof(eventRules));
    for(int i=0;i<EVENT_MAX_RULES;i++){
      EventRule &r=eventRules[i];EventRuleV1 &o=old[i];
      r.enabled=o.enabled;r.trigger=o.trigger;r.condition=o.condition;r.priority=o.priority;
      r.count=o.count;r.intervalMs=o.intervalMs;
      r.actions[0]=o.action;r.actionValues[0]=o.actionValue;
    }
    eventSettingsSave(); // сразу пересохраняем в новом формате
    return;
  }
  p.end();
  memcpy(eventRules,EVENT_DEFAULTS,sizeof(eventRules));
}
void eventSettingsReset(){memcpy(eventRules,EVENT_DEFAULTS,sizeof(eventRules));memset(eventRuleNames,0,sizeof(eventRuleNames));eventSettingsSave();}
void serviceModeApply(bool on){if(serviceModeActive==on)return;serviceModeActive=on;if(on){if(pasCurrentLevel>1)pasCurrentLevel=1;cruiseEnabled=false;cruiseCurrentLevel=0;cruiseEngaged=false;cruisePendingResume=false;buzzerPattern(3);}else buzzerPattern(2);}
void eventExecute(uint8_t a,int16_t v){switch(a){case EV_SERVICE_TOGGLE:serviceModeApply(!serviceModeActive);break;case EV_SERVICE_ON:serviceModeApply(true);break;case EV_SERVICE_OFF:serviceModeApply(false);break;case EV_LIGHT_TOGGLE:toggleHeadlight();break;case EV_DRL_TOGGLE:toggleDrl();break;case EV_TURN_L_TOGGLE:toggleTurnLeft();break;case EV_TURN_R_TOGGLE:toggleTurnRight();break;case EV_HORN_BEEP:hornBeep(v>0?v:300);break;case EV_BUZZER_BEEP:buzzerClick(v>0?v:150);break;case EV_PAS_SET_LEVEL:pasCurrentLevel=constrain(v,0,pasLevelsCount);pasEnabled=pasCurrentLevel>0;break;case EV_PAS_TOGGLE:pasEnabled=!pasEnabled;if(!pasEnabled)pasCurrentLevel=0;else if(!pasCurrentLevel)pasCurrentLevel=1;break;case EV_LIGHT_BLINK:toggleLightBlink(true,v>0?v:500);break;case EV_DRL_BLINK:toggleLightBlink(false,v>0?v:500);break;default:break;}}
void eventFire(int i){unsigned long n=millis();if(n-eventRuntime[i].lastFire<300)return;eventRuntime[i].lastFire=n;for(int k=0;k<EVENT_MAX_ACTIONS;k++)if(eventRules[i].actions[k]!=EV_NO_ACTION)eventExecute(eventRules[i].actions[k],eventRules[i].actionValues[k]);eventLogAdd(i);}
// Обработка фронта нажатия: без условия — сразу, с серией — счётчик в окне intervalMs.
// Условие EV_HOLD_MS здесь игнорируется: удержание считает отдельный хелпер.
static void eventHandlePressEdge(int i, unsigned long n) {
  EventRule &r = eventRules[i];
  if (r.condition == EV_HOLD_MS) return;
  if (r.condition == EV_PRESS_COUNT) {
    if (n - eventRuntime[i].lastPress > r.intervalMs) eventRuntime[i].count = 0;
    eventRuntime[i].count++;
    eventRuntime[i].lastPress = n;
    if (eventRuntime[i].count >= r.count) { eventFire(i); eventRuntime[i].count = 0; }
  } else {
    eventFire(i);
  }
}

// Обработка удержания: срабатывает один раз через intervalMs, сброс при отпускании.
static void eventHandleHold(int i, unsigned long n, bool active, unsigned long activeSince) {
  EventRule &r = eventRules[i];
  if (r.condition != EV_HOLD_MS) return;
  if (active) {
    if (!eventRuntime[i].holdFired && n - activeSince >= r.intervalMs) { eventRuntime[i].holdFired = true; eventFire(i); }
  } else {
    eventRuntime[i].holdFired = false;
  }
}

// Срабатывание всех включённых правил с данным триггером и условием EV_NONE.
static void eventFireTrigger(uint8_t trig) {
  for (int i = 0; i < EVENT_MAX_RULES; i++) {
    EventRule &r = eventRules[i];
    if (r.enabled && r.trigger == trig && r.condition == EV_NONE) eventFire(i);
  }
}

void updateEventEngine() {
  static bool lastBrake = false, booted = false, stateInitialized = false;
  static int lastPasLevel = 0;
  static bool lastPasEnabled = false, lastCruiseEnabled = false;
  static unsigned long down = 0;
  static const int btnPins[5] = { BTN_HEADLIGHT_PIN, BTN_TURN_LEFT_PIN, BTN_TURN_RIGHT_PIN, BTN_HORN_PIN, PAS_BUTTON_PIN };
  static int btnLast[5] = {HIGH, HIGH, HIGH, HIGH, HIGH}, btnStable[5] = {HIGH, HIGH, HIGH, HIGH, HIGH};
  static bool btnPrevActive[5] = {false, false, false, false, false};
  static unsigned long btnDeb[5] = {0, 0, 0, 0, 0};
  unsigned long n = millis();

  // Boot: однократное срабатывание при старте основного цикла
  if (!booted) { booted = true; eventFireTrigger(EV_BOOT); }

  // --- Тормоз ---
  bool brake = isBrakePressed();
  if (brake && !lastBrake) {
    down = n;
    for (int i = 0; i < EVENT_MAX_RULES; i++) {
      EventRule &r = eventRules[i];
      if (r.enabled && r.trigger == EV_BRAKE_PRESS) eventHandlePressEdge(i, n);
    }
  }
  if (!brake && lastBrake) eventFireTrigger(EV_BRAKE_RELEASE);
  for (int i = 0; i < EVENT_MAX_RULES; i++) {
    EventRule &r = eventRules[i];
    if (r.enabled && r.trigger == EV_BRAKE_HOLD) eventHandleHold(i, n, brake, down);
  }
  lastBrake = brake;

  // --- Физические кнопки (фара, поворотники, гудок, PAS) с дебаунсом ---
  for (int b = 0; b < 5; b++) {
    uint8_t trig = EV_BTN_HEADLIGHT + b;
    int reading = digitalRead(btnPins[b]);
    if (reading != btnLast[b]) btnDeb[b] = n;
    bool active = (btnStable[b] == LOW);
    if (n - btnDeb[b] > DEBOUNCE_MS && reading != btnStable[b]) {
      btnStable[b] = reading;
      active = (reading == LOW);
      if (active) { // фронт нажатия
        for (int i = 0; i < EVENT_MAX_RULES; i++) {
          EventRule &r = eventRules[i];
          if (r.enabled && r.trigger == trig) eventHandlePressEdge(i, n);
        }
      }
    }
    btnLast[b] = reading;
    // Кнопочный триггер означает именно фронт нажатия; отпускание не является
    // отдельным событием (в отличие от EV_BRAKE_RELEASE).
    for (int i = 0; i < EVENT_MAX_RULES; i++) {
      EventRule &r = eventRules[i];
      if (r.enabled && r.trigger == trig) eventHandleHold(i, n, active, btnDeb[b]);
    }
    btnPrevActive[b] = active;
  }

  // Не считать исходное состояние после запуска событием включения/выключения.
  if (!stateInitialized) {
    lastPasLevel = pasCurrentLevel;
    lastPasEnabled = pasEnabled;
    lastCruiseEnabled = cruiseEnabled;
    stateInitialized = true;
  }

  // --- PAS: уровень, включение, выключение ---
  if (pasCurrentLevel != lastPasLevel) {
    int lvl = pasCurrentLevel;
    lastPasLevel = lvl;
    for (int i = 0; i < EVENT_MAX_RULES; i++) {
      EventRule &r = eventRules[i];
      if (r.enabled && r.trigger == EV_PAS_LEVEL && (int)r.count == lvl && r.condition == EV_NONE) eventFire(i);
    }
  }
  if (pasEnabled && !lastPasEnabled) eventFireTrigger(EV_PAS_ON);
  if (!pasEnabled && lastPasEnabled) eventFireTrigger(EV_PAS_OFF);
  lastPasEnabled = pasEnabled;

  // --- Круиз: включение, выключение ---
  if (cruiseEnabled && !lastCruiseEnabled) eventFireTrigger(EV_CRUISE_ON);
  if (!cruiseEnabled && lastCruiseEnabled) eventFireTrigger(EV_CRUISE_OFF);
  lastCruiseEnabled = cruiseEnabled;
}

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
portMUX_TYPE debugBufferMux = portMUX_INITIALIZER_UNLOCKED;
const unsigned long DEBUG_SAMPLE_INTERVAL_MS = 100; // Sample every 100ms for oscilloscope

// ================= Пассивная запись неизвестной цифровой линии =================
// GPIO36 работает только как вход и не имеет внутренней подтяжки.
const int BUS_CAPTURE_PIN = 36;
const uint32_t BUS_CAPTURE_CAPACITY = 4096;
struct BusEdge { uint32_t tUs; uint8_t level; };
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

bool busCapturePinBusy(String *reason = nullptr) {
  for (int i = 0; i < PIN_ROLE_COUNT; i++) {
    if (pinField(pinConfig, i) == BUS_CAPTURE_PIN) {
      if (reason) *reason = "GPIO36 занят системной ролью «" + pinRoleName(i) + "»";
      return true;
    }
  }
  for (int i = 0; i < CUSTOM_PIN_MAX; i++) {
    if (customPins[i].used && customPins[i].gpio == BUS_CAPTURE_PIN) {
      if (reason) *reason = "GPIO36 занят дополнительной ролью «" + String(customPins[i].name) + "»";
      return true;
    }
  }
  return false;
}

void IRAM_ATTR onBusCaptureEdge() {
  if (!busCaptureRunning) return;
  uint32_t now = micros();
  uint8_t level = (uint8_t)gpio_get_level((gpio_num_t)BUS_CAPTURE_PIN);
  portENTER_CRITICAL_ISR(&busCaptureMux);
  // Снимок буфера временно сбрасывает running. Повторная проверка под тем же
  // mux не даёт уже ожидавшему ISR записать фронт во время копирования.
  if (!busCaptureRunning) {
    portEXIT_CRITICAL_ISR(&busCaptureMux);
    return;
  }
  uint32_t idx = busCaptureHead;
  busCapture[idx].tUs = now;
  busCapture[idx].level = level;
  busCaptureHead = (idx + 1) % BUS_CAPTURE_CAPACITY;
  if (busCaptureCount < BUS_CAPTURE_CAPACITY) busCaptureCount++;
  else busCaptureOverwritten++;
  busCaptureTotal++;
  busCaptureLastUs = now;
  portEXIT_CRITICAL_ISR(&busCaptureMux);
}

void clearBusCapture() {
  portENTER_CRITICAL(&busCaptureMux);
  busCaptureHead = 0;
  busCaptureCount = 0;
  busCaptureTotal = 0;
  busCaptureOverwritten = 0;
  busCaptureStartedUs = micros();
  busCaptureLastUs = busCaptureStartedUs;
  portEXIT_CRITICAL(&busCaptureMux);
}

uint32_t snapshotBusCapture(uint32_t &total, uint32_t &overwritten, uint32_t &startedUs, uint32_t &lastUs, bool &running) {
  // Краткая критическая секция: на время копирования отключаем только ISR сниффера,
  // чтобы не блокировать PAS и цикл управления глобальным запретом прерываний.
  portENTER_CRITICAL(&busCaptureMux);
  running = busCaptureRunning;
  busCaptureRunning = false;
  uint32_t count = busCaptureCount;
  uint32_t first = (busCaptureHead + BUS_CAPTURE_CAPACITY - count) % BUS_CAPTURE_CAPACITY;
  total = busCaptureTotal;
  overwritten = busCaptureOverwritten;
  startedUs = busCaptureStartedUs;
  lastUs = busCaptureLastUs;
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

// ================= Throttle-by-wire =================
void setThrottleOutputSafeZero() {
  dacWrite(THROTTLE_DAC_PIN, 0);
}

void updateThrottle() {
  // Во время заводского сброса фоновая задача не должна повторно поднять ЦАП
  // после того, как веб-обработчик принудительно установил безопасный ноль.
  if (factoryResetInProgress) {
    setThrottleOutputSafeZero();
    throttleSmoothOutV = 0;
    pasSmoothOutV = 0;
    cruiseSmoothOutV = 0;
    hwThrottleOutV = 0.0f;
    hwMotorOutPct = 0.0f;
    return;
  }

  // Физический тормоз всегда имеет приоритет над выходом газа.
  // ownBrakeCutoffEnabled относится только к дополнительной программной
  // функции; удержание throttleOutMinV никогда не должно работать при тормозе.
  bool brakePressed = isBrakePressed();

  int raw;
  // АЦП на ESP32 дорогой (~50-100 мкс на чтение): на 1 кГц это до ~10% CPU.
  // Децимируем выборку до 200 Гц (раз в 5 мс) — для ручки газа с рампами
  // 200-2000 мс задержка в 5 мс неощутима, а PAS/тормоз остаются на 1 кГц.
  static int cachedRaw = 0;
  static unsigned long lastAdcSampleMs = 0;
  unsigned long adcNowMs = millis();
  if (adcNowMs - lastAdcSampleMs >= 5) {
    cachedRaw = analogRead(THROTTLE_ADC_PIN);
    lastAdcSampleMs = adcNowMs;
  }
  raw = cachedRaw;
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

  // Конечный автомат круиз-контроля (State Machine). В сервисном режиме
  // круиз запрещён — FSM полностью замирает.
  if (cruiseEnabled && cruiseCurrentLevel > 0 && !serviceModeActive) {
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

  // Сервисный режим: жёсткий потолок мощности. Физический тормоз (ниже) и
  // ограничение ЦАП сильнее этого потолка — его обойти нельзя никак.
  if (serviceModeActive) {
    float svcOutMin = throttleOutMinV;
    float svcOutMax = (throttleOutMaxV > svcOutMin + 0.01f) ? throttleOutMaxV : (svcOutMin + 0.01f);
    float limitV = svcOutMin + (constrain(serviceThrottleLimitPct, 0, 100) / 100.0f) * (svcOutMax - svcOutMin);
    if (combinedV > limitV) combinedV = limitV;
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
:root{--ui-bg:#101214;--ui-card:#191c20;--ui-button:#252a30;--ui-border:#3b424a;--ui-hover:#30363d;--ui-active:#383f47;--ui-text:#eee;--ui-muted:#8b949e;--ui-focus:#aeb6bf;--ui-accent:#4a90d9;--ui-success:#2ecc71;--ui-warning:#f39c12;--ui-danger:#e74c3c;--ui-purple:#9b59b6;--ui-dark:#0a0f0d;--ui-black:#000;--ui-led:#ff8c00;--ui-led-glow:#ff7700;--ui-led-off:#1e140a;--ui-radius:8px;--ui-control-height:44px}
.u-muted{color:var(--ui-muted)}.u-small{font-size:12px}.u-dim{opacity:.7}.back-link,a.back{color:var(--ui-accent);text-decoration:none}.hint{color:var(--ui-muted);font-size:13px}.is-hidden{display:none!important}
.top-bar-sticky{position:sticky;top:0;left:0;right:0;z-index:9999;background:var(--ui-card);border-bottom:1px solid var(--ui-border);padding:8px 12px;margin:-20px -20px 15px -20px;font-size:12px;color:var(--ui-muted);display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;box-shadow:0 2px 8px rgba(0,0,0,.5)}
.tb-item{display:inline-flex;align-items:center;gap:4px;white-space:nowrap}
.tb-link{color:var(--ui-accent);text-decoration:none;padding:2px 6px;border-radius:6px;background:var(--ui-button);border:1px solid var(--ui-border);transition:background .2s,border-color .2s}
.tb-link:hover{background:var(--ui-hover);border-color:var(--ui-muted);color:var(--ui-text)}
.tb-dot{width:8px;height:8px;border-radius:50%;display:inline-block}
.dot-green{background:var(--ui-success);box-shadow:0 0 5px var(--ui-success)}
.dot-yellow{background:var(--ui-warning);box-shadow:0 0 5px var(--ui-warning)}
.dot-red{background:var(--ui-danger)}
.dot-gray{background:var(--ui-muted)}
)rawliteral");
}

String getSettingsCss() {
  return String(R"rawliteral(
:root{--ui-bg:#101214;--ui-card:#191c20;--ui-button:#252a30;--ui-border:#3b424a;--ui-hover:#30363d;--ui-active:#383f47;--ui-text:#eee;--ui-muted:#8b949e;--ui-focus:#aeb6bf;--ui-accent:#4a90d9;--ui-success:#2ecc71;--ui-warning:#f39c12;--ui-danger:#e74c3c;--ui-purple:#9b59b6;--ui-dark:#0a0f0d;--ui-black:#000;--ui-led:#ff8c00;--ui-led-glow:#ff7700;--ui-led-off:#1e140a;--ui-radius:8px;--ui-control-height:44px}
*{box-sizing:border-box}body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-size:16px;line-height:1.45;padding:20px;max-width:520px;margin:auto;background:var(--ui-bg);color:var(--ui-text)}h1{font-size:24px;line-height:1.2;margin:20px 0 16px}h2{font-size:18px;line-height:1.3}p{line-height:1.5}
label{display:block;margin-top:12px;line-height:1.35}input,select{width:100%;min-height:var(--ui-control-height);padding:9px 10px;background:var(--ui-button);color:var(--ui-text);border:1px solid var(--ui-border);border-radius:var(--ui-radius);font:inherit}input[type=checkbox],input[type=radio]{width:20px;min-height:20px;accent-color:var(--ui-accent)}input[type=range]{min-height:32px;padding:0;accent-color:var(--ui-accent)}
button{min-height:var(--ui-control-height);margin-top:15px;padding:9px 12px;width:100%;font:inherit;font-weight:600;border:1px solid var(--ui-border);border-radius:var(--ui-radius);background:var(--ui-button);color:var(--ui-text);cursor:pointer;transition:background .12s,border-color .12s,box-shadow .12s}
button:hover{background:var(--ui-hover)}button:active{background:var(--ui-active)}button:focus-visible,input:focus-visible,select:focus-visible,a:focus-visible{outline:2px solid var(--ui-focus);outline-offset:2px}button:disabled{opacity:.45;cursor:not-allowed}
.chk{display:flex;gap:10px;align-items:center;justify-content:space-between;margin-top:10px;min-height:44px}.chk input{flex:0 0 auto;margin:0;order:2}.chk label{margin-top:0;min-width:0;order:1}
.frow{display:flex;align-items:center;gap:10px;margin-top:10px;flex-wrap:nowrap;min-width:0}.frow>label{margin-top:0;flex:1 1 0;min-width:0;overflow-wrap:anywhere}.frow>input,.frow>select{width:170px;flex:0 0 170px;min-width:0;max-width:170px;margin:0}.frow>.fval{flex:0 0 auto;font-weight:600;text-align:right}.fhint{color:var(--ui-muted);font-size:12px;line-height:1.35;margin:3px 0 0;overflow-wrap:anywhere}.panel{background:var(--ui-card);border:1px solid var(--ui-border);border-radius:var(--ui-radius);padding:12px;margin-bottom:12px}
fieldset{border:1px solid var(--ui-border);border-radius:var(--ui-radius);margin-top:15px;padding:10px 12px;background:var(--ui-card)}legend{padding:0 6px;color:var(--ui-muted);font-size:13px}.levels{border-left:2px solid var(--ui-border);padding-left:10px;margin-top:10px}.hint{color:var(--ui-muted);font-size:13px}.warn{color:var(--ui-text);font-size:13px;margin-top:6px}.cal-btn{margin-top:4px}
.card{display:flex;align-items:center;min-height:var(--ui-control-height);background:var(--ui-button);color:var(--ui-text);padding:12px 15px;border:1px solid var(--ui-border);border-radius:var(--ui-radius);margin-bottom:10px;text-decoration:none;cursor:pointer;transition:background .12s,border-color .12s}.card:hover{background:var(--ui-hover)}.card:active{background:var(--ui-active)}
a.back{color:var(--ui-accent);text-decoration:none}@media(max-width:420px){body{padding:12px}.top-bar-sticky{margin-left:-12px;margin-right:-12px}h1{font-size:22px}.frow>input,.frow>select{width:128px;flex-basis:128px;max-width:128px}}
)rawliteral");
}

String getSettingsJs() {
  return String(R"rawliteral(
<script>
function renderLevelFields(containerId, inputName, values, count, label, max) {
  const div=document.getElementById(containerId); if(!div) return;
  const safeCount=Math.max(0, Math.min(Number(count)||0, Number(max)||100));
  div.replaceChildren();
  for(let i=0;i<safeCount;i++) {
    const row=document.createElement('div'); row.className='frow';
    const text=document.createElement('label'); text.textContent=label+' '+(i+1);
    const input=document.createElement('input'); input.type='number'; input.className='lvl-input'; input.name=inputName+i;
    input.min='0'; input.max='100'; input.step='1'; input.value=Math.round(values[i] ?? 0);
    row.append(text,input); div.append(row);
  }
}
function distributeLevels(values,count,start=0,end=100) {
  count=Math.max(0,Number(count)||0); if(!count) return;
  for(let i=0;i<count;i++) values[i]=Math.round(count===1?end:start+i*(end-start)/(count-1));
}
</script>
)rawliteral");
}

String getTopBarHtml() {
  return String(R"rawliteral(
<div class="top-bar-sticky">
  <div class="tb-item" title="Нагрузка процессора ESP32">
    <span>CPU:</span> <b id="tbCpu">0%</b>
  </div>
  <div class="tb-item" title="Оперативная память (занято / свободно)">
    <span>RAM:</span> <b id="tbRam">0%</b> <span id="tbRamKb" class="u-muted u-small">(0k)</span>
  </div>
  <div class="tb-item" title="Flash память (прошивка / всего)">
    <span>ROM:</span> <span id="tbRom" class="u-muted">0k</span>
  </div>
  <a href="/wifi" class="tb-item tb-link" title="Настройки Wi-Fi">
    <span>WiFi:</span>
    <span class="tb-dot dot-gray" id="tbWifiDot"></span>
    <span id="tbWifiTxt">...</span>
  </a>
  <div class="tb-item" title="Температура процессора">
    <span>Temp:</span> <b id="tbTemp">--°C</b>
  </div>
  <div class="tb-item u-dim" title="Bluetooth (не используется)">
    <span>BT:</span>
    <span class="tb-dot dot-gray"></span>
    <span class="u-muted">Выкл</span>
  </div>
</div>
)rawliteral");
}

String getTopBarJs() {
  return String(R"rawliteral(
<script>
let statusErrCount = 0;
let statusBusy = false;
function updateSysStatus(){
  if (localStorage.getItem('ui_show_topbar') === 'false') {
    const tb = document.querySelector('.top-bar-sticky');
    if (tb) tb.style.display = 'none';
  } else {
    const tb = document.querySelector('.top-bar-sticky');
    if (tb) tb.style.display = 'flex';
  }

  if (statusBusy) return; // не наслаиваем запросы поверх незавершённых — иначе "connection reset by peer"
  statusBusy = true;
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
    if (statusErrCount >= 5) {
      location.reload();
    }
  }).finally(()=>{statusBusy=false;});
}
setInterval(updateSysStatus, 2000);
updateSysStatus();
</script>
)rawliteral");
}

// ================= Веб: отладочный график =================
void handleDebugPage() {
  // Страница хранится целиком во flash и отправляется с точным Content-Length.
  // Это не требует большой String в heap и не зависит от chunked-передачи.
  static const char debugPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Отладка</title>
<style>
:root{--ui-bg:#101214;--ui-card:#191c20;--ui-button:#252a30;--ui-border:#3b424a;--ui-hover:#30363d;--ui-active:#383f47;--ui-text:#eee;--ui-muted:#8b949e;--ui-focus:#aeb6bf;--ui-accent:#4a90d9;--ui-success:#2ecc71;--ui-warning:#f39c12;--ui-danger:#e74c3c;--ui-purple:#9b59b6;--ui-black:#000;--ui-radius:8px;--ui-control-height:44px}
*{box-sizing:border-box}body{background:var(--ui-bg);color:var(--ui-text);font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-size:16px;line-height:1.45;padding:20px;max-width:760px;margin:auto}h1{font-size:24px;line-height:1.2}h2{font-size:18px;line-height:1.3}
canvas{background:var(--ui-black);border:1px solid var(--ui-border);border-radius:var(--ui-radius);width:100%;max-width:700px;display:block}
.debug-tools{margin-bottom:10px;display:flex;align-items:center;flex-wrap:wrap;gap:8px}.tool-label{font-size:16px}.osc-toggle{margin-left:10px;font-weight:600;font-size:16px;min-height:44px;display:flex;align-items:center;gap:8px}.osc-toggle input{width:20px;height:20px;accent-color:var(--ui-accent)}.tbtn{display:inline-flex;align-items:center;justify-content:center;min-height:var(--ui-control-height);background:var(--ui-button);color:var(--ui-text);border:1px solid var(--ui-border);padding:9px 12px;border-radius:var(--ui-radius);cursor:pointer;margin:0;font:inherit;font-weight:600;transition:background .12s,border-color .12s}.tbtn.selected{background:var(--ui-accent);border-color:var(--ui-accent)}
.tbtn:hover{background:var(--ui-hover)}.tbtn:active{background:var(--ui-active)}.tbtn:focus-visible,a:focus-visible{outline:2px solid var(--ui-focus);outline-offset:2px}
.legend{display:flex;gap:15px;flex-wrap:wrap;margin:10px 0;font-size:13px}
.legend span{display:inline-flex;align-items:center;gap:5px}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block}.dot-in{background:var(--ui-accent)}.dot-out{background:var(--ui-danger)}.dot-brake{background:var(--ui-warning)}.dot-pas{background:var(--ui-success)}.dot-btn{background:var(--ui-purple)}
#vals{font-size:14px;margin-top:10px;line-height:1.6}
.bus-card{max-width:700px;margin-top:22px;padding:14px;border:1px solid var(--ui-border);border-radius:8px;background:var(--ui-card)}.bus-card h2{margin-top:0}.bus-actions{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0}.bus-link{text-decoration:none;display:inline-block}#busStatus{font-size:13px;line-height:1.5;margin:8px 0}#busChart{height:220px}
a{color:var(--ui-accent)}
</style></head><body>
<p><a href="/">&larr; Настройки</a></p>
<h1>Отладка (мини-осциллограф)</h1>
<div class="debug-tools">
  <span class="tool-label">Масштаб времени:</span>
  <button type="button" class="tbtn" onclick="setTimeScale(1)" id="tb1">1с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(3)" id="tb3">3с</button>
  <button type="button" class="tbtn selected" onclick="setTimeScale(5)" id="tb5">5с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(10)" id="tb10">10с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(30)" id="tb30">30с</button>
  <label class="osc-toggle">
    <input type="checkbox" id="chkOscilloscope" onchange="updateOscilloscopeState()"> Запускать осциллограф
  </label>
</div>
<canvas id="chart" width="700" height="320"></canvas>
<div class="legend">
<span><span class="dot dot-in"></span>Газ вход, В</span>
<span><span class="dot dot-out"></span>Газ выход, В</span>
<span><span class="dot dot-brake"></span>Тормоз</span>
<span><span class="dot dot-pas"></span>PAS активен</span>
<span><span class="dot dot-btn"></span>Кнопка PAS</span>
</div>
<div id="vals">Осциллограф отключен. Включите галочку для запуска.</div>
<section class="bus-card">
  <h2>Пассивный сниффер цифровой линии</h2>
  <p>Вход <b>GPIO36</b> (только чтение, без внутренней подтяжки). Подключайте только через согласование уровня до 3,3 В и общий GND.</p>
  <div class="bus-actions">
    <button type="button" class="tbtn" onclick="busCommand(&#39;start&#39;)">Старт</button>
    <button type="button" class="tbtn" onclick="busCommand(&#39;stop&#39;)">Стоп</button>
    <button type="button" class="tbtn" onclick="busCommand(&#39;clear&#39;)">Очистить</button>
    <a class="tbtn bus-link" href="/debug/bus/csv">Скачать CSV</a>
  </div>
  <div id="busStatus">Получение состояния...</div>
  <canvas id="busChart" width="700" height="220"></canvas>
</section>


<script>
const canvas = document.getElementById('chart');
const ctx = canvas ? canvas.getContext('2d') : null;
const W = canvas ? canvas.width : 0, H = canvas ? canvas.height : 0;
const VMAX = 5.0; // шкала по напряжению, В
let currentTimeScaleSec = 5;
let isOscRunning = false;

function setTimeScale(sec) {
  currentTimeScaleSec = sec;
  [1, 3, 5, 10, 30].forEach(s => {
    const el = document.getElementById('tb' + s);
    if (el) {
      el.classList.toggle('selected', s === sec);
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
    if (ctx) ctx.clearRect(0,0,W,H);
  }
}

function draw(rawHistory) {
  if (!ctx) return;
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
const busCanvas = document.getElementById("busChart");
const busCtx = busCanvas ? busCanvas.getContext("2d") : null;
let busBusy = false;

async function busCommand(cmd) {
  try {
    const r = await fetch("/debug/bus/control?cmd=" + encodeURIComponent(cmd), {method:"POST"});
    if (!r.ok) throw new Error(await r.text());
    await refreshBus();
  } catch (e) { alert("Сниффер: " + e.message); }
}

function drawBus(edges) {
  if (!busCanvas || !busCtx) return;
  const w=busCanvas.width,h=busCanvas.height;
  busCtx.clearRect(0,0,w,h); busCtx.fillStyle="#050505"; busCtx.fillRect(0,0,w,h);
  if (!edges || edges.length < 2) return;
  const first=edges[0][0], last=edges[edges.length-1][0], span=Math.max(1,last-first);

  // Если на один пиксель приходится несколько фронтов, отдельные ступеньки
  // уже неразличимы. Показываем плотность переходов вместо ложного муара.
  if (edges.length > w*2) {
    const density=new Uint16Array(w);
    let maxDensity=1;
    for(let i=1;i<edges.length;i++) {
      const x=Math.min(w-1,Math.max(0,Math.floor((edges[i][0]-first)*w/span)));
      density[x]++;
      if(density[x]>maxDensity) maxDensity=density[x];
    }
    for(let x=0;x<w;x++) {
      if(!density[x]) continue;
      const intensity=Math.sqrt(density[x]/maxDensity);
      busCtx.fillStyle='rgba(46,204,113,'+(0.18+0.82*intensity).toFixed(3)+')';
      busCtx.fillRect(x,20,1,h-40);
    }
    return;
  }

  busCtx.strokeStyle="#2ecc71"; busCtx.lineWidth=1.5; busCtx.beginPath();
  let py=edges[0][1]?35:h-35; busCtx.moveTo(0,py);
  for (let i=1;i<edges.length;i++) {
    const x=(edges[i][0]-first)*w/span, y=edges[i][1]?35:h-35;
    busCtx.lineTo(x,py); busCtx.lineTo(x,y); py=y;
  }
  busCtx.lineTo(w,py); busCtx.stroke();
}

async function fetchJson(url) {
  const response=await fetch(url);
  if (!response.ok) throw new Error("HTTP "+response.status+": "+await response.text());
  return response.json();
}

async function refreshBus() {
  if (busBusy) return; busBusy=true;
  try {
    const status=await fetchJson("/debug/bus/status");
    let text=(status.running?"Запись идёт":"Остановлено")+" · фронтов в памяти: "+status.count+"/"+status.capacity+" · всего: "+status.total;
    if(status.overwritten) text+=" · перезаписано: "+status.overwritten;
    if(status.busy) text+=" · НЕДОСТУПНО: "+status.reason;
    const statusElement=document.getElementById("busStatus");
    if (statusElement) statusElement.innerText=text;
    if(status.count) {
      const data=await fetchJson("/debug/bus/data");
      drawBus(data.edges);
    } else drawBus([]);
  } catch(e) {
    const statusElement=document.getElementById("busStatus");
    if (statusElement) statusElement.innerText="Ошибка: "+e.message;
  } finally { busBusy=false; }
}
setInterval(refreshBus, 1000);
refreshBus();

</script>
</body></html>
)rawliteral";
  server.send_P(200, PSTR("text/html; charset=utf-8"), debugPage, sizeof(debugPage) - 1);
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
  if (command == "start") {
    String reason;
    if (busCapturePinBusy(&reason)) { server.send(409, "text/plain", reason); return; }
    if (!busCaptureRunning) {
      pinMode(BUS_CAPTURE_PIN, INPUT);
      clearBusCapture();
      attachInterrupt(digitalPinToInterrupt(BUS_CAPTURE_PIN), onBusCaptureEdge, CHANGE);
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
    detachInterrupt(digitalPinToInterrupt(BUS_CAPTURE_PIN));
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
  bool busy = busCapturePinBusy(&reason);
  uint32_t durationUs = total ? (lastUs - startedUs) : 0;
  String json = "{\"running\":" + String(running ? "true" : "false") +
                ",\"pin\":" + String(BUS_CAPTURE_PIN) +
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

// ================= Веб: WiFi и Точка Доступа =================
void handleWifiPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Связь и сеть</title>
<style>
)rawliteral" + getTopBarCss() + getSettingsCss() + R"rawliteral(
h2{font-size:15px;margin-top:0;color:var(--ui-muted);border-bottom:1px solid var(--ui-border);padding-bottom:8px}
.net{padding:10px;background:var(--ui-card);border-radius:6px;margin-top:6px;cursor:pointer;display:flex;justify-content:space-between;border:1px solid var(--ui-border)}
.net:active{background:var(--ui-hover)}
.net .rssi{color:var(--ui-muted);font-size:12px}
.status{color:var(--ui-muted);font-size:13px;margin:8px 0;line-height:1.4}
.hint{color:var(--ui-muted);font-size:12px;margin-top:-6px;margin-bottom:10px;display:block}
.back-link{color:var(--ui-muted);text-decoration:none}.back-link:hover{color:var(--ui-text)}
.message-success{color:var(--ui-success)}.message-error{color:var(--ui-danger)}.form-gap{margin-top:15px}.block-gap{margin-top:10px}.status-gap{margin-top:8px}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a class="back-link" href="/">&larr; Меню</a></p>
<h1>Связь и сеть</h1>

<div class="panel">
  <h2>Подключение к Wi-Fi (Клиент)</h2>
  <div class="status" id="wifi_status">Текущий статус: )rawliteral";
  html += (WiFi.status() == WL_CONNECTED) ? ("<b>Подключено к " + WiFi.SSID() + "</b> (IP: " + WiFi.localIP().toString() + ")") : "<i>Не подключено к внешней сети</i>";
  html += R"rawliteral(</div>

  <button type="button" onclick="scan()">Найти доступные сети</button>
  <div id="nets" class="block-gap"></div>

  <form id="f_wifi" class="form-gap">
    <div class="frow"><label for="ssid">SSID</label><input type="text" id="ssid" name="ssid" placeholder="Сеть или вручную"></div>
    <div class="frow"><label for="pass">Пароль</label><input type="password" id="pass" name="pass" placeholder="Пароль Wi-Fi"></div>
    <button type="submit">Подключиться к Wi-Fi</button>
  </form>
</div>

<div class="panel">
  <h2>Настройки точки доступа (AP)</h2>
  <div class="status">
    Режим точки доступа: <b>активен</b><br>
    IP-адрес точки: <b>)rawliteral";
  html += WiFi.softAPIP().toString();
  html += R"rawliteral(</b>
  </div>

  <form id="f_ap" class="form-gap">
    <div class="frow"><label for="ap_ssid">SSID точки</label><input type="text" id="ap_ssid" name="ap_ssid" value=")rawliteral";
  html += htmlEscape(storedApSsid);
  html += R"rawliteral("></div>

    <div class="frow"><label for="ap_pass">Пароль точки</label><input type="text" id="ap_pass" name="ap_pass" value=")rawliteral";
  html += htmlEscape(storedApPass);
  html += R"rawliteral(" placeholder="пусто = открытая сеть"></div>
    <p class="fhint">Пароль отображается открыто. Оставьте пустым для открытой точки (без пароля). Для WPA2 нужно минимум 8 символов.</p>

    <button type="submit">Сохранить настройки точки доступа</button>
  </form>
  <div id="ap_status" class="status status-gap"></div>
</div>

<script>
function scan() {
  document.getElementById('nets').innerHTML = '<i>Поиск сетей...</i>';
  fetch('/wifi/scan').then(r=>r.json()).then(list=>{
    if (!list || !list.length) { document.getElementById('nets').innerHTML = '<i>Сети не найдены</i>'; return; }
    document.getElementById('nets').innerHTML = list.map(n =>
      '<div class="net" onclick="pick(\''+n.ssid.replace(/'/g,"")+'\')"><span>'+n.ssid+'</span><span class="rssi">'+n.rssi+' dBm</span></div>'
    ).join('');
  }).catch(() => {
    document.getElementById('nets').innerHTML = '<span class="message-error">Ошибка сканирования</span>';
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
  fetch('/wifi/ap/save', {method:'POST', body:d}).then(async r=>{
    const txt = await r.text();
    st.innerHTML = r.ok
      ? '<b class="message-success">Настройки точки доступа сохранены и применены!</b>'
      : '<span class="message-error">Ошибка: ' + txt + '</span>';
  }).catch(e=>{
    st.innerHTML = '<span class="message-error">Ошибка: ' + e.message + '</span>';
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

bool validApCredentials(const String &ssidValue, const String &passValue, String &error) {
  if (ssidValue.length() < 1 || ssidValue.length() > 32) {
    error = "SSID точки доступа должен содержать от 1 до 32 байт";
    return false;
  }
  if (passValue.length() != 0 && (passValue.length() < 8 || passValue.length() > 63)) {
    error = "Пароль точки доступа должен быть пустым либо содержать от 8 до 63 байт";
    return false;
  }
  return true;
}

bool startConfiguredAp() {
  return storedApPass.length() == 0
    ? WiFi.softAP(storedApSsid.c_str())
    : WiFi.softAP(storedApSsid.c_str(), storedApPass.c_str());
}

void handleApSave() {
  String newSsid = server.hasArg("ap_ssid") ? server.arg("ap_ssid") : storedApSsid;
  String newPass = server.hasArg("ap_pass") ? server.arg("ap_pass") : storedApPass;
  String error;
  if (!validApCredentials(newSsid, newPass, error)) {
    server.send(400, "text/plain", error);
    return;
  }
  storedApSsid = newSsid;
  storedApPass = newPass;
  apSettingsSave();
  WiFi.softAPdisconnect(true);
  if (!startConfiguredAp()) {
    server.send(500, "text/plain", "Не удалось запустить точку доступа");
    return;
  }
  server.send(200, "text/plain", "OK");
}

// ================= Веб: хаб =================
void handleHub() {
  // Главное меню компактно и также хранится во flash, не занимая heap большим String.
  static const char hubPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenBike Controller v0.3.1</title>
<style>
:root{--ui-bg:#101214;--ui-card:#191c20;--ui-button:#252a30;--ui-border:#3b424a;--ui-hover:#30363d;--ui-active:#383f47;--ui-text:#eee;--ui-muted:#8b949e;--ui-focus:#aeb6bf;--ui-accent:#4a90d9;--ui-success:#2ecc71;--ui-warning:#f39c12;--ui-danger:#e74c3c;--ui-radius:8px;--ui-control-height:44px}
*{box-sizing:border-box}body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-size:16px;line-height:1.45;padding:20px;padding-top:max(20px,env(safe-area-inset-top));max-width:560px;margin:auto;background:var(--ui-bg);color:var(--ui-text)}
.top-bar-sticky{position:sticky;top:0;top:env(safe-area-inset-top);z-index:9999;background:var(--ui-card);border-bottom:1px solid var(--ui-border);padding:8px 12px;margin:-20px -20px 15px;font-size:12px;color:var(--ui-muted);display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;box-shadow:0 2px 8px rgba(0,0,0,.5)}
.tb-item{display:inline-flex;align-items:center;gap:4px;white-space:nowrap}.tb-link{color:var(--ui-accent);text-decoration:none;padding:2px 6px;border-radius:6px;background:var(--ui-button);border:1px solid var(--ui-border)}.tb-link:hover{background:var(--ui-hover);color:var(--ui-text)}.tb-dot{width:8px;height:8px;border-radius:50%;display:inline-block}.dot-green{background:var(--ui-success);box-shadow:0 0 5px var(--ui-success)}.dot-yellow{background:var(--ui-warning);box-shadow:0 0 5px var(--ui-warning)}.dot-red{background:var(--ui-danger)}.dot-gray{background:var(--ui-muted)}
.header{text-align:center;margin-bottom:18px}.header h1{margin:0;font-size:24px;line-height:1.2}.version,.u-muted{color:var(--ui-muted)}.u-small{font-size:12px}.u-dim{opacity:.7}
a.card{display:flex;align-items:center;justify-content:center;min-height:var(--ui-control-height);background:var(--ui-button);color:var(--ui-text);padding:12px 15px;border:1px solid var(--ui-border);border-radius:var(--ui-radius);margin-bottom:10px;text-decoration:none;text-align:center;font-weight:600;transition:background .12s,border-color .12s}
a.card:hover{background:var(--ui-hover)}a.card:active{background:var(--ui-active)}a.primary{background:var(--ui-accent);border-color:var(--ui-accent);color:#fff}a:focus-visible{outline:2px solid var(--ui-focus);outline-offset:2px}
@media(max-width:420px){body{padding-left:12px;padding-right:12px}.top-bar-sticky{margin-left:-12px;margin-right:-12px;justify-content:flex-start}}
</style></head><body>
<div class="top-bar-sticky">
  <div class="tb-item" title="Нагрузка процессора ESP32"><span>CPU:</span> <b id="tbCpu">0%</b></div>
  <div class="tb-item" title="Оперативная память"><span>RAM:</span> <b id="tbRam">0%</b> <span id="tbRamKb" class="u-muted u-small">(0k)</span></div>
  <div class="tb-item" title="Flash-память"><span>ROM:</span> <span id="tbRom" class="u-muted">0k</span></div>
  <a href="/wifi" class="tb-item tb-link" title="Настройки Wi-Fi"><span>WiFi:</span> <span class="tb-dot dot-gray" id="tbWifiDot"></span> <span id="tbWifiTxt">...</span></a>
  <div class="tb-item" title="Температура процессора"><span>Temp:</span> <b id="tbTemp">--°C</b></div>
  <div class="tb-item u-dim" title="Bluetooth не используется"><span>BT:</span> <span class="tb-dot dot-gray"></span> <span>Выкл</span></div>
</div>
<div class="header"><h1>OpenBike Controller</h1><div class="version">v0.3.1</div></div>
<a class="card primary" href="/emulation">Эмуляция дисплея и пульта &rarr;</a>
<a class="card" href="/settings/throttle">Газ &rarr;</a>
<a class="card" href="/settings/pas">PAS &rarr;</a>
<a class="card" href="/settings/cruise">Круиз &rarr;</a>
<a class="card" href="/settings/pins">GPIO &rarr;</a>
<a class="card" href="/settings/events">События &rarr;</a>
<a class="card" href="/wifi">Сеть &rarr;</a>
<a class="card" href="/debug">Отладка &rarr;</a>
<a class="card" href="/system">Система &rarr;</a>
<script>
let statusBusy=false;
function updateSysStatus(){
  if(statusBusy)return;
  statusBusy=true;
  fetch('/status/sys',{cache:'no-store'}).then(r=>r.json()).then(d=>{
    const set=(id,value)=>{const el=document.getElementById(id);if(el)el.textContent=value};
    set('tbCpu',(d.cpu||0)+'%');
    set('tbRam',(d.ram_pct||0)+'%');
    set('tbRamKb','('+(d.ram_free_kb||0)+'k)');
    set('tbRom',(d.rom_sketch_kb||0)+'k/'+(d.rom_total_kb||0)+'k');
    set('tbTemp',d.temp!==undefined?d.temp+'°C':'--°C');
    const dot=document.getElementById('tbWifiDot'),txt=document.getElementById('tbWifiTxt');
    if(dot&&txt){dot.className='tb-dot '+(d.wifi_mode==='STA'?'dot-green':d.wifi_mode==='AP'?'dot-yellow':'dot-red');txt.textContent=d.wifi_mode==='STA'?(d.wifi_ssid||'WiFi'):d.wifi_mode==='AP'?'AP: '+(d.wifi_ssid||'Bike'):'Подключение...'}
  }).catch(()=>{const dot=document.getElementById('tbWifiDot');if(dot)dot.className='tb-dot dot-red'}).finally(()=>{statusBusy=false});
}
setInterval(updateSysStatus,2000);updateSysStatus();
</script>
</body></html>)rawliteral";
  server.send_P(200, PSTR("text/html; charset=utf-8"), hubPage, sizeof(hubPage) - 1);
}


void handleEmulationPage() {
  // Страница эмуляции большая, поэтому держим её во flash и отправляем напрямую.
  // Начальные значения синхронизируются с контроллером через /status/sys.
  static const char emulationPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenBike Controller v0.3.1</title>
<style>

:root{--ui-bg:#101214;--ui-card:#191c20;--ui-button:#252a30;--ui-border:#3b424a;--ui-hover:#30363d;--ui-active:#383f47;--ui-text:#eee;--ui-muted:#8b949e;--ui-focus:#aeb6bf;--ui-accent:#4a90d9;--ui-success:#2ecc71;--ui-warning:#f39c12;--ui-danger:#e74c3c;--ui-purple:#9b59b6;--ui-dark:#0a0f0d;--ui-black:#000;--ui-led:#ff8c00;--ui-led-glow:#ff7700;--ui-led-off:#1e140a;--ui-radius:8px;--ui-control-height:44px}
.u-muted{color:var(--ui-muted)}.u-small{font-size:12px}.u-dim{opacity:.7}.back-link,a.back{color:var(--ui-accent);text-decoration:none}.hint{color:var(--ui-muted);font-size:13px}.is-hidden{display:none!important}
.top-bar-sticky{position:sticky;top:0;left:0;right:0;z-index:9999;background:var(--ui-card);border-bottom:1px solid var(--ui-border);padding:8px 12px;margin:-20px -20px 15px -20px;font-size:12px;color:var(--ui-muted);display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;box-shadow:0 2px 8px rgba(0,0,0,.5)}
.tb-item{display:inline-flex;align-items:center;gap:4px;white-space:nowrap}
.tb-link{color:var(--ui-accent);text-decoration:none;padding:2px 6px;border-radius:6px;background:var(--ui-button);border:1px solid var(--ui-border);transition:background .2s,border-color .2s}
.tb-link:hover{background:var(--ui-hover);border-color:var(--ui-muted);color:var(--ui-text)}
.tb-dot{width:8px;height:8px;border-radius:50%;display:inline-block}
.dot-green{background:var(--ui-success);box-shadow:0 0 5px var(--ui-success)}
.dot-yellow{background:var(--ui-warning);box-shadow:0 0 5px var(--ui-warning)}
.dot-red{background:var(--ui-danger)}
.dot-gray{background:var(--ui-muted)}

body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-size:16px;line-height:1.45;padding:20px;max-width:560px;margin:auto;background:var(--ui-bg);color:var(--ui-text)}
.header{text-align:center;margin-bottom:16px}.header h1{margin:0;font-size:24px;line-height:1.2}.header .version{color:var(--ui-muted);font-size:14px}
a.card{display:block;background:var(--ui-button);color:var(--ui-text);padding:15px;border:1px solid var(--ui-border);border-radius:var(--ui-radius);margin-bottom:10px;text-decoration:none;transition:background .2s,border-color .2s}
a.card:hover{background:var(--ui-hover)}a.card:active{background:var(--ui-active)}
.warn{background:var(--ui-card);border:1px solid var(--ui-danger);color:var(--ui-text);padding:12px;border-radius:var(--ui-radius);margin-bottom:16px;font-weight:bold;font-size:13px}

/* LED Matrix Simulator */
.matrix-card{background:var(--ui-card);border:1px solid var(--ui-border);border-radius:12px;padding:10px;margin-bottom:12px;text-align:center;box-shadow:0 4px 12px rgba(0,0,0,.4);transition:all .2s}
.matrix-title{font-size:11px;font-weight:bold;color:var(--ui-muted);letter-spacing:1px;text-transform:uppercase;margin-bottom:8px}
.sim-matrix-area{display:flex;justify-content:center;align-items:center;margin-bottom:8px}
#ledMatrixCanvas{background:var(--ui-black);border:2px solid var(--ui-border);border-radius:6px;box-shadow:inset 0 0 8px rgba(0,0,0,.7);display:block;max-width:100%;height:auto}

/* Компактная панель эмуляции */
.simulator-layout{display:grid;grid-template-columns:minmax(0,1fr) 64px;gap:8px;align-items:stretch;margin-bottom:12px}
.sim-left-col{min-width:0;display:flex;flex-direction:column;gap:8px}
.sim-section-title{font-size:10px;font-weight:900;letter-spacing:1px;text-transform:uppercase;color:var(--ui-muted);margin-bottom:2px}
.sim-btns-group{display:flex;flex-direction:row;gap:6px;width:100%}
.sim-side-btn{flex:1;display:flex;align-items:center;justify-content:center;gap:6px;padding:10px 6px;font-size:13px;font-weight:bold;border-radius:8px;border:1px solid var(--ui-border);background:var(--ui-button);color:var(--ui-text);cursor:pointer;user-select:none;-webkit-user-select:none;touch-action:manipulation;transition:background .1s,border-color .1s}
.sim-btn-ico{font-size:18px;line-height:1}.sim-side-btn:hover{background:var(--ui-hover)}.sim-side-btn:active{background:var(--ui-active)}
.sim-side-btn:focus-visible,.dpad-btn:focus-visible{outline:2px solid var(--ui-focus);outline-offset:2px}
.sim-side-btn.active{background:var(--ui-card);border-color:var(--ui-danger);color:var(--ui-text)}
.sim-side-btn.pedal-btn.active{background:var(--ui-card);border-color:var(--ui-success);color:var(--ui-text)}

.sim-throttle-group{flex:1 1 auto;min-width:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;background:var(--ui-card);border:1px solid var(--ui-border);border-radius:10px;padding:8px 6px}
.sim-throttle-label{font-size:10px;font-weight:900;color:var(--ui-muted);text-transform:uppercase;letter-spacing:1px}
.sim-slider-vert{writing-mode:vertical-lr;direction:rtl;-webkit-appearance:slider-vertical;appearance:slider-vertical;width:24px;height:126px;cursor:pointer;accent-color:var(--ui-accent)}
.sim-throttle-val{font-size:12px;font-weight:bold;color:var(--ui-accent);min-width:36px;text-align:center}

/* D-Pad Джойстик */
.joystick-panel{align-self:center;background:var(--ui-card);border:1px solid var(--ui-border);border-radius:12px;padding:7px;width:fit-content;max-width:100%;min-width:0;box-sizing:border-box;margin-bottom:0;box-shadow:0 4px 12px rgba(0,0,0,.35);display:flex;flex-direction:column;justify-content:flex-start}.joystick-panel .sim-section-title,.joystick-panel .joy-screen{width:193px;max-width:100%;box-sizing:border-box}
.joy-screen{background:var(--ui-dark);border:1px solid var(--ui-border);border-radius:10px;padding:8px 10px;margin-bottom:10px;text-align:center;font-family:monospace}
.screen-mode{font-size:12px;font-weight:bold;letter-spacing:1px;color:var(--ui-muted);text-transform:uppercase}.screen-mode.mode-pas{color:var(--ui-success)}.screen-mode.mode-cruise{color:var(--ui-accent)}.screen-mode.mode-off{color:var(--ui-danger)}
.screen-val{font-size:34px;font-weight:900;color:var(--ui-text);margin:4px 0}.screen-status{font-size:10px;color:var(--ui-muted);font-weight:bold;text-transform:uppercase}.screen-status.dirty{color:var(--ui-warning);animation:blink 1s infinite}
@keyframes blink{50%{opacity:0.4}}

.dpad-container{display:grid;grid-template-columns:repeat(3,60px);grid-template-rows:repeat(3,50px);gap:6px;justify-content:center;margin:2px auto 0}
.dpad-btn{background:var(--ui-button);color:var(--ui-text);border:1px solid var(--ui-border);border-bottom-width:2px;border-radius:10px;display:flex;align-items:center;justify-content:center;font-size:19px;font-weight:900;cursor:pointer;user-select:none;-webkit-user-select:none;touch-action:manipulation;transition:background .08s,transform .08s}
.dpad-btn:hover{background:var(--ui-hover)}.dpad-btn:active{transform:translateY(2px);border-bottom-width:2px;background:var(--ui-active)}
.btn-up{grid-column:2;grid-row:1}
.btn-left{grid-column:1;grid-row:2}
.btn-ok{grid-column:2;grid-row:2;background:var(--ui-card);border-color:var(--ui-success);border-bottom-color:var(--ui-success);color:var(--ui-success);font-size:19px}
.btn-ok:hover{background:var(--ui-hover)}.btn-ok:active{background:var(--ui-active)}
.btn-ok.dirty-pulse{background:var(--ui-card);border-color:var(--ui-warning);border-bottom-color:var(--ui-warning);color:var(--ui-warning);animation:pulse 1s infinite}
@keyframes pulse{50%{box-shadow:0 0 14px rgba(243,156,18,0.7)}}
.btn-right{grid-column:3;grid-row:2}
.btn-down{grid-column:2;grid-row:3}
.joy-legend{display:none}
@media(max-width:520px){body{padding:12px}.top-bar-sticky{margin:-12px -12px 12px}.sim-side-btn{padding:8px 5px}.matrix-card{padding:8px}.screen-val{font-size:23px}}
@media(max-width:360px){.simulator-layout{grid-template-columns:minmax(0,1fr) 54px}.joystick-panel .sim-section-title,.joystick-panel .joy-screen{width:158px}.dpad-container{grid-template-columns:repeat(3,49px);grid-template-rows:repeat(3,43px);gap:5px}}
</style>
</head><body>

<div class="top-bar-sticky">
  <div class="tb-item" title="Нагрузка процессора ESP32">
    <span>CPU:</span> <b id="tbCpu">0%</b>
  </div>
  <div class="tb-item" title="Оперативная память (занято / свободно)">
    <span>RAM:</span> <b id="tbRam">0%</b> <span id="tbRamKb" class="u-muted u-small">(0k)</span>
  </div>
  <div class="tb-item" title="Flash память (прошивка / всего)">
    <span>ROM:</span> <span id="tbRom" class="u-muted">0k</span>
  </div>
  <a href="/wifi" class="tb-item tb-link" title="Настройки Wi-Fi">
    <span>WiFi:</span>
    <span class="tb-dot dot-gray" id="tbWifiDot"></span>
    <span id="tbWifiTxt">...</span>
  </a>
  <div class="tb-item" title="Температура процессора">
    <span>Temp:</span> <b id="tbTemp">--°C</b>
  </div>
  <div class="tb-item u-dim" title="Bluetooth (не используется)">
    <span>BT:</span>
    <span class="tb-dot dot-gray"></span>
    <span class="u-muted">Выкл</span>
  </div>
</div>

<div class="header">
  <p><a href="/" class="back-link">&larr; Главное меню</a></p>
  <h1>Эмуляция управления</h1>
  <div class="version">v0.3.1 &bull; 16&times;32 LED Matrix</div>
</div>

<div class="matrix-card" id="simMatrixCard">
  <div class="matrix-title">Эмулятор дисплея 16&times;32</div>
  <div class="sim-matrix-area">
    <canvas id="ledMatrixCanvas" width="320" height="160"></canvas>
  </div>
  <div class="simulator-layout">
    <div class="sim-left-col">
      <div class="joystick-panel" id="joystickPanel">
        <div class="sim-section-title">Джойстик</div>
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
      </div>
      <div class="sim-btns-group" id="simControlsPanel">
        <button type="button" class="sim-side-btn" id="btnSimBrake">
          <span class="sim-btn-ico">[ • ]</span><span>Тормоз</span>
        </button>
        <button type="button" class="sim-side-btn pedal-btn" id="btnSimPedal">
          <span class="sim-btn-ico">&#129461;</span><span>Педали</span>
        </button>
      </div>
    </div>
    <div class="sim-throttle-group">
      <span class="sim-throttle-label">Газ</span>
      <input type="range" min="0" max="100" value="0" orient="vertical" class="sim-slider-vert" id="simGas">
      <span class="sim-throttle-val" id="lblGas">0%</span>
    </div>
  </div>
</div>

<script>
let statusErrCount = 0;
let statusBusy = false;
function updateSysStatus(){
  if (localStorage.getItem('ui_show_topbar') === 'false') {
    const tb = document.querySelector('.top-bar-sticky');
    if (tb) tb.style.display = 'none';
  } else {
    const tb = document.querySelector('.top-bar-sticky');
    if (tb) tb.style.display = 'flex';
  }

  if (statusBusy) return; // не наслаиваем запросы поверх незавершённых — иначе "connection reset by peer"
  statusBusy = true;
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
    if (statusErrCount >= 5) {
      location.reload();
    }
  }).finally(()=>{statusBusy=false;});
}
setInterval(updateSysStatus, 2000);
updateSysStatus();
</script>

<script>
let activeMode = "off";
let activePasLvl = 0;
let activeCruiseLvl = 0;
const pasMax = 5;
const cruiseMax = 5;

const cfgThrottleInMin = 1.10;
const cfgThrottleInMax = 4.10;
const cfgThrottleOutMin = 1.10;
const cfgThrottleOutMax = 4.10;

const cfgCruiseConfirmThrottle = false;
const cfgCruiseAfterBraking = 1;
const cfgCruiseAfterThrottle = 2;

let draftMode = (activeMode === "off") ? "pas" : activeMode;
let draftPasLvl = activePasLvl;
let draftCruiseLvl = activeCruiseLvl;
let draftSettingIdx = 0;
let draftSettingEditing = false;
let isDirty = false;
let userInteractingUntil = 0;
let applyInProgressUntil = 0;
let applyInProgress = false;
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

// Центральная зона: P/C — единая сетка 6x11 и равномерный штрих 2 пикселя.
const BIG_GLYPHS = {
  'P': [
    [0,1,1,1,1,0],
    [1,1,1,1,1,1],
    [1,1,0,0,1,1],
    [1,1,0,0,1,1],
    [1,1,1,1,1,1],
    [1,1,1,1,1,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0]
  ],
  'C': [
    [0,1,1,1,1,0],
    [1,1,1,1,1,1],
    [1,1,0,0,1,1],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,1,1],
    [1,1,1,1,1,1],
    [0,1,1,1,1,0]
  ]
};

// Компактные цифры 3x5: три столбца, пять строк.
const DIGIT_GLYPHS = {
  '0': [0x1F, 0x11, 0x1F],
  '1': [0x00, 0x1F, 0x00],
  '2': [0x1D, 0x15, 0x17],
  '3': [0x15, 0x15, 0x1F],
  '4': [0x07, 0x04, 0x1F],
  '5': [0x17, 0x15, 0x1D],
  '6': [0x1F, 0x15, 0x1D],
  '7': [0x01, 0x19, 0x07],
  '8': [0x1F, 0x15, 0x1F],
  '9': [0x17, 0x15, 0x1F]
};

const ARROW_UP = [
  [0,0,1,0,0],
  [0,1,1,1,0]
];
const ARROW_DOWN = [
  [0,1,1,1,0],
  [0,0,1,0,0]
];

const ICON_GEAR_7X7 = [
  [0,1,0,1,0,1,0],
  [1,1,1,1,1,1,1],
  [0,1,0,0,0,1,0],
  [1,1,0,0,0,1,1],
  [0,1,0,0,0,1,0],
  [1,1,1,1,1,1,1],
  [0,1,0,1,0,1,0]
];

const ICON_CHECK_5X5 = [
  0b00000,
  0b00001,
  0b00010,
  0b10100,
  0b01000
];

// 3x5 font bit patterns (columns 0..2)
const FONT_3X5 = {
  'A': [0x1E, 0x05, 0x1E],
  'B': [0x1F, 0x15, 0x0A],
  'C': [0x0E, 0x11, 0x11],
  'D': [0x1F, 0x11, 0x0E],
  'E': [0x1F, 0x15, 0x11],
  'F': [0x1F, 0x05, 0x01],
  'G': [0x0E, 0x11, 0x1D],
  'H': [0x1F, 0x04, 0x1F],
  'I': [0x11, 0x1F, 0x11],
  'J': [0x08, 0x10, 0x0F],
  'K': [0x1F, 0x04, 0x1B],
  'L': [0x1F, 0x10, 0x10],
  'M': [0x1F, 0x02, 0x1F],
  'N': [0x1F, 0x06, 0x1F],
  'O': [0x0E, 0x11, 0x0E],
  'P': [0x1F, 0x05, 0x02],
  'Q': [0x0E, 0x11, 0x1E],
  'R': [0x1F, 0x05, 0x1A],
  'S': [0x12, 0x15, 0x09],
  'T': [0x01, 0x1F, 0x01],
  'U': [0x0F, 0x10, 0x0F],
  'V': [0x07, 0x18, 0x07],
  'W': [0x1F, 0x08, 0x1F],
  'X': [0x1B, 0x04, 0x1B],
  'Y': [0x03, 0x1C, 0x03],
  'Z': [0x19, 0x15, 0x13],
  '0': [0x1F, 0x11, 0x1F],
  '1': [0x00, 0x1F, 0x00],
  '2': [0x1D, 0x15, 0x17],
  '3': [0x15, 0x15, 0x1F],
  '4': [0x07, 0x04, 0x1F],
  '5': [0x17, 0x15, 0x1D],
  '6': [0x1F, 0x15, 0x1D],
  '7': [0x01, 0x19, 0x07],
  '8': [0x1F, 0x15, 0x1F],
  '9': [0x17, 0x15, 0x1F],
  '.': [0x00, 0x10, 0x00],
  ':': [0x00, 0x0A, 0x00],
  '-': [0x04, 0x04, 0x04],
  '_': [0x10, 0x10, 0x10],
  '/': [0x18, 0x06, 0x01],
  ' ': [0x00, 0x00, 0x00]
};

function drawChar3x5(ch, sr, sc, clipMinR = 0, clipMaxR = 15, clipMinC = 0, clipMaxC = 31) {
  const g = FONT_3X5[ch.toUpperCase()] || FONT_3X5[' '];
  for (let c = 0; c < 3; c++) {
    const colBits = g[c];
    for (let r = 0; r < 5; r++) {
      if ((colBits >> r) & 1) {
        const tr = sr + r;
        const tc = sc + c;
        if (tr >= clipMinR && tr <= clipMaxR && tc >= clipMinC && tc <= clipMaxC) {
          setMatrixPixel(tr, tc, 1);
        }
      }
    }
  }
}

function drawText3x5(str, sr, sc, clipMinR = 0, clipMaxR = 15, clipMinC = 0, clipMaxC = 31) {
  let currC = sc;
  for (let i = 0; i < str.length; i++) {
    const ch = str[i];
    if (ch === '.') {
      // Точка прилегает к предыдущей цифре и к следующей цифре.
      drawChar3x5('.', sr, currC - 2, clipMinR, clipMaxR, clipMinC, clipMaxC);
    } else {
      drawChar3x5(ch, sr, currC, clipMinR, clipMaxR, clipMinC, clipMaxC);
      currC += 4;
    }
  }
}

const SETTINGS_MENU = [
  { id: 'in_min', name: 'THROTTLE IN MIN', unit: 'V', step: 0.05, min: 0.0, max: 4.5, val: cfgThrottleInMin },
  { id: 'in_max', name: 'THROTTLE IN MAX', unit: 'V', step: 0.05, min: 0.5, max: 5.0, val: cfgThrottleInMax },
  { id: 'out_min', name: 'THROTTLE OUT MIN', unit: 'V', step: 0.05, min: 0.0, max: 4.5, val: cfgThrottleOutMin },
  { id: 'out_max', name: 'THROTTLE OUT MAX', unit: 'V', step: 0.05, min: 0.5, max: 5.0, val: cfgThrottleOutMax }
];

// Тормоз: квадратные скобки с точкой внутри.
const ICON_BRAKE = [0b10001, 0b10001, 0b10101, 0b10001, 0b10001];
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
  for (let r = 0; r < g.length; r++) {
    for (let c = 0; c < g[r].length; c++) {
      if (g[r][c]) setMatrixPixel(sr + r, sc + c, 1);
    }
  }
}
function drawDigit3x5ToBuffer(dChar, buf) {
  const g = DIGIT_GLYPHS[dChar];
  if (!g) return;
  for (let c = 0; c < 3; c++) {
    for (let r = 0; r < 5; r++) {
      if ((g[c] >> r) & 1) buf[r][c] = 1;
    }
  }
}

function drawArrow5x2(arrowMatrix, sr, sc, hollowCenter = false) {
  for (let r = 0; r < arrowMatrix.length; r++) {
    for (let c = 0; c < arrowMatrix[r].length; c++) {
      if (hollowCenter && r === 0 && c === 2) continue;
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
function drawGear7x7(sr, sc) {
  for (let r = 0; r < 7; r++) {
    for (let c = 0; c < 7; c++) {
      if (ICON_GEAR_7X7[r][c]) setMatrixPixel(sr + r, sc + c, 1);
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

  if (draftMode === "settings") {
    // Gear icon top-left rows 1..7, cols 1..7
    drawGear7x7(1, 1);

    // Fixed checkmark on the right: rows 10..14, cols 24..28
    if (!draftSettingEditing) {
      drawIcon5x5(ICON_CHECK_5X5, 10, 24);
    } else if (blinkOn) {
      drawIcon5x5(ICON_CHECK_5X5, 10, 24);
    }

    // Current setting item
    const curItem = SETTINGS_MENU[draftSettingIdx];
    const fullText = curItem.name; // e.g. "THROTTLE IN MIN"
    const words = fullText.split(' ');
    let lines = [];
    let curLine = "";
    for (let w of words) {
      if (!curLine) {
        curLine = w;
      } else if ((curLine + " " + w).length <= 5) {
        curLine += " " + w;
      } else {
        lines.push(curLine);
        curLine = w;
      }
    }
    if (curLine) lines.push(curLine);

    // Scrolling logic for setting title (clipped in rows 0..9, cols 10..29)
    // Display window fits 2 lines at height 5 with 1px gap: line 0 at r=0, line 1 at r=5 (0..9)
    const lineHeight = 5;
    const lineSpacing = 1;
    const totalLinePitch = lineHeight + lineSpacing; // 6px
    const maxScroll = Math.max(0, (lines.length - 2) * totalLinePitch);

    let scrollY = 0;
    if (maxScroll > 0) {
      const scrollCycleMs = 3000;
      const t = (now % scrollCycleMs) / scrollCycleMs;
      if (t < 0.35) {
        scrollY = 0;
      } else if (t < 0.5) {
        scrollY = ((t - 0.35) / 0.15) * maxScroll;
      } else if (t < 0.85) {
        scrollY = maxScroll;
      } else {
        scrollY = maxScroll * (1 - (t - 0.85) / 0.15);
      }
    }

    for (let i = 0; i < lines.length; i++) {
      const y = Math.round(i * totalLinePitch - scrollY);
      if (y + 5 >= 0 && y <= 9) {
        drawText3x5(lines[i], y, 10, 0, 9, 10, 29);
      }
    }

    // Value area at bottom: rows 11..15, cols 2..22
    const valStr = curItem.val.toFixed(2) + curItem.unit;
    if (!draftSettingEditing || blinkOn) {
      drawText3x5(valStr, 11, 2, 10, 15, 0, 23);
    }
  } else {
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

    // 1. Draw Big Letter P or C (6x11, rows 0..10, cols 0..5) - blink when draft mode differs from active mode
    let modeSwitchPending = isDirty && (draftMode !== activeMode);
    if (!modeSwitchPending || blinkOn) {
      if (modeToDraw === "pas") {
        drawBigLetter('P', 0, 0);
      } else if (modeToDraw === "cruise") {
        drawBigLetter('C', 0, 0);
      }
    }

    // 2. Стрелки центрированы относительно блока цифр 3x5 (две цифры: cols 7..13, одна: cols 8..12).
    let isTwoDigits = (lvlToDraw >= 10);
    let arrowCol = isTwoDigits ? 8 : 7;
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
      drawArrow5x2(ARROW_UP, 0 + arrowUpVOffset, arrowCol);
    }
    // Нижняя стрелка показывается всегда, но при уровне 0 — без центрального пикселя.
    drawArrow5x2(ARROW_DOWN, 9 + arrowDownVOffset, arrowCol, lvlToDraw === 0);

    // 3. Elevator Animation inside digit window: rows 3..7 (height 5px), strictly clipped
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
        let startC = twoD ? 7 : 8;

        let buf1 = Array.from({length: 5}, () => new Uint8Array(3));
        let buf2 = Array.from({length: 5}, () => new Uint8Array(3));

        if (twoD) {
          drawDigit3x5ToBuffer(tD.toString(), buf1);
          drawDigit3x5ToBuffer(oD.toString(), buf2);
        } else {
          drawDigit3x5ToBuffer(oD.toString(), buf1);
        }

        for (let r = 0; r < 5; r++) {
          let targetRow = Math.round(3 + r + rowOffset);
          if (targetRow >= 3 && targetRow <= 7) {
            for (let c = 0; c < 3; c++) {
              if (buf1[r][c]) setMatrixPixel(targetRow, startC + c, 1);
              if (twoD && buf2[r][c]) setMatrixPixel(targetRow, startC + 4 + c, 1);
            }
          }
        }
      };

      if (animProgress < 1 && fromLvl !== toLvl) {
        renderLevelToMatrix(Math.round(fromLvl), Math.round(-dir * animProgress * 5));
        renderLevelToMatrix(Math.round(toLvl), Math.round(dir * (1 - animProgress) * 5));
      } else {
        renderLevelToMatrix(lvlToDraw, 0);
      }
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
  for (let r = 0; r < inGasLeds; r++) {
    const row = 15 - r;
    if (row < 13 || row > 15) setMatrixPixel(row, 30, 1);
  }

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
  for (let r = 0; r < outGasLeds; r++) {
    const row = 15 - r;
    if (row < 13 || row > 15) setMatrixPixel(row, 31, 1);
  }

  // Bottom-right 5x5 Indicator area: cols 24..28, rows 11..15
  if (effectiveBrake) {
    const brakeBlink = Math.floor(now / 90) % 2 === 0;
    if (brakeBlink) drawIcon5x5(ICON_BRAKE, 11, 24);
  } else if (effectivePedal) {
    let frame = Math.floor((now - simPedalStartMs) / 75) % 8;
    drawIcon5x5(ICON_PEDAL_FRAMES[frame], 11, 24);
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

// Виртуальные входы эмуляции
let simGasPct = 0; // Значение виртуальной ручки газа, 0-100%
let simBrakeActive = false; // Virtual brake input (momentary, active only while held)
let simPedalActive = false; // Virtual pedal button state
let simPedalStartMs = 0;
const sliderGas = document.getElementById("simGas");
const lblGas = document.getElementById("lblGas");

// Слайдер газа — это именно виртуальный вход эмулятора, а не индикатор
// физической ручки. Телеметрия hardware обновляется отдельно ниже.
if (sliderGas) {
  sliderGas.addEventListener("input", (e) => {
    simGasPct = Math.max(0, Math.min(100, Number(e.target.value) || 0));
    if (lblGas) lblGas.innerText = simGasPct + "%";
    userInteractingUntil = Date.now() + 1000;
    renderJoystick();
  });
}

// Real-time data from hardware used by the virtual display
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

// Обновление результирующих состояний по входам эмулятора и оборудования
function updateEffectiveStates() {
  effectiveBrake = simBrakeActive || hwBrakeActive;
  effectivePedal = simPedalActive || hwPasActive;
}

function refreshHubData(forceSync = false) {
  if (!forceSync && (isDirty || applyInProgress || Date.now() < applyInProgressUntil)) return;
  fetch("/status/sys")
    .then(r => r.json())
    .then(d => {
      // Игнорируем устаревший ответ, если OK был нажат после отправки запроса.
      if (applyInProgress && !forceSync) return;

      // Update hardware states used by the virtual display
      hwBrakeActive = d.brake || false;
      hwPasActive = d.pas_active || false; // Use 'pas_active' field

      // Для эмулятора мы не перезаписываем слайдер (simGasPct) данными физической
      // ручки, чтобы виртуальный интерфейс работал независимо (анимация и логика
      // UI продолжают реагировать на simGasPct).
      // Hardware-телеметрия только обновляет флаги, не трогая simGasPct.

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

// Update effective states whenever sim states change.
// Тормоз — momentary: активен только пока кнопка удерживается.
const btnSimBrakeEl = document.getElementById("btnSimBrake");
function setSimBrake(active) {
  if (simBrakeActive === active) return;
  simBrakeActive = active;
  updateEffectiveStates();
  renderJoystick();
}
if (btnSimBrakeEl) {
  btnSimBrakeEl.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    if (btnSimBrakeEl.setPointerCapture) btnSimBrakeEl.setPointerCapture(e.pointerId);
    setSimBrake(true);
  });
  btnSimBrakeEl.addEventListener("pointerup", (e) => {
    setSimBrake(false);
    if (btnSimBrakeEl.releasePointerCapture && btnSimBrakeEl.hasPointerCapture(e.pointerId)) btnSimBrakeEl.releasePointerCapture(e.pointerId);
  });
  btnSimBrakeEl.addEventListener("pointerleave", () => {});
  btnSimBrakeEl.addEventListener("lostpointercapture", () => setSimBrake(false));
  btnSimBrakeEl.addEventListener("pointercancel", () => setSimBrake(false));
  btnSimBrakeEl.addEventListener("contextmenu", (e) => e.preventDefault());
}

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
  } else if (draftMode === "settings") {
    const cur = SETTINGS_MENU[draftSettingIdx];
    modeEl.innerText = "НАСТРОЙКИ: " + cur.name;
    if (draftSettingEditing) {
      valEl.innerText = "РЕДАКТИРОВАНИЕ: " + cur.val.toFixed(2) + cur.unit;
    } else {
      valEl.innerText = "ЗНАЧЕНИЕ: " + cur.val.toFixed(2) + cur.unit + " (OK - правка)";
    }
  }

  let modified = false;
  if (draftMode === "settings") {
    modified = draftSettingEditing;
  } else if (activeMode === "off") {
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
  if (draftMode === "settings") {
    if (draftSettingEditing) {
      statusEl.innerText = "НАЖМИТЕ OK ДЛЯ СОХРАНЕНИЯ ЗНАЧЕНИЯ";
      statusEl.className = "screen-status dirty";
      okBtn.className = "dpad-btn btn-ok dirty-pulse";
    } else {
      statusEl.innerText = "UP/DOWN: ПУНКТ, OK: ИЗМЕНИТЬ";
      statusEl.className = "screen-status";
      okBtn.className = "dpad-btn btn-ok";
    }
  } else if (isDirty) {
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

function toggleMode(dir = 1) {
  vib();
  userInteractingUntil = Date.now() + 4000;
  const modes = ["pas", "cruise", "settings"];
  let curIdx = modes.indexOf(draftMode);
  if (curIdx === -1) curIdx = 0;
  if (dir > 0) {
    curIdx = (curIdx + 1) % modes.length;
  } else {
    curIdx = (curIdx - 1 + modes.length) % modes.length;
  }
  draftMode = modes[curIdx];
  draftSettingEditing = false;
  renderJoystick();
}

document.getElementById("btnLeft").addEventListener("click", () => toggleMode(-1));
document.getElementById("btnRight").addEventListener("click", () => toggleMode(1));

document.getElementById("btnUp").addEventListener("click", () => {
  vib();
  userInteractingUntil = Date.now() + 4000;
  if (draftMode === "settings") {
    const cur = SETTINGS_MENU[draftSettingIdx];
    if (draftSettingEditing) {
      cur.val = Math.min(cur.max, +(cur.val + cur.step).toFixed(2));
    } else {
      if (draftSettingIdx > 0) draftSettingIdx--;
      else draftSettingIdx = SETTINGS_MENU.length - 1;
    }
  } else {
    let curLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
    let maxLvl = (draftMode === "pas") ? pasMax : cruiseMax;
    if (curLvl < maxLvl) {
      if (draftMode === "pas") draftPasLvl++;
      else if (draftMode === "cruise") draftCruiseLvl++;
      arrowBounceUntil = Date.now() + 500;
      arrowBounceDir = 1;
    }
  }
  renderJoystick();
});

document.getElementById("btnDown").addEventListener("click", () => {
  vib();
  userInteractingUntil = Date.now() + 4000;
  if (draftMode === "settings") {
    const cur = SETTINGS_MENU[draftSettingIdx];
    if (draftSettingEditing) {
      cur.val = Math.max(cur.min, +(cur.val - cur.step).toFixed(2));
    } else {
      if (draftSettingIdx < SETTINGS_MENU.length - 1) draftSettingIdx++;
      else draftSettingIdx = 0;
    }
  } else {
    let curLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
    if (curLvl > 0) {
      if (draftMode === "pas") draftPasLvl--;
      else if (draftMode === "cruise") draftCruiseLvl--;
      arrowBounceUntil = Date.now() + 500;
      arrowBounceDir = -1;
    }
  }
  renderJoystick();
});

function applyJoystickDraft(event) {
  if (event) {
    event.preventDefault();
    event.stopPropagation();
  }
  if (applyInProgress) return;

  vib();
  if (navigator.vibrate) navigator.vibrate([40, 30, 40]);
  if (draftMode === "settings") {
    draftSettingEditing = !draftSettingEditing;
    renderJoystick();
    return;
  }

  let targetMode = draftMode;
  let targetLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
  let requestMode = targetLvl === 0 ? "off" : targetMode;
  const previousActiveMode = activeMode;
  const previousActivePasLvl = activePasLvl;
  const previousActiveCruiseLvl = activeCruiseLvl;

  applyInProgress = true;
  applyInProgressUntil = Date.now() + 2000;

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
  isDirty = false;
  renderJoystick();

  fetch("/api/joystick/apply?mode=" + encodeURIComponent(requestMode) + "&level=" + targetLvl)
    .then(res => {
      if (!res.ok) throw new Error("HTTP " + res.status);
      return new Promise(resolve => setTimeout(resolve, 300));
    })
    .then(() => {
      applyInProgress = false;
      applyInProgressUntil = 0;
      refreshHubData(true);
    })
    .catch(err => {
      console.warn("Apply mode error:", err);
      applyInProgress = false;
      applyInProgressUntil = 0;
      activeMode = previousActiveMode;
      activePasLvl = previousActivePasLvl;
      activeCruiseLvl = previousActiveCruiseLvl;
      draftMode = targetMode;
      draftPasLvl = (targetMode === "pas") ? targetLvl : 0;
      draftCruiseLvl = (targetMode === "cruise") ? targetLvl : 0;
      renderJoystick();
      const statusEl = document.getElementById("joyStatus");
      if (statusEl) {
        statusEl.innerText = "ОШИБКА ПРИМЕНЕНИЯ — НАЖМИТЕ OK ЕЩЁ РАЗ: " + err.message;
        statusEl.className = "screen-status dirty";
      }
    });
}

document.getElementById("btnOk").addEventListener("click", applyJoystickDraft, false);

// Remove the duplicate function definition

document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible") {
    if (!applyInProgress) {
      applyInProgressUntil = 0;
      refreshHubData(true);
    }
    if (typeof updateSysStatus === "function") updateSysStatus();
  }
});

window.addEventListener("focus", () => {
  if (!applyInProgress) {
    applyInProgressUntil = 0;
    refreshHubData(true);
  }
  if (typeof updateSysStatus === "function") updateSysStatus();
});

renderJoystick();
</script>
</body>
</html>)rawliteral";
  server.send_P(200, PSTR("text/html; charset=utf-8"), emulationPage, sizeof(emulationPage) - 1);
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
)rawliteral" + getTopBarCss() + getSettingsCss() + R"rawliteral(
#liveV{font-size:18px;font-weight:bold;color:var(--ui-text);display:inline-block;padding:4px 8px;background:var(--ui-button);border-radius:4px;border:1px solid var(--ui-border)}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a href="/" style="color:var(--ui-accent)">&larr; Настройки</a></p>
<h1>Газ</h1>
<form id="f">
<fieldset><legend>Калибровка (в реальных вольтах на проводах)</legend>
<p class="fhint">Напряжение измеряется на проводах ручки газа и входа контроллера. Делитель и усилитель уже учтены.</p>
<div class="frow"><label>Текущее напряжение</label><span class="fval" id="liveV">-- В</span></div>
<p class="fhint">Автокалибровка выходного порога старта по датчику скорости запланирована на будущее.</p>

<div class="frow"><label for="inMinV">Вход минимум, В</label><input type="number" step="0.01" min="0" max="5" id="inMinV" name="inMinV" value=")rawliteral"; html += String(throttleInMinV, 2);
  html += R"rawliteral("></div>
<p class="fhint">Напряжение ручки газа в покое.</p>
<button type="button" class="cal-btn" onclick="calMin()">Захватить минимум (ручка отпущена)</button>

<div class="frow"><label for="inMaxV">Вход максимум, В</label><input type="number" step="0.01" min="0" max="5" id="inMaxV" name="inMaxV" value=")rawliteral"; html += String(throttleInMaxV, 2);
  html += R"rawliteral("></div>
<p class="fhint">Напряжение ручки на полном газу.</p>
<button type="button" class="cal-btn" onclick="calMax()">Захватить максимум (полный газ)</button>

<div class="frow"><label>Выход минимум, В</label><input type="number" step="0.05" min="0" max="5" name="outMinV" value=")rawliteral"; html += String(throttleOutMinV, 2);
  html += R"rawliteral("></div>
<p class="fhint">Холостой уровень на входе мотор-контроллера.</p>
<div class="frow"><label>Выход максимум, В</label><input type="number" step="0.05" min="0" max="5" name="outMaxV" value=")rawliteral"; html += String(throttleOutMaxV, 2);
  html += R"rawliteral("></div>
<p class="fhint">Уровень полного газа на входе мотор-контроллера.</p>
</fieldset>
<fieldset><legend>Согласующие цепи (подстроить под фактические резисторы)</legend>
<div class="frow"><label>Коэффициент делителя</label><input type="number" step="0.001" min="0.1" max="1" name="divRatio" value=")rawliteral"; html += String(throttleInputDividerRatio, 3);
  html += R"rawliteral("></div>
<p class="fhint">R2/(R1+R2). Для R1=10 кОм и R2=24 кОм: примерно 0,706.</p>
<div class="frow"><label>Коэффициент усиления ОУ</label><input type="number" step="0.01" min="1" max="3" name="gain" value=")rawliteral"; html += String(throttleOutputGain, 2);
  html += R"rawliteral("></div>
<p class="fhint">1 + R4/R3. Для R3=10 кОм и R4=2,7 кОм: 1,27.</p>
<p class="warn">Выше 3.3В на самом ЦАП ESP32 не поднимется — это аппаратный предел чипа. ОУ после ЦАП компенсирует это усилением, но выше напряжения питания ОУ (обычно 5В) выход тоже не поднимется физически.</p>
</fieldset>
<fieldset><legend>Мягкий старт</legend>
<div class="chk"><label for="throttleSsEn">Мягкий старт</label><input type="checkbox" id="throttleSsEn" name="ssEn" )rawliteral"; html += throttleSoftStartEnabled?"checked":"";
  html += R"rawliteral(></div>
<div class="frow"><label>Время разгона, мс</label><input type="number" name="ssMs" value=")rawliteral"; html += String(throttleSoftStartMs);
  html += R"rawliteral("></div>
<input type="hidden" name="spEn" value=")rawliteral"; html += throttleSoftStopEnabled ? "1" : "0";
  html += R"rawliteral(">
<input type="hidden" name="spMs" value=")rawliteral"; html += String(throttleSoftStopMs);
  html += R"rawliteral(">
</fieldset>
<button type="submit">Сохранить</button>
</form>
<script>
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  const d=new FormData(this);
  fetch('/settings/throttle/save',{method:'POST',body:d}).then(()=>alert('Сохранено'));
});
let pollVBusy=false;
function pollV(){
  if(pollVBusy) return; // не наслаиваем запросы
  pollVBusy=true;
  fetch('/status/sys').then(r=>r.json()).then(d=>{
    if(d.gas_in_v!==undefined) document.getElementById('liveV').textContent = d.gas_in_v.toFixed(2)+' В';
  }).catch(()=>{}).finally(()=>{pollVBusy=false;});
}
setInterval(pollV, 500);
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
  throttleSoftStopEnabled = server.arg("spEn").toInt() != 0; // скрытое поле: значение сохраняем, UI скрыт
  throttleSoftStartMs = server.arg("ssMs").toInt();
  throttleSoftStopMs = server.arg("spMs").toInt();
  ownBrakeCutoffEnabled = true; // дублирование тормоза всегда включено (опция скрыта в UI)
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
)rawliteral" + getTopBarCss() + getSettingsCss() + R"rawliteral(
.cal-status{margin-top:8px;font-weight:bold}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a class="back" href="/">&larr; Меню</a></p>
<h1>PAS</h1>
<form id="f">
<fieldset><legend>Ассистент PAS</legend>
<div class="chk"><label for="pasEn">Включить PAS</label><input type="checkbox" id="pasEn" name="en" )rawliteral"; html += pasEnabled?"checked":"";
  html += R"rawliteral(></div>
</fieldset>
<fieldset><legend>Датчик</legend>
<div class="frow"><label>Магниты</label><input type="number" name="magnets" value=")rawliteral"; html += String(pasMagnetCount);
  html += R"rawliteral("></div>
<p class="fhint">Количество магнитов на диске PAS.</p>
<div class="frow"><label>Фронт сигнала</label>
<select name="edge">
<option value="2")rawliteral"; html += (pasEdgeMode==FALLING?" selected":"");
  html += R"rawliteral(>При уходе магнита (FALLING)</option>
<option value="3")rawliteral"; html += (pasEdgeMode==RISING?" selected":"");
  html += R"rawliteral(>При появлении магнита (RISING)</option>
<option value="1")rawliteral"; html += (pasEdgeMode==CHANGE?" selected":"");
  html += R"rawliteral(>При любом изменении (CHANGE)</option>
</select></div>
<p class="fhint">Событие датчика, которое считается импульсом.</p>
<div class="frow"><label>Угол активации</label>
<select name="angle">
<option value="90")rawliteral"; html += (pasActivationAngle==90?" selected":"");
  html += R"rawliteral(>90&deg;</option>
<option value="180")rawliteral"; html += (pasActivationAngle==180?" selected":"");
  html += R"rawliteral(>180&deg;</option>
<option value="270")rawliteral"; html += (pasActivationAngle==270?" selected":"");
  html += R"rawliteral(>270&deg;</option>
<option value="360")rawliteral"; html += (pasActivationAngle==360?" selected":"");
  html += R"rawliteral(>360&deg;</option>
</select></div>
<p class="fhint">Поворот педалей до включения тяги.</p>
<div class="frow"><label>Память импульса, мс</label><input type="number" name="timeout" value=")rawliteral"; html += String(pasTimeoutMs);
  html += R"rawliteral("></div>
<p class="fhint">Как долго счётчик помнит вращение при медленном педалировании.</p>
<div class="frow"><label>Остановка, мс</label><input type="number" name="stopTO" value=")rawliteral"; html += String(pasStopTimeoutMs);
  html += R"rawliteral("></div>
<p class="fhint">Как быстро отключить тягу после остановки педалей.</p>
</fieldset>

<fieldset><legend>Калибровка магнитов</legend>
<p class="hint">Нажми «Старт», проверни педали ровно на 2 полных оборота, затем нажми «Готово» (или подожди 3 с после остановки — калибровка завершится сама). Количество магнитов будет посчитано и сохранено.</p>
<button type="button" onclick="calStart()">Старт</button>
<button type="button" onclick="calStop()">Готово</button>
<div id="calStat" class="cal-status">—</div>
</fieldset>

<fieldset><legend>Уровни усилия (0-)rawliteral"; html += String(PAS_MAX_LEVELS); html += R"rawliteral()</legend>
<div class="frow"><label>Количество уровней</label><input type="number" id="count" name="count" min="0" max=")rawliteral"; html += String(PAS_MAX_LEVELS);
  html += R"rawliteral(" value=")rawliteral"; html += String(pasLevelsCount);
  html += R"rawliteral(" oninput="renderLevels()"></div>
<div id="levels"></div>
<button type="button" onclick="autoDistribute()">Автораспределение</button>
</fieldset>

<fieldset><legend>Мягкий старт</legend>
<div class="chk"><label for="pasSsEn">Мягкий старт</label><input type="checkbox" id="pasSsEn" name="ssEn" )rawliteral"; html += pasSoftStartEnabled?"checked":"";
  html += R"rawliteral(></div>
<div class="frow"><label>Время разгона, мс</label><input type="number" name="ssMs" value=")rawliteral"; html += String(pasSoftStartMs);
  html += R"rawliteral("></div>
<input type="hidden" name="spEn" value=")rawliteral"; html += pasSoftStopEnabled ? "1" : "0";
  html += R"rawliteral(">
<input type="hidden" name="spMs" value=")rawliteral"; html += String(pasSoftStopMs);
  html += R"rawliteral(">
</fieldset>

<button type="submit">Сохранить</button>
</form>
)rawliteral" + getSettingsJs() + R"rawliteral(<script>
const saved = [)rawliteral";
  for (int i = 0; i < PAS_MAX_LEVELS; i++) { html += String(pasLevelPercent[i]); if (i<PAS_MAX_LEVELS-1) html += ","; }
  html += R"rawliteral(];
function renderLevels(){
  renderLevelFields('levels','lvl',saved,document.getElementById('count').value,'Уровень — усилие (%)',100);
}
function autoDistribute(){
  const count=parseInt(document.getElementById('count').value)||0;
  distributeLevels(saved,count); renderLevels();
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
  pasSoftStopEnabled = server.arg("spEn").toInt() != 0; // скрытое поле: значение сохраняем, UI скрыт
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
  portENTER_CRITICAL(&pasPulseMux);
  pasCalRunning = false;
  unsigned long pulses = pasCalPulses;
  portEXIT_CRITICAL(&pasPulseMux);
  int magnets = (int)(pulses / 2);
  if (magnets < 1) magnets = 1;
  pasMagnetCount = magnets;
  pasSettingsSave();
  String json = "{\"finished\":true,\"pulses\":" + String(pulses) +
                ",\"magnets\":" + String(magnets) + "}";
  return json;
}

void handlePasCalStart() {
  portENTER_CRITICAL(&pasPulseMux);
  pasCalPulses = 0;
  pasCalLastPulseMicros = 0;
  pasCalRunning = true;
  portEXIT_CRITICAL(&pasPulseMux);
  pasCalStartMs = millis();
  server.send(200, "text/plain", "OK");
}

void handlePasCalStatus() {
  bool running;
  unsigned long pulses;
  unsigned long lastPulseMicros;
  portENTER_CRITICAL(&pasPulseMux);
  running = pasCalRunning;
  pulses = pasCalPulses;
  lastPulseMicros = pasCalLastPulseMicros;
  portEXIT_CRITICAL(&pasPulseMux);
  if (running) {
    // Авто-завершение: 3 с без импульсов (педали встали) или общий тайм-аут 60 с
    bool idleDone = pulses > 0 && lastPulseMicros != 0 &&
                    (micros() - lastPulseMicros) > 3000000UL;
    bool timeoutDone = (millis() - pasCalStartMs) > PAS_CAL_TIMEOUT_MS;
    if (idleDone || timeoutDone) {
      server.send(200, "application/json", pasCalFinishAndJson());
      return;
    }
    String json = "{\"running\":true,\"pulses\":" + String(pulses) +
                  ",\"estimatedMagnets\":" + String((float)pulses / 2.0f, 1) + "}";
    server.send(200, "application/json", json);
  } else {
    server.send(200, "application/json", "{\"running\":false}");
  }
}

void handlePasCalStop() {
  portENTER_CRITICAL(&pasPulseMux);
  bool running = pasCalRunning;
  portEXIT_CRITICAL(&pasPulseMux);
  if (running) {
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
)rawliteral" + getTopBarCss() + getSettingsCss() + R"rawliteral(
body{max-width:400px}.update-hint{color:var(--ui-muted);font-size:13px}input[type=file]{padding:8px}</style>
</head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a class="back-link" href="/">&larr; Меню</a></p>
<h1>Загрузить прошивку (.bin)</h1>
<p class="update-hint">В Arduino IDE: Sketch &rarr; Export Compiled Binary — появится .bin рядом со скетчем. Выбери его тут и жми "Залить". Займёт секунд 20-30, плата сама перезагрузится.</p>
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
  // read-write: begin() сам создаёт namespace при первом запуске. isKey() не даёт
  // библиотеке писать в лог "ssid NOT_FOUND / pass NOT_FOUND" на чистой NVS.
  prefs.begin("wifi", false);
  if (prefs.isKey("ssid")) storedSsid = prefs.getString("ssid", "");
  if (prefs.isKey("pass")) storedPass = prefs.getString("pass", "");
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
  if (prefs.isKey("ssid")) storedApSsid = prefs.getString("ssid", "BikeControllerAP");
  if (prefs.isKey("pass")) storedApPass = prefs.getString("pass", "");
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

  Serial.printf("WiFi Connect -> SSID: '%s', Pass: <скрыт> (len=%d), Source: %s\n",
                useSsid.c_str(), usePass.length(),
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
        if (!startConfiguredAp()) {
          Serial.println("Ошибка запуска точки доступа");
          return;
        }
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
  Serial.printf("--- OpenBike Controller v%s ---\n", FIRMWARE_VERSION);

  // Распиновка: загрузка из NVS и применение до настройки всех пинов
  pinSettingsLoad();
  applyPinConfig();
  for (int i = 0; i < CUSTOM_PIN_MAX; i++) {
    if (!customPins[i].used || !gpioExists(customPins[i].gpio)) continue;
    uint8_t mode = customPins[i].mode;
    pinMode(customPins[i].gpio, mode == CUSTOM_PIN_OUTPUT ? OUTPUT : (mode == CUSTOM_PIN_INPUT_PULLUP ? INPUT_PULLUP : INPUT));
    if (mode == CUSTOM_PIN_OUTPUT) digitalWrite(customPins[i].gpio, LOW);
  }
  if (pinConfigCustom) Serial.println(F("Распиновка: применяется пользовательская конфигурация из NVS"));

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
  eventSettingsLoad();

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
  server.on("/emulation", handleEmulationPage);
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
  server.on("/debug/bus/control", HTTP_POST, handleBusCaptureControl);
  server.on("/debug/bus/status", HTTP_GET, handleBusCaptureStatus);
  server.on("/debug/bus/data", HTTP_GET, handleBusCaptureData);
  server.on("/debug/bus/csv", HTTP_GET, handleBusCaptureCsv);
  server.on("/settings/pins", handlePinsPage);
  server.on("/settings/pins/save", HTTP_POST, handlePinsSave);
  server.on("/settings/pins/row/save", HTTP_POST, handlePinRowSave);
  server.on("/settings/pins/custom/save", HTTP_POST, handleCustomPinSave);
  server.on("/settings/pins/custom/delete", HTTP_POST, handleCustomPinDelete);
  server.on("/settings/events", handleEventsPage);
  server.on("/settings/events/save", HTTP_POST, handleEventsSave);
  server.on("/settings/events/rule/save", HTTP_POST, handleEventRuleSave);
  server.on("/settings/events/rule/delete", HTTP_POST, handleEventRuleDelete);
  server.on("/settings/events/reset", HTTP_POST, handleEventsReset);
  server.on("/system", handleSystemPage);
  server.on("/system/export", handleSettingsExport);
  server.on("/system/import", HTTP_POST, handleSettingsImport);
  server.on("/system/factory-reset", HTTP_POST, handleSystemFactoryReset);
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
    8192, // стек с запасом: веб-задача собирает большие HTML-строки, 4096 было впритык
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
    unsigned long secStart = micros();
    updatePasDetection();
    cpuUsPas += micros() - secStart;

    secStart = micros();
    updatePasButton();
    cpuUsPasBtn += micros() - secStart;

    secStart = micros();
    updateThrottle();
    cpuUsThrottle += micros() - secStart;

    // 2. Вспомогательное управление освещением и звуком
    secStart = micros();
    updateLightButtons();
    cpuUsLight += micros() - secStart;

    // События обрабатываются после штатных кнопок, чтобы действие правила
    // не было отменено штатным toggle в том же цикле.
    secStart = micros();
    updateEventEngine();
    cpuUsThrottle += micros() - secStart;

    secStart = micros();
    updateLightBlink();
    updateTurnSignals();
    updateHorn();
    updateBuzzer();
    cpuUsSound += micros() - secStart;

    unsigned long elapsed = micros() - startMicros;
    cpuBusyTimeMicros += elapsed;

    if (millis() - cpuMeasureStartMs >= 1000) {
      unsigned long totalElapsedMs = millis() - cpuMeasureStartMs;
      if (totalElapsedMs > 0) {
        cpuUsagePercent = (int)constrain((cpuBusyTimeMicros * 100ULL) / (totalElapsedMs * 1000ULL), 0ULL, 100ULL);
        cpuPasPct = (int)constrain((cpuUsPas * 100ULL) / (totalElapsedMs * 1000ULL), 0ULL, 100ULL);
        cpuPasBtnPct = (int)constrain((cpuUsPasBtn * 100ULL) / (totalElapsedMs * 1000ULL), 0ULL, 100ULL);
        cpuThrottlePct = (int)constrain((cpuUsThrottle * 100ULL) / (totalElapsedMs * 1000ULL), 0ULL, 100ULL);
        cpuLightPct = (int)constrain((cpuUsLight * 100ULL) / (totalElapsedMs * 1000ULL), 0ULL, 100ULL);
        cpuSoundPct = (int)constrain((cpuUsSound * 100ULL) / (totalElapsedMs * 1000ULL), 0ULL, 100ULL);
      }
      cpuBusyTimeMicros = 0;
      cpuUsPas = 0; cpuUsPasBtn = 0; cpuUsThrottle = 0; cpuUsLight = 0; cpuUsSound = 0;
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
  cruiseLevelsCount = constrain(prefs.getInt("cnt", 3), 0, CRUISE_MAX_LEVELS);
  cruiseStartPercent = constrain(round(prefs.getFloat("stPct", 20.0f)), 0.0f, 100.0f);
  cruiseEndPercent = constrain(round(prefs.getFloat("endPct", 100.0f)), 0.0f, 100.0f);
  size_t got = prefs.isKey("pct") && prefs.getBytesLength("pct") == sizeof(cruiseLevelPercent)
                 ? prefs.getBytes("pct", cruiseLevelPercent, sizeof(cruiseLevelPercent)) : 0;
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
  for (int i = 0; i < CRUISE_MAX_LEVELS; i++) cruiseLevelPercent[i] = constrain(cruiseLevelPercent[i], 0.0f, 100.0f);
  cruiseCurrentLevel = constrain(cruiseCurrentLevel, 0, cruiseLevelsCount);
}

// ================= Веб: Cruise Control =================
void handleCruisePage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Круиз</title>
<style>
)rawliteral" + getTopBarCss() + getSettingsCss() + R"rawliteral(
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a class="back" href="/">&larr; Меню</a></p>
<h1>Круиз</h1>
<form id="f">
<fieldset><legend>Уровни</legend>
<div class="frow"><label>Количество уровней (1–100)</label>
<input type="number" id="count" name="count" min="1" max="100" value=")rawliteral"; 
  html += String(cruiseLevelsCount);
  html += R"rawliteral(" oninput="autoDistribute()"></div>

<div class="frow"><label for="stPct">Начало, %</label>
<input type="number" id="stPct" name="stPct" step="1" min="0" max="100" oninput="autoDistribute()" value=")rawliteral";
  html += String((int)round(cruiseStartPercent));
  html += R"rawliteral("></div>
<div class="frow"><label for="endPct">Конец, %</label>
<input type="number" id="endPct" name="endPct" step="1" min="0" max="100" oninput="autoDistribute()" value=")rawliteral";
  html += String((int)round(cruiseEndPercent));
  html += R"rawliteral("></div>

<p class="fhint">Значения распределяются автоматически при изменении диапазона или количества.</p>
<div id="levels" class="levels"></div>
</fieldset>
<fieldset><legend>Мягкий старт</legend>
<div class="chk"><label for="cruiseSsEn">Мягкий старт</label><input type="checkbox" id="cruiseSsEn" name="ssEn" )rawliteral";
  html += cruiseSoftStartEnabled ? "checked" : "";
  html += R"rawliteral(></div>
<div class="frow"><label>Время разгона, мс</label><input type="number" name="ssMs" value=")rawliteral";
  html += String(cruiseSoftStartMs);
  html += R"rawliteral("></div>
<input type="hidden" name="spEn" value=")rawliteral";
  html += cruiseSoftStopEnabled ? "1" : "0";
  html += R"rawliteral(">
<input type="hidden" name="spMs" value=")rawliteral";
  html += String(cruiseSoftStopMs);
  html += R"rawliteral(">
</fieldset>
<fieldset><legend>Поведение</legend>
<div class="chk"><label for="confThr">Подтверждать газом после старта</label><input type="checkbox" id="confThr" name="confThr" )rawliteral";
  html += cruiseConfirmThrottleAfterStart ? "checked" : "";
  html += R"rawliteral(></div>
<div class="frow"><label>После торможения</label>
<select name="brkMode">
  <option value="0")rawliteral"; html += (cruiseAfterBrakingMode == 0 ? " selected" : ""); html += R"rawliteral(>Сброс круиза</option>
  <option value="1")rawliteral"; html += (cruiseAfterBrakingMode == 1 ? " selected" : ""); html += R"rawliteral(>Подтверждение газом</option>
  <option value="2")rawliteral"; html += (cruiseAfterBrakingMode == 2 ? " selected" : ""); html += R"rawliteral(>Восстановить</option>
</select></div>
<p class="fhint">Сброс: требуется нажатие ОК и газ. Подтверждение: нужно снова выжать газ. Восстановление: вернуться к прежнему значению (осторожно!).</p>
<div class="frow"><label>После перегазовки</label>
<select name="thrMode">
  <option value="0")rawliteral"; html += (cruiseAfterThrottleMode == 0 ? " selected" : ""); html += R"rawliteral(>Сброс круиза</option>
  <option value="1")rawliteral"; html += (cruiseAfterThrottleMode == 1 ? " selected" : ""); html += R"rawliteral(>Подтверждение газом</option>
  <option value="2")rawliteral"; html += (cruiseAfterThrottleMode == 2 ? " selected" : ""); html += R"rawliteral(>Восстановить</option>
</select></div>
<p class="fhint">Действие после кратковременного увеличения газа поверх круиза.</p>
</fieldset>
<button type="submit">Сохранить</button>
</form>
)rawliteral" + getSettingsJs() + R"rawliteral(<script>
const saved = [)rawliteral";
  for (int i = 0; i < CRUISE_MAX_LEVELS; i++) {
    html += String(cruiseLevelPercent[i]);
    if (i < CRUISE_MAX_LEVELS - 1) html += ",";
  }
  html += R"rawliteral(];
function renderLevels(){
  renderLevelFields('levels','lvl',saved,document.getElementById('count').value,'Цель, % — уровень',100);
}
function autoDistribute(){
  const count=parseInt(document.getElementById('count').value)||0;
  const st=parseFloat(document.getElementById('stPct').value)||0;
  const end=parseFloat(document.getElementById('endPct').value)||100;
  distributeLevels(saved,count,st,end); renderLevels();
}
renderLevels();
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  const d=new FormData(this);
  const count = parseInt(document.getElementById('count').value)||0;
  const lvls = [];
  document.querySelectorAll('.lvl-input').forEach(inp => {
    lvls.push(Math.round(parseFloat(inp.value)||0));
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
  
  if (server.hasArg("stPct")) cruiseStartPercent = constrain(server.arg("stPct").toInt(), 0, 100);
  if (server.hasArg("endPct")) cruiseEndPercent = constrain(server.arg("endPct").toInt(), 0, 100);

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
          int v = constrain(item.toInt(), 0, 100);
          cruiseLevelPercent[idx++] = v;
        }
        currentPos = commaPos + 1;
      }
    }
  } else {
    for (int i = 0; i < cruiseLevelsCount; i++) {
      String key = "lvl" + String(i);
      if (server.hasArg(key)) {
        cruiseLevelPercent[i] = constrain(server.arg(key).toInt(), 0, 100);
      }
    }
  }

  cruiseSoftStartEnabled = server.hasArg("ssEn");
  cruiseSoftStopEnabled = server.arg("spEn").toInt() != 0; // скрытое поле: значение сохраняем, UI скрыт
  cruiseSoftStartMs = server.arg("ssMs").toInt();
  cruiseSoftStopMs = server.arg("spMs").toInt();
  cruiseConfirmThrottleAfterStart = server.hasArg("confThr");
  if (server.hasArg("brkMode")) cruiseAfterBrakingMode = server.arg("brkMode").toInt();
  if (server.hasArg("thrMode")) cruiseAfterThrottleMode = server.arg("thrMode").toInt();
  cruiseSettingsSave();
  server.send(200, "text/plain", "OK");
}

// ================= Веб: конструктор распиновки =================
String gpioCapabilityText(int g) {
  String text;
  if (gpioHasAdc(g)) text += gpioIsAdc2(g) ? "АЦП2" : "АЦП1";
  if (gpioHasDac(g)) text += String(text.length() ? " · " : "") + "ЦАП";
  if (gpioInputOnly(g)) text += String(text.length() ? " · " : "") + "только вход, без подтяжки";
  if (gpioIsStrap(g)) text += String(text.length() ? " · " : "") + "strap";
  if (gpioIsUart(g)) text += String(text.length() ? " · " : "") + "UART0";
  if (gpioIsFlash(g)) text += String(text.length() ? " · " : "") + "Flash";
  return text.length() ? text : "цифровой GPIO";
}

String pinOptionsHtml(int selected, bool output, bool needAdc, bool needDac, bool needPullup) {
  String html = "";
  for (int g = 0; g <= 39; g++) {
    if (!gpioExists(g)) continue;
    bool ok = true;
    String note = "";
    if (gpioIsFlash(g)) { ok = false; note = " (Flash)"; }
    else if (gpioIsUart(g)) { ok = false; note = " (UART0)"; }
    else if (output && gpioInputOnly(g)) { ok = false; note = " (только вход)"; }
    else if (needPullup && gpioInputOnly(g)) { ok = false; note = " (нет подтяжки)"; }
    else if (needAdc && !gpioHasAdc(g)) { ok = false; note = " (нет АЦП)"; }
    else if (needDac && !gpioHasDac(g)) { ok = false; note = " (нет ЦАП)"; }
    if (gpioIsStrap(g)) note += " (страп)";
    if (needAdc && gpioIsAdc2(g)) note += " (ADC2/Wi-Fi)";
    String opt = "<option value=\"" + String(g) + "\" data-cap=\"" + htmlEscape(gpioCapabilityText(g)) + "\"";
    if (g == selected) opt += " selected";
    if (!ok) opt += " disabled";
    opt += ">GPIO" + String(g) + note + "</option>";
    html += opt;
  }
  return html;
}

void handlePinsPage() {
  String errors, warnings;
  PinConfig tmp = pinConfig;
  errors = validatePinConfig(tmp, &warnings);

  // Карта занятости пинов
  bool used[40] = {false};
  for (int i = 0; i < PIN_ROLE_COUNT; i++) {
    int g = pinField(pinConfig, i);
    if (g >= 0 && g <= 39) used[g] = true;
  }
  for (int i = 0; i < CUSTOM_PIN_MAX; i++) {
    int g = customPins[i].gpio;
    if (customPins[i].used && g >= 0 && g <= 39) used[g] = true;
  }
  String freeList = "";
  int freeCount = 0;
  for (int g = 0; g <= 39; g++) {
    if (!gpioExists(g) || gpioIsFlash(g) || gpioIsUart(g) || used[g]) continue;
    freeList += String(freeCount ? ", " : "") + "GPIO" + String(g);
    freeCount++;
  }

  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Распиновка GPIO</title>
<style>
)rawliteral" + getTopBarCss() + getSettingsCss() + R"rawliteral(
body{max-width:560px}
.pin-row{display:grid;grid-template-columns:minmax(0,1fr) minmax(118px,170px);align-items:start;gap:6px 10px;background:var(--ui-card);border:1px solid var(--ui-border);border-radius:8px;padding:8px 10px;margin-bottom:6px;box-sizing:border-box}.pin-main,.pin-side{min-width:0}.pin-name{display:block;box-sizing:border-box;width:100%;min-width:0;margin:0;background:var(--ui-button);color:var(--ui-text);border:1px solid var(--ui-border);border-radius:6px;padding:6px;font-size:14px}.pin-row select{box-sizing:border-box;width:100%;min-width:0;margin:0;background:var(--ui-button);color:var(--ui-text);border:1px solid var(--ui-border);border-radius:6px;padding:6px;font-size:13px}.pin-side{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:5px}.custom-row .pin-side{grid-template-columns:minmax(0,1fr) auto auto}.pin-meta{grid-column:1/-1;display:grid;grid-template-columns:minmax(0,1fr) minmax(118px,170px);gap:10px;color:var(--ui-muted);font-size:11px;line-height:1.25}.pin-cap{text-align:right}.custom-mode{grid-column:1/-1;display:flex;align-items:center;justify-content:flex-end;gap:6px;margin-top:1px}.custom-mode label{font-size:10px;color:var(--ui-muted)}.custom-mode .pin-mode{width:min(170px,55%)}.row-save,.row-del{display:none!important;width:44px!important;min-width:44px;margin:0!important;padding:6px!important}.pin-row.dirty .row-save{display:block!important}.custom-row .row-del{display:block!important}.row-msg{grid-column:1/-1;min-height:0;font-size:12px;white-space:pre-line}.row-msg.msg-ok{color:var(--ui-success)}.row-msg.msg-err{color:var(--ui-danger)}.add-pin{border-style:dashed;color:var(--ui-accent)}
@media(max-width:420px){body{padding-left:12px;padding-right:12px}.top-bar-sticky{margin-left:-12px;margin-right:-12px}.pin-row{grid-template-columns:minmax(0,1fr) minmax(105px,38%);gap:5px 7px;padding:7px}.pin-name,.pin-row select{font-size:14px;padding:6px}.pin-meta{grid-template-columns:minmax(0,1fr) minmax(105px,38%);gap:7px;font-size:11px}.custom-mode .pin-mode{width:min(150px,60%)}}
button{margin-top:10px}
.msg{padding:10px;border-radius:8px;margin:10px 0;font-size:13px;white-space:pre-line}
.msg-err{border:1px solid var(--ui-danger);color:var(--ui-danger)}
.msg-warn{border:1px solid var(--ui-warning);color:var(--ui-warning)}
.msg-ok{border:1px solid var(--ui-success);color:var(--ui-success)}
.free{background:var(--ui-card);border:1px solid var(--ui-border);border-radius:8px;padding:10px;margin:10px 0;font-size:13px;color:var(--ui-muted)}
h2{font-size:15px;margin:16px 0 8px}.pin-title{font-size:20px}.pin-badges{color:var(--ui-muted);font-size:11px}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a class="back-link" href="/">&larr; Меню</a></p>
<h1 class="pin-title">GPIO</h1>
<p class="hint">Назначение пинов сохраняется в NVS и применяется после перезагрузки.
)rawliteral" + String(pinConfigCustom ? "Сейчас действует <b>пользовательская</b> конфигурация." : "Сейчас действует <b>заводская</b> конфигурация.") + R"rawliteral(</p>
<div class="free"><b>Свободные пины:</b> )rawliteral" + (freeCount ? freeList : "нет") + R"rawliteral(</div>
)rawliteral" + (errors.length() ? "<div class=\"msg msg-err\">" + errors + "</div>" : "") + R"rawliteral(
)rawliteral" + (warnings.length() ? "<div class=\"msg msg-warn\">" + warnings + "</div>" : "") + R"rawliteral(
<form method="POST" action="/settings/pins/save">
)rawliteral";

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(html);
  html = "";

  for (int i = 0; i < PIN_ROLE_COUNT; i++) {
    const PinRole &r = pinRoles[i];
    int g = pinField(pinConfig, i);
    String badges = String(r.output ? "[выход] " : "") + (r.adc ? "[АЦП] " : "") + (r.dac ? "[ЦАП] " : "") + (r.pullup ? "[подтяжка] " : "") + (r.pwm ? "[ШИМ]" : "");
    html += "<div class=\"pin-row\" data-row=\"" + String(i) + "\" data-slot=\"" + String(i) + "\" data-orig-name=\"" + htmlEscape(pinRoleName(i)) + "\" data-orig-gpio=\"" + String(g) + "\">";
    html += "<div class=\"pin-main\"><input class=\"pin-name\" type=\"text\" maxlength=\"64\" name=\"nm" + String(i) + "\" value=\"" + htmlEscape(pinRoleName(i)) + "\" aria-label=\"Название пина\"></div>";
    html += "<div class=\"pin-side\"><select name=\"" + String(r.key) + "\" aria-label=\"Выбор GPIO\">" + pinOptionsHtml(g, r.output, r.adc, r.dac, r.pullup) + "</select>";
    html += "<button type=\"button\" class=\"row-save\" title=\"Сохранить строку\" aria-label=\"Сохранить изменения\">✓</button></div>";
    html += "<div class=\"pin-meta\"><span class=\"pin-badges\">" + badges + "</span><span class=\"pin-cap\">" + htmlEscape(gpioCapabilityText(g)) + "</span></div>";
    html += "<div class=\"row-msg\" aria-live=\"polite\"></div></div>";
    server.sendContent(html);
    html = "";
  }

  // Дополнительные пользовательские GPIO
  html += "<h2>Дополнительные GPIO</h2>";
  html += "<p class=\"hint\">Резервируют и инициализируют пин, но не привязаны к функциям контроллера. Применяются сразу, без перезагрузки.</p>";
  html += "<div id=\"customRows\">";
  for (int i = 0; i < CUSTOM_PIN_MAX; i++) {
    CustomPinRole &c = customPins[i];
    if (!c.used) continue;
    html += customPinRowHtml(i, c);
    server.sendContent(html);
    html = "";
  }
  html += "</div>";
  html += "<button type=\"button\" id=\"addCustom\" class=\"add-pin\">+ GPIO</button>";

  html += R"rawliteral(
<button type="submit">Проверить и сохранить</button>
</form>
<form method="POST" action="/settings/pins/reset" onsubmit="return confirm('Вернуть заводскую распиновку?')">
<button type="submit">Сбросить к заводской</button>
</form>
)rawliteral" + getTopBarJs() + R"rawliteral(
<script>
function rowMsg(row, ok, text) {
  var m = row.querySelector('.row-msg');
  if (!m) return;
  m.textContent = text;
  m.className = 'row-msg ' + (ok ? 'msg-ok' : 'msg-err');
  if (text) { setTimeout(function(){ if (m.textContent === text) { m.textContent = ''; m.className = 'row-msg'; } }, 6000); }
}
function rowDirty(row) {
  var name = row.querySelector('.pin-name').value;
  var sel = row.querySelector('select');
  var mode = row.querySelector('.pin-mode');
  var changed = name !== row.dataset.origName;
  if (sel) changed = changed || sel.value !== row.dataset.origGpio;
  if (mode) changed = changed || mode.value !== row.dataset.origMode;
  row.classList.toggle('dirty', changed);
}
function rowBusy(row, busy) {
  var b = row.querySelector('.row-save');
  if (b) { b.disabled = busy; b.textContent = busy ? '…' : '✓'; }
}
function serialArgs(row) {
  var fd = new FormData();
  fd.append('slot', row.dataset.slot);
  fd.append('nm', row.querySelector('.pin-name').value);
  if (row.classList.contains('custom-row')) {
    fd.append('gpio', row.querySelector('.pin-gpio').value);
    fd.append('mode', row.querySelector('.pin-mode').value);
  } else {
    fd.append('gpio', row.querySelector('select').value);
  }
  return fd;
}
async function saveRow(row, url) {
  var slot = row.dataset.slot;
  var name = row.querySelector('.pin-name');
  name.value = name.value.trim();
  if (!name.reportValidity()) return;
  rowBusy(row, true);
  try {
    var r = await fetch(url, { method: 'POST', body: serialArgs(row) });
    var t = await r.text();
    rowMsg(row, r.ok, t);
    if (r.ok) {
      row.dataset.origName = row.querySelector('.pin-name').value;
      var sel = row.querySelector('select');
      var gpioSel = row.querySelector('.pin-gpio');
      var mode = row.querySelector('.pin-mode');
      if (gpioSel) row.dataset.origGpio = gpioSel.value;
      else if (sel) row.dataset.origGpio = sel.value;
      if (mode) row.dataset.origMode = mode.value;
      row.classList.remove('dirty');
      if (url.indexOf('/custom/') !== -1) { setTimeout(function(){ location.reload(); }, 800); }
    }
  } catch (e) {
    rowMsg(row, false, 'Ошибка сети: ' + e);
  }
  rowBusy(row, false);
}
function updatePinCapability(row) {
  var sel = row.querySelector('.pin-gpio') || row.querySelector('.pin-side select');
  var cap = row.querySelector('.pin-cap');
  if (!sel || !cap || !sel.options.length) return;
  cap.textContent = sel.options[sel.selectedIndex].dataset.cap || '';
}
function bindRow(row, url) {
  row.addEventListener('input', function(){ rowDirty(row); });
  row.addEventListener('change', function(){ rowDirty(row); updatePinCapability(row); });
  updatePinCapability(row);
  var b = row.querySelector('.row-save');
  if (b) b.addEventListener('click', function(){ saveRow(row, url); });
}
document.querySelectorAll('.pin-row[data-row]').forEach(function(row){ bindRow(row, '/settings/pins/row/save'); });
document.querySelectorAll('.custom-row').forEach(function(row){ bindRow(row, '/settings/pins/custom/save'); });
document.querySelectorAll('.custom-row .row-del').forEach(function(del){
  del.addEventListener('click', async function(){
    var row = del.closest('.custom-row');
    if (!confirm('Удалить этот GPIO?')) return;
    del.disabled = true;
    try {
      var fd = new FormData();
      fd.append('slot', row.dataset.slot);
      var r = await fetch('/settings/pins/custom/delete', { method: 'POST', body: fd });
      var t = await r.text();
      rowMsg(row, r.ok, t);
      if (r.ok) setTimeout(function(){ location.reload(); }, 800);
      else del.disabled = false;
    } catch (e) { rowMsg(row, false, 'Ошибка сети: ' + e); del.disabled = false; }
  });
});
var add = document.getElementById('addCustom');
if (add) add.addEventListener('click', async function(){
  add.disabled = true;
  try {
    var fd = new FormData();
    fd.append('nm', 'GPIO ' + String(document.querySelectorAll('.custom-row').length + 1));
    var r = await fetch('/settings/pins/custom/save', { method: 'POST', body: fd });
    var t = await r.text();
    if (!r.ok) { alert(t); add.disabled = false; return; }
    location.reload();
  } catch (e) { alert('Ошибка сети: ' + e); add.disabled = false; }
});
</script>
</body></html>
)rawliteral";
  server.sendContent(html);
  server.sendContent("");
}

String customPinRowHtml(int i, const CustomPinRole &c) {
  String html = "<div class=\"pin-row custom-row\" data-slot=\"" + String(i) + "\" data-orig-name=\"" + htmlEscape(c.name) + "\" data-orig-gpio=\"" + String(c.gpio) + "\" data-orig-mode=\"" + String(c.mode) + "\">";
  html += "<div class=\"pin-main\"><input class=\"pin-name\" type=\"text\" maxlength=\"64\" value=\"" + htmlEscape(c.name) + "\" aria-label=\"Название пользовательского GPIO\"></div>";
  html += "<div class=\"pin-side\">";
  html += "<select class=\"pin-gpio\" aria-label=\"Выбор GPIO\">";
  for (int g = 0; g <= 39; g++) {
    if (!gpioExists(g) || gpioIsFlash(g) || gpioIsUart(g)) continue;
    html += "<option value=\"" + String(g) + "\" data-cap=\"" + htmlEscape(gpioCapabilityText(g)) + "\"" + (g == c.gpio ? " selected" : "") + ">GPIO" + String(g) + "</option>";
  }
  html += "</select>";
  html += "<button type=\"button\" class=\"row-save\" title=\"Сохранить\" aria-label=\"Сохранить\">✓</button>";
  html += "<button type=\"button\" class=\"row-del\" title=\"Удалить\" aria-label=\"Удалить\">✕</button></div>";
  html += "<div class=\"pin-meta\"><span class=\"pin-badges\">[доп.]</span><span class=\"pin-cap\">" + htmlEscape(gpioCapabilityText(c.gpio)) + "</span></div>";
  html += "<div class=\"custom-mode\"><label>Режим</label><select class=\"pin-mode\" aria-label=\"Режим GPIO\">";
  const char* modes[] = {"вход", "вход + подтяжка", "выход"};
  for (int m = 0; m < 3; m++) html += "<option value=\"" + String(m) + "\"" + (m == (int)c.mode ? " selected" : "") + ">" + modes[m] + "</option>";
  html += "</select></div>";
  html += "<div class=\"row-msg\" aria-live=\"polite\"></div></div>";
  return html;
}

void handlePinRowSave() {
  if (!server.hasArg("slot")) { server.send(400, "text/plain", "Нет параметра slot"); return; }
  int slot = server.arg("slot").toInt();
  if (slot < 0 || slot >= PIN_ROLE_COUNT) { server.send(400, "text/plain", "Неверный слот"); return; }
  String name = server.arg("nm");
  int g = server.hasArg("gpio") ? server.arg("gpio").toInt() : pinField(pinConfig, slot);
  if (!normalizeUserLabel(name)) { server.send(400, "text/plain", "Название: от 1 до 64 символов, без < и >"); return; }
  for (int j = 0; j < PIN_ROLE_COUNT; j++) {
    if (j == slot) continue;
    if (String(pinRoleNames[j]).length() && name.equalsIgnoreCase(String(pinRoleNames[j]))) { server.send(400, "text/plain", "Название уже используется: " + name); return; }
  }
  for (int j = 0; j < CUSTOM_PIN_MAX; j++) {
    if (!customPins[j].used) continue;
    if (name.equalsIgnoreCase(String(customPins[j].name))) { server.send(400, "text/plain", "Название уже используется: " + name); return; }
  }
  PinConfig next = pinConfig;
  pinField(next, slot) = (int16_t)g;
  String warnings;
  String errors = validatePinConfig(next, &warnings);
  if (errors.length()) { server.send(400, "text/plain", errors); return; }
  pinConfig = next;
  setUserLabel(pinRoleNames[slot], name);
  pinConfigCustom = true;
  pinSettingsSave();
  server.send(200, "text/plain", "Строка сохранена. Перезагрузка для применения...");
}

void handleCustomPinSave() {
  bool adding = !server.hasArg("slot");
  int slot = adding ? -1 : server.arg("slot").toInt();
  String name = server.arg("nm");
  int gpio = server.hasArg("gpio") ? server.arg("gpio").toInt() : -1;
  int mode = server.hasArg("mode") ? server.arg("mode").toInt() : -1;
  if (adding) {
    int freeSlot = -1;
    for (int i = 0; i < CUSTOM_PIN_MAX; i++) if (!customPins[i].used) { freeSlot = i; break; }
    if (freeSlot == -1) { server.send(400, "text/plain", "Достигнут лимит дополнительных GPIO (" + String(CUSTOM_PIN_MAX) + ")"); return; }
    slot = freeSlot;
    if (name.length() == 0) name = "Доп. GPIO";
    if (gpio == -1) {
      bool used[40] = {false};
      for (int i = 0; i < PIN_ROLE_COUNT; i++) { int gg = pinField(pinConfig, i); if (gg >= 0 && gg <= 39) used[gg] = true; }
      for (int i = 0; i < CUSTOM_PIN_MAX; i++) { if (customPins[i].used && customPins[i].gpio >= 0 && customPins[i].gpio <= 39) used[customPins[i].gpio] = true; }
      for (int gg = 0; gg <= 39; gg++) {
        if (gpioExists(gg) && !gpioIsFlash(gg) && !gpioIsUart(gg) && !used[gg]) { gpio = gg; break; }
      }
      if (gpio == -1) { server.send(400, "text/plain", "Нет свободных GPIO"); return; }
      mode = CUSTOM_PIN_INPUT;
    }
  } else {
    if (slot < 0 || slot >= CUSTOM_PIN_MAX) { server.send(400, "text/plain", "Неверный слот"); return; }
    if (!customPins[slot].used) { server.send(400, "text/plain", "Слот не занят"); return; }
  }
  if (!normalizeUserLabel(name)) { server.send(400, "text/plain", "Название: от 1 до 64 символов, без < и >"); return; }
  if (gpio < 0 || gpio > 39 || !gpioExists(gpio) || gpioIsFlash(gpio) || gpioIsUart(gpio)) { server.send(400, "text/plain", "Недопустимый GPIO"); return; }
  if (mode < 0 || mode > 2) { server.send(400, "text/plain", "Неверный режим"); return; }
  if (gpioInputOnly(gpio) && mode == CUSTOM_PIN_OUTPUT) { server.send(400, "text/plain", "GPIO" + String(gpio) + " поддерживает только вход"); return; }
  if (gpioInputOnly(gpio) && mode == CUSTOM_PIN_INPUT_PULLUP) { server.send(400, "text/plain", "GPIO" + String(gpio) + " не имеет внутренней подтяжки"); return; }
  for (int j = 0; j < PIN_ROLE_COUNT; j++) {
    if (pinField(pinConfig, j) == gpio) { server.send(400, "text/plain", "GPIO" + String(gpio) + " уже занят системной ролью"); return; }
    if (String(pinRoleNames[j]).length() && name.equalsIgnoreCase(String(pinRoleNames[j]))) { server.send(400, "text/plain", "Название уже используется: " + name); return; }
  }
  for (int j = 0; j < CUSTOM_PIN_MAX; j++) {
    if (j != slot && customPins[j].used && customPins[j].gpio == gpio) { server.send(400, "text/plain", "GPIO" + String(gpio) + " уже занят другой доп. ролью"); return; }
    if (j != slot && customPins[j].used && name.equalsIgnoreCase(String(customPins[j].name))) { server.send(400, "text/plain", "Название уже используется: " + name); return; }
  }
  customPins[slot].used = 1;
  customPins[slot].mode = (uint8_t)mode;
  customPins[slot].gpio = (int16_t)gpio;
  strncpy(customPins[slot].name, name.c_str(), USER_LABEL_SIZE - 1);
  customPins[slot].name[USER_LABEL_SIZE - 1] = 0;
  pinMode(gpio, mode == CUSTOM_PIN_OUTPUT ? OUTPUT : (mode == CUSTOM_PIN_INPUT_PULLUP ? INPUT_PULLUP : INPUT));
  if (mode == CUSTOM_PIN_OUTPUT) digitalWrite(gpio, LOW);
  pinSettingsSave();
  server.send(200, "text/plain", "Доп. GPIO сохранён");
}

void handleCustomPinDelete() {
  int slot = server.hasArg("slot") ? server.arg("slot").toInt() : -1;
  if (slot < 0 || slot >= CUSTOM_PIN_MAX) { server.send(400, "text/plain", "Неверный слот"); return; }
  if (!customPins[slot].used) { server.send(400, "text/plain", "Слот не занят"); return; }
  customPins[slot] = CustomPinRole();
  pinSettingsSave();
  server.send(200, "text/plain", "Доп. GPIO удалён");
}

void handlePinsSave() {
  PinConfig next = pinConfig;
  char nextNames[PIN_ROLE_COUNT][USER_LABEL_SIZE] = {};
  String errors = "";
  for (int i = 0; i < PIN_ROLE_COUNT; i++) {
    if (server.hasArg(pinRoles[i].key)) pinField(next, i) = (int16_t)server.arg(pinRoles[i].key).toInt();
    String name = server.arg("nm" + String(i));
    if (!normalizeUserLabel(name)) errors += "Название пина " + String(i + 1) + ": от 1 до 64 символов, без < и >\n";
    else {
      setUserLabel(nextNames[i], name);
      for (int j = 0; j < i; j++) if (String(nextNames[j]).length() && name.equalsIgnoreCase(String(nextNames[j]))) errors += "Названия пинов не должны повторяться: " + name + "\n";
    }
  }
  String warnings;
  errors += validatePinConfig(next, &warnings);
  if (errors.length()) {
    server.send(400, "text/plain", "Ошибки в распиновке, конфиг не сохранён:\n" + errors);
    return;
  }
  pinConfig = next;
  memcpy(pinRoleNames, nextNames, sizeof(pinRoleNames));
  pinConfigCustom = true;
  pinSettingsSave();
  server.send(200, "text/plain", "Распиновка сохранена. Перезагрузка для применения...");
  delay(1500);
  ESP.restart();
}

void handlePinsReset() {
  pinConfig = PIN_CONFIG_DEFAULTS;
  memset(pinRoleNames, 0, sizeof(pinRoleNames));
  for (int i = 0; i < CUSTOM_PIN_MAX; i++) customPins[i] = CustomPinRole();
  pinConfigCustom = false;
  pinSettingsSave();
  server.send(200, "text/plain", "Возвращена заводская распиновка. Перезагрузка...");
  delay(1500);
  ESP.restart();
}

// ================= Веб: конструктор событий =================
void handleEventsPage() {
  String html=R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>События</title><style>)rawliteral"+getTopBarCss()+getSettingsCss()+R"rawliteral(.rule{background:var(--ui-card);border:1px solid var(--ui-border);padding:9px;border-radius:8px;margin:7px 0}.rule.is-free{display:none}.rule-head{display:flex;align-items:center;gap:8px;min-height:26px;cursor:pointer}.rule-head strong{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.rule-head .chevron{transition:transform .15s}.rule.open .chevron{transform:rotate(180deg)}.badge{font-size:11px;color:var(--ui-muted);background:var(--ui-button);padding:3px 6px;border-radius:4px}.rule-body{display:none;padding-top:8px}.rule.open .rule-body{display:block}.rule,.rule-body,.name-row,.if-box,.acts,.act{box-sizing:border-box;min-width:0;max-width:100%}.name-row{display:grid;grid-template-columns:34px minmax(0,1fr);gap:7px;align-items:center}.name-row .chk{display:flex;align-items:center;justify-content:center;margin:0;padding:7px 4px}.name-row .chk input{margin:0}.name-row .rule-name{margin:0;min-width:0;font-weight:700}.if-box,.act{position:relative;border:1px solid var(--ui-border);border-radius:7px;padding:23px 7px 7px;margin-top:7px}.flow{position:absolute;top:6px;left:7px;font-size:12px;line-height:1;font-weight:800;color:var(--ui-accent);text-transform:uppercase}.field-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px}.condition-fields{display:flex;gap:6px;flex-wrap:wrap;margin-top:6px}.field{display:flex;flex:1 1 105px;min-width:0;flex-direction:column;gap:2px}.field label{font-size:10px;line-height:1.1;color:var(--ui-muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.field input,.field select{display:block;width:100%;max-width:100%;min-width:0;margin:0;padding:6px}.condition-fields .is-hidden{display:none}.acts{margin-top:6px}.act{display:grid;grid-template-columns:minmax(0,1fr) 34px;gap:6px}.act-fields{grid-column:1;min-width:0}.act>.icon-btn,.act>.act-spacer{grid-column:2;align-self:center}.act.is-hidden{display:none}.act-spacer{display:block}.value-field{margin-top:6px}.value-field.is-hidden{display:none}.icon-btn,.add-action{width:auto;margin:0;padding:7px 9px}.icon-btn{min-width:34px;color:var(--ui-danger)}.add-action{margin:5px 0 0;border-style:dashed;background:transparent;color:var(--ui-accent)}.rule-tools{display:flex;gap:7px;margin-top:8px}.rule-tools button{width:auto;flex:1;margin:0;padding:8px}.save-rule{color:var(--ui-success);border-color:var(--ui-success);background:var(--ui-button)}.delete-rule{color:var(--ui-danger);border-color:var(--ui-danger);background:var(--ui-button)}.save-rule:hover,.delete-rule:hover{background:var(--ui-hover)}.rule.dirty .save-rule{box-shadow:0 0 0 2px var(--ui-accent)}.rule-msg{font-size:12px;margin-top:6px;white-space:pre-line}.rule-msg.ok{color:var(--ui-success)}.rule-msg.err{color:var(--ui-danger)}.add-rule{border-style:dashed;background:transparent;color:var(--ui-accent)}.reset-events{color:var(--ui-warning);background:var(--ui-button)}.log{font-size:13px;color:var(--ui-muted)}#eventMsg{white-space:pre-wrap;font-size:13px;margin-top:8px;color:var(--ui-danger)}@media(max-width:420px){body{padding-left:12px;padding-right:12px}.top-bar-sticky{margin-left:-12px;margin-right:-12px}.rule{padding:8px}.if-box,.act{padding-left:6px;padding-right:6px}.field-grid{gap:4px}.field input,.field select{font-size:13px;padding:6px 4px}.field label{font-size:9px}.rule-tools{flex-direction:column}.rule-tools button{width:100%}}</style></head><body>)rawliteral"+getTopBarHtml()+R"rawliteral(<p><a class="back" href="/">&larr; Меню</a></p><h1>События</h1><p class="hint">Правило строится по схеме «Если → Тогда». Защитные ограничения газа не изменяются.</p><form id="eventsForm" method="POST" action="/settings/events/save">)rawliteral";
  const char* trig[] = {"-","Тормоз: нажатие","Тормоз: отпускание","Тормоз: удержание","Кнопка: фара","Кнопка: левый поворотник","Кнопка: правый поворотник","Кнопка: гудок","Кнопка: PAS","PAS: достигнут уровень","PAS: включён","PAS: выключен","Круиз: включён","Круиз: выключен","Загрузка (boot)"};
  const char* cond[] = {"Нажатие","Серия нажатий","Удержание"};
  const char* acts[] = {"Нет","Переключить","Включить","Выключить","Переключить","Переключить","Переключить","Переключить","Сигнал","Сигнал","Установить уровень","Переключить","BLINK","BLINK"};
  const char* devices[] = {"—","Сервис","Фара","ДХО","Левый поворотник","Правый поворотник","Гудок","Пищалка","PAS"};
  const uint8_t actionDevice[] = {0,1,1,1,2,3,4,5,6,7,8,8,2,3};
  // Страница заметно больше остальных: отправляем её частями, чтобы не держать
  // весь HTML в одном String и не фрагментировать кучу ESP32.
  server.sendHeader("Cache-Control","no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma","no-cache");
  server.sendHeader("Expires","0");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200,"text/html; charset=utf-8","");
  server.sendContent(html);
  html="";
  for(int i=0;i<EVENT_MAX_RULES;i++){
    EventRule&r=eventRules[i];
    html+="<div class=\"rule "+String(r.trigger==EV_NONE?"is-free":"")+"\" data-slot=\""+String(i)+"\" data-used=\""+String(r.trigger==EV_NONE?"0":"1")+"\"><div class=\"rule-head\"><strong>"+htmlEscape(eventRuleName(i))+"</strong><span class=\"badge\">"+String(r.enabled?"вкл":"выкл")+"</span><span class=\"chevron\">⌄</span></div><div class=\"rule-body\"><div class=\"name-row\"><label class=\"chk\" title=\"Правило включено\"><input type=\"checkbox\" name=\"en"+String(i)+"\" "+(r.enabled?"checked":"")+" aria-label=\"Включить правило\"></label><input class=\"rule-name\" type=\"text\" maxlength=\"64\" required name=\"nm"+String(i)+"\" value=\""+htmlEscape(eventRuleName(i))+"\" placeholder=\"Название правила\" aria-label=\"Название правила\"></div><div class=\"if-box\"><span class=\"flow\">Если</span><div class=\"field-grid\"><div class=\"field\"><label>Событие</label><select name=\"tr"+String(i)+"\" aria-label=\"Триггер\" title=\"Триггер\">";
    for(int x=0;x<15;x++)html+="<option value=\""+String(x)+"\" "+(r.trigger==x?"selected":"")+">"+trig[x]+"</option>";
    html+="</select></div><div class=\"field\"><label>Тип срабатывания</label><select class=\"condition-select\" name=\"co"+String(i)+"\" aria-label=\"Тип срабатывания\" title=\"Тип срабатывания\">";
    for(int x=0;x<3;x++)html+="<option value=\""+String(x)+"\" "+(r.condition==x?"selected":"")+">"+cond[x]+"</option>";
    html+="</select></div></div><div class=\"condition-fields\"><div class=\"field count-field\"><label>Количество нажатий</label><input type=\"number\" min=\"1\" max=\"20\" name=\"ct"+String(i)+"\" value=\""+String(r.count?r.count:1)+"\" aria-label=\"Количество нажатий\"></div><div class=\"field interval-field\"><label>Интервал, мс</label><input type=\"number\" min=\"50\" max=\"60000\" name=\"ms"+String(i)+"\" value=\""+String(r.intervalMs?r.intervalMs:700)+"\" aria-label=\"Интервал, мс\"></div><div class=\"field priority-field\"><label>Приоритет</label><input type=\"number\" min=\"0\" max=\"255\" name=\"pr"+String(i)+"\" value=\""+String(r.priority)+"\" aria-label=\"Приоритет\"></div></div></div><div class=\"acts\">";
    for(int k=0;k<EVENT_MAX_ACTIONS;k++){
      bool show=(k==0)||r.actions[k]!=EV_NO_ACTION;
      uint8_t dev=r.actions[k]<=EV_ACTION_MAX?actionDevice[r.actions[k]]:0;
      html+="<div class=\"act"+String(show?"":" is-hidden")+"\" id=\"act"+String(i)+"_"+String(k)+"\" data-idx=\""+String(k)+"\"><span class=\"flow\">Тогда</span><div class=\"act-fields\"><div class=\"field-grid\"><div class=\"field\"><label>Устройство</label><select class=\"ev-device\" aria-label=\"Устройство\" title=\"Устройство\">";
      for(int d=0;d<9;d++)html+="<option value=\""+String(d)+"\" "+(dev==d?"selected":"")+">"+devices[d]+"</option>";
      html+="</select></div><div class=\"field\"><label>Действие</label><select class=\"ev-action\" name=\"ac"+String(i)+"_"+String(k)+"\" aria-label=\"Действие\" title=\"Действие\">";
      for(int x=0;x<=EV_ACTION_MAX;x++)html+="<option data-dev=\""+String(actionDevice[x])+"\" value=\""+String(x)+"\" "+(r.actions[k]==x?"selected":"")+">"+acts[x]+"</option>";
      html+="</select></div></div><div class=\"field value-field\"><label>Значение (мс / уровень)</label><input class=\"action-value\" type=\"number\" min=\"0\" max=\"60000\" name=\"va"+String(i)+"_"+String(k)+"\" value=\""+String(r.actionValues[k])+"\" aria-label=\"Значение действия\" title=\"Значение: мс или уровень\"></div></div>";
      if(k>0)html+="<button type=\"button\" class=\"icon-btn remove-action\" data-action=\"remove-action\" title=\"Удалить действие\" aria-label=\"Удалить действие\">×</button>";else html+="<span class=\"act-spacer\"></span>";
      html+="</div>";
    }
    html+="<button type=\"button\" id=\"add"+String(i)+"\" data-action=\"add-action\" class=\"add-action "+String(r.actions[1]!=EV_NO_ACTION&&r.actions[2]!=EV_NO_ACTION?"is-hidden":"")+"\">+ действие</button></div><div class=\"rule-tools\"><button type=\"button\" class=\"save-rule\" data-action=\"save-rule\">✓ Сохранить</button><button type=\"button\" class=\"delete-rule\" data-action=\"delete-rule\">Удалить</button></div></div></div>";
    server.sendContent(html);
    html="";
  }
  html+=R"rawliteral(<button type="button" class="add-rule" data-action="add-rule">+ правило</button><div id="eventMsg" aria-live="polite"></div></form><form method="POST" action="/settings/events/reset" onsubmit="return confirm('Вернуть заводские правила?')"><button type="submit" class="reset-events">Сбросить правила</button></form><h2>Журнал</h2><div class="log">)rawliteral";
  if(!eventLogCount)html+="Событий пока нет";else for(int n=0;n<eventLogCount;n++){int p=(eventLogHead-1-n+10)%10;html+=htmlEscape(eventRuleName(eventLog[p].rule))+" — "+String(eventLog[p].at)+" мс<br>";}
  html+="</div>"+getTopBarJs()+R"rawliteral(<script>
function actionUi(r){const a=Number(r.querySelector('.ev-action').value),v=r.querySelector('.value-field'),label=v.querySelector('label');const uses=[9,10,11,13,14].includes(a);v.classList.toggle('is-hidden',!uses);label.textContent=a===11?'Уровень PAS':(a===13||a===14?'Период мигания, мс':'Длительность сигнала, мс')}
function evFilter(r,d){const a=r.querySelector('.ev-action');let f=null;for(const o of a.options){const ok=Number(o.dataset.dev)===d;o.hidden=!ok;o.disabled=!ok;if(ok&&!f)f=o}const cur=a.selectedOptions[0];if(cur&&cur.disabled&&f)a.value=f.value;actionUi(r)}
function evDevice(s){evFilter(s.closest('.act'),Number(s.value))}function evAction(s){const r=s.closest('.act'),o=s.selectedOptions[0];if(o)r.querySelector('.ev-device').value=o.dataset.dev;actionUi(r)}
function conditionUi(c){const mode=Number(c.querySelector('.condition-select').value),count=c.querySelector('.count-field'),interval=c.querySelector('.interval-field');count.classList.toggle('is-hidden',mode!==1);interval.classList.toggle('is-hidden',mode===0);interval.querySelector('label').textContent=mode===2?'Длительность удержания, мс':'Интервал серии, мс'}
function evShowAct(c){for(let k=1;k<3;k++){const d=c.querySelector('.act[data-idx="'+k+'"]');if(d&&d.classList.contains('is-hidden')){d.classList.remove('is-hidden');evFilter(d,Number(d.querySelector('.ev-device').value));if(k===2)c.querySelector('.add-action').classList.add('is-hidden');ruleDirty(c);return}}}
function evHideAct(d){const c=d.closest('.rule');d.classList.add('is-hidden');d.querySelector('.ev-action').value=0;d.querySelector('.ev-device').value=0;d.querySelector('.action-value').value=0;c.querySelector('.add-action').classList.remove('is-hidden');ruleDirty(c)}
function cards(){return Array.from(document.querySelectorAll('.rule[data-slot]'))}function refreshAdd(){const b=document.querySelector('.add-rule');if(b)b.disabled=cards().filter(c=>c.dataset.used==='1').length>=8}
function addRule(){const c=cards().find(x=>x.dataset.used!=='1');if(!c)return;c.classList.remove('is-free');c.classList.add('open');c.querySelector('.rule-name').value='Правило '+(Number(c.dataset.slot)+1);c.querySelector('input[name^=en]').checked=true;c.dataset.used='1';ruleDirty(c);c.querySelector('.rule-name').focus();refreshAdd()}
function ruleData(c){const values=[];function add(k,v){values.push(encodeURIComponent(k)+'='+encodeURIComponent(String(v)))}add('slot',c.dataset.slot);add('nm',c.querySelector('.rule-name').value);add('en',c.querySelector('input[name^=en]').checked?'1':'0');['tr','co','ct','ms','pr'].forEach(function(p){add(p,c.querySelector('[name^='+p+']').value)});c.querySelectorAll('.act:not(.is-hidden)').forEach(function(a){const k=a.dataset.idx;add('ac'+k,a.querySelector('.ev-action').value);add('va'+k,a.querySelector('[name^=va]').value)});return values.join('&')}
function ruleSig(c){const values=[];function add(k,v){values.push(k+'='+encodeURIComponent(String(v)))}add('nm',c.querySelector('.rule-name').value);add('en',c.querySelector('input[name^=en]').checked?'1':'0');['tr','co','ct','ms','pr'].forEach(p=>add(p,c.querySelector('[name^='+p+']').value));c.querySelectorAll('.act:not(.is-hidden)').forEach(a=>{const k=a.dataset.idx;add('ac'+k,a.querySelector('.ev-action').value);add('va'+k,a.querySelector('[name^=va]').value)});return values.join('&')}
function ruleMsg(c,ok,t){let m=c.querySelector('.rule-msg');if(!m){m=document.createElement('div');m.className='rule-msg';c.querySelector('.rule-tools').parentNode.insertBefore(m,c.querySelector('.rule-tools').nextSibling)}m.textContent=t;m.classList.toggle('ok',!!ok);m.classList.toggle('err',!ok);if(t)setTimeout(function(){if(m.textContent===t){m.textContent='';m.classList.remove('ok','err')}},6000)}
function ruleDirty(c){if(!c.dataset.orig)return;c.classList.toggle('dirty',ruleSig(c)!==c.dataset.orig)}
function ruleBusy(c,busy){c.dataset.busy=busy?'1':'0';c.querySelectorAll('.rule-tools button,.add-action,.remove-action').forEach(function(b){b.disabled=busy});const save=c.querySelector('.save-rule');if(save)save.textContent=busy?'Сохранение…':'✓ Сохранить'}
async function saveRules(slot){const c=document.querySelector('.rule[data-slot="'+slot+'"]'),name=c.querySelector('.rule-name');if(c.dataset.busy==='1')return;name.value=name.value.trim();if(!name.reportValidity())return;ruleBusy(c,true);try{const r=await fetch('/settings/events/rule/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8','Cache-Control':'no-cache'},cache:'no-store',body:ruleData(c)}),data=await r.json();ruleMsg(c,r.ok,data.message||'Неизвестный ответ контроллера.');if(r.ok){name.value=data.name;c.dataset.used='1';c.dataset.orig=ruleSig(c);c.classList.remove('dirty');c.classList.add('open');c.querySelector('.rule-head strong').textContent=data.name;c.querySelector('.badge').textContent=data.enabled?'вкл':'выкл'}refreshAdd()}catch(e){ruleMsg(c,false,'Ошибка сети или ответа контроллера: '+e)}finally{ruleBusy(c,false)}}
function resetDeletedCard(c){const slot=Number(c.dataset.slot);c.querySelector('.rule-name').value='Правило '+(slot+1);c.querySelector('input[name^=en]').checked=false;c.querySelector('[name^=tr]').value=0;c.querySelector('[name^=co]').value=0;c.querySelector('[name^=ct]').value=1;c.querySelector('[name^=ms]').value=700;c.querySelector('[name^=pr]').value=0;c.querySelectorAll('.act').forEach(function(a){a.querySelector('.ev-device').value=0;a.querySelector('.ev-action').value=0;a.querySelector('.action-value').value=0;if(a.dataset.idx!=='0')a.classList.add('is-hidden')});c.querySelector('.add-action').classList.remove('is-hidden');conditionUi(c);c.querySelector('.rule-head strong').textContent='Правило '+(slot+1);c.querySelector('.badge').textContent='выкл';c.dataset.orig=ruleSig(c)}
async function deleteRule(slot){const c=document.querySelector('.rule[data-slot="'+slot+'"]');if(c.dataset.busy==='1'||!confirm('Удалить правило?'))return;ruleBusy(c,true);try{const r=await fetch('/settings/events/rule/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8','Cache-Control':'no-cache'},cache:'no-store',body:'slot='+encodeURIComponent(slot)}),data=await r.json();ruleMsg(c,r.ok,data.message||'Неизвестный ответ контроллера.');if(r.ok){c.dataset.used='0';resetDeletedCard(c);c.classList.add('is-free');c.classList.remove('open','dirty')}refreshAdd()}catch(e){ruleMsg(c,false,'Ошибка сети или ответа контроллера: '+e)}finally{ruleBusy(c,false)}}
cards().forEach(function(c){conditionUi(c);c.dataset.orig=ruleSig(c);c.addEventListener('input',function(){ruleDirty(c)});c.addEventListener('change',function(e){if(e.target.matches('.ev-device'))evDevice(e.target);else if(e.target.matches('.ev-action'))evAction(e.target);else if(e.target.matches('.condition-select'))conditionUi(c);ruleDirty(c)});c.querySelector('.rule-head').addEventListener('click',function(){c.classList.toggle('open')});c.addEventListener('click',function(e){const b=e.target.closest('[data-action]');if(!b)return;const action=b.dataset.action;if(action==='add-action')evShowAct(c);else if(action==='remove-action')evHideAct(b.closest('.act'));else if(action==='save-rule')saveRules(c.dataset.slot);else if(action==='delete-rule')deleteRule(c.dataset.slot)})});
const addRuleButton=document.querySelector('[data-action="add-rule"]');if(addRuleButton)addRuleButton.addEventListener('click',addRule);document.querySelectorAll('.act').forEach(r=>evFilter(r,Number(r.querySelector('.ev-device').value)));refreshAdd();
</script></body></html>)rawliteral";
  server.sendContent(html);
  server.sendContent("");
}

void handleEventsSave(){
  EventRule next[EVENT_MAX_RULES];memset(next,0,sizeof(next));
  char nextNames[EVENT_MAX_RULES][USER_LABEL_SIZE] = {};
  String errors="",warnings="";
  for(int i=0;i<EVENT_MAX_RULES;i++){
    EventRule &r=next[i];
    String ruleLabel=server.arg("nm"+String(i));
    if(!normalizeUserLabel(ruleLabel)) errors+="Правило "+String(i+1)+": название должно содержать от 1 до 64 символов, без < и >\n";
    else {
      setUserLabel(nextNames[i],ruleLabel);
      for(int j=0;j<i;j++) if(String(nextNames[j]).length()&&ruleLabel.equalsIgnoreCase(String(nextNames[j]))) errors+="Названия правил не должны повторяться: "+ruleLabel+"\n";
    }
    r.enabled=server.hasArg("en"+String(i));
    r.trigger=server.arg("tr"+String(i)).toInt();
    r.condition=server.arg("co"+String(i)).toInt();
    r.count=server.arg("ct"+String(i)).toInt();
    r.intervalMs=server.arg("ms"+String(i)).toInt();
    r.priority=server.arg("pr"+String(i)).toInt();
    // Сбор непустых результатов и уплотнение в начало списка без дыр.
    int nActs=0;
    for(int k=0;k<EVENT_MAX_ACTIONS;k++){
      uint8_t a=server.arg("ac"+String(i)+"_"+String(k)).toInt();
      int16_t v=server.arg("va"+String(i)+"_"+String(k)).toInt();
      if(a!=EV_NO_ACTION){
        if(nActs<k)warnings+="Правило "+String(i+1)+": результат "+String(k+1)+" перемещён в слот "+String(nActs+1)+"\n";
        r.actions[nActs]=a;r.actionValues[nActs]=v;nActs++;
      }
    }
    if(r.enabled&&nActs==0)errors+="Правило "+String(i+1)+": добавьте хотя бы один результат\n";
    for(int k=0;k<nActs;k++){
      if(r.enabled&&(r.actions[k]<1||r.actions[k]>EV_ACTION_MAX))errors+="Правило "+String(i+1)+": неверное действие в результате "+String(k+1)+"\n";
      if(r.enabled&&r.actions[k]==EV_PAS_SET_LEVEL&&(r.actionValues[k]<0||r.actionValues[k]>pasLevelsCount))errors+="Правило "+String(i+1)+": уровень PAS в результате "+String(k+1)+" 0.."+String(pasLevelsCount)+"\n";
      if(r.enabled&&(r.actions[k]==EV_LIGHT_BLINK||r.actions[k]==EV_DRL_BLINK)&&(r.actionValues[k]<100||r.actionValues[k]>5000))errors+="Правило "+String(i+1)+": BLINK должен быть 100..5000 мс\n";
      for(int j=0;j<k;j++)if(r.actions[j]==r.actions[k])warnings+="Правило "+String(i+1)+": действие повторяется в результатах "+String(j+1)+" и "+String(k+1)+"\n";
    }
    if(r.enabled&&(r.trigger<1||r.trigger>EV_TRIGGER_MAX))errors+="Правило "+String(i+1)+": неверный триггер\n";
    if(r.condition==EV_PRESS_COUNT&&(r.count<1||r.count>20))errors+="Правило "+String(i+1)+": количество 1..20\n";
    if(r.trigger==EV_PAS_LEVEL&&(r.count<1||r.count>pasLevelsCount))errors+="Правило "+String(i+1)+": уровень PAS 1.."+String(pasLevelsCount)+"\n";
    if(r.enabled&&(r.trigger==EV_PAS_LEVEL||r.trigger==EV_PAS_ON||r.trigger==EV_PAS_OFF||r.trigger==EV_CRUISE_ON||r.trigger==EV_CRUISE_OFF||r.trigger==EV_BOOT)&&r.condition!=EV_NONE)errors+="Правило "+String(i+1)+": для этого триггера условие не используется\n";
    if(r.enabled&&r.trigger!=EV_BOOT&&r.intervalMs<50)errors+="Правило "+String(i+1)+": интервал не меньше 50 мс\n";
    for(int j=0;j<i;j++)if(r.enabled&&next[j].enabled&&r.trigger==next[j].trigger&&r.condition==next[j].condition)warnings+="Дублируются правила "+String(j+1)+" и "+String(i+1)+"\n";
  }
  if(errors.length()){server.send(400,"text/plain","Ошибки, правила не сохранены:\n"+errors);return;}
  memcpy(eventRules,next,sizeof(eventRules));memcpy(eventRuleNames,nextNames,sizeof(eventRuleNames));memset(eventRuntime,0,sizeof(eventRuntime));eventSettingsSave();
  server.send(200,"text/plain","Правила сохранены и применены без перезагрузки."+(warnings.length()?"\nПредупреждения:\n"+warnings:""));
}
void handleEventRuleSave(){
  int slot=server.arg("slot").toInt();
  if(!server.hasArg("slot")||slot<0||slot>=EVENT_MAX_RULES){server.send(400,"application/json","{\"message\":\"Неверный слот правила.\"}");return;}
  EventRule r={}; String errors="";
  String label=server.arg("nm");
  if(!normalizeUserLabel(label))errors+="Название должно содержать от 1 до 64 символов, без < и >.\n";
  for(int i=0;i<EVENT_MAX_RULES;i++)if(i!=slot&&eventRules[i].trigger!=EV_NONE&&label.equalsIgnoreCase(eventRuleName(i)))errors+="Название правила уже используется.\n";
  r.enabled=server.arg("en")=="1";
  r.trigger=server.arg("tr").toInt(); r.condition=server.arg("co").toInt();
  r.count=server.arg("ct").toInt(); r.intervalMs=server.arg("ms").toInt(); r.priority=server.arg("pr").toInt();
  int nActs=0;
  for(int k=0;k<EVENT_MAX_ACTIONS;k++){
    uint8_t a=server.arg("ac"+String(k)).toInt(); int16_t v=server.arg("va"+String(k)).toInt();
    if(a!=EV_NO_ACTION&&nActs<EVENT_MAX_ACTIONS){r.actions[nActs]=a;r.actionValues[nActs]=v;nActs++;}
  }
  if(r.trigger<1||r.trigger>EV_TRIGGER_MAX)errors+="Выберите триггер.\n";
  if(nActs==0)errors+="Добавьте хотя бы одно действие.\n";
  if(r.condition==EV_PRESS_COUNT&&(r.count<1||r.count>20))errors+="Количество должно быть 1..20.\n";
  if(r.trigger==EV_PAS_LEVEL&&(r.count<1||r.count>pasLevelsCount))errors+="Уровень PAS должен быть 1.."+String(pasLevelsCount)+".\n";
  if((r.trigger==EV_PAS_LEVEL||r.trigger==EV_PAS_ON||r.trigger==EV_PAS_OFF||r.trigger==EV_CRUISE_ON||r.trigger==EV_CRUISE_OFF||r.trigger==EV_BOOT)&&r.condition!=EV_NONE)errors+="Для этого триггера условие не используется.\n";
  if(r.trigger!=EV_BOOT&&r.intervalMs<50)errors+="Интервал должен быть не меньше 50 мс.\n";
  for(int k=0;k<nActs;k++){
    if(r.actions[k]<1||r.actions[k]>EV_ACTION_MAX)errors+="Неверное действие.\n";
    if(r.actions[k]==EV_PAS_SET_LEVEL&&(r.actionValues[k]<0||r.actionValues[k]>pasLevelsCount))errors+="Уровень PAS в действии должен быть 0.."+String(pasLevelsCount)+".\n";
    if((r.actions[k]==EV_LIGHT_BLINK||r.actions[k]==EV_DRL_BLINK)&&(r.actionValues[k]<100||r.actionValues[k]>5000))errors+="BLINK должен быть 100..5000 мс.\n";
  }
  if(errors.length()){server.send(400,"application/json","{\"message\":\""+jsonEscape(errors)+"\"}");return;}
  EventRule previousRule=eventRules[slot];
  char previousName[USER_LABEL_SIZE]; memcpy(previousName,eventRuleNames[slot],sizeof(previousName));
  eventRules[slot]=r; setUserLabel(eventRuleNames[slot],label);
  if(!eventSettingsSave()){
    eventRules[slot]=previousRule; memcpy(eventRuleNames[slot],previousName,sizeof(previousName));
    server.send(500,"application/json","{\"message\":\"Ошибка записи настроек в память ESP32.\"}");return;
  }
  memset(&eventRuntime[slot],0,sizeof(eventRuntime[slot]));
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json","{\"slot\":"+String(slot)+",\"name\":\""+jsonEscape(eventRuleName(slot))+"\",\"enabled\":"+String(r.enabled?"true":"false")+",\"message\":\"Правило сохранено и применено.\"}");
}
void handleEventRuleDelete(){
  int slot=server.arg("slot").toInt();
  if(!server.hasArg("slot")||slot<0||slot>=EVENT_MAX_RULES){server.send(400,"application/json","{\"message\":\"Неверный слот правила.\"}");return;}
  EventRule previousRule=eventRules[slot];
  char previousName[USER_LABEL_SIZE]; memcpy(previousName,eventRuleNames[slot],sizeof(previousName));
  memset(&eventRules[slot],0,sizeof(eventRules[slot])); memset(&eventRuleNames[slot],0,sizeof(eventRuleNames[slot]));
  if(!eventSettingsSave()){
    eventRules[slot]=previousRule; memcpy(eventRuleNames[slot],previousName,sizeof(previousName));
    server.send(500,"application/json","{\"message\":\"Ошибка записи настроек в память ESP32.\"}");return;
  }
  memset(&eventRuntime[slot],0,sizeof(eventRuntime[slot]));
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json","{\"message\":\"Правило удалено.\"}");
}
void handleEventsReset(){eventSettingsReset();memset(eventRuntime,0,sizeof(eventRuntime));server.sendHeader("Location","/settings/events");server.send(303,"text/plain","");}

void handleSystemFactoryReset() {
  // Сначала блокируем контур управления и принудительно обнуляем выход газа.
  factoryResetInProgress = true;
  setThrottleOutputSafeZero();
  hwThrottleOutV = 0.0f;
  hwMotorOutPct = 0.0f;

  esp_err_t err = nvs_flash_erase();
  if (err == ESP_OK) err = nvs_flash_init();
  if (err != ESP_OK) {
    Serial.printf("Factory reset NVS error: 0x%x (%s)\n", err, esp_err_to_name(err));
    factoryResetInProgress = false;
    server.send(500, "text/plain", "Ошибка сброса настроек. Устройство не перезагружено.");
    return;
  }

  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "Заводские настройки восстановлены. Перезагрузка...");
  delay(1500);
  ESP.restart();
}

// ================= Веб: Система (экспорт/импорт настроек, OTA) =================
void handleSystemPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Система</title>
<style>
)rawliteral" + getTopBarCss() + getSettingsCss() + R"rawliteral(
a.back{display:inline-block;margin-top:20px}.ui-panel{background:var(--ui-card);border:1px solid var(--ui-border);border-radius:8px;padding:12px;margin-bottom:15px}.ui-panel-title{font-weight:bold;margin-bottom:8px;color:var(--ui-accent)}.ui-toggle{display:flex;align-items:center;gap:10px;cursor:pointer;margin:0 0 10px}.ui-toggle:last-child{margin-bottom:0}.ui-toggle input{width:auto;cursor:pointer}.import-form{margin-top:10px}.file-input{display:none}.danger-panel{border-color:var(--ui-danger);margin-top:16px}.danger-panel .ui-panel-title{color:var(--ui-danger)}.danger-button{width:100%;background:var(--ui-danger);border-color:var(--ui-danger);color:#fff}.danger-button:hover{filter:brightness(1.08)}.danger-button:disabled{opacity:.6;cursor:wait}.reset-status{min-height:1.2em;margin:10px 0 0;color:var(--ui-muted)}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a class="back" href="/">&larr; Меню</a></p>
<h1>Система</h1>
<div class="ui-panel">
  <div class="ui-panel-title">Интерфейс</div>
  <label class="ui-toggle">
    <input type="checkbox" id="chkShowTopbar" onchange="toggleTopbar(this.checked)">
    <span>Верхняя панель статуса (Top Bar)</span>
  </label>
  <label class="ui-toggle">
    <input type="checkbox" id="chkShowMatrix" onchange="toggleUiItem('ui_show_matrix', this.checked)">
    <span>LED Matrix дисплей (16&times;32)</span>
  </label>
  <label class="ui-toggle">
    <input type="checkbox" id="chkShowControls" onchange="toggleUiItem('ui_show_controls', this.checked)">
    <span>Кнопки эмуляции (Тормоз, Педали, Газ)</span>
  </label>
  <label class="ui-toggle">
    <input type="checkbox" id="chkShowScreen" onchange="toggleUiItem('ui_show_screen', this.checked)">
    <span>Экранчик джойстика</span>
  </label>
  <label class="ui-toggle">
    <input type="checkbox" id="chkShowDpad" onchange="toggleUiItem('ui_show_dpad', this.checked)">
    <span>Кнопки джойстика (D-Pad)</span>
  </label>
</div>
<a class="card" href="/update">Обновить прошивку (.bin) &rarr;</a>
<a class="card" href="#" onclick="exportSettings(); return false;">Экспортировать настройки (.json) &rarr;</a>
<form id="importForm" class="import-form" enctype="multipart/form-data" method="post" action="/system/import">
  <label for="settingsFile" class="card">Импортировать настройки (.json) &rarr;</label>
  <input type="file" class="file-input" id="settingsFile" name="settingsFile" accept=".json" onchange="importSettings(this)">
</form>
<div class="ui-panel danger-panel">
  <div class="ui-panel-title">Заводские настройки</div>
  <p class="hint">Будут удалены все настройки устройства: калибровки газа и PAS, круиз, правила событий, распиновка, WiFi и параметры точки доступа.</p>
  <form id="factoryResetForm" method="post" action="/system/factory-reset" onsubmit="return factoryReset(event)">
    <button id="factoryResetButton" class="danger-button" type="submit">Возврат к заводским настройкам</button>
  </form>
  <p id="factoryResetStatus" class="reset-status" aria-live="polite"></p>
</div>

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

function factoryReset(event) {
  event.preventDefault();
  const warning = 'Сбросить ВСЕ настройки к заводским? Будут удалены калибровки газа и PAS, круиз, правила событий, распиновка, WiFi и параметры точки доступа. Действие необратимо.';
  if (!confirm(warning)) return false;

  const button = document.getElementById('factoryResetButton');
  const status = document.getElementById('factoryResetStatus');
  button.disabled = true;
  status.textContent = 'Выполняется сброс...';

  fetch('/system/factory-reset', { method: 'POST' })
    .then(async response => {
      const message = await response.text();
      if (!response.ok) throw new Error(message || 'Не удалось выполнить сброс');
      ['ui_show_topbar', 'ui_show_matrix', 'ui_show_controls', 'ui_show_screen', 'ui_show_dpad']
        .forEach(key => localStorage.removeItem(key));
      status.textContent = message;
      alert(message);
    })
    .catch(error => {
      button.disabled = false;
      status.textContent = 'Ошибка: ' + error.message;
      alert(status.textContent);
    });
  return false;
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
  json += "\"cnt\":" + String(constrain(pasLevelsCount, 0, PAS_MAX_LEVELS)) + ",";
  json += "\"curLvl\":" + String(pasCurrentLevel) + ",";
  json += "\"ssEn\":" + String(pasSoftStartEnabled ? 1 : 0) + ",";
  json += "\"spEn\":" + String(pasSoftStopEnabled ? 1 : 0) + ",";
  json += "\"ssMs\":" + String(pasSoftStartMs) + ",";
  json += "\"spMs\":" + String(pasSoftStopMs) + ",";
  json += "\"enabled\":" + String(pasEnabled ? 1 : 0) + ",";
  json += "\"pct\":[";
  int safePasLevelsCount = constrain(pasLevelsCount, 0, PAS_MAX_LEVELS);
  for (int i = 0; i < safePasLevelsCount; i++) {
    json += String(pasLevelPercent[i]);
    if (i < safePasLevelsCount - 1) json += ",";
  }
  json += "]";
  json += "},";
  json += "\"customPins\":[";
  bool firstCustom = true;
  for (int i = 0; i < CUSTOM_PIN_MAX; i++) {
    if (!customPins[i].used) continue;
    if (!firstCustom) json += ",";
    firstCustom = false;
    json += "{\"nm\":\"" + jsonEscape(String(customPins[i].name)) + "\",\"gpio\":" + String(customPins[i].gpio) + ",\"mode\":" + String(customPins[i].mode) + "}";
  }
  json += "],";
  json += "\"pinNames\":[";
  for (int i = 0; i < PIN_ROLE_COUNT; i++) { if (i) json += ","; json += "\"" + jsonEscape(pinRoleName(i)) + "\""; }
  json += "],";
  json += "\"wifi\":{";
  json += "\"ssid\":\"" + jsonEscape(storedSsid) + "\",";
  json += "\"pass\":\"" + jsonEscape(storedPass) + "\"";
  json += "},";
  json += "\"events\":{";
  json += "\"service\":" + String(serviceModeActive ? 1 : 0) + ",\"rules\":[";
  for (int i = 0; i < EVENT_MAX_RULES; i++) {
    EventRule &r = eventRules[i];
    json += "{\"name\":\"" + jsonEscape(eventRuleName(i)) + "\",\"en\":" + String(r.enabled ? 1 : 0) + ",\"tr\":" + String(r.trigger) + ",\"co\":" + String(r.condition) + ",\"pr\":" + String(r.priority) + ",\"ct\":" + String(r.count) + ",\"ms\":" + String(r.intervalMs) + ",\"ac\":" + String(r.actions[0]) + ",\"va\":" + String(r.actionValues[0]) + ",\"acts\":[";
    int nActs = 0;
    for (int k = 0; k < EVENT_MAX_ACTIONS; k++) {
      if (r.actions[k] == EV_NO_ACTION) continue;
      if (nActs++) json += ",";
      json += "{\"ac\":" + String(r.actions[k]) + ",\"va\":" + String(r.actionValues[k]) + "}";
    }
    json += "]}";
    if (i < EVENT_MAX_RULES - 1) json += ",";
  }
  json += "]},";
  json += "\"ap\":{";
  json += "\"ssid\":\"" + jsonEscape(storedApSsid) + "\",";
  json += "\"pass\":\"" + jsonEscape(storedApPass) + "\"";
  json += "},";
  json += "\"cruise\":{"; // New Cruise Control settings section
  json += "\"cnt\":" + String(constrain(cruiseLevelsCount, 0, CRUISE_MAX_LEVELS)) + ",";
  json += "\"stPct\":" + String(cruiseStartPercent) + ",";
  json += "\"endPct\":" + String(cruiseEndPercent) + ",";
  json += "\"pct\":[";
  int safeCruiseLevelsCount = constrain(cruiseLevelsCount, 0, CRUISE_MAX_LEVELS);
  for (int i = 0; i < safeCruiseLevelsCount; i++) {
    json += String(cruiseLevelPercent[i]);
    if (i < safeCruiseLevelsCount - 1) json += ",";
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

// ================= Безопасный разбор импортируемых настроек =================
// Импортируемый файл приходит от пользователя, поэтому все строки и числа
// разбираются с проверкой границ: отсутствующая запятая, кавычка или '}'
// не должны приводить к substring(-1) или выходу за границы массивов.
const size_t SETTINGS_IMPORT_MAX_BYTES = 16384;

double jsonNumberAfter(const String &src, const char *key, bool &ok) {
  ok = false;
  int p = src.indexOf(key);
  if (p == -1) return 0;
  p += strlen(key);
  int end = p;
  while (end < (int)src.length()) {
    char c = src.charAt(end);
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') { end++; continue; }
    break;
  }
  if (end == p) return 0;
  ok = true;
  return src.substring(p, end).toDouble();
}

int jsonIntAfter(const String &src, const char *key, bool &ok) {
  double v = jsonNumberAfter(src, key, ok);
  return (int)lround(v);
}

String jsonStringAfter(const String &src, const char *key, bool &ok) {
  ok = false;
  int p = src.indexOf(key);
  if (p == -1) return "";
  p += strlen(key);
  if (p >= (int)src.length() || src.charAt(p) != '"') return "";
  p++;
  String out;
  for (; p < (int)src.length(); p++) {
    char c = src.charAt(p);
    if (c == '\\') {
      if (p + 1 < (int)src.length()) {
        char n = src.charAt(p + 1);
        if (n == '"' || n == '\\' || n == '/') out += n;
        else if (n == 'n') out += '\n';
        else if (n == 'r') out += '\r';
        else if (n == 't') out += '\t';
        else out += n;
        p++;
      }
      continue;
    }
    if (c == '"') { ok = true; return out; }
    out += c;
  }
  return "";
}

bool extractJsonObject(const String &src, const char *key, String &out) {
  int keyPos = src.indexOf(key);
  if (keyPos == -1) return false;
  int open = src.indexOf('{', keyPos);
  if (open == -1) return false;
  int depth = 0;
  for (int i = open; i < (int)src.length(); i++) {
    char c = src.charAt(i);
    if (c == '{') depth++;
    else if (c == '}') {
      depth--;
      if (depth == 0) { out = src.substring(open + 1, i); return true; }
    }
  }
  return false;
}

// Читает "pct":[...] в массив float, обрезая значения до 0..100.
// Возвращает количество разобранных значений.
int readPercentArray(const String &src, float *dst, int maxCount) {
  int arrPos = src.indexOf('[');
  if (arrPos == -1) return 0;
  int arrEnd = src.indexOf(']', arrPos);
  if (arrEnd == -1) return 0;
  String body = src.substring(arrPos + 1, arrEnd);
  int count = 0;
  int pos = 0;
  while (pos <= (int)body.length() && count < maxCount) {
    int comma = body.indexOf(',', pos);
    if (comma == -1) comma = body.length();
    String item = body.substring(pos, comma);
    item.trim();
    if (item.length()) {
      bool parsed = false;
      jsonNumberAfter(String("v:") + item, "v:", parsed);
      if (parsed) dst[count++] = constrain((float)item.toDouble(), 0.0f, 100.0f);
    }
    if (comma >= (int)body.length()) break;
    pos = comma + 1;
  }
  return count;
}

int readPercentArray(const String &src, int *dst, int maxCount) {
  float values[PAS_MAX_LEVELS];
  int count = readPercentArray(src, values, min(maxCount, PAS_MAX_LEVELS));
  for (int i = 0; i < count; i++) dst[i] = (int)lround(values[i]);
  return count;
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
  if (fileContent.length() > SETTINGS_IMPORT_MAX_BYTES) {
    server.send(413, "text/plain", "Файл настроек слишком большой");
    return;
  }

  int idx = fileContent.indexOf("\"throttle\":{");
  if (idx != -1) {
    String throttleJson;
    if (extractJsonObject(fileContent, "\"throttle\":{", throttleJson)) {
      bool ok = false;
      double v = jsonNumberAfter(throttleJson, "\"inMinV\":", ok);
      if (ok) throttleInMinV = constrain((float)v, 0.0f, 4.2f);
      v = jsonNumberAfter(throttleJson, "\"inMaxV\":", ok);
      if (ok) throttleInMaxV = constrain((float)v, 0.0f, 4.2f);
      v = jsonNumberAfter(throttleJson, "\"outMinV\":", ok);
      if (ok) throttleOutMinV = constrain((float)v, 0.0f, 4.2f);
      v = jsonNumberAfter(throttleJson, "\"outMaxV\":", ok);
      if (ok) throttleOutMaxV = constrain((float)v, 0.0f, 4.2f);
      v = jsonNumberAfter(throttleJson, "\"divRatio\":", ok);
      if (ok) throttleInputDividerRatio = constrain((float)v, 0.01f, 1.0f);
      v = jsonNumberAfter(throttleJson, "\"gain\":", ok);
      if (ok) throttleOutputGain = constrain((float)v, 0.01f, 10.0f);
      int iv = jsonIntAfter(throttleJson, "\"ssMs\":", ok);
      if (ok && iv >= 0) throttleSoftStartMs = (unsigned long)iv;
      iv = jsonIntAfter(throttleJson, "\"spMs\":", ok);
      if (ok && iv >= 0) throttleSoftStopMs = (unsigned long)iv;
      iv = jsonIntAfter(throttleJson, "\"ssEn\":", ok);
      if (ok) throttleSoftStartEnabled = iv != 0;
      iv = jsonIntAfter(throttleJson, "\"spEn\":", ok);
      if (ok) throttleSoftStopEnabled = iv != 0;
      // Импорт не может отключить обязательное отключение по тормозу.
      ownBrakeCutoffEnabled = true;
      throttleSettingsSave();
    }
  }
  idx = fileContent.indexOf("\"pas\":{");
  if (idx != -1) {
    String pasJson;
    if (extractJsonObject(fileContent, "\"pas\":{", pasJson)) {
      bool ok = false;
      int value = jsonIntAfter(pasJson, "\"magnets\":", ok);
      if (ok) pasMagnetCount = constrain(value, 1, 1000);
      value = jsonIntAfter(pasJson, "\"edge\":", ok);
      if (ok && (value == RISING || value == FALLING || value == CHANGE)) pasEdgeMode = value;
      value = jsonIntAfter(pasJson, "\"angle\":", ok);
      if (ok) pasActivationAngle = constrain(value, 1, 360);
      value = jsonIntAfter(pasJson, "\"timeout\":", ok);
      if (ok) pasTimeoutMs = (unsigned long)constrain(value, 20, 60000);
      value = jsonIntAfter(pasJson, "\"stopTO\":", ok);
      if (ok) pasStopTimeoutMs = (unsigned long)constrain(value, 20, 60000);
      value = jsonIntAfter(pasJson, "\"cnt\":", ok);
      if (ok) pasLevelsCount = constrain(value, 0, PAS_MAX_LEVELS);
      value = jsonIntAfter(pasJson, "\"curLvl\":", ok);
      if (ok) pasCurrentLevel = constrain(value, 0, pasLevelsCount);
      value = jsonIntAfter(pasJson, "\"ssEn\":", ok);
      if (ok) pasSoftStartEnabled = value != 0;
      value = jsonIntAfter(pasJson, "\"spEn\":", ok);
      if (ok) pasSoftStopEnabled = value != 0;
      value = jsonIntAfter(pasJson, "\"ssMs\":", ok);
      if (ok && value >= 0) pasSoftStartMs = (unsigned long)value;
      value = jsonIntAfter(pasJson, "\"spMs\":", ok);
      if (ok && value >= 0) pasSoftStopMs = (unsigned long)value;
      int pctPos = pasJson.indexOf("\"pct\":[");
      if (pctPos != -1) readPercentArray(pasJson.substring(pctPos + 6), pasLevelPercent, pasLevelsCount);
      pasCurrentLevel = constrain(pasCurrentLevel, 0, pasLevelsCount);
      pasSettingsSave();
      reattachPasInterrupt();
    }
  }
  idx = fileContent.indexOf("\"wifi\":{");
  if (idx != -1) {
    String wifiJson;
    if (extractJsonObject(fileContent, "\"wifi\":{", wifiJson)) {
      bool ssidOk = false, passOk = false;
      String importedSsid = jsonStringAfter(wifiJson, "\"ssid\":", ssidOk);
      String importedPass = jsonStringAfter(wifiJson, "\"pass\":", passOk);
      if (ssidOk && importedSsid.length() > 0 && importedSsid.length() <= 32) {
        wifiCredsSave(importedSsid, passOk && importedPass.length() <= 63 ? importedPass : "");
      }
    }
  }

  // --- AP Settings ---
  idx = fileContent.indexOf("\"ap\":{");
  if (idx != -1) {
    String apJson;
    if (extractJsonObject(fileContent, "\"ap\":{", apJson)) {
      bool ssidOk = false, passOk = false;
      String importedApSsid = jsonStringAfter(apJson, "\"ssid\":", ssidOk);
      String importedApPass = jsonStringAfter(apJson, "\"pass\":", passOk);
      if (!passOk) importedApPass = "";
      String error;
      if (ssidOk && validApCredentials(importedApSsid, importedApPass, error)) {
        storedApSsid = importedApSsid;
        storedApPass = importedApPass;
        apSettingsSave();
      }
    }
  }

  // --- Пользовательские названия ролей GPIO ---
  idx = fileContent.indexOf("\"pinNames\":[");
  if (idx != -1) {
    int pos = idx + 12;
    for (int i = 0; i < PIN_ROLE_COUNT; i++) {
      int q1 = fileContent.indexOf('\"', pos), q2 = q1 == -1 ? -1 : fileContent.indexOf('\"', q1 + 1);
      if (q1 == -1 || q2 == -1) break;
      String name = fileContent.substring(q1 + 1, q2);
      if (normalizeUserLabel(name)) setUserLabel(pinRoleNames[i], name);
      pos = q2 + 1;
    }
    pinSettingsSave();
  }

  // --- Дополнительные пользовательские GPIO ---
  idx = fileContent.indexOf("\"customPins\":[");
  if (idx != -1) {
    memset(customPins, 0, sizeof(customPins));
    int pos = idx + 14;
    int curSlot = 0;
    while (curSlot < CUSTOM_PIN_MAX) {
      int objStart = fileContent.indexOf("{", pos);
      if (objStart == -1 || objStart > fileContent.indexOf("]", pos)) break;
      int objEnd = fileContent.indexOf("}", objStart);
      if (objEnd == -1) break;
      String obj = fileContent.substring(objStart + 1, objEnd);
      int nmPos = obj.indexOf("\"nm\":\"");
      int gpPos = obj.indexOf("\"gpio\":");
      int mdPos = obj.indexOf("\"mode\":");
      if (nmPos != -1 && gpPos != -1 && mdPos != -1) {
        String nm = obj.substring(nmPos + 6);
        int qEnd = nm.indexOf("\"");
        if (qEnd != -1) nm = nm.substring(0, qEnd);
        int gpioVal = obj.substring(gpPos + 7).toInt();
        int mdPosEnd = obj.indexOf(",", mdPos);
        String mdStr = obj.substring(mdPos + 7, mdPosEnd == -1 ? obj.length() : mdPosEnd);
        int modeVal = mdStr.toInt();
        if (normalizeUserLabel(nm) && gpioVal >= 0 && gpioVal <= 39 && modeVal >= 0 && modeVal <= 2) {
          customPins[curSlot].used = 1;
          customPins[curSlot].mode = (uint8_t)modeVal;
          customPins[curSlot].gpio = (int16_t)gpioVal;
          strncpy(customPins[curSlot].name, nm.c_str(), USER_LABEL_SIZE - 1);
          customPins[curSlot].name[USER_LABEL_SIZE - 1] = 0;
          curSlot++;
        }
      }
      pos = objEnd + 1;
    }
    pinSettingsSave();
  }

  // --- Event Constructor Settings ---
  idx = fileContent.indexOf("\"events\":{");
  if (idx != -1) {
    int rulesStart = fileContent.indexOf("\"rules\":[", idx);
    int rulesEnd = fileContent.indexOf("]", rulesStart);
    if (rulesStart != -1 && rulesEnd != -1) {
      String rulesJson = fileContent.substring(rulesStart + 9, rulesEnd);
      int pos = 0;
      for (int i = 0; i < EVENT_MAX_RULES; i++) {
        int end = rulesJson.indexOf('}', pos);
        if (end == -1) break;
        String item = rulesJson.substring(pos, end + 1);
        EventRule &r = eventRules[i];
        memset(r.actions, 0, sizeof(r.actions));
        memset(r.actionValues, 0, sizeof(r.actionValues));
        int p;
        if ((p=item.indexOf("\"name\":\""))!=-1) { String name=item.substring(p+8); int e=name.indexOf('\"'); if(e!=-1){name=name.substring(0,e);if(normalizeUserLabel(name))setUserLabel(eventRuleNames[i],name);} }
        if ((p=item.indexOf("\"en\":"))!=-1) r.enabled=item.substring(p+5).toInt()!=0;
        if ((p=item.indexOf("\"tr\":"))!=-1) r.trigger=item.substring(p+5).toInt();
        if ((p=item.indexOf("\"co\":"))!=-1) r.condition=item.substring(p+5).toInt();
        if ((p=item.indexOf("\"pr\":"))!=-1) r.priority=item.substring(p+5).toInt();
        if ((p=item.indexOf("\"ct\":"))!=-1) r.count=item.substring(p+5).toInt();
        if ((p=item.indexOf("\"ms\":"))!=-1) r.intervalMs=item.substring(p+5).toInt();
        // Новый формат: список результатов "acts":[{"ac":..,"va":..},...]
        int actsPos = item.indexOf("\"acts\":[");
        if (actsPos != -1) {
          int actsEnd = item.indexOf(']', actsPos);
          if (actsEnd != -1) {
            String actsJson = item.substring(actsPos + 8, actsEnd);
            int ap = 0;
            for (int k = 0; k < EVENT_MAX_ACTIONS; k++) {
              int ae = actsJson.indexOf('}', ap);
              if (ae == -1) break;
              String act = actsJson.substring(ap, ae + 1);
              int q;
              if ((q=act.indexOf("\"ac\":"))!=-1) r.actions[k]=act.substring(q+6).toInt();
              if ((q=act.indexOf("\"va\":"))!=-1) r.actionValues[k]=act.substring(q+6).toInt();
              ap = ae + 1;
            }
          }
        } else {
          // Старый формат: одиночное действие переносится в слот 0.
          if ((p=item.indexOf("\"ac\":"))!=-1) r.actions[0]=item.substring(p+5).toInt();
          if ((p=item.indexOf("\"va\":"))!=-1) r.actionValues[0]=item.substring(p+5).toInt();
        }
        pos=end+2;
      }
      eventSettingsSave();
    }
  }

  // --- Cruise Control Settings ---
  idx = fileContent.indexOf("\"cruise\":{");
  if (idx != -1) {
    String cruiseJson;
    if (extractJsonObject(fileContent, "\"cruise\":{", cruiseJson)) {
      bool ok = false;
      int value = jsonIntAfter(cruiseJson, "\"cnt\":", ok);
      if (ok) cruiseLevelsCount = constrain(value, 0, CRUISE_MAX_LEVELS);
      double number = jsonNumberAfter(cruiseJson, "\"stPct\":", ok);
      if (ok) cruiseStartPercent = constrain((float)number, 0.0f, 100.0f);
      number = jsonNumberAfter(cruiseJson, "\"endPct\":", ok);
      if (ok) cruiseEndPercent = constrain((float)number, 0.0f, 100.0f);
      value = jsonIntAfter(cruiseJson, "\"ssEn\":", ok);
      if (ok) cruiseSoftStartEnabled = value != 0;
      value = jsonIntAfter(cruiseJson, "\"spEn\":", ok);
      if (ok) cruiseSoftStopEnabled = value != 0;
      value = jsonIntAfter(cruiseJson, "\"ssMs\":", ok);
      if (ok && value >= 0) cruiseSoftStartMs = (unsigned long)value;
      value = jsonIntAfter(cruiseJson, "\"spMs\":", ok);
      if (ok && value >= 0) cruiseSoftStopMs = (unsigned long)value;
      int pctPos = cruiseJson.indexOf("\"pct\":[");
      if (pctPos != -1) readPercentArray(cruiseJson.substring(pctPos + 6), cruiseLevelPercent, cruiseLevelsCount);
      cruiseCurrentLevel = constrain(cruiseCurrentLevel, 0, cruiseLevelsCount);
      cruiseSettingsSave();
    }
  }

  server.send(200, "text/plain", "Настройки успешно импортированы. Перезагрузка...");
  delay(1500);
  ESP.restart();
}
