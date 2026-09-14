#include "web/web_handlers_pins.h"
#include <WebServer.h>
#include "system/hardware_config.h"

extern WebServer server;

// Полные определения структур из main.cpp


// Pin configuration
extern PinConfig pinConfig;
extern const PinConfig PIN_CONFIG_DEFAULTS;
extern const PinRole pinRoles[PIN_ROLE_COUNT];
extern char pinRoleNames[PIN_ROLE_COUNT][USER_LABEL_SIZE];
extern CustomPinRole customPins[CUSTOM_PIN_MAX];
extern bool pinConfigCustom;

// Pin helper functions (from main.cpp)
extern bool gpioExists(int g);
extern bool gpioHasAdc(int g);
extern bool gpioIsAdc2(int g);
extern bool gpioHasDac(int g);
extern bool gpioInputOnly(int g);
extern bool gpioIsStrap(int g);
extern bool gpioIsUart(int g);
extern bool gpioIsFlash(int g);

// Helper functions
extern int16_t& pinField(PinConfig &cfg, int role);
extern String pinRoleName(int role);
extern bool normalizeUserLabel(String &s);
extern void setUserLabel(char *dest, const String &value);

// Functions
extern void pinSettingsSave();
extern void reapplyPinConfig();
extern String validatePinConfig(PinConfig &cfg, String *warnings);
extern String getTopBarCss();
extern String getSettingsCss();
extern String getTopBarHtml();
extern String getTopBarJs();
extern String htmlEscape(const String &value);

// ================= Веб: конструктор распиновки =================
String gpioCapabilityText(int g) {
  String text;
  if (gpioHasAdc(g)) text += gpioIsAdc2(g) ? "АЦП2" : "АЦП1";
  if (gpioHasDac(g)) text += String(text.length() ? " · " : "") + "ЦАП";
  if (gpioInputOnly(g)) text += String(text.length() ? " · " : "") + "только вход, без подтяжки";
  if (gpioIsStrap(g)) text += String(text.length() ? " · " : "") + "strap";
  if (gpioIsUart(g)) text += String(text.length() ? " · " : "") + "UART0";
  if (gpioIsFlash(g)) text += String(text.length() ? " · " : "") + "Flash";
  return text.length() ? text : "цифровой GPIO";
}

String pinOptionsHtml(int selected, bool output, bool needAdc, bool needDac, bool needPullup) {
  String html = "";
  for (int g = 0; g <= 39; g++) {
    if (!gpioExists(g)) continue;
    bool ok = true;
    String note = "";
    if (gpioIsFlash(g)) { ok = false; note = " (Flash)"; }
    else if (gpioIsUart(g)) { ok = false; note = " (UART0)"; }
    else if (output && gpioInputOnly(g)) { ok = false; note = " (только вход)"; }
    else if (needPullup && gpioInputOnly(g)) { ok = false; note = " (нет подтяжки)"; }
    else if (needAdc && !gpioHasAdc(g)) { ok = false; note = " (нет АЦП)"; }
    else if (needDac && !gpioHasDac(g)) { ok = false; note = " (нет ЦАП)"; }
    if (gpioIsStrap(g)) note += " (страп)";
    if (needAdc && gpioIsAdc2(g)) note += " (ADC2/Wi-Fi)";
    String opt = "<option value=\"" + String(g) + "\" data-cap=\"" + htmlEscape(gpioCapabilityText(g)) + "\"";
    if (g == selected) opt += " selected";
    if (!ok) opt += " disabled";
    opt += ">GPIO" + String(g) + note + "</option>";
    html += opt;
  }
  return html;
}

