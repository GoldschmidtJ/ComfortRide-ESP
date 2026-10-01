#include "web/web_handlers_settings.h"
#include <WebServer.h>
#include <math.h>

#include "core/throttle.h"
#include "core/pas.h"
#include "core/cruise.h"
#include "system/storage.h"
#include "system/inputs.h"
#include "web/web_ui.h"
#include "web/html_pages.h"
#include "web/web_routes.h"

// Объединенная страница настроек тяги: Газ + PAS + Круиз
void handleDrivePage() {
  String html = R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title data-i18n="drive">Управление тягой</title>
<style>
)rawliteral" + getTopBarCss() + getSettingsCss() + R"rawliteral(
#liveV{font-size:var(--ui-fs-h2);font-weight:bold;color:var(--ui-text);display:inline-block;padding:4px 8px;background:var(--ui-button);border-radius:4px;border:1px solid var(--ui-border)}
.cal-status{margin-top:8px;font-weight:bold}
.tab-row{display:flex;gap:6px;margin:12px 0 16px;background:var(--ui-card);padding:4px;border-radius:var(--ui-radius);border:1px solid var(--ui-border)}
.tab-btn{flex:1;background:transparent;border:none;color:var(--ui-muted);font-weight:600;padding:8px 6px;margin:0;min-height:36px;border-radius:6px;cursor:pointer;font-size:var(--ui-fs-small);transition:all .15s}
.tab-btn.active{background:var(--ui-accent);color:#fff;box-shadow:0 0 8px rgba(74,144,217,.3)}
.sec-card{background:var(--ui-card);border:1px solid var(--ui-border);border-radius:var(--ui-radius);padding:14px;margin-bottom:14px}
.sec-card h2{margin-top:0;font-size:var(--ui-fs-h2);border-bottom:1px solid var(--ui-border);padding-bottom:8px}
</style></head><body>
)rawliteral" + getTopBarHtml() + getBackMenuHtml() + R"rawliteral(
<h1 data-i18n="drive">Управление тягой</h1>
<div class="tab-row">
  <button type="button" class="tab-btn active" onclick="showSec('all',this)" data-i18n="all">Всё</button>
  <button type="button" class="tab-btn" onclick="showSec('th',this)" data-i18n="throttle">Газ</button>
  <button type="button" class="tab-btn" onclick="showSec('pas',this)" data-i18n="pas">PAS</button>
  <button type="button" class="tab-btn" onclick="showSec('cr',this)" data-i18n="cruise">Круиз</button>
</div>

<!-- ================= БЛОК 1: ГАЗ ================= -->
<div id="sec_th" class="sec-card">
<h2>⚡ Ручка газа (Throttle)</h2>
<form id="f_th">
<fieldset><legend>Калибровка (в реальных вольтах)</legend>
<div class="frow"><label>Текущее напряжение</label><span class="fval" id="liveV">-- В</span></div>
<div class="frow"><label for="inMinV">Вход минимум, В</label><input type="number" step="0.01" min="0" max="5" id="inMinV" name="inMinV" value=")rawliteral"; html += String(throttleInMinV, 2); html += R"rawliteral("></div>
<p class="fhint">Напряжение ручки газа в покое.</p>
<button type="button" class="cal-btn" onclick="calMin()">Захватить минимум (ручка отпущена)</button>

<div class="frow"><label for="inMaxV">Вход максимум, В</label><input type="number" step="0.01" min="0" max="5" id="inMaxV" name="inMaxV" value=")rawliteral"; html += String(throttleInMaxV, 2); html += R"rawliteral("></div>
<p class="fhint">Напряжение ручки на полном газу.</p>
<button type="button" class="cal-btn" onclick="calMax()">Захватить максимум (полный газ)</button>

<div class="frow"><label>Выход минимум, В</label><input type="number" step="0.05" min="0" max="5" name="outMinV" value=")rawliteral"; html += String(throttleOutMinV, 2); html += R"rawliteral("></div>
<p class="fhint">Холостой уровень на входе мотор-контроллера (удерживается для быстрого отклика).</p>
<div class="frow"><label>Выход максимум, В</label><input type="number" step="0.05" min="0" max="5" name="outMaxV" value=")rawliteral"; html += String(throttleOutMaxV, 2); html += R"rawliteral("></div>
</fieldset>

<fieldset><legend>Согласующие цепи (MCP6002)</legend>
<div class="frow"><label>Коэффициент делителя</label><input type="number" step="0.001" min="0.1" max="1" name="divRatio" value=")rawliteral"; html += String(throttleInputDividerRatio, 3); html += R"rawliteral("></div>
<div class="frow"><label>Коэффициент усиления ОУ</label><input type="number" step="0.01" min="1" max="3" name="gain" value=")rawliteral"; html += String(throttleOutputGain, 2); html += R"rawliteral("></div>
</fieldset>

<fieldset><legend>Мягкий старт (по умолчанию для PAS и Круиза)</legend>
<div class="chk"><label for="throttleSsEn">Мягкий старт</label><input type="checkbox" id="throttleSsEn" name="ssEn" )rawliteral"; html += throttleSoftStartEnabled?"checked":""; html += R"rawliteral(></div>
<div class="frow"><label>Время разгона, мс</label><input type="number" name="ssMs" value=")rawliteral"; html += String(throttleSoftStartMs); html += R"rawliteral("></div>
<input type="hidden" name="spEn" value=")rawliteral"; html += throttleSoftStopEnabled ? "1" : "0"; html += R"rawliteral(">
<input type="hidden" name="spMs" value=")rawliteral"; html += String(throttleSoftStopMs); html += R"rawliteral(">
<p class="fhint">Параметры разгона применяются к ручке газа, а также к PAS и Круизу (при выборе режима «По умолчанию»).</p>
</fieldset>
<button type="submit" data-i18n="save">Сохранить газ</button>
</form>
</div>
)rawliteral";

  // ================= БЛОК 2: PAS =================
  html += R"rawliteral(
<div id="sec_pas" class="sec-card">
<h2>🦶 Ассистент педалей (PAS)</h2>
<form id="f_pas">
<fieldset><legend>Ассистент PAS</legend>
<div class="chk"><label for="pasEn" data-i18n="pasOn">Включить PAS</label><input type="checkbox" id="pasEn" name="en" )rawliteral"; html += pasEnabled?"checked":""; html += R"rawliteral(></div>
</fieldset>
<fieldset><legend>Датчик</legend>
<div class="frow"><label>Магниты</label><input type="number" name="magnets" value=")rawliteral"; html += String(pasMagnetCount); html += R"rawliteral("></div>
<div class="frow"><label>Фронт сигнала</label>
<select name="edge">
<option value="2")rawliteral"; html += (pasEdgeMode==FALLING?" selected":""); html += R"rawliteral(>При уходе магнита (FALLING)</option>
<option value="3")rawliteral"; html += (pasEdgeMode==RISING?" selected":""); html += R"rawliteral(>При появлении магнита (RISING)</option>
<option value="1")rawliteral"; html += (pasEdgeMode==CHANGE?" selected":""); html += R"rawliteral(>При любом изменении (CHANGE)</option>
</select></div>
<div class="frow"><label>Угол активации</label>
<select name="angle">
<option value="90")rawliteral"; html += (pasActivationAngle==90?" selected":""); html += R"rawliteral(>90&deg;</option>
<option value="180")rawliteral"; html += (pasActivationAngle==180?" selected":""); html += R"rawliteral(>180&deg;</option>
<option value="270")rawliteral"; html += (pasActivationAngle==270?" selected":""); html += R"rawliteral(>270&deg;</option>
<option value="360")rawliteral"; html += (pasActivationAngle==360?" selected":""); html += R"rawliteral(>360&deg;</option>
</select></div>
<div class="frow"><label>Память импульса, мс</label><input type="number" name="timeout" value=")rawliteral"; html += String(pasTimeoutMs); html += R"rawliteral("></div>
<div class="frow"><label>Остановка, мс</label><input type="number" name="stopTO" value=")rawliteral"; html += String(pasStopTimeoutMs); html += R"rawliteral("></div>
</fieldset>

<fieldset><legend>Калибровка магнитов</legend>
<button type="button" onclick="calStart()" data-i18n="start">Старт калибровки (2 оборота)</button>
<button type="button" onclick="calStop()" data-i18n="ok">Готово</button>
<div id="calStat" class="cal-status">—</div>
</fieldset>

<fieldset><legend>Уровни усилия (0-)rawliteral"; html += String(PAS_MAX_LEVELS); html += R"rawliteral()</legend>
<div class="frow"><label>Количество уровней</label><input type="number" id="pas_count" name="count" min="0" max=")rawliteral"; html += String(PAS_MAX_LEVELS); html += R"rawliteral(" value=")rawliteral"; html += String(pasLevelsCount); html += R"rawliteral(" oninput="renderPasLevels()"></div>
<div id="pas_levels"></div>
<button type="button" onclick="autoDistributePas()" data-i18n="cruiseAutoDist">Автораспределение</button>
</fieldset>

<fieldset><legend>Мягкий старт</legend>
<div class="frow"><label>Режим сглаживания</label>
<select name="smMode" id="pasSmMode" onchange="togglePasCustomSmooth()">
  <option value="0")rawliteral"; html += (pasSmoothMode == 0 ? " selected" : ""); html += R"rawliteral(>По умолчанию (из настроек Газа)</option>
  <option value="1")rawliteral"; html += (pasSmoothMode == 1 ? " selected" : ""); html += R"rawliteral(>Свои настройки</option>
  <option value="2")rawliteral"; html += (pasSmoothMode == 2 ? " selected" : ""); html += R"rawliteral(>Выключено</option>
</select></div>
<div id="pasCustomBlock">
<div class="chk"><label for="pasSsEn" data-i18n="softStart">Мягкий старт</label><input type="checkbox" id="pasSsEn" name="ssEn" )rawliteral"; html += pasSoftStartEnabled?"checked":""; html += R"rawliteral(></div>
<div class="frow"><label>Время разгона, мс</label><input type="number" name="ssMs" value=")rawliteral"; html += String(pasSoftStartMs); html += R"rawliteral("></div>
</div>
<input type="hidden" name="spEn" value=")rawliteral"; html += pasSoftStopEnabled ? "1" : "0"; html += R"rawliteral(">
<input type="hidden" name="spMs" value=")rawliteral"; html += String(pasSoftStopMs); html += R"rawliteral(">
</fieldset>
<button type="submit" data-i18n="save">Сохранить PAS</button>
</form>
</div>
)rawliteral";

  // ================= БЛОК 3: КРУИЗ =================
  html += R"rawliteral(
