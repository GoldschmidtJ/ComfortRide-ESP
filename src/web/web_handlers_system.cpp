#include "web/web_handlers_system.h"

#include <WebServer.h>
#include "web/web_routes.h"     // server
#include "web/web_ui.h"         // getTopBarCss/getSettingsCss/getTopBarHtml/getBackMenuHtml
#include "web/html_pages.h"     // sendHubPage, getTopBarJs
#include "utils/utils.h"        // htmlEscape
#include "core/throttle.h"      // setThrottleOutputSafeZero, factoryResetInProgress, hwThrottle*
#include "nvs_flash.h"          // nvs_flash_erase/nvs_flash_init (заводской сброс)
#include "system/events_engine.h" // serviceModeActive, serviceThrottleLimitPct
#include "system/storage.h"     // odometerKm, odometerReset, odometerLoad, odometerSave
#include "system/cpu_profile.h" // cpuUsagePercent

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
.slider-row{display:flex;align-items:center;gap:10px;margin:8px 0}
.slider-row input[type=range]{flex:1}
.slider-row .val{min-width:50px;text-align:right;font-weight:bold}
.odometer{font-size:var(--ui-fs-h2);font-weight:bold;color:var(--ui-accent)}
</style></head><body>
)rawliteral" + getTopBarHtml() + getBackMenuHtml() +R"rawliteral(
<h1 data-i18n="system">Система</h1>
)rawliteral" + banner + R"rawliteral(
<fieldset><legend data-i18n="odometer">Пробег</legend>
  <p class="odometer" id="odomVal">-- км</p>
  <form id="fOdomReset" onsubmit="return confirm('Сбросить пробег до 0?')">
    <button type="submit" class="danger" data-i18n="resetOdometer">Сбросить пробег</button>
  </form>
  <p class="fhint" data-i18n="odometerKm">Общий пробег хранится в NVS, сбрасывается только вручную.</p>
</fieldset>
<fieldset><legend data-i18n="serviceMode">Сервисный режим (Anti-Police)</legend>
  <p class="fhint" data-i18n="serviceLimit">Ограничение мощности газа в сервисном режиме (активируется 5 быстрыми нажатиями тормоза).</p>
  <div class="slider-row">
    <label for="svcLimit" data-i18n="serviceLimit">Потолок газа:</label> <span class="val" id="svcVal">)rawliteral";
  html += String(serviceThrottleLimitPct);
  html += R"rawliteral(%</span></label>
  <input type="range" id="svcLimit" min="10" max="80" value=")rawliteral";
  html += String(serviceThrottleLimitPct);
  html += R"rawliteral( step="5" oninput="document.getElementById('svcVal').textContent=this.value+'%'">
  </div>
  <button type="button" id="btnSvcSave" data-i18n="save">Сохранить</button>
  <p class="fhint">Текущее состояние сервисного режима: )rawliteral";
  html += String(serviceModeActive ? "<span data-i18n=\"active\">АКТИВЕН</span>" : "<span data-i18n=\"off\">выключен</span>");
  html += R"rawliteral(</p>
</fieldset>
<fieldset><legend data-i18n="settings">Файл настроек</legend>
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
<fieldset><legend data-i18n="firmware">Прошивка</legend>
  <p class="fhint">Загрузка нового .bin по воздуху. Плата перезагрузится сама.</p>
  <a class="card" href="/update">Обновление прошивки</a>
</fieldset>
<fieldset><legend data-i18n="reset">Сброс</legend>
  <p class="fhint">Все настройки вернутся к заводским: газ, PAS, круиз, события, распиновка и Wi-Fi.</p>
  <form method="POST" action="/system/factory-reset" onsubmit="return confirm('Сбросить все настройки?')">
  <button type="submit" class="danger" data-i18n="factoryReset">Заводской сброс</button>
  </form>
</fieldset>
)rawliteral" + getTopBarJs() + R"rawliteral(
</body></html>
)rawliteral";
  server.send(200, "text/html; charset=utf-8", html);
}

// ================= О пробеге =================
void handleSystemOdometerReset() {
  odometerReset();
  server.send(200, "text/plain", "OK");
}

// ================= Сервисный режим: сохранение лимита =================
void handleSystemServiceSave() {
  int limit = server.arg("limit").toInt();
  if (limit < 10) limit = 10;
  if (limit > 80) limit = 80;
  serviceThrottleLimitPct = limit;
  serviceSettingsSave();
  server.send(200, "text/plain", "OK");
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
