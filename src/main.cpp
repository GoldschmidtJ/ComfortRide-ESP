#include "nvs_flash.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include <freertos/FreeRTOS.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
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
#include "utils/json_utils.h"   // Безопасный разбор импортируемых настроек (P2)
#include "system/hardware_config.h" // GPIO утилиты и конфигурация пинов
#include "system/events_engine.h"   // Событийный движок (триггеры, правила, действия)
#include "system/peripherals.h"     // Зуммер, гудок, временные иконки дисплея, виртуальные кнопки
#include "system/joystick.h"        // Физический джойстик (переключение режимов, VRx/VRy/SW)
#include "web/web_routes.h"          // Регистрация HTTP-маршрутов
#include "web/web_handlers_telemetry.h" // HTTP-телеметрия
#include "system/cpu_profile.h"      // Секционный CPU-профайлер (замер загрузки контура) // HTTP-телеметрия и захват шины
#include "web/web_handlers_emulation.h" // HTTP-эмуляция и API управления
#include "web/web_handlers_settings.h"  // HTTP-обработчики настроек (WiFi, Throttle, PAS, Cruise)
#include "web/web_handlers_pins.h"      // HTTP-обработчики конструктора пинов
#include "web/web_handlers_events.h"    // HTTP-обработчики конструктора событий
#include "web/web_handlers_system.h"    // HTTP-обработчики системных страниц (экспорт/импорт, статус)
#include "web/web_handlers_backup.h"    // HTTP-обработчики экспорта/импорта файла настроек
#include "web/web_handlers_update.h"    // HTTP-обработчики OTA-обновления прошивки
#include "system/version.h"             // FIRMWARE_VERSION — единая точка версии
#include "system/debug_capture.h"       // Отладочный буфер и сниффер шины (P3)
#include "system/battery_sag.h"         // Батарейный саг-гард


void criticalControlTask(void *pvParameters);
void nonCriticalTask(void *pvParameters);
// Периферия и виртуальные кнопки вынесены в peripherals.cpp.
// Отладочный буфер и пассивная запись шины перенесены в
// src/system/debug_capture.{h,cpp} (P3 рефакторинга).

// Виртуальные кнопки страницы эмуляции: state=down/up — удержание,
// state=pulse — короткое нажатие (автоотпуск через 150 мс).



// ================= Setup / Loop =================

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

  // Watchdog: сброс если основной цикл зависнет (5 с)
  esp_task_wdt_init(5, true);
  esp_task_wdt_add(NULL);
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

  // Инициализация физического джойстика (переключение режимов: VRx/VRy/SW)
  joystickInit();

  // Инициализация модуля освещения (фара, ДХО, поворотники)
  lightsInit(HEADLIGHT_PIN, DRL_PIN);
  lightsSetTurnPins(TURN_LEFT_PIN, TURN_RIGHT_PIN);
  eventSettingsLoad();

  // Инициализация батарейного саг-гарда
  batterySagInit();
  batterySagLoad();

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
  serviceSettingsLoad();
  odometerLoad();

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
  // Регистрируем задачу в watchdog — сброс если цикл повиснет
  esp_task_wdt_add(NULL);
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(1); // 1 kHz цикл управления

  for (;;) {
    cpuProfileLoopBegin();

    // 1. Наивысший приоритет: Тормоз, Газ, PAS
    cpuProfileBegin(CPU_SEC_PAS);
    updatePasDetection();
    cpuProfileEnd();

    cpuProfileBegin(CPU_SEC_PAS_BTN);
    handlePasButtonPress();
    cpuProfileEnd();

    cpuProfileBegin(CPU_SEC_THROTTLE);
    updateThrottle();
    cpuProfileEnd();

    // 2. Вспомогательное управление освещением и звуком
    cpuProfileBegin(CPU_SEC_LIGHT);
    updateVirtualButtons();
    updateJoystick(); // Физический джойстик: переключение режимов (аналог /api/joystick/apply)
    // УДАЛЕНО: updateLightButtons() — кнопки теперь через систему событий
    cpuProfileEnd();

    // События обрабатываются после штатных кнопок, чтобы действие правила
    // не было отменено штатным toggle в том же цикле.
    cpuProfileBegin(CPU_SEC_THROTTLE);
    updateEventEngine();
    updateDisplayIcons(); // Автоматическое скрытие временных иконок (гудок, тормоз)
    cpuProfileEnd();

    cpuProfileBegin(CPU_SEC_SOUND);
    updateLightBlink();
    updateTurnSignals();
    updateHorn();
    updateBuzzer();
    cpuProfileEnd();

    // Батарейный саг-гард: обновление состояния
    updateBatterySag();

    cpuProfileLoopEnd();

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