<div id="sec_cr" class="sec-card">
<h2>🏎 Круиз-контроль (Cruise)</h2>
<form id="f_cr">
<fieldset><legend>Уровни</legend>
<div class="frow"><label>Количество уровней (1–100)</label>
<input type="number" id="cr_count" name="count" min="1" max="100" value=")rawliteral"; html += String(cruiseLevelsCount); html += R"rawliteral(" oninput="autoDistributeCruise()"></div>
<div class="frow"><label for="stPct">Начало, %</label><input type="number" id="stPct" name="stPct" step="1" min="0" max="100" oninput="autoDistributeCruise()" value=")rawliteral"; html += String((int)round(cruiseStartPercent)); html += R"rawliteral("></div>
<div class="frow"><label for="endPct">Конец, %</label><input type="number" id="endPct" name="endPct" step="1" min="0" max="100" oninput="autoDistributeCruise()" value=")rawliteral"; html += String((int)round(cruiseEndPercent)); html += R"rawliteral("></div>
<div id="cruise_levels" class="levels"></div>
</fieldset>

<fieldset><legend>Мягкий старт</legend>
<div class="frow"><label>Режим сглаживания</label>
<select name="smMode" id="cruiseSmMode" onchange="toggleCruiseCustomSmooth()">
  <option value="0")rawliteral"; html += (cruiseSmoothMode == 0 ? " selected" : ""); html += R"rawliteral(>По умолчанию (из настроек Газа)</option>
  <option value="1")rawliteral"; html += (cruiseSmoothMode == 1 ? " selected" : ""); html += R"rawliteral(>Свои настройки</option>
  <option value="2")rawliteral"; html += (cruiseSmoothMode == 2 ? " selected" : ""); html += R"rawliteral(>Выключено</option>
</select></div>
<div id="cruiseCustomBlock">
<div class="chk"><label for="cruiseSsEn">Мягкий старт</label><input type="checkbox" id="cruiseSsEn" name="ssEn" )rawliteral"; html += cruiseSoftStartEnabled ? "checked" : ""; html += R"rawliteral(></div>
<div class="frow"><label>Время разгона, мс</label><input type="number" name="ssMs" value=")rawliteral"; html += String(cruiseSoftStartMs); html += R"rawliteral("></div>
</div>
<input type="hidden" name="spEn" value=")rawliteral"; html += cruiseSoftStopEnabled ? "1" : "0"; html += R"rawliteral(">
<input type="hidden" name="spMs" value=")rawliteral"; html += String(cruiseSoftStopMs); html += R"rawliteral(">
</fieldset>

<fieldset><legend>Поведение</legend>
<div class="chk"><label for="confThr">Подтверждать газом после старта</label><input type="checkbox" id="confThr" name="confThr" )rawliteral"; html += cruiseConfirmThrottleAfterStart ? "checked" : ""; html += R"rawliteral(></div>
<div class="frow"><label>После торможения</label>
<select name="brkMode">
  <option value="0")rawliteral"; html += (cruiseAfterBrakingMode == 0 ? " selected" : ""); html += R"rawliteral(>Сброс круиза</option>
  <option value="1")rawliteral"; html += (cruiseAfterBrakingMode == 1 ? " selected" : ""); html += R"rawliteral(>Подтверждение газом</option>
  <option value="2")rawliteral"; html += (cruiseAfterBrakingMode == 2 ? " selected" : ""); html += R"rawliteral(>Восстановить</option>
</select></div>
<div class="frow"><label>После перегазовки</label>
<select name="thrMode">
  <option value="0")rawliteral"; html += (cruiseAfterThrottleMode == 0 ? " selected" : ""); html += R"rawliteral(>Сброс круиза</option>
  <option value="1")rawliteral"; html += (cruiseAfterThrottleMode == 1 ? " selected" : ""); html += R"rawliteral(>Подтверждение газом</option>
  <option value="2")rawliteral"; html += (cruiseAfterThrottleMode == 2 ? " selected" : ""); html += R"rawliteral(>Восстановить</option>
</select></div>
</fieldset>
<button type="submit" data-i18n="save">Сохранить круиз</button>
</form>
</div>
)rawliteral";

  // ================= СКРИПТЫ И ОБРАБОТЧИКИ =================
  html += getSettingsJs();
  html += R"rawliteral(<script>
