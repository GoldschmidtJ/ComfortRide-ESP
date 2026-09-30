#include "web/web_handlers_backup.h"

#include <WebServer.h>
#include "web/web_routes.h"            // server
#include "system/hardware_config.h"    // customPins, pinRoleNames, PIN_ROLE_COUNT
#include "system/events_engine.h"      // EventRule, eventRules, eventRuleName, serviceModeActive
#include "core/throttle.h"             // throttle*-параметры
#include "core/pas.h"                  // pas*-параметры, reattachPasInterrupt, PAS_MAX_LEVELS
#include "core/cruise.h"               // cruise*-параметры, CRUISE_MAX_LEVELS
#include "system/inputs.h"              // ownBrakeCutoffEnabled
#include "system/storage.h"            // storedSsid/storedPass/storedApSsid/storedApPass, *SettingsSave
#include "utils/utils.h"               // jsonEscape, normalizeUserLabel, setUserLabel, htmlEscape
#include "utils/json_utils.h"          // JSON-хелперы, SETTINGS_IMPORT_MAX_BYTES
#include "web/web_ui.h"                // getTopBarCss/getSettingsCss (страница результата импорта)
#include "web/web_handlers_settings.h" // validApCredentials

// ================= Импорт / экспорт файла настроек =================

// Буфер multipart-загрузки JSON-файла настроек: WebServer отдаёт chunks в
// handleSettingsUpload(), а разбирает их уже handleSettingsImport().
static String gSettingsUpload;
static bool gSettingsUploadTooBig = false;

// Позиция закрывающего символа, парного к openPos, с учётом вложенности,
// строк и escape-последовательностей. -1, если блок не закрыт.
static int jsonBlockEnd(const String &src, int openPos, char openCh, char closeCh) {
  int depth = 0;
  bool inString = false;
  bool escaped = false;
  for (int i = openPos; i < (int)src.length(); i++) {
    char c = src.charAt(i);
    if (inString) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') inString = false;
      continue;
    }
    if (c == '"') inString = true;
    else if (c == openCh) depth++;
    else if (c == closeCh && --depth <= 0) return i;
  }
  return -1;
}

// Следующий объект {...} внутри JSON-массива (ограничен arrayEnd). pos — текущая
// позиция сканирования; сдвигается за найденный объект. false — объектов больше нет.
static bool nextJsonArrayObject(const String &src, int &pos, int arrayEnd, String &item) {
  int open = src.indexOf('{', pos);
  if (open == -1 || open > arrayEnd) return false;
  int close = jsonBlockEnd(src, open, '{', '}');
  if (close == -1 || close > arrayEnd) return false;
  item = src.substring(open, close + 1);
  pos = close + 1;
  return true;
}

// Следующая строка JSON-массива («...») в пределах arrayEnd; pos сдвигается за неё.
// Распаковывает \" и \\ — иначе имя пина с кавычкой рассинхронизировало бы весь массив.
static bool nextJsonArrayString(const String &src, int &pos, int arrayEnd, String &out) {
  int q = src.indexOf('"', pos);
  if (q == -1 || q > arrayEnd) return false;
  out = "";
  pos = q + 1;
  while (pos < (int)src.length()) {
    char c = src.charAt(pos);
    if (c == '\\' && pos + 1 < (int)src.length()) {
      char n = src.charAt(pos + 1);
      out += (n == 'n') ? '\n' : (n == 't') ? '\t' : (n == 'r') ? '\r' : n;
      pos += 2;
      continue;
    }
    pos++;
    if (c == '"') return true;
    out += c;
  }
  return false;
}

// Есть ли в документе хотя бы один раздел нашего формата настроек.
static bool hasSettingsSection(const String &src) {
  static const char *const kSections[] = {"\"throttle\"", "\"pas\"", "\"cruise\"",
                                          "\"events\"", "\"wifi\"", "\"ap\"",
                                          "\"pinNames\"", "\"customPins\""};
  for (size_t i = 0; i < sizeof(kSections) / sizeof(kSections[0]); i++) {
    if (src.indexOf(kSections[i]) != -1) return true;
  }
  return false;
}

