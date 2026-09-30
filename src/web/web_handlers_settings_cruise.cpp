#include "web/web_handlers_settings.h"
#include <WebServer.h>
#include <math.h>              // round

#include "core/cruise.h"       // cruise*-настройки, CRUISE_MAX_LEVELS
#include "core/pas.h"          // SmoothMode (SMOOTH_MODE_*)
#include "web/param_utils.h"   // getArgInt (семантика toInt)
#include "system/storage.h"    // cruiseSettingsSave
#include "web/web_ui.h"        // getTopBarCss/getSettingsCss/getTopBarHtml/getBackMenuHtml/getSettingsJs
#include "web/html_pages.h"    // getTopBarJs
#include "web/web_routes.h"    // server

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
<div class="frow"><label>Режим сглаживания</label>
<select name="smMode" id="cruiseSmMode" onchange="toggleCruiseCustomSmooth()">
  <option value="0")rawliteral"; html += (cruiseSmoothMode == 0 ? " selected" : ""); html += R"rawliteral(>По умолчанию (из настроек Газа)</option>
  <option value="1")rawliteral"; html += (cruiseSmoothMode == 1 ? " selected" : ""); html += R"rawliteral(>Свои настройки</option>
  <option value="2")rawliteral"; html += (cruiseSmoothMode == 2 ? " selected" : ""); html += R"rawliteral(>Выключено</option>
</select></div>
<div id="cruiseCustomBlock">
<div class="chk"><label for="cruiseSsEn">Мягкий старт</label><input type="checkbox" id="cruiseSsEn" name="ssEn" )rawliteral";
  html += cruiseSoftStartEnabled ? "checked" : "";
  html += R"rawliteral(></div>
<div class="frow"><label>Время разгона, мс</label><input type="number" name="ssMs" value=")rawliteral";
  html += String(cruiseSoftStartMs);
  html += R"rawliteral("></div>
</div>
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
function toggleCruiseCustomSmooth(){
  document.getElementById('cruiseCustomBlock').style.display = document.getElementById('cruiseSmMode').value === '1' ? '' : 'none';
}
toggleCruiseCustomSmooth();
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
  cruiseLevelsCount = getArgInt(server, "count");
  if (cruiseLevelsCount < 1) cruiseLevelsCount = 1; // 0 уровней делает круиз неработоспособным
  if (cruiseLevelsCount > CRUISE_MAX_LEVELS) cruiseLevelsCount = CRUISE_MAX_LEVELS;
  
  if (server.hasArg("stPct")) cruiseStartPercent = constrain(getArgInt(server, "stPct"), 0, 100);
  if (server.hasArg("endPct")) cruiseEndPercent = constrain(getArgInt(server, "endPct"), 0, 100);

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
        cruiseLevelPercent[i] = constrain(getArgInt(server, key), 0, 100);
      }
    }
  }

  cruiseSoftStartEnabled = server.hasArg("ssEn");
  cruiseSoftStopEnabled = getArgInt(server, "spEn") != 0; // скрытое поле: значение сохраняем, UI скрыт
  cruiseSoftStartMs = getArgInt(server, "ssMs");
  cruiseSoftStopMs = getArgInt(server, "spMs");
  {
    int sm = getArgInt(server, "smMode");
    cruiseSmoothMode = (uint8_t)constrain(sm, (int)SMOOTH_MODE_DEFAULT, (int)SMOOTH_MODE_OFF);
  }
  cruiseConfirmThrottleAfterStart = server.hasArg("confThr");
  if (server.hasArg("brkMode")) cruiseAfterBrakingMode = getArgInt(server, "brkMode");
  if (server.hasArg("thrMode")) cruiseAfterThrottleMode = getArgInt(server, "thrMode");
  cruiseSettingsSave();
  server.send(200, "text/plain", "OK");
}


