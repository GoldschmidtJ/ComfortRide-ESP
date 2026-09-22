#include "web/web_handlers_system.h"

#include <WebServer.h>
#include "web/web_routes.h"     // server
#include "web/web_ui.h"         // getTopBarCss/getSettingsCss/getTopBarHtml/getBackMenuHtml
#include "web/html_pages.h"     // sendHubPage, getTopBarJs
#include "utils/utils.h"        // htmlEscape
#include "core/throttle.h"      // setThrottleOutputSafeZero, factoryResetInProgress, hwThrottle*
#include "nvs_flash.h"          // nvs_flash_erase/nvs_flash_init (заводской сброс)

// ================= Главная страница (хаб) =================
void handleHub() { sendHubPage(server); }


void handleSystemPage() {
  String msg = server.arg("msg");
  String banner;
  if (msg.length()) {
    banner = "<p class=\"banner " + (msg.startsWith("Ошибка") ? String("bad") : String("ok")) + "\">" + htmlEscape(msg) + "</p>";
  }
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Система</title>
<style>
)rawliteral" + getTopBarCss() + getSettingsCss() + R"rawliteral(
textarea{width:100%;min-height:120px;padding:9px 10px;background:var(--ui-button);color:var(--ui-text);border:1px solid var(--ui-border);border-radius:var(--ui-radius);font:inherit;resize:vertical}
input[type=file]{padding:8px;border-style:dashed;cursor:pointer}
.danger{border-color:var(--ui-danger);color:var(--ui-danger)}.danger:hover{background:var(--ui-danger);color:#fff}
.banner{padding:10px 12px;border-radius:var(--ui-radius);border:1px solid var(--ui-success);background:var(--ui-success-soft);color:var(--ui-success);font-size:var(--ui-fs-mid)}
.banner.bad{border-color:var(--ui-danger);background:var(--ui-danger-soft);color:var(--ui-danger)}
details{margin-top:12px}summary{cursor:pointer;color:var(--ui-muted);font-size:var(--ui-fs-mid);padding:4px 0}
</style></head><body>
)rawliteral" + getTopBarHtml() + getBackMenuHtml() +R"rawliteral(
<h1>Система</h1>
)rawliteral" + banner + R"rawliteral(
<fieldset><legend>Файл настроек</legend>
<p class="fhint">Экспорт сохраняет все параметры одним JSON-файлом: газ, PAS, круиз, GPIO, Wi-Fi, AP и события. Импорт читает такой файл обратно и перезагружает плату.</p>
<p><a class="card" href="/system/export" download="bike_controller_settings.json">&#8681; Скачать настройки (JSON)</a></p>
<form method="POST" action="/system/import" enctype="multipart/form-data">
<label for="settingsFileInput">Файл настроек (.json)</label>
<input type="file" id="settingsFileInput" name="settingsFile" accept=".json,application/json,text/json" required>
<button type="submit">Загрузить из файла</button>
</form>
<details><summary>Вставить JSON вручную</summary>
<form method="POST" action="/system/import">
<label for="settingsJson">JSON настроек</label>
<textarea id="settingsJson" name="settingsFile" rows="8" placeholder='{"throttle":{...},"pas":{...}}'></textarea>
<button type="submit">Импортировать текст</button>
</form>
</details>
</fieldset>
<fieldset><legend>Прошивка</legend>
<p class="fhint">Загрузка нового .bin по воздуху. Плата перезагрузится сама.</p>
<a class="card" href="/update">Обновление прошивки</a>
</fieldset>
<fieldset><legend>Сброс</legend>
<p class="fhint">Все настройки вернутся к заводским: газ, PAS, круиз, события, распиновка и Wi-Fi.</p>
<form method="POST" action="/system/factory-reset" onsubmit="return confirm('Сбросить все настройки?')">
<button type="submit" class="danger">Заводской сброс</button>
</form>
</fieldset>
)rawliteral" + getTopBarJs() + R"rawliteral(
</body></html>
)rawliteral";
  server.send(200, "text/html; charset=utf-8", html);
}

// ================= Веб: возврат к заводским настройкам =================
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