function showSec(sec, btn){
  document.querySelectorAll('.tab-btn').forEach(b=>b.classList.remove('active'));
  btn.classList.add('active');
  document.getElementById('sec_th').style.display = (sec==='all'||sec==='th')?'block':'none';
  document.getElementById('sec_pas').style.display = (sec==='all'||sec==='pas')?'block':'none';
  document.getElementById('sec_cr').style.display = (sec==='all'||sec==='cr')?'block':'none';
}

// Газ
document.getElementById('f_th').addEventListener('submit',function(e){
  e.preventDefault();
  fetch('/settings/throttle/save',{method:'POST',body:new FormData(this)}).then(()=>alert('Настройки газа сохранены'));
});
let pollVBusy=false;
function pollV(){
  if(pollVBusy) return;
  pollVBusy=true;
  fetch('/status/sys').then(r=>r.json()).then(d=>{
    if(d.gas_in_v!==undefined) document.getElementById('liveV').textContent = d.gas_in_v.toFixed(2)+' В';
  }).catch(()=>{}).finally(()=>{pollVBusy=false;});
}
setInterval(pollV, 500); pollV();
function calMin(){ fetch('/settings/throttle/cal_min',{method:'POST'}).then(r=>r.json()).then(d=>{ document.getElementById('inMinV').value = d.val.toFixed(2); }); }
function calMax(){ fetch('/settings/throttle/cal_max',{method:'POST'}).then(r=>r.json()).then(d=>{ document.getElementById('inMaxV').value = d.val.toFixed(2); }); }