void handlePinsPage() {
  String errors, warnings;
  PinConfig tmp = pinConfig;
  errors = validatePinConfig(tmp, &warnings);

  // Карта занятости пинов
  bool used[40] = {false};
  for (int i = 0; i < PIN_ROLE_COUNT; i++) {
    int g = pinField(pinConfig, i);
    if (g >= 0 && g <= 39) used[g] = true;
  }
  for (int i = 0; i < CUSTOM_PIN_MAX; i++) {
    int g = customPins[i].gpio;
    if (customPins[i].used && g >= 0 && g <= 39) used[g] = true;
  }
  String freeList = "";
  int freeCount = 0;
  for (int g = 0; g <= 39; g++) {
    if (!gpioExists(g) || gpioIsFlash(g) || gpioIsUart(g) || used[g]) continue;
    freeList += String(freeCount ? ", " : "") + "GPIO" + String(g);
    freeCount++;
  }

  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Распиновка GPIO</title>
<style>
)rawliteral" + getTopBarCss() + getSettingsCss() + R"rawliteral(
body{max-width:560px}
.pin-row{display:grid;grid-template-columns:minmax(0,1fr) minmax(118px,170px);align-items:start;gap:6px 10px;background:var(--ui-card);border:1px solid var(--ui-border);border-radius:8px;padding:8px 10px;margin-bottom:6px;box-sizing:border-box}.pin-main,.pin-side{min-width:0}.pin-name{display:block;box-sizing:border-box;width:100%;min-width:0;margin:0;background:var(--ui-button);color:var(--ui-text);border:1px solid var(--ui-border);border-radius:6px;padding:6px;font-size:14px}.pin-row select{box-sizing:border-box;width:100%;min-width:0;margin:0;background:var(--ui-button);color:var(--ui-text);border:1px solid var(--ui-border);border-radius:6px;padding:6px;font-size:13px}.pin-side{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:5px}.custom-row .pin-side{grid-template-columns:minmax(0,1fr) auto auto}.pin-meta{grid-column:1/-1;display:grid;grid-template-columns:minmax(0,1fr) minmax(118px,170px);gap:10px;color:var(--ui-muted);font-size:11px;line-height:1.25}.pin-cap{text-align:right}.custom-mode{grid-column:1/-1;display:flex;align-items:center;justify-content:flex-end;gap:6px;margin-top:1px}.custom-mode label{font-size:10px;color:var(--ui-muted)}.custom-mode .pin-mode{width:min(170px,55%)}.row-save,.row-del{display:none!important;width:44px!important;min-width:44px;margin:0!important;padding:6px!important}.pin-row.dirty .row-save{display:block!important}.custom-row .row-del{display:block!important}.row-msg{grid-column:1/-1;min-height:0;font-size:12px;white-space:pre-line}.row-msg.msg-ok{color:var(--ui-success)}.row-msg.msg-err{color:var(--ui-danger)}.add-pin{border-style:dashed;color:var(--ui-accent)}
@media(max-width:420px){body{padding-left:12px;padding-right:12px}.top-bar-sticky{margin-left:-12px;margin-right:-12px}.pin-row{grid-template-columns:minmax(0,1fr) minmax(105px,38%);gap:5px 7px;padding:7px}.pin-name,.pin-row select{font-size:14px;padding:6px}.pin-meta{grid-template-columns:minmax(0,1fr) minmax(105px,38%);gap:7px;font-size:11px}.custom-mode .pin-mode{width:min(150px,60%)}}
button{margin-top:10px}
.msg{padding:10px;border-radius:8px;margin:10px 0;font-size:13px;white-space:pre-line}
.msg-err{border:1px solid var(--ui-danger);color:var(--ui-danger)}
.msg-warn{border:1px solid var(--ui-warning);color:var(--ui-warning)}
.msg-ok{border:1px solid var(--ui-success);color:var(--ui-success)}
.free{background:var(--ui-card);border:1px solid var(--ui-border);border-radius:8px;padding:10px;margin:10px 0;font-size:13px;color:var(--ui-muted)}
h2{font-size:15px;margin:16px 0 8px}.pin-title{font-size:20px}.pin-badges{color:var(--ui-muted);font-size:11px}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a class="back-link" href="/">&larr; Меню</a></p>
<h1 class="pin-title">GPIO</h1>
<p class="hint">Назначение пинов сохраняется в NVS и применяется после перезагрузки.
)rawliteral" + String(pinConfigCustom ? "Сейчас действует <b>пользовательская</b> конфигурация." : "Сейчас действует <b>заводская</b> конфигурация.") + R"rawliteral(</p>
<div class="free"><b>Свободные пины:</b> )rawliteral" + (freeCount ? freeList : "нет") + R"rawliteral(</div>
)rawliteral" + (errors.length() ? "<div class=\"msg msg-err\">" + errors + "</div>" : "") + R"rawliteral(
)rawliteral" + (warnings.length() ? "<div class=\"msg msg-warn\">" + warnings + "</div>" : "") + R"rawliteral(
<form method="POST" action="/settings/pins/save">
)rawliteral";

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(html);
  html = "";

  for (int i = 0; i < PIN_ROLE_COUNT; i++) {
    const PinRole &r = pinRoles[i];
    int g = pinField(pinConfig, i);
    String badges = String(r.output ? "[выход] " : "") + (r.adc ? "[АЦП] " : "") + (r.dac ? "[ЦАП] " : "") + (r.pullup ? "[подтяжка] " : "") + (r.pwm ? "[ШИМ]" : "");
    html += "<div class=\"pin-row\" data-row=\"" + String(i) + "\" data-slot=\"" + String(i) + "\" data-orig-name=\"" + htmlEscape(pinRoleName(i)) + "\" data-orig-gpio=\"" + String(g) + "\">";
    html += "<div class=\"pin-main\"><input class=\"pin-name\" type=\"text\" maxlength=\"64\" name=\"nm" + String(i) + "\" value=\"" + htmlEscape(pinRoleName(i)) + "\" aria-label=\"Название пина\"></div>";
    html += "<div class=\"pin-side\"><select name=\"" + String(r.key) + "\" aria-label=\"Выбор GPIO\">" + pinOptionsHtml(g, r.output, r.adc, r.dac, r.pullup) + "</select>";
    html += "<button type=\"button\" class=\"row-save\" title=\"Сохранить строку\" aria-label=\"Сохранить изменения\">✓</button></div>";
    html += "<div class=\"pin-meta\"><span class=\"pin-badges\">" + badges + "</span><span class=\"pin-cap\">" + htmlEscape(gpioCapabilityText(g)) + "</span></div>";
    html += "<div class=\"row-msg\" aria-live=\"polite\"></div></div>";
    server.sendContent(html);
    html = "";
  }

  // Дополнительные пользовательские GPIO
  html += "<h2>Дополнительные GPIO</h2>";
  html += "<p class=\"hint\">Резервируют и инициализируют пин, но не привязаны к функциям контроллера. Применяются сразу, без перезагрузки.</p>";
  html += "<div id=\"customRows\">";
  for (int i = 0; i < CUSTOM_PIN_MAX; i++) {
    CustomPinRole &c = customPins[i];
    if (!c.used) continue;
    html += customPinRowHtml(i, c);
    server.sendContent(html);
    html = "";
  }
  html += "</div>";
  html += "<button type=\"button\" id=\"addCustom\" class=\"add-pin\">+ GPIO</button>";

  html += R"rawliteral(