// Сжимает JSON на месте: убирает пробелы/переносы вне строк. Импорт ищет секции
// вида "\"throttle\":{", а файл, пересохранённый редактором с отступами, выглядел бы
// как "\"throttle\": {" и молча пропускался.
static void compactJson(String &src) {
  bool inString = false;
  bool escaped = false;
  int w = 0;
  for (int i = 0; i < (int)src.length(); i++) {
    char c = src.charAt(i);
    if (inString) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') inString = false;
    } else if (c == '"') {
      inString = true;
    } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      continue;
    }
    if (w != i) src.setCharAt(w, c);
    w++;
  }
  src.remove(w);
}

void handleSettingsUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    gSettingsUpload = String();
    gSettingsUploadTooBig = false;
    Serial.printf("Импорт настроек: файл %s\n", upload.filename.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (gSettingsUploadTooBig) return;
    if (gSettingsUpload.length() + upload.currentSize > SETTINGS_IMPORT_MAX_BYTES) {
      gSettingsUpload = String();
      gSettingsUploadTooBig = true;
      Serial.println("Импорт настроек: файл больше лимита");
      return;
    }
    gSettingsUpload.concat(reinterpret_cast<const char *>(upload.buf), upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    Serial.printf("Импорт настроек: принято %u байт\n", upload.totalSize);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    gSettingsUpload = String();
    gSettingsUploadTooBig = false;
  }
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
  json += "\"brakeCut\":" + String(ownBrakeCutoffEnabled ? 1 : 0) + ",";
  json += "\"extRange\":" + String(throttleExtendedRangeAllowed ? 1 : 0);
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
  json += "\"smMode\":" + String(pasSmoothMode) + ",";
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
  json += "\"spMs\":" + String(cruiseSoftStopMs) + ",";
  json += "\"smMode\":" + String(cruiseSmoothMode) + ",";
  json += "\"enabled\":" + String(cruiseEnabled ? 1 : 0) + ",";
  json += "\"confThr\":" + String(cruiseConfirmThrottleAfterStart ? 1 : 0) + ",";
  json += "\"brkMode\":" + String(cruiseAfterBrakingMode) + ",";
  json += "\"thrMode\":" + String(cruiseAfterThrottleMode);
  json += "}";
  json += "}";
  // Отдаём именно файлом, иначе браузер показывает JSON в вкладке вместо сохранения.
  server.sendHeader("Content-Disposition", "attachment; filename=\"bike_controller_settings.json\"", true);
  server.sendHeader("Cache-Control", "no-store", true);
  server.sendHeader("Connection", "close", true);
  server.send(200, "application/json; charset=utf-8", json);
}

// Страница результата импорта: общий дизайн с остальными страницами, ссылка назад,
// а при успехе — ожидание перезагрузки с автовозвратом в раздел «Система».
static void sendImportResult(bool ok, const String &headline, const String &detail) {
  String html = R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Импорт настроек</title>
<style>
)rawliteral" + getTopBarCss() + getSettingsCss() + R"rawliteral(
.banner{padding:10px 12px;border-radius:var(--ui-radius);border:1px solid var(--ui-success);background:var(--ui-success-soft);color:var(--ui-success)}
.banner.bad{border-color:var(--ui-danger);background:var(--ui-danger-soft);color:var(--ui-danger)}
</style></head><body>
<h1>Импорт настроек</h1>
)rawliteral";
  html += String("<p class=\"banner") + (ok ? String("") : String(" bad")) + "\">" + htmlEscape(headline) + "</p>";
  html += "<p class=\"fhint\">" + htmlEscape(detail) + "</p>";
  if (ok) {
    html += R"rawliteral(<p class="fhint">Плата перезагружается, чтобы применить пины и датчики. Страница откроется сама, когда устройство вернётся в сеть.</p>
<script>
setTimeout(function poll(){
  fetch('/status/sys?_=' + Date.now(), {cache:'no-store'})
    .then(function(r){ if(r.ok) location.href = '/system?msg=' + encodeURIComponent('Настройки загружены из файла'); else setTimeout(poll, 2000); })
    .catch(function(){ setTimeout(poll, 2000); });
}, 4000);
</script>
)rawliteral";
  } else {
    html += "<p><a class=\"card\" href=\"/system\">&larr; Вернуться в «Система»</a></p>";
  }
  html += "</body></html>";
  server.send(ok ? 200 : 400, "text/html; charset=utf-8", html);
}

