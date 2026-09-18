#include "web/web_handlers_settings.h"
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>

extern WebServer server;
extern const char* MDNS_HOST;
extern String storedApSsid;
extern String storedApPassword;
extern String storedApPass;
extern const char* FIRMWARE_VERSION;

// Сеть
extern void wifiSettingsSave();
extern void apSettingsSave();
extern void wifiCredsSave(const String &newSsid, const String &newPass);
extern void wifiConnect();
extern bool startConfiguredAp();
extern String htmlEscape(const String &value);

// UI/HTML Helpers
extern String getTopBarCss();
extern String getSettingsCss();
extern String getTopBarHtml();
extern String getBackMenuHtml();
extern String getTopBarJs();
extern String getSettingsJs();

// Throttle
extern float throttleInMinV, throttleInMaxV;
extern float throttleOutMinV, throttleOutMaxV;
extern float throttleInputDividerRatio;
extern float throttleOutputGain;
extern bool throttleSoftStartEnabled;
extern bool throttleSoftStopEnabled;
extern unsigned long throttleSoftStartMs;
extern unsigned long throttleSoftStopMs;
extern volatile float hwThrottleInV;
extern bool ownBrakeCutoffEnabled;
extern void throttleSettingsSave();

// PAS
extern const int PAS_MAX_LEVELS;
extern const unsigned long PAS_CAL_TIMEOUT_MS;
extern bool pasEnabled;
extern int pasLevelsCount;
extern int pasLevelPercent[20];  // PAS_MAX_LEVELS
extern int pasCurrentLevel;
extern int pasDirection;
extern int pasPoles;
extern float pasMaxSpeedKmH;
extern bool pasSensorAnalog;
extern float pasAnalogMinV, pasAnalogMaxV;
extern int pasMagnetCount;
extern int pasEdgeMode;
extern int pasActivationAngle;
extern unsigned long pasTimeoutMs;
extern unsigned long pasStopTimeoutMs;
extern bool pasSoftStartEnabled;
extern bool pasSoftStopEnabled;
extern unsigned long pasSoftStartMs;
extern unsigned long pasSoftStopMs;
extern volatile float hwThrottleOutV;
extern volatile bool hwPasActive;
extern volatile uint32_t pasEmaPeriod;
extern volatile uint32_t pasCalEmaPeriod;
extern volatile bool pasCalMode;
extern volatile uint32_t pasCalMinPeriod;
extern volatile uint32_t pasCalMaxPeriod;
extern volatile bool pasCalRunning;
extern volatile unsigned long pasCalPulses;
extern volatile unsigned long pasCalLastPulseMicros;
extern unsigned long pasCalStartMs;
extern portMUX_TYPE pasPulseMux;
extern void pasSettingsSave();
extern void reattachPasInterrupt();
extern String jsonEscape(const String &value);

// Cruise
extern const int CRUISE_MAX_LEVELS;
extern bool cruiseEnabled;
extern int cruiseLevelsCount;
extern float cruiseLevelPercent[100];
extern float cruiseStartPercent;
extern float cruiseEndPercent;
extern bool cruiseConfirmThrottleAfterStart;
extern float cruiseMaxSpeedKmH;
extern float cruiseMaxAccelPas;
extern float cruiseMaxAccelGas;
extern int cruiseAfterBrakingMode;
extern int cruiseAfterThrottleMode;
extern bool cruiseSoftStartEnabled;
extern bool cruiseSoftStopEnabled;
extern unsigned long cruiseSoftStartMs;
extern unsigned long cruiseSoftStopMs;
extern void cruiseSettingsSave();

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
.net .rssi{color:var(--ui-muted);font-size:var(--ui-fs-tiny)}
.status{color:var(--ui-muted);font-size:var(--ui-fs-small);margin:8px 0;line-height:1.4}
.hint{color:var(--ui-muted);font-size:var(--ui-fs-small)}
.back-link{color:var(--ui-muted);text-decoration:none}.back-link:hover{color:var(--ui-text)}
.message-success{color:var(--ui-success)}.message-error{color:var(--ui-danger)}.form-gap{margin-top:15px}.block-gap{margin-top:10px}.status-gap{margin-top:8px}
</style></head><body>
)rawliteral" + getTopBarHtml() + getBackMenuHtml() + R"rawliteral(
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


// ================= Веб: Газ =================
void handleThrottlePage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Газ</title>
<style>
)rawliteral" + getTopBarCss() + getSettingsCss() + R"rawliteral(
#liveV{font-size:var(--ui-fs-h2);font-weight:bold;color:var(--ui-text);display:inline-block;padding:4px 8px;background:var(--ui-button);border-radius:4px;border:1px solid var(--ui-border)}
</style></head><body>
)rawliteral" + getTopBarHtml() + getBackMenuHtml() + R"rawliteral(
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
<p class="fhint">R2/(R1+R2). Для R1=10 кОм и R2=20 кОм: 0,667.</p>
<div class="frow"><label>Коэффициент усиления ОУ</label><input type="number" step="0.01" min="1" max="3" name="gain" value=")rawliteral"; html += String(throttleOutputGain, 2);
  html += R"rawliteral("></div>
<p class="fhint">1 + R4/R3. Для R3=10 кОм и R4=3,3 кОм: 1,33.</p>
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
)rawliteral" + getTopBarHtml() + getBackMenuHtml() + R"rawliteral(
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


// ================= Веб: Cruise Control =================
void handleCruisePage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Круиз</title>
<style>
)rawliteral" + getTopBarCss() + getSettingsCss() + R"rawliteral(
</style></head><body>
)rawliteral" + getTopBarHtml() + getBackMenuHtml() + R"rawliteral(
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