<button type="submit">Проверить и сохранить</button>
</form>
<form method="POST" action="/settings/pins/reset" onsubmit="return confirm('Вернуть заводскую распиновку?')">
<button type="submit">Сбросить к заводской</button>
</form>
)rawliteral" + getTopBarJs() + R"rawliteral(
<script>
function rowMsg(row, ok, text) {
  var m = row.querySelector('.row-msg');
  if (!m) return;
  m.textContent = text;
  m.className = 'row-msg ' + (ok ? 'msg-ok' : 'msg-err');
  if (text) { setTimeout(function(){ if (m.textContent === text) { m.textContent = ''; m.className = 'row-msg'; } }, 6000); }
}
function rowDirty(row) {
  var name = row.querySelector('.pin-name').value;
  var sel = row.querySelector('select');
  var mode = row.querySelector('.pin-mode');
  var changed = name !== row.dataset.origName;
  if (sel) changed = changed || sel.value !== row.dataset.origGpio;
  if (mode) changed = changed || mode.value !== row.dataset.origMode;
  row.classList.toggle('dirty', changed);
}
function rowBusy(row, busy) {
  var b = row.querySelector('.row-save');
  if (b) { b.disabled = busy; b.textContent = busy ? '…' : '✓'; }
}
function serialArgs(row) {
  var fd = new FormData();
  fd.append('slot', row.dataset.slot);
  fd.append('nm', row.querySelector('.pin-name').value);
  if (row.classList.contains('custom-row')) {
    fd.append('gpio', row.querySelector('.pin-gpio').value);
    fd.append('mode', row.querySelector('.pin-mode').value);
  } else {
    fd.append('gpio', row.querySelector('select').value);
  }
  return fd;
}
async function saveRow(row, url) {
  var slot = row.dataset.slot;
  var name = row.querySelector('.pin-name');
  name.value = name.value.trim();
  if (!name.reportValidity()) return;
  rowBusy(row, true);
  try {
    var r = await fetch(url, { method: 'POST', body: serialArgs(row) });
    var t = await r.text();
    rowMsg(row, r.ok, t);
    if (r.ok) {
      row.dataset.origName = row.querySelector('.pin-name').value;
      var sel = row.querySelector('select');
      var gpioSel = row.querySelector('.pin-gpio');
      var mode = row.querySelector('.pin-mode');
      if (gpioSel) row.dataset.origGpio = gpioSel.value;
      else if (sel) row.dataset.origGpio = sel.value;
      if (mode) row.dataset.origMode = mode.value;
      row.classList.remove('dirty');
      if (url.indexOf('/custom/') !== -1) { setTimeout(function(){ location.reload(); }, 800); }
    }
  } catch (e) {
    rowMsg(row, false, 'Ошибка сети: ' + e);
  }
  rowBusy(row, false);
}
function updatePinCapability(row) {
  var sel = row.querySelector('.pin-gpio') || row.querySelector('.pin-side select');
  var cap = row.querySelector('.pin-cap');
  if (!sel || !cap || !sel.options.length) return;
  cap.textContent = sel.options[sel.selectedIndex].dataset.cap || '';
}
function bindRow(row, url) {
  row.addEventListener('input', function(){ rowDirty(row); });
  row.addEventListener('change', function(){ rowDirty(row); updatePinCapability(row); });
  updatePinCapability(row);
  var b = row.querySelector('.row-save');
  if (b) b.addEventListener('click', function(){ saveRow(row, url); });
}
document.querySelectorAll('.pin-row[data-row]').forEach(function(row){ bindRow(row, '/settings/pins/row/save'); });
document.querySelectorAll('.custom-row').forEach(function(row){ bindRow(row, '/settings/pins/custom/save'); });
document.querySelectorAll('.custom-row .row-del').forEach(function(del){
  del.addEventListener('click', async function(){
    var row = del.closest('.custom-row');
    if (!confirm('Удалить этот GPIO?')) return;
    del.disabled = true;
    try {
      var fd = new FormData();
      fd.append('slot', row.dataset.slot);
      var r = await fetch('/settings/pins/custom/delete', { method: 'POST', body: fd });
      var t = await r.text();
      rowMsg(row, r.ok, t);
      if (r.ok) setTimeout(function(){ location.reload(); }, 800);
      else del.disabled = false;
    } catch (e) { rowMsg(row, false, 'Ошибка сети: ' + e); del.disabled = false; }
  });
});
var add = document.getElementById('addCustom');
if (add) add.addEventListener('click', async function(){
  add.disabled = true;
  try {
    var fd = new FormData();
    fd.append('nm', 'GPIO ' + String(document.querySelectorAll('.custom-row').length + 1));
    var r = await fetch('/settings/pins/custom/save', { method: 'POST', body: fd });
    var t = await r.text();
    if (!r.ok) { alert(t); add.disabled = false; return; }
    location.reload();
  } catch (e) { alert('Ошибка сети: ' + e); add.disabled = false; }
});
</script>
</body></html>
)rawliteral";
  server.sendContent(html);
  server.sendContent("");
}