void handleSettingsImport() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  // Источник JSON: сначала загруженный файл (multipart), затем поле settingsFile
  // (форма с ручным вставленным текстом или curl).
  String fileContent = gSettingsUpload;
  const bool tooBig = gSettingsUploadTooBig;
  gSettingsUpload = String();
  gSettingsUploadTooBig = false;
  const bool fromFile = fileContent.length() > 0;
  if (!fromFile && server.hasArg("settingsFile")) fileContent = server.arg("settingsFile");
  fileContent.trim();

  if (tooBig) {
    sendImportResult(false, "Ошибка: файл слишком большой",
                     "Лимит — " + String(SETTINGS_IMPORT_MAX_BYTES / 1024) + " КБ. Удалите лишние правила событий или названия пинов и повторите.");
    return;
  }
  if (fileContent.length() == 0) {
    sendImportResult(false, fromFile ? "Ошибка: пустой файл" : "Ошибка: настройки не переданы",
                     "Выберите JSON-файл, скачанный через «Скачать настройки», или вставьте текст вручную.");
    return;
  }
  if (fileContent.length() > SETTINGS_IMPORT_MAX_BYTES || fileContent.indexOf('{') == -1 || !hasSettingsSection(fileContent)) {
    sendImportResult(false, "Ошибка: это не файл настроек",
                     "Ожидается JSON, экспортированный из этого устройства (разделы throttle / pas / cruise / events / wifi / ap / pinNames). Получено байт: " + String(fileContent.length()));
    return;
  }

  String applied;  // перечисляем, какие разделы реально применены
  compactJson(fileContent);  // терпим к "красивому" JSON с отступами
  auto markApplied = [&applied](const char *name) {
    if (applied.length()) applied += ", ";
    applied += name;
  };

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
      iv = jsonIntAfter(throttleJson, "\"extRange\":", ok);
      if (ok) throttleExtendedRangeAllowed = iv != 0;
      throttleSettingsSave();
      markApplied("газ");
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
      value = jsonIntAfter(pasJson, "\"smMode\":", ok);
      if (ok) pasSmoothMode = (uint8_t)constrain(value, (int)SMOOTH_MODE_DEFAULT, (int)SMOOTH_MODE_OFF);
      int pctPos = pasJson.indexOf("\"pct\":[");
      if (pctPos != -1) readPercentArray(pasJson.substring(pctPos + 6), pasLevelPercent, pasLevelsCount);
      pasCurrentLevel = constrain(pasCurrentLevel, 0, pasLevelsCount);
      value = jsonIntAfter(pasJson, "\"enabled\":", ok);
      if (ok) pasEnabled = value != 0;
      pasSettingsSave();
      reattachPasInterrupt();
      markApplied("PAS");
    }
  }
  idx = fileContent.indexOf("\"wifi\":{");
  if (idx != -1) {
    String wifiJson;
    if (extractJsonObject(fileContent, "\"wifi\":{", wifiJson)) {
      bool ssidOk = false, passOk = false;
      String importedSsid = jsonStringAfter(wifiJson, "\"ssid\":", ssidOk);
      String importedPass = jsonStringAfter(wifiJson, "\"pass\":", passOk);
      // Пароль длиннее 63 байт разобрать нельзя: считаем, что ключа нет.
      if (passOk && importedPass.length() > 63) passOk = false;
      if (ssidOk && importedSsid.length() > 0 && importedSsid.length() <= 32) {
        // Если ключа "pass" в файле нет (экспорт старой прошивки/обрезанный файл),
        // оставляем текущий сохранённый пароль: иначе импорт собственного файла
        // стирал бы креды и устройство теряло Wi-Fi после перезагрузки.
        if (!passOk) importedPass = storedPass;
        wifiCredsSave(importedSsid, importedPass);
        markApplied("Wi-Fi");
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
      // Нет ключа "pass" — не превращаем точку доступа в открытую, оставляем текущий.
      if (!passOk) importedApPass = storedApPass;
      String error;
      if (ssidOk && validApCredentials(importedApSsid, importedApPass, error)) {
        storedApSsid = importedApSsid;
        storedApPass = importedApPass;
        apSettingsSave();
        markApplied("точка доступа");
      }
    }
  }

  // --- Пользовательские названия ролей GPIO ---
  idx = fileContent.indexOf("\"pinNames\":[");
  if (idx != -1) {
    int namesArr = fileContent.indexOf('[', idx);
    int namesEnd = namesArr == -1 ? -1 : jsonBlockEnd(fileContent, namesArr, '[', ']');
    if (namesEnd != -1) {
      int pos = namesArr + 1;
      for (int i = 0; i < PIN_ROLE_COUNT; i++) {
        String name;
        if (!nextJsonArrayString(fileContent, pos, namesEnd, name)) break;
        if (normalizeUserLabel(name)) setUserLabel(pinRoleNames[i], name);
      }
      pinSettingsSave();
      markApplied("имена пинов");
    }
  }

  // --- Дополнительные пользовательские GPIO ---
  idx = fileContent.indexOf("\"customPins\":[");
  if (idx != -1) {
    int pinsArr = fileContent.indexOf('[', idx);
    int pinsEnd = pinsArr == -1 ? -1 : jsonBlockEnd(fileContent, pinsArr, '[', ']');
    if (pinsEnd != -1) {
      memset(customPins, 0, sizeof(customPins));
      int pos = pinsArr + 1;
      int curSlot = 0;
      while (curSlot < CUSTOM_PIN_MAX) {
        String obj;
        if (!nextJsonArrayObject(fileContent, pos, pinsEnd, obj)) break;
        int gpPos = obj.indexOf("\"gpio\":");
        int mdPos = obj.indexOf("\"mode\":");
        bool nmOk = false;
        String nm = jsonStringAfter(obj, "\"nm\":", nmOk);
        if (nmOk && gpPos != -1 && mdPos != -1) {
          int gpioVal = obj.substring(gpPos + 7).toInt();
          int modeVal = obj.substring(mdPos + 7).toInt();
          if (normalizeUserLabel(nm) && gpioVal >= 0 && gpioVal <= 39 && modeVal >= 0 && modeVal <= 2) {
            customPins[curSlot].used = 1;
            customPins[curSlot].mode = (uint8_t)modeVal;
            customPins[curSlot].gpio = (int16_t)gpioVal;
            strncpy(customPins[curSlot].name, nm.c_str(), USER_LABEL_SIZE - 1);
            customPins[curSlot].name[USER_LABEL_SIZE - 1] = 0;
            curSlot++;
          }
        }
      }
      pinSettingsSave();
      markApplied("доп. GPIO");
    }
  }

  // --- Правила конструктора событий ---
  // Массив rules содержит вложенный массив acts, поэтому границы находятся
  // подсчётом скобок, а не первым "]".
  idx = fileContent.indexOf("\"events\":{");
  if (idx != -1) {
    int rulesKey = fileContent.indexOf("\"rules\"", idx);
    int rulesArr = rulesKey == -1 ? -1 : fileContent.indexOf('[', rulesKey);
    int rulesEnd = rulesArr == -1 ? -1 : jsonBlockEnd(fileContent, rulesArr, '[', ']');
    if (rulesEnd != -1) {
      int pos = rulesArr + 1;
      for (int i = 0; i < EVENT_MAX_RULES; i++) {
        String item;
        if (!nextJsonArrayObject(fileContent, pos, rulesEnd, item)) break;
        EventRule &r = eventRules[i];
        memset(r.actions, 0, sizeof(r.actions));
        memset(r.actionValues, 0, sizeof(r.actionValues));
        bool okStr = false;
        String name = jsonStringAfter(item, "\"name\":", okStr);
        if (okStr && normalizeUserLabel(name)) setUserLabel(eventRuleNames[i], name);
        int p;
        if ((p = item.indexOf("\"en\":")) != -1) r.enabled = item.substring(p + 5).toInt() != 0;
        if ((p = item.indexOf("\"tr\":")) != -1) r.trigger = item.substring(p + 5).toInt();
        if ((p = item.indexOf("\"co\":")) != -1) r.condition = item.substring(p + 5).toInt();
        if ((p = item.indexOf("\"pr\":")) != -1) r.priority = constrain(item.substring(p + 5).toInt(), 1, 100);
        if ((p = item.indexOf("\"ct\":")) != -1) r.count = constrain(item.substring(p + 5).toInt(), 1, 100);
        if ((p = item.indexOf("\"ms\":")) != -1) r.intervalMs = constrain(item.substring(p + 5).toInt(), 0, 1000000L);
        // Новый формат: список результатов "acts":[{"ac":..,"va":..},...]
        int actsKey = item.indexOf("\"acts\"");
        int actsArr = actsKey == -1 ? -1 : item.indexOf('[', actsKey);
        int actsEnd = actsArr == -1 ? -1 : jsonBlockEnd(item, actsArr, '[', ']');
        if (actsEnd != -1) {
          int ap = actsArr + 1;
          for (int k = 0; k < EVENT_MAX_ACTIONS; k++) {
            String act;
            if (!nextJsonArrayObject(item, ap, actsEnd, act)) break;
            int q;
            // Ключ "\"ac\":" — 5 символов, значение начинается сразу после него.
            if ((q = act.indexOf("\"ac\":")) != -1) r.actions[k] = act.substring(q + 5).toInt();
            if ((q = act.indexOf("\"va\":")) != -1) r.actionValues[k] = act.substring(q + 5).toInt();
          }
        } else {
          // Старый формат: одиночное действие переносится в слот 0.
          if ((p = item.indexOf("\"ac\":")) != -1) r.actions[0] = item.substring(p + 5).toInt();
          if ((p = item.indexOf("\"va\":")) != -1) r.actionValues[0] = item.substring(p + 5).toInt();
        }
      }
      eventSettingsSave();
      markApplied("события");
    }
  }

  // --- Cruise Control Settings ---
  idx = fileContent.indexOf("\"cruise\":{");
  if (idx != -1) {
    String cruiseJson;
    if (extractJsonObject(fileContent, "\"cruise\":{", cruiseJson)) {
      bool ok = false;
      int value = jsonIntAfter(cruiseJson, "\"cnt\":", ok);
      if (ok) cruiseLevelsCount = constrain(value, 1, CRUISE_MAX_LEVELS);
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
      value = jsonIntAfter(cruiseJson, "\"smMode\":", ok);
      if (ok) cruiseSmoothMode = (uint8_t)constrain(value, (int)SMOOTH_MODE_DEFAULT, (int)SMOOTH_MODE_OFF);
      int pctPos = cruiseJson.indexOf("\"pct\":[");
      if (pctPos != -1) readPercentArray(cruiseJson.substring(pctPos + 6), cruiseLevelPercent, cruiseLevelsCount);
      cruiseCurrentLevel = constrain(cruiseCurrentLevel, 0, cruiseLevelsCount);
      value = jsonIntAfter(cruiseJson, "\"enabled\":", ok);
      if (ok) cruiseEnabled = value != 0;
      value = jsonIntAfter(cruiseJson, "\"confThr\":", ok);
      if (ok) cruiseConfirmThrottleAfterStart = value != 0;
      value = jsonIntAfter(cruiseJson, "\"brkMode\":", ok);
      if (ok) cruiseAfterBrakingMode = constrain(value, 0, 2);
      value = jsonIntAfter(cruiseJson, "\"thrMode\":", ok);
      if (ok) cruiseAfterThrottleMode = constrain(value, 0, 2);
      cruiseSettingsSave();
      markApplied("круиз");
    }
  }

  if (applied.length() == 0) {
    sendImportResult(false, "Ошибка: ни один раздел не распознан",
                     "Файл похож на JSON, но нужные секции в нём не найдены. Скачайте настройки заново на этой плате и попробуйте снова.");
    return;
  }

  sendImportResult(true, "Настройки загружены",
                   "Применено байт: " + String(fileContent.length()) + ". Разделы: " + applied + ".");
  delay(800);
  ESP.restart();
}

