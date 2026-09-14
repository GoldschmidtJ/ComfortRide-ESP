#include "nvs_flash.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <LittleFS.h>
#include <math.h>

#include "core/light_logic.h" // чистая логика света/поворотников (юнит-тесты используют тот же файл)
#include "web/web_ui.h"       // UI компоненты (CSS, HTML, JS)
#include "system/storage.h"      // Работа с NVS (сохранение/загрузка настроек)
#include "system/network.h"      // Сетевой модуль (WiFi, mDNS, Captive Portal)
#include "core/lights.h"       // Управление освещением (фара, ДХО, поворотники)
#include "core/pas.h"          // PAS-датчик педалей
#include "core/throttle.h"     // Управление газом
#include "core/cruise.h"       // Круиз-контроль
#include "system/inputs.h"       // Обработка входов (тормоз, кнопка PAS)
#include "utils/utils.h"        // Утилиты (htmlEscape, jsonEscape и др.)
#include "system/hardware_config.h" // GPIO утилиты и конфигурация пинов
#include "system/events_engine.h"   // Событийный движок (триггеры, правила, действия)
#include "system/peripherals.h"     // Зуммер, гудок, временные иконки дисплея, виртуальные кнопки
#include "web/web_routes.h"   // Регистрация HTTP-маршрутов
#include "web/web_handlers_telemetry.h" // HTTP-телеметрия и захват шины
#include "web/web_handlers_emulation.h" // HTTP-эмуляция и API управления
#include "web/web_handlers_settings.h"  // HTTP-обработчики настроек (WiFi, Throttle, PAS, Cruise)
#include "web/web_handlers_pins.h"      // HTTP-обработчики конструктора пинов
#include "web/web_handlers_events.h"    // HTTP-обработчики конструктора событий
#include "web/web_handlers_system.h"    // HTTP-обработчики системных страниц (экспорт/импорт, статус)

#define FIRMWARE_VERSION "0.3.1"


// ================= НАСТРОЙКА WiFi =================
extern const char* ssid = "Donut";
extern const char* password = "doughnut";
extern const char* MDNS_HOST = "openbike";

// Статический IP для надежного подключения к точке доступа телефона (Android)
extern const IPAddress staticSTAIP(192, 168, 43, 88);
extern const IPAddress staticSTAGateway(192, 168, 43, 1);
extern const IPAddress staticSTASubnet(255, 255, 255, 0);
extern const IPAddress staticSTADNS(192, 168, 43, 1);

DNSServer dnsServer;
const byte DNS_PORT = 53;

// ================= Рабочие переменные пинов =================
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

#define CUSTOM_PIN_MAX 8

WebServer server(80);
Preferences prefs;
String storedApSsid = "BikeControllerAP";
String storedApPass = "";

// Forward declarations
void wifiCredsSave(const String &newSsid, const String &newPass);
void setThrottleOutputSafeZero();
void throttleSettingsSave();
void throttleSettingsLoad();
void pasSettingsSave();
void pasSettingsLoad();
void reattachPasInterrupt();
void handleEmulationPage();
void handleDebugData();
void handleBusCaptureControl();
void handleBusCaptureStatus();
void handleBusCaptureData();
void handleBusCaptureCsv();
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
extern uint8_t lightCycleMode; // конечный автомат света (0=Выкл, 1=ДХО, 2=Ближний, 3=Бл+ДХО)

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

void cruiseSettingsSave();
void cruiseSettingsLoad();

// AP Settings forward declarations
void apSettingsSave();
void apSettingsLoad();

// ================= Сервисный режим (конструктор событий, «проблема 5») =================
// Включается/выключается событиями (см. updateEventEngine): ограничение газа,
// PAS не выше 1 уровня, круиз запрещён. Аппаратный тормоз и fail-safe нуля
// газа на старте не затрагиваются — они приоритетнее любых событий.
bool serviceModeActive = false;
int serviceThrottleLimitPct = 30; // % от рабочего диапазона выхода газа

// ================= Обработка кнопки переключения уровня PAS =================
void handlePasButtonPress() {
  if (updatePasButton()) {
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

// Периферия и виртуальные кнопки вынесены в peripherals.cpp.
// ================= Отладочный буфер и пассивная запись шины =================
extern const int DEBUG_BUFFER_SIZE = 200;
DebugSample debugBuffer[DEBUG_BUFFER_SIZE];
int debugBufferHead = 0;
unsigned long lastDebugSampleMs = 0;
portMUX_TYPE debugBufferMux = portMUX_INITIALIZER_UNLOCKED;
const unsigned long DEBUG_SAMPLE_INTERVAL_MS = 100;

extern const int BUS_CAPTURE_PIN = 36;
extern const uint32_t BUS_CAPTURE_CAPACITY = 4096;
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

bool busCapturePinBusy(String *reason) {
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

// Виртуальные кнопки страницы эмуляции: state=down/up — удержание,
// state=pulse — короткое нажатие (автоотпуск через 150 мс).



// ================= Setup / Loop =================
// ================= WiFi: сохранённая сеть (сверх дефолтной из кода) =================
// ssid/password вверху файла — это дефолт "из коробки". Если через веб выберешь
// другую сеть — она сохранится в NVS и будет использоваться вместо дефолтной.
String storedSsid = "";
String storedPass = "";

void setup() {
  if (!LittleFS.begin(true)) {
    Serial.println(F("LittleFS: ошибка монтирования"));
  }

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

  // Инициализация модуля освещения (фара, ДХО, поворотники)
  lightsInit(HEADLIGHT_PIN, DRL_PIN);
  lightsSetTurnPins(TURN_LEFT_PIN, TURN_RIGHT_PIN);
  eventSettingsLoad();

  // Настоящий ЦАП ESP32 — ledcAttach больше не нужен, dacWrite() работает сразу

  throttleSettingsLoad();
  pasSettingsLoad();
  apSettingsLoad();

  setThrottleOutputSafeZero();

  reattachPasInterrupt();

  // Инициализация WiFi (через модуль network)
  wifiCredsLoad();
  networkInit();

  cruiseSettingsLoad();

  // Регистрация всех HTTP-маршрутов веб-интерфейса и API
  initWebRoutes(server);
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
    handlePasButtonPress();
    cpuUsPasBtn += micros() - secStart;

    secStart = micros();
    updateThrottle();
    cpuUsThrottle += micros() - secStart;

    // 2. Вспомогательное управление освещением и звуком
    secStart = micros();
    updateVirtualButtons();
    // УДАЛЕНО: updateLightButtons() — кнопки теперь через систему событий
    cpuUsLight += micros() - secStart;

    // События обрабатываются после штатных кнопок, чтобы действие правила
    // не было отменено штатным toggle в том же цикле.
    secStart = micros();
    updateEventEngine();
    updateDisplayIcons(); // Автоматическое скрытие временных иконок (гудок, тормоз)
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
    processCaptiveDns();
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



// ================= Безопасный разбор импортируемых настроек =================
// Импортируемый файл приходит от пользователя, поэтому все строки и числа
// разбираются с проверкой границ: отсутствующая запятая, кавычка или '}'
// не должны приводить к substring(-1) или выходу за границы массивов.
extern const size_t SETTINGS_IMPORT_MAX_BYTES = 16384;

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

extern String jsonStringAfter(const String &src, const char *key, bool &ok) {
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