String customPinRowHtml(int i, const CustomPinRole &c) {
  String html = "<div class=\"pin-row custom-row\" data-slot=\"" + String(i) + "\" data-orig-name=\"" + htmlEscape(c.name) + "\" data-orig-gpio=\"" + String(c.gpio) + "\" data-orig-mode=\"" + String(c.mode) + "\">";
  html += "<div class=\"pin-main\"><input class=\"pin-name\" type=\"text\" maxlength=\"64\" value=\"" + htmlEscape(c.name) + "\" aria-label=\"Название пользовательского GPIO\"></div>";
  html += "<div class=\"pin-side\">";
  html += "<select class=\"pin-gpio\" aria-label=\"Выбор GPIO\">";
  for (int g = 0; g <= 39; g++) {
    if (!gpioExists(g) || gpioIsFlash(g) || gpioIsUart(g)) continue;
    html += "<option value=\"" + String(g) + "\" data-cap=\"" + htmlEscape(gpioCapabilityText(g)) + "\"" + (g == c.gpio ? " selected" : "") + ">GPIO" + String(g) + "</option>";
  }
  html += "</select>";
  html += "<button type=\"button\" class=\"row-save\" title=\"Сохранить\" aria-label=\"Сохранить\">✓</button>";
  html += "<button type=\"button\" class=\"row-del\" title=\"Удалить\" aria-label=\"Удалить\">✕</button></div>";
  html += "<div class=\"pin-meta\"><span class=\"pin-badges\">[доп.]</span><span class=\"pin-cap\">" + htmlEscape(gpioCapabilityText(c.gpio)) + "</span></div>";
  html += "<div class=\"custom-mode\"><label>Режим</label><select class=\"pin-mode\" aria-label=\"Режим GPIO\">";
  const char* modes[] = {"вход", "вход + подтяжка", "выход"};
  for (int m = 0; m < 3; m++) html += "<option value=\"" + String(m) + "\"" + (m == (int)c.mode ? " selected" : "") + ">" + modes[m] + "</option>";
  html += "</select></div>";
  html += "<div class=\"row-msg\" aria-live=\"polite\"></div></div>";
  return html;
}

