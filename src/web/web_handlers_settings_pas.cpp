#include "web/web_handlers_settings.h"
#include <WebServer.h>

#include "web/param_utils.h"   // getArgInt (семантика toInt)
#include "core/pas.h"          // pas*-настройки, pasPulseMux, PAS_MAX_LEVELS
#include "system/storage.h"    // pasSettingsSave
#include "web/web_ui.h"        // getTopBarCss/getSettingsCss/getTopBarHtml/getBackMenuHtml/getSettingsJs
#include "web/html_pages.h"    // getTopBarJs
#include "web/web_routes.h"    // server

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
<div class="frow"><label>Режим сглаживания</label>
<select name="smMode" id="pasSmMode" onchange="togglePasCustomSmooth()">
  <option value="0")rawliteral"; html += (pasSmoothMode == 0 ? " selected" : ""); html += R"rawliteral(>По умолчанию (из настроек Газа)</option>
  <option value="1")rawliteral"; html += (pasSmoothMode == 1 ? " selected" : ""); html += R"rawliteral(>Свои настройки</option>
  <option value="2")rawliteral"; html += (pasSmoothMode == 2 ? " selected" : ""); html += R"rawliteral(>Выключено</option>
</select></div>
<div id="pasCustomBlock">
<div class="chk"><label for="pasSsEn">Мягкий старт</label><input type="checkbox" id="pasSsEn" name="ssEn" )rawliteral"; html += pasSoftStartEnabled?"checked":"";
  html += R"rawliteral(></div>
<div class="frow"><label>Время разгона, мс</label><input type="number" name="ssMs" value=")rawliteral"; html += String(pasSoftStartMs);
  html += R"rawliteral("></div>
</div>
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
function togglePasCustomSmooth(){
  document.getElementById('pasCustomBlock').style.display = document.getElementById('pasSmMode').value === '1' ? '' : 'none';
}
togglePasCustomSmooth();
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
  pasMagnetCount = getArgInt(server, "magnets");
  if (pasMagnetCount < 1) pasMagnetCount = 1;
  int newEdge = getArgInt(server, "edge");
  bool edgeChanged = (newEdge != pasEdgeMode);
  pasEdgeMode = newEdge;
  pasActivationAngle = getArgInt(server, "angle");
  pasTimeoutMs = getArgInt(server, "timeout");
  pasStopTimeoutMs = getArgInt(server, "stopTO");
  if (pasStopTimeoutMs < 20) pasStopTimeoutMs = 20;

  int count = getArgInt(server, "count");
  if (count < 0) count = 0;
  if (count > PAS_MAX_LEVELS) count = PAS_MAX_LEVELS;
  pasLevelsCount = count;
  for (int i = 0; i < count; i++) {
    String key = "lvl" + String(i);
    if (server.hasArg(key)) {
      int v = getArgInt(server, key);
      if (v < 0) v = 0;
      if (v > 100) v = 100;
      pasLevelPercent[i] = v;
    }
  }

  pasSoftStartEnabled = server.hasArg("ssEn");
  pasSoftStopEnabled = getArgInt(server, "spEn") != 0; // скрытое поле: значение сохраняем, UI скрыт
  pasSoftStartMs = getArgInt(server, "ssMs");
  pasSoftStopMs = getArgInt(server, "spMs");
  {
    int sm = getArgInt(server, "smMode");
    pasSmoothMode = (uint8_t)constrain(sm, (int)SMOOTH_MODE_DEFAULT, (int)SMOOTH_MODE_OFF);
  }
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
