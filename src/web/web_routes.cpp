#include "web/web_routes.h"
#include "web/web_handlers_telemetry.h"

// ================= Экземпляр веб-сервера =================
WebServer server(80);
#include "web/web_handlers_emulation.h"
#include "web/web_handlers_settings.h"
#include "web/web_handlers_pins.h"
#include "web/web_handlers_events.h"
#include "web/web_handlers_system.h"
#include "web/web_handlers_backup.h" // Экспорт/импорт файла настроек (JSON)
#include "web/web_handlers_update.h" // Прошивка по воздуху (OTA)
#include <WebServer.h>
#include "web/static_resources.h"

// Forward declarations обработчиков, которые пока принадлежат основному модулю.
void handleEmulationPage();
void handleApiJoystickApply();
void handleApiButtonPress();
void handleApiPasSetLevel();
void handleApiPasToggleMode();
void handleApiCruiseToggleMode();
void handleThrottlePage();
void handleDrivePage();

void handleThrottleSave();
void handleThrottleCalMin();
void handleThrottleCalMax();
void handlePasPage();
void handlePasSave();
void handlePasCalStart();
void handlePasCalStatus();
void handlePasCalStop();
void handleApSave();
void handleWifiPage();
void handleWifiScan();
void handleWifiSave();
void handleCruisePage();
void handleCruiseSave();

// ================= Регистрация всех HTTP-маршрутов =================
void initWebRoutes(WebServer& server) {
  // Страница / обслуживается static_resources.cpp после завершения регистрации.
  initStaticResources(server);
  
  // Эмуляция дисплея и управление
  server.on("/api/joystick/apply", HTTP_GET, handleApiJoystickApply);
  server.on("/api/buttons/press", HTTP_GET, handleApiButtonPress);
  server.on("/api/pas/set_level", HTTP_GET, handleApiPasSetLevel);
  server.on("/api/pas/toggle_mode", HTTP_GET, handleApiPasToggleMode);
  server.on("/api/cruise/toggle", HTTP_GET, handleApiCruiseToggleMode);
  
  // Настройки газа (throttle)
  // Объединенные настройки управления тягой: Газ + PAS + Круиз
  server.on("/settings/drive", handleDrivePage);


  server.on("/settings/throttle", handleThrottlePage);
  server.on("/settings/throttle/save", HTTP_POST, handleThrottleSave);
  server.on("/settings/throttle/cal_min", HTTP_POST, handleThrottleCalMin);
  server.on("/settings/throttle/cal_max", HTTP_POST, handleThrottleCalMax);
  
  // Настройки PAS
  server.on("/settings/pas", handlePasPage);
  server.on("/settings/pas/save", HTTP_POST, handlePasSave);
  server.on("/settings/pas/cal_start", HTTP_GET, handlePasCalStart);
  server.on("/settings/pas/cal_status", HTTP_GET, handlePasCalStatus);
  server.on("/settings/pas/cal_stop", HTTP_GET, handlePasCalStop);
  
  // Настройки круиз-контроля
  server.on("/settings/cruise", handleCruisePage);
  server.on("/settings/cruise/save", HTTP_POST, handleCruiseSave);
  
  // Настройки WiFi и AP
  server.on("/wifi/ap/save", HTTP_POST, handleApSave);
  server.on("/wifi", handleWifiPage);
  server.on("/wifi/scan", handleWifiScan);
  server.on("/wifi/save", HTTP_POST, handleWifiSave);
  
  // Отладка и мониторинг
  server.on("/debug", handleDebugPage);
  server.on("/debug/data", handleDebugData);
  server.on("/debug/bus/control", HTTP_POST, handleBusCaptureControl);
  server.on("/debug/bus/status", HTTP_GET, handleBusCaptureStatus);
  server.on("/debug/bus/data", HTTP_GET, handleBusCaptureData);
  server.on("/debug/bus/csv", HTTP_GET, handleBusCaptureCsv);
  
  // Настройки GPIO (pins)
  server.on("/settings/pins", handlePinsPage);
  server.on("/settings/pins/save", HTTP_POST, handlePinsSave);
  server.on("/settings/pins/reset", HTTP_POST, handlePinsReset);
  server.on("/settings/pins/row/save", HTTP_POST, handlePinRowSave);
  server.on("/settings/pins/custom/save", HTTP_POST, handleCustomPinSave);
  server.on("/settings/pins/custom/delete", HTTP_POST, handleCustomPinDelete);
  
  // Настройки событий (event engine)
  server.on("/settings/events", handleEventsPage);
  server.on("/settings/events/save", HTTP_POST, handleEventsSave);
  server.on("/settings/events/rule/save", HTTP_POST, handleEventRuleSave);
  server.on("/settings/events/rule/delete", HTTP_POST, handleEventRuleDelete);
  server.on("/settings/events/reset", HTTP_POST, handleEventsReset);
  
  // Системные страницы
  server.on("/system", handleSystemPage);
  server.on("/system/export", handleSettingsExport);
  // Импорт принимает и multipart-загрузку файла (handleSettingsUpload), и обычный
  // urlencoded POST с полем settingsFile (вставка JSON вручную / curl).
  server.on("/system/import", HTTP_POST, handleSettingsImport, handleSettingsUpload);
  server.on("/system/factory-reset", HTTP_POST, handleSystemFactoryReset);
  server.on("/system/odometer/reset", HTTP_POST, handleSystemOdometerReset);
  server.on("/system/service/save", HTTP_POST, handleSystemServiceSave);
  
  // Телеметрия и статус
  server.on("/status/sys", HTTP_GET, handleSystemStatus);
  
  // OTA обновление прошивки
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);
}