void handlePinRowSave() {
  if (!server.hasArg("slot")) { server.send(400, "text/plain", "Нет параметра slot"); return; }
  int slot = server.arg("slot").toInt();
  if (slot < 0 || slot >= PIN_ROLE_COUNT) { server.send(400, "text/plain", "Неверный слот"); return; }
  String name = server.arg("nm");
  int g = server.hasArg("gpio") ? server.arg("gpio").toInt() : pinField(pinConfig, slot);
  if (!normalizeUserLabel(name)) { server.send(400, "text/plain", "Название: от 1 до 64 символов, без < и >"); return; }
  for (int j = 0; j < PIN_ROLE_COUNT; j++) {
    if (j == slot) continue;
    if (String(pinRoleNames[j]).length() && name.equalsIgnoreCase(String(pinRoleNames[j]))) { server.send(400, "text/plain", "Название уже используется: " + name); return; }
  }
  for (int j = 0; j < CUSTOM_PIN_MAX; j++) {
    if (!customPins[j].used) continue;
    if (name.equalsIgnoreCase(String(customPins[j].name))) { server.send(400, "text/plain", "Название уже используется: " + name); return; }
  }
  PinConfig next = pinConfig;
  pinField(next, slot) = (int16_t)g;
  String warnings;
  String errors = validatePinConfig(next, &warnings);
  if (errors.length()) { server.send(400, "text/plain", errors); return; }
  pinConfig = next;
  setUserLabel(pinRoleNames[slot], name);
  pinConfigCustom = true;
  pinSettingsSave();
  server.send(200, "text/plain", "Строка сохранена. Перезагрузка для применения...");
}

void handleCustomPinSave() {
  bool adding = !server.hasArg("slot");
  int slot = adding ? -1 : server.arg("slot").toInt();
  String name = server.arg("nm");
  int gpio = server.hasArg("gpio") ? server.arg("gpio").toInt() : -1;
  int mode = server.hasArg("mode") ? server.arg("mode").toInt() : -1;
  if (adding) {
    int freeSlot = -1;
    for (int i = 0; i < CUSTOM_PIN_MAX; i++) if (!customPins[i].used) { freeSlot = i; break; }
    if (freeSlot == -1) { server.send(400, "text/plain", "Достигнут лимит дополнительных GPIO (" + String(CUSTOM_PIN_MAX) + ")"); return; }
    slot = freeSlot;
    if (name.length() == 0) name = "Доп. GPIO";
    if (gpio == -1) {
      bool used[40] = {false};
      for (int i = 0; i < PIN_ROLE_COUNT; i++) { int gg = pinField(pinConfig, i); if (gg >= 0 && gg <= 39) used[gg] = true; }
      for (int i = 0; i < CUSTOM_PIN_MAX; i++) { if (customPins[i].used && customPins[i].gpio >= 0 && customPins[i].gpio <= 39) used[customPins[i].gpio] = true; }
      for (int gg = 0; gg <= 39; gg++) {
        if (gpioExists(gg) && !gpioIsFlash(gg) && !gpioIsUart(gg) && !used[gg]) { gpio = gg; break; }
      }
      if (gpio == -1) { server.send(400, "text/plain", "Нет свободных GPIO"); return; }
      mode = CUSTOM_PIN_INPUT;
    }
  } else {
    if (slot < 0 || slot >= CUSTOM_PIN_MAX) { server.send(400, "text/plain", "Неверный слот"); return; }
    if (!customPins[slot].used) { server.send(400, "text/plain", "Слот не занят"); return; }
  }
  if (!normalizeUserLabel(name)) { server.send(400, "text/plain", "Название: от 1 до 64 символов, без < и >"); return; }
  if (gpio < 0 || gpio > 39 || !gpioExists(gpio) || gpioIsFlash(gpio) || gpioIsUart(gpio)) { server.send(400, "text/plain", "Недопустимый GPIO"); return; }
  if (mode < 0 || mode > 2) { server.send(400, "text/plain", "Неверный режим"); return; }
  if (gpioInputOnly(gpio) && mode == CUSTOM_PIN_OUTPUT) { server.send(400, "text/plain", "GPIO" + String(gpio) + " поддерживает только вход"); return; }
  if (gpioInputOnly(gpio) && mode == CUSTOM_PIN_INPUT_PULLUP) { server.send(400, "text/plain", "GPIO" + String(gpio) + " не имеет внутренней подтяжки"); return; }
  for (int j = 0; j < PIN_ROLE_COUNT; j++) {
    if (pinField(pinConfig, j) == gpio) { server.send(400, "text/plain", "GPIO" + String(gpio) + " уже занят системной ролью"); return; }
    if (String(pinRoleNames[j]).length() && name.equalsIgnoreCase(String(pinRoleNames[j]))) { server.send(400, "text/plain", "Название уже используется: " + name); return; }
  }
  for (int j = 0; j < CUSTOM_PIN_MAX; j++) {
    if (j != slot && customPins[j].used && customPins[j].gpio == gpio) { server.send(400, "text/plain", "GPIO" + String(gpio) + " уже занят другой доп. ролью"); return; }
    if (j != slot && customPins[j].used && name.equalsIgnoreCase(String(customPins[j].name))) { server.send(400, "text/plain", "Название уже используется: " + name); return; }
  }
  customPins[slot].used = 1;
  customPins[slot].mode = (uint8_t)mode;
  customPins[slot].gpio = (int16_t)gpio;
  strncpy(customPins[slot].name, name.c_str(), USER_LABEL_SIZE - 1);
  customPins[slot].name[USER_LABEL_SIZE - 1] = 0;
  pinMode(gpio, mode == CUSTOM_PIN_OUTPUT ? OUTPUT : (mode == CUSTOM_PIN_INPUT_PULLUP ? INPUT_PULLUP : INPUT));
  if (mode == CUSTOM_PIN_OUTPUT) digitalWrite(gpio, LOW);
  pinSettingsSave();
  server.send(200, "text/plain", "Доп. GPIO сохранён");
}

