#include "web/web_handlers_settings.h"
#include <WebServer.h>
#include <WiFi.h>

#include "system/storage.h"    // storedApSsid/Pass, apSettingsSave, wifiCredsSave
#include "system/network.h"    // wifiConnect, startConfiguredAp
#include "utils/utils.h"       // htmlEscape
#include "web/web_ui.h"        // getTopBarCss/getSettingsCss/getTopBarHtml/getBackMenuHtml
#include "web/html_pages.h"    // getTopBarJs
#include "web/web_routes.h"    // server

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
<h1 data-i18n="network">Связь и сеть</h1>

<div class="panel">
  <h2 data-i18n="wifiTitle">Подключение к Wi-Fi (Клиент)</h2>
  <div class="status" id="wifi_status" data-i18n="wifiStatus">Текущий статус: )rawliteral";
  html += (WiFi.status() == WL_CONNECTED) ? ("<b>Подключено к " + WiFi.SSID() + "</b> (IP: " + WiFi.localIP().toString() + ")") : "<i>Не подключено к внешней сети</i>";
  html += R"rawliteral(</div>

  <button type="button" onclick="scan()" data-i18n="scanNetworks">Найти доступные сети</button>
  <div id="nets" class="block-gap"></div>

  <form id="f_wifi" class="form-gap">
    <div class="frow"><label for="ssid" data-i18n="ssid">SSID</label><input type="text" id="ssid" name="ssid" placeholder="Сеть или вручную"></div>
    <div class="frow"><label for="pass" data-i18n="password">Пароль</label><input type="password" id="pass" name="pass" placeholder="Пароль Wi-Fi"></div>
    <button type="submit" data-i18n="connectWifi">Подключиться к Wi-Fi</button>
  </form>
</div>

<div class="panel">
  <h2 data-i18n="apTitle">Настройки точки доступа (AP)</h2>
  <div class="status">
    Режим точки доступа: <b>активен</b><br>
    IP-адрес точки: <b>)rawliteral";
  html += WiFi.softAPIP().toString();
  html += R"rawliteral(</b>
  </div>

  <form id="f_ap" class="form-gap">
    <div class="frow"><label for="ap_ssid" data-i18n="apSsid">SSID точки</label><input type="text" id="ap_ssid" name="ap_ssid" value=")rawliteral";
  html += htmlEscape(storedApSsid);
  html += R"rawliteral("></div>

    <div class="frow"><label for="ap_pass" data-i18n="apPass">Пароль точки</label><input type="text" id="ap_pass" name="ap_pass" value=")rawliteral";
  html += htmlEscape(storedApPass);
  html += R"rawliteral(" placeholder="пусто = открытая сеть"></div>
    <p class="fhint">Пароль отображается открыто. Оставьте пустым для открытой точки (без пароля). Для WPA2 нужно минимум 8 символов.</p>

    <button type="submit" data-i18n="saveAp">Сохранить AP</button>
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
