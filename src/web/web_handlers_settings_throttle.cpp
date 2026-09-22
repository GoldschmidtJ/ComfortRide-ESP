#include "web/web_handlers_settings.h"
#include <WebServer.h>

#include "web/param_utils.h"   // getArgFloat/getArgInt (семантика toFloat/toInt)
#include "core/throttle.h"     // throttleInMinV/MaxV, hwThrottleInV
#include "system/storage.h"    // throttleSettingsSave
#include "system/inputs.h"     // ownBrakeCutoffEnabled
#include "web/web_ui.h"        // getTopBarCss/getSettingsCss/getTopBarHtml/getBackMenuHtml/getSettingsJs
#include "web/html_pages.h"    // getTopBarJs
#include "web/web_routes.h"    // server

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
  throttleInMinV = constrain(getArgFloat(server, "inMinV"), 0.0f, 5.0f);
  throttleInMaxV = constrain(getArgFloat(server, "inMaxV"), 0.0f, 5.0f);
  if (throttleInMaxV < throttleInMinV + 0.1f) throttleInMaxV = throttleInMinV + 0.1f;
  if (throttleInMaxV > 5.0f) {
    throttleInMaxV = 5.0f;
    throttleInMinV = min(throttleInMinV, throttleInMaxV - 0.1f);
  }
  throttleOutMinV = constrain(getArgFloat(server, "outMinV"), 0.0f, 5.0f);
  throttleOutMaxV = constrain(getArgFloat(server, "outMaxV"), 0.0f, 5.0f);
  if (throttleOutMaxV < throttleOutMinV + 0.1f) throttleOutMaxV = throttleOutMinV + 0.1f;
  if (throttleOutMaxV > 5.0f) {
    throttleOutMaxV = 5.0f;
    throttleOutMinV = min(throttleOutMinV, throttleOutMaxV - 0.1f);
  }
  float newDivRatio = getArgFloat(server, "divRatio");
  if (newDivRatio > 0.05f && newDivRatio <= 1.0f) throttleInputDividerRatio = newDivRatio;
  float newGain = getArgFloat(server, "gain");
  if (newGain >= 1.0f) throttleOutputGain = newGain;
  throttleSoftStartEnabled = server.hasArg("ssEn");
  throttleSoftStopEnabled = getArgInt(server, "spEn") != 0; // скрытое поле: значение сохраняем, UI скрыт
  throttleSoftStartMs = getArgInt(server, "ssMs");
  throttleSoftStopMs = getArgInt(server, "spMs");
  ownBrakeCutoffEnabled = true; // дублирование тормоза всегда включено (опция скрыта в UI)
  throttleSettingsSave();
  server.send(200, "text/plain", "OK");
}