void handleCustomPinDelete() {
  int slot = server.hasArg("slot") ? server.arg("slot").toInt() : -1;
  if (slot < 0 || slot >= CUSTOM_PIN_MAX) { server.send(400, "text/plain", "Неверный слот"); return; }
  if (!customPins[slot].used) { server.send(400, "text/plain", "Слот не занят"); return; }
  customPins[slot] = CustomPinRole();
  pinSettingsSave();
  server.send(200, "text/plain", "Доп. GPIO удалён");
}

void handlePinsSave() {
  PinConfig next = pinConfig;
  char nextNames[PIN_ROLE_COUNT][USER_LABEL_SIZE] = {};
  String errors = "";
  for (int i = 0; i < PIN_ROLE_COUNT; i++) {
    if (server.hasArg(pinRoles[i].key)) pinField(next, i) = (int16_t)server.arg(pinRoles[i].key).toInt();
    String name = server.arg("nm" + String(i));
    if (!normalizeUserLabel(name)) errors += "Название пина " + String(i + 1) + ": от 1 до 64 символов, без < и >\n";
    else {
      setUserLabel(nextNames[i], name);
      for (int j = 0; j < i; j++) if (String(nextNames[j]).length() && name.equalsIgnoreCase(String(nextNames[j]))) errors += "Названия пинов не должны повторяться: " + name + "\n";
    }
  }
  String warnings;
  errors += validatePinConfig(next, &warnings);
  if (errors.length()) {
    server.send(400, "text/plain", "Ошибки в распиновке, конфиг не сохранён:\n" + errors);
    return;
  }
  pinConfig = next;
  memcpy(pinRoleNames, nextNames, sizeof(pinRoleNames));
  pinConfigCustom = true;
  pinSettingsSave();
  server.send(200, "text/plain", "Распиновка сохранена. Перезагрузка для применения...");
  delay(1500);
  ESP.restart();
}

void handlePinsReset() {
  pinConfig = PIN_CONFIG_DEFAULTS;
  memset(pinRoleNames, 0, sizeof(pinRoleNames));
  for (int i = 0; i < CUSTOM_PIN_MAX; i++) customPins[i] = CustomPinRole();
  pinConfigCustom = false;
  pinSettingsSave();
  server.send(200, "text/plain", "Возвращена заводская распиновка. Перезагрузка...");
  delay(1500);
  ESP.restart();
}