// PAS
const pasSaved = [)rawliteral";
  for (int i = 0; i < PAS_MAX_LEVELS; i++) { html += String(pasLevelPercent[i]); if (i<PAS_MAX_LEVELS-1) html += ","; }
  html += R"rawliteral(];
function renderPasLevels(){ renderLevelFields('pas_levels','lvl',pasSaved,document.getElementById('pas_count').value,'Уровень — усилие (%)',100); }
function autoDistributePas(){ distributeLevels(pasSaved,parseInt(document.getElementById('pas_count').value)||0); renderPasLevels(); }
renderPasLevels();
function togglePasCustomSmooth(){ document.getElementById('pasCustomBlock').style.display = document.getElementById('pasSmMode').value === '1' ? '' : 'none'; }
togglePasCustomSmooth();
document.getElementById('f_pas').addEventListener('submit',function(e){
  e.preventDefault();
  const d=new FormData(this);
  const count=parseInt(document.getElementById('pas_count').value)||0;
  for(let i=0;i<count;i++){ const el=document.querySelector('#sec_pas [name=lvl'+i+']'); if(el) d.append('lvl'+i, el.value); }
  fetch('/settings/pas/save',{method:'POST',body:d}).then(()=>alert('Настройки PAS сохранены'));
});
let calTimer=null;
function calStart(){ fetch('/settings/pas/cal_start').then(()=>{ calTimer=setInterval(calPoll,250); document.getElementById('calStat').textContent='Крути педали ровно 2 оборота...'; }); }
function calStop(){ fetch('/settings/pas/cal_stop').then(r=>r.json()).then(d=>{ if(calTimer){clearInterval(calTimer);calTimer=null;} if(d.finished){ document.getElementById('calStat').textContent='Готово! Магнитов: '+d.magnets; } }); }
function calPoll(){ fetch('/settings/pas/cal_status').then(r=>r.json()).then(d=>{ if(d.finished){ if(calTimer){clearInterval(calTimer);calTimer=null;} document.getElementById('calStat').textContent='Готово! Магнитов: '+d.magnets; } else { const estM=Math.round((d.pulses||0)/2); document.getElementById('calStat').textContent='Импульсов: '+d.pulses+' → магнитов: ~'+estM; } }); }

