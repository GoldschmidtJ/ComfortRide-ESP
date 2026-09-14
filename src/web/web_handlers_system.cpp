#include "web/web_handlers_system.h"
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include "nvs_flash.h"
#include "system/hardware_config.h"
#include "system/events_engine.h"
#include "core/throttle.h"     // setThrottleOutputSafeZero(), factoryResetInProgress
#include "core/pas.h"
#include "core/cruise.h"
#include "system/storage.h"
#include "system/inputs.h"
#include "utils/utils.h"
#include "web/web_ui.h"
#include "web/html_pages.h"   // getUpdatePageHtml()

extern WebServer server;
extern String storedSsid;
extern String storedPass;
extern String storedApSsid;
extern String storedApPass;
extern volatile float hwThrottleOutV;
extern volatile float hwMotorOutPct;

// JSON helpers remain implemented in main.cpp during the current refactor.
extern const size_t SETTINGS_IMPORT_MAX_BYTES;
extern double jsonNumberAfter(const String &src, const char *key, bool &ok);
extern int jsonIntAfter(const String &src, const char *key, bool &ok);
extern bool extractJsonObject(const String &src, const char *key, String &out);
extern int readPercentArray(const String &src, float *dst, int maxCount);
extern int readPercentArray(const String &src, int *dst, int maxCount);
extern String jsonStringAfter(const String &src, const char *key, bool &ok);
extern bool validApCredentials(const String &ssidValue, const String &passValue, String &error);

// ================= Главная страница (хаб) =================
void handleHub() { sendHubPage(server); }

// Configuration structures









void handleSystemPage() {
  String html = F(R"rawliteral(<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Система</title><style>)rawliteral");
  html += getTopBarCss();
  html += getSettingsCss();
  html += F(R"rawliteral(</style></head><body>)rawliteral");
  html += getTopBarHtml();
  html += F(R"rawliteral(<div class="wrap"><div class="card"><h2>Система</h2><p><a href="/system/export">Экспортировать настройки</a></p><form method="POST" action="/system/import"><textarea name="settingsFile" rows="8" style="width:100%" placeholder="Вставьте JSON настроек"></textarea><button type="submit">Импортировать</button></form><p><a href="/update">Обновление прошивки</a></p><form method="POST" action="/system/factory-reset" onsubmit="return confirm('Сбросить все настройки?')"><button type="submit">Заводской сброс</button></form></div></div>)rawliteral");
  html += getTopBarJs();
  html += F("</body></html>");
  server.send(200, "text/html; charset=utf-8", html);
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

// ================= Веб: заливка прошивки прямо через браузер =================
void handleUpdatePage() { server.send(200, "text/html", getUpdatePageHtml()); }

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