// Круиз
const crSaved = [)rawliteral";
  for (int i = 0; i < CRUISE_MAX_LEVELS; i++) { html += String(cruiseLevelPercent[i]); if (i < CRUISE_MAX_LEVELS - 1) html += ","; }
  html += R"rawliteral(];
function renderCruiseLevels(){ renderLevelFields('cruise_levels','lvl',crSaved,document.getElementById('cr_count').value,'Цель, % — уровень',100); }
function autoDistributeCruise(){
  const count=parseInt(document.getElementById('cr_count').value)||0;
  const st=parseFloat(document.getElementById('stPct').value)||0;
  const end=parseFloat(document.getElementById('endPct').value)||100;
  distributeLevels(crSaved,count,st,end); renderCruiseLevels();
}
renderCruiseLevels();
function toggleCruiseCustomSmooth(){ document.getElementById('cruiseCustomBlock').style.display = document.getElementById('cruiseSmMode').value === '1' ? '' : 'none'; }
toggleCruiseCustomSmooth();
document.getElementById('f_cr').addEventListener('submit',function(e){
  e.preventDefault();
  const d=new FormData(this);
  const count=parseInt(document.getElementById('cr_count').value)||0;
  const lvls=[];
  for(let i=0;i<count;i++){ const el=document.querySelector('#sec_cr [name=lvl'+i+']'); if(el) lvls.push(parseFloat(el.value)||0); }
  d.append('levelsJson', JSON.stringify(lvls));
  fetch('/settings/cruise/save',{method:'POST',body:d}).then(()=>alert('Настройки круиза сохранены'));
});
</script>
)rawliteral" + getTopBarJs() + R"rawliteral(
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}
