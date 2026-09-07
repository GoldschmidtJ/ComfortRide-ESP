#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <MD_MAX72xx.h>

// WiFi Defaults
const char* ssid = "Waffle";
const char* password = "gotohell";

// Pins
const int THROTTLE_ADC_PIN = 34;
const int THROTTLE_DAC_PIN = 25;
const int BRAKE_PIN        = 27;
const int PAS_SENSOR_PIN   = 14;
const int PAS_BUTTON_PIN   = 13;

#define DISP_CLK_PIN  16
#define DISP_DATA_PIN 12
#define DISP_CS_PIN   5
#define MAX_DEVICES   8

MD_MAX72XX display = MD_MAX72XX(MD_MAX72XX::PAROLA_HW, DISP_DATA_PIN, DISP_CLK_PIN, DISP_CS_PIN, MAX_DEVICES);

const int JOY_VRX_PIN = 36;
const int JOY_VRY_PIN = 39;
const int JOY_SW_PIN  = 35;

const float HW_MAX_VOLTAGE = 3.3f;
WebServer server(80);
Preferences prefs;

// Forward declarations
void wifiCredsSave(const String &newSsid, const String &newPass);
void wifiConnect();
void setThrottleOutputSafeZero();
void throttleSettingsSave();
void throttleSettingsLoad();
void pasSettingsSave();
void pasSettingsLoad();
void reattachPasInterrupt();

bool isBrakePressed() { return digitalRead(BRAKE_PIN) == LOW; }
bool ownBrakeCutoffEnabled = true;

float throttleInputDividerRatio = 20.0f / 30.0f;
float throttleOutputGain = 1.0f + 3.3f / 10.0f;
float throttleInMinV = 0.8f, throttleInMaxV = 3.6f;
float throttleOutMinV = 0.8f, throttleOutMaxV = 4.0f;

bool throttleSoftStartEnabled = false;
bool throttleSoftStopEnabled = false;
unsigned long throttleSoftStartMs = 500;
unsigned long throttleSoftStopMs = 500;
float throttleSmoothOutV = 0;
unsigned long throttleSmoothLastMs = 0;

float calibrateThrottleV(float rawV) {
  float inMin = throttleInMinV;
  float inMax = (throttleInMaxV > inMin + 0.01f) ? throttleInMaxV : (inMin + 0.01f);
  float outMin = throttleOutMinV;
  float outMax = (throttleOutMaxV > outMin + 0.01f) ? throttleOutMaxV : (outMin + 0.01f);
  if (rawV <= inMin) return outMin;
  if (rawV >= inMax) return outMax;
  return outMin + ((rawV - inMin) / (inMax - inMin)) * (outMax - outMin);
}

float throttleAdcToGripV(int rawAdc) {
  float vPin = ((float)rawAdc / 4095.0f) * HW_MAX_VOLTAGE;
  float div = (throttleInputDividerRatio > 0.01f) ? throttleInputDividerRatio : 1.0f;
  return vPin / div;
}

uint8_t realVToDacValue(float desiredOutV) {
  float gain = (throttleOutputGain > 0.01f) ? throttleOutputGain : 1.0f;
  float vPin = desiredOutV / gain;
  if (vPin < 0.0f) vPin = 0.0f;
  if (vPin > HW_MAX_VOLTAGE) vPin = HW_MAX_VOLTAGE;
  int dac = (int)round((vPin / HW_MAX_VOLTAGE) * 255.0f);
  if (dac < 0) dac = 0;
  if (dac > 255) dac = 255;
  return (uint8_t)dac;
}

float applyThrottleSmoothing(float targetV) {
  unsigned long now = millis();
  unsigned long dt = now - throttleSmoothLastMs;
  if (dt == 0) dt = 1;
  throttleSmoothLastMs = now;
  float vRange = throttleOutMaxV - throttleOutMinV;
  if (vRange < 0.1f) vRange = 0.1f;
  if (targetV > throttleSmoothOutV) {
    if (!throttleSoftStartEnabled || throttleSoftStartMs == 0) throttleSmoothOutV = targetV;
    else {
      throttleSmoothOutV += (vRange * (float)dt) / (float)throttleSoftStartMs;
      if (throttleSmoothOutV > targetV) throttleSmoothOutV = targetV;
    }
  } else if (targetV < throttleSmoothOutV) {
    if (!throttleSoftStopEnabled || throttleSoftStopMs == 0) throttleSmoothOutV = targetV;
    else {
      throttleSmoothOutV -= (vRange * (float)dt) / (float)throttleSoftStopMs;
      if (throttleSmoothOutV < targetV) throttleSmoothOutV = targetV;
    }
  }
  return throttleSmoothOutV;
}

void throttleSettingsSave() {
  prefs.begin("throttle", false);
  prefs.putFloat("inMinV", throttleInMinV);
  prefs.putFloat("inMaxV", throttleInMaxV);
  prefs.putFloat("outMinV", throttleOutMinV);
  prefs.putFloat("outMaxV", throttleOutMaxV);
  prefs.putFloat("divRatio", throttleInputDividerRatio);
  prefs.putFloat("gain", throttleOutputGain);
  prefs.putInt("ssEn", throttleSoftStartEnabled ? 1 : 0);
  prefs.putInt("spEn", throttleSoftStopEnabled ? 1 : 0);
  prefs.putULong("ssMs", throttleSoftStartMs);
  prefs.putULong("spMs", throttleSoftStopMs);
  prefs.putInt("brakeCut", ownBrakeCutoffEnabled ? 1 : 0);
  prefs.end();
}

void throttleSettingsLoad() {
  prefs.begin("throttle", true);
  throttleInMinV = prefs.getFloat("inMinV", 0.8f);
  throttleInMaxV = prefs.getFloat("inMaxV", 3.6f);
  throttleOutMinV = prefs.getFloat("outMinV", 0.8f);
  throttleOutMaxV = prefs.getFloat("outMaxV", 4.0f);
  throttleInputDividerRatio = prefs.getFloat("divRatio", 20.0f / 30.0f);
  throttleOutputGain = prefs.getFloat("gain", 1.33f);
  throttleSoftStartEnabled = prefs.getInt("ssEn", 0) != 0;
  throttleSoftStopEnabled = prefs.getInt("spEn", 0) != 0;
  throttleSoftStartMs = prefs.getULong("ssMs", 500);
  throttleSoftStopMs = prefs.getULong("spMs", 500);
  ownBrakeCutoffEnabled = prefs.getInt("brakeCut", 1) != 0;
  prefs.end();
}

// ================= PAS: настройки датчика =================
int pasMagnetCount = 12;
int pasEdgeMode = FALLING;
int pasActivationAngle = 180;
unsigned long pasTimeoutMs = 350;

volatile unsigned long pasLastPulseMicros = 0;
volatile unsigned long pasConsecutivePulses = 0;
bool pasConfirmedActive = false;

void IRAM_ATTR onPasPulse() {
  pasLastPulseMicros = micros();
  pasConsecutivePulses++;
}

int pasRequiredPulses() {
  int required = (int)round(pasActivationAngle * pasMagnetCount / 360.0f);
  if (required < 1) required = 1;
  return required;
}

void updatePasDetection() {
  unsigned long now = micros();
  bool withinWindow = (pasLastPulseMicros != 0) && (now - pasLastPulseMicros < pasTimeoutMs * 1000UL);
  if (!withinWindow) {
    pasConsecutivePulses = 0;
    pasConfirmedActive = false;
  } else if (pasConsecutivePulses >= (unsigned long)pasRequiredPulses()) {
    pasConfirmedActive = true;
  }
}

void reattachPasInterrupt() {
  detachInterrupt(digitalPinToInterrupt(PAS_SENSOR_PIN));
  attachInterrupt(digitalPinToInterrupt(PAS_SENSOR_PIN), onPasPulse, pasEdgeMode);
}

// ================= PAS: уровни (усилие в %, до 20 штук) =================
const int PAS_MAX_LEVELS = 20;
int pasLevelsCount = 3;
int pasLevelPercent[PAS_MAX_LEVELS];
int pasCurrentLevel = 0;

void pasAutoDistribute() {
  for (int i = 0; i < pasLevelsCount; i++) {
    pasLevelPercent[i] = (int)round((float)(i + 1) * 100.0f / (float)pasLevelsCount);
  }
}

// ================= PAS: мягкий старт / мягкий стоп (в вольтах) =================
bool pasSoftStartEnabled = false;
bool pasSoftStopEnabled = false;
unsigned long pasSoftStartMs = 500;
unsigned long pasSoftStopMs = 800;

float pasSmoothOutV = 0;
unsigned long pasSmoothLastMs = 0;

float applyPasSmoothing(float targetV) {
  unsigned long now = millis();
  unsigned long dt = now - pasSmoothLastMs;
  if (dt == 0) dt = 1;
  pasSmoothLastMs = now;

  float diff = targetV - pasSmoothOutV;
  bool rising = diff > 0;
  bool enabled = rising ? pasSoftStartEnabled : pasSoftStopEnabled;
  unsigned long tau = rising ? pasSoftStartMs : pasSoftStopMs;

  if (!enabled || tau == 0) { pasSmoothOutV = targetV; return targetV; }

  float alpha = 1.0f - expf(-(float)dt / (float)tau);
  pasSmoothOutV += diff * alpha;
  if (fabs(targetV - pasSmoothOutV) < 0.01f) pasSmoothOutV = targetV;
  return pasSmoothOutV;
}

float getPasTargetV() {
  if (pasCurrentLevel <= 0 || pasCurrentLevel > pasLevelsCount) return 0.0f;
  if (!pasConfirmedActive) return 0.0f;
  float vRange = throttleOutMaxV - throttleOutMinV;
  return throttleOutMinV + (vRange * (float)pasLevelPercent[pasCurrentLevel - 1] / 100.0f);
}

void pasSettingsSave() {
  prefs.begin("pas", false);
  prefs.putInt("magnets", pasMagnetCount);
  prefs.putInt("edge", pasEdgeMode);
  prefs.putInt("angle", pasActivationAngle);
  prefs.putULong("timeout", pasTimeoutMs);
  prefs.putInt("cnt", pasLevelsCount);
  prefs.putBytes("pct", pasLevelPercent, sizeof(pasLevelPercent));
  prefs.putInt("curLvl", pasCurrentLevel);
  prefs.putInt("ssEn", pasSoftStartEnabled ? 1 : 0);
  prefs.putInt("spEn", pasSoftStopEnabled ? 1 : 0);
  prefs.putULong("ssMs", pasSoftStartMs);
  prefs.putULong("spMs", pasSoftStopMs);
  prefs.end();
}

void pasSettingsLoad() {
  prefs.begin("pas", true);
  pasMagnetCount = prefs.getInt("magnets", 12);
  pasEdgeMode = prefs.getInt("edge", FALLING);
  pasActivationAngle = prefs.getInt("angle", 180);
  pasTimeoutMs = prefs.getULong("timeout", 350);
  pasLevelsCount = prefs.getInt("cnt", 3);
  size_t got = prefs.getBytes("pct", pasLevelPercent, sizeof(pasLevelPercent));
  pasCurrentLevel = prefs.getInt("curLvl", 0);
  pasSoftStartEnabled = prefs.getInt("ssEn", 0) != 0;
  pasSoftStopEnabled = prefs.getInt("spEn", 0) != 0;
  pasSoftStartMs = prefs.getULong("ssMs", 500);
  pasSoftStopMs = prefs.getULong("spMs", 800);
  prefs.end();
  if (got != sizeof(pasLevelPercent)) {
    pasAutoDistribute();
  }
}

int pasButtonLastReading = HIGH;
int pasButtonStableState = HIGH;
unsigned long pasButtonDebounceMs = 0;

void updatePasButton() {
  int reading = digitalRead(PAS_BUTTON_PIN);
  if (reading != pasButtonLastReading) pasButtonDebounceMs = millis();
  if (millis() - pasButtonDebounceMs > 50) {
    if (reading != pasButtonStableState) {
      pasButtonStableState = reading;
      if (pasButtonStableState == LOW) {
        pasCurrentLevel++;
        if (pasCurrentLevel > pasLevelsCount) pasCurrentLevel = 0;
        prefs.begin("pas", false);
        prefs.putInt("curLvl", pasCurrentLevel);
        prefs.end();
      }
    }
  }
  pasButtonLastReading = reading;
}

void setThrottleOutputSafeZero() {
  dacWrite(THROTTLE_DAC_PIN, realVToDacValue(throttleOutMinV));
}

struct DebugSample {
  unsigned long t;
  float throttleInV, throttleOutV;
  bool brake, pasActive;
  int pasLevel, joyX, joyY;
  bool joySw;
};

const int DEBUG_BUFFER_SIZE = 100;
DebugSample debugBuf[DEBUG_BUFFER_SIZE];
int debugBufHead = 0;

void debugRecord(float inV, float outV, int jx, int jy, bool jsw) {
  DebugSample &s = debugBuf[debugBufHead];
  s.t = millis();
  s.throttleInV = inV;
  s.throttleOutV = outV;
  s.brake = isBrakePressed();
  s.pasActive = pasConfirmedActive;
  s.pasLevel = pasCurrentLevel;
  s.joyX = jx;
  s.joyY = jy;
  s.joySw = jsw;
  debugBufHead = (debugBufHead + 1) % DEBUG_BUFFER_SIZE;
}

void updateThrottle() {
  int joyX = analogRead(JOY_VRX_PIN);
  int joyY = analogRead(JOY_VRY_PIN);
  bool joySw = (digitalRead(JOY_SW_PIN) == LOW);
  if (ownBrakeCutoffEnabled && isBrakePressed()) {
    setThrottleOutputSafeZero();
    throttleSmoothOutV = 0;
    pasSmoothOutV = 0;
    debugRecord(0, throttleOutMinV, joyX, joyY, joySw);
    return;
  }
  float realGripV = throttleAdcToGripV(analogRead(THROTTLE_ADC_PIN));
  float throttleOutV = applyThrottleSmoothing(calibrateThrottleV(realGripV));
  float pasOutV = applyPasSmoothing(getPasTargetV());
  float combinedV = (throttleOutV > pasOutV) ? throttleOutV : pasOutV;
  dacWrite(THROTTLE_DAC_PIN, realVToDacValue(combinedV));
  debugRecord(realGripV, combinedV, joyX, joyY, joySw);
}

unsigned long lastDisplayUpdateMs = 0;
void updateDisplay() {
  if (millis() - lastDisplayUpdateMs < 100) return;
  lastDisplayUpdateMs = millis();
  display.clear();
  if (isBrakePressed()) {
    display.setChar(0, 'S');
    display.setChar(1, 'T');
    display.setChar(2, 'O');
    display.setChar(3, 'P');
  } else {
    display.setChar(0, 'P');
    display.setChar(1, '0' + pasCurrentLevel);
  }
}

String storedSsid = "", storedPass = "";
void wifiCredsLoad() {
  prefs.begin("wifi", true);
  storedSsid = prefs.getString("ssid", "");
  storedPass = prefs.getString("pass", "");
  prefs.end();
}

void wifiCredsSave(const String &newSsid, const String &newPass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", newSsid);
  prefs.putString("pass", newPass);
  prefs.end();
  storedSsid = newSsid;
  storedPass = newPass;
}

void wifiConnect() {
  String useSsid = storedSsid.length() > 0 ? storedSsid : String(ssid);
  String usePass = storedSsid.length() > 0 ? storedPass : String(password);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(useSsid.c_str(), usePass.c_str());
}

void handleDebugData() {
  String json = "[";
  for (int i = 0; i < DEBUG_BUFFER_SIZE; i++) {
    int idx = (debugBufHead + i) % DEBUG_BUFFER_SIZE;
    DebugSample &s = debugBuf[idx];
    if (s.t == 0) continue;
    if (json.length() > 1) json += ",";
    json += "{\"t\":";
    json += String(s.t);
    json += ",\"in\":";
    json += String(s.throttleInV, 2);
    json += ",\"out\":";
    json += String(s.throttleOutV, 2);
    json += ",\"brake\":";
    json += (s.brake ? "1" : "0");
    json += ",\"pas\":";
    json += (s.pasActive ? "1" : "0");
    json += ",\"lvl\":";
    json += String(s.pasLevel);
    json += ",\"jx\":";
    json += String(s.joyX);
    json += ",\"jy\":";
    json += String(s.joyY);
    json += ",\"jsw\":";
    json += (s.joySw ? "1" : "0");
    json += "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleDebugPage() {
  server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Отладка</title>
<style>
body{background:#111;color:#eee;font-family:sans-serif;padding:15px}
canvas{background:#000;border-radius:6px;width:100%;max-width:700px;display:block}
.legend{display:flex;gap:15px;flex-wrap:wrap;margin:10px 0;font-size:13px}
.legend span{display:inline-flex;align-items:center;gap:5px}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block}
#vals{font-size:14px;margin-top:10px;line-height:1.6}
a{color:#4a90d9}
</style></head><body>
<p><a href="/">&larr; Настройки</a></p>
<h1>Отладка (мини-осциллограф)</h1>
<canvas id="chart" width="700" height="320"></canvas>
<div class="legend">
<span><span class="dot" style="background:#4a90d9"></span>Газ вход, В</span>
<span><span class="dot" style="background:#e74c3c"></span>Газ выход, В</span>
<span><span class="dot" style="background:#f39c12"></span>Тормоз</span>
<span><span class="dot" style="background:#2ecc71"></span>PAS активен</span>
</div>
<div id="vals">Загрузка...</div>

<script>
const canvas = document.getElementById('chart');
const ctx = canvas.getContext('2d');
const W = canvas.width, H = canvas.height;
const VMAX = 5.0;

function draw(data) {
  ctx.clearRect(0,0,W,H);
  if (data.length < 2) return;

  ctx.strokeStyle = '#222';
  ctx.lineWidth = 1;
  for (let v = 0; v <= VMAX; v++) {
    const y = H - 60 - (v/VMAX)*(H-80);
    ctx.beginPath(); ctx.moveTo(0,y); ctx.lineTo(W,y); ctx.stroke();
    ctx.fillStyle = '#555'; ctx.font = '10px sans-serif';
    ctx.fillText(v.toFixed(1)+'В', 2, y-2);
  }

  const stepX = W / (data.length - 1);
  function plotV(key, color) {
    ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.beginPath();
    data.forEach((d,i) => {
      const x = i*stepX;
      const y = H - 60 - (d[key]/VMAX)*(H-80);
      if (i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    });
    ctx.stroke();
  }
  function plotBool(key, color, baseY) {
    ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.beginPath();
    data.forEach((d,i) => {
      const x = i*stepX;
      const y = d[key] ? baseY - 15 : baseY;
      if (i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    });
    ctx.stroke();
  }

  plotV('in', '#4a90d9');
  plotV('out', '#e74c3c');
  plotBool('brake', '#f39c12', H-25);
  plotBool('pas', '#2ecc71', H-45);
}

function refresh() {
  fetch('/debug/data').then(r=>r.json()).then(data=>{
    draw(data);
    if (data.length) {
      const last = data[data.length-1];
      document.getElementById('vals').innerHTML =
        'Газ вход: <b>' + last.in.toFixed(2) + 'В</b> &nbsp;|&nbsp; ' +
        'Газ выход: <b>' + last.out.toFixed(2) + 'В</b> &nbsp;|&nbsp; ' +
        'Тормоз: <b>' + (last.brake ? 'НАЖАТ' : 'отпущен') + '</b> &nbsp;|&nbsp; ' +
        'PAS: <b>' + (last.pas ? ('активен, уровень '+last.lvl) : 'неактивен') + '</b>';
    }
  });
}
setInterval(refresh, 300);
refresh();
</script>
</body></html>
)rawliteral");
}

void handleHub() {
  server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Настройки</title>
<style>body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
a.card{display:block;background:#333;color:#fff;padding:15px;border-radius:8px;margin-bottom:10px;text-decoration:none}
.warn{background:#5c1a1a;padding:12px;border-radius:8px;margin-bottom:20px;font-weight:bold;font-size:14px}
</style>
</head><body>
<h1>Упрощённый прототип</h1>
<div class="warn">&#9888; Тормоз продублирован сюда как приоритет 0 (можно отключить в настройках газа).</div>
<a class="card" href="/settings/throttle">Газ &rarr;</a>
<a class="card" href="/settings/pas">PAS &rarr;</a>
<a class="card" href="/wifi">WiFi &rarr;</a>
<a class="card" href="/debug">Отладка (график) &rarr;</a>
<a class="card" href="/update">Обновить прошивку (.bin) &rarr;</a>
</body></html>
)rawliteral");
}

void handleThrottlePage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Газ</title>
<style>body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
label{display:block;margin-top:12px}input{width:100%;padding:6px;box-sizing:border-box;background:#222;color:#eee;border:1px solid #444}
button{margin-top:15px;padding:10px;width:100%;font-size:16px}
.chk{display:flex;gap:8px;align-items:center;margin-top:12px}.chk input{width:auto}
fieldset{border:1px solid #333;border-radius:8px;margin-top:15px;padding:10px}
.warn{color:#ff8888;font-size:13px;margin-top:6px}
</style></head><body>
<p><a href="/" style="color:#4a90d9">&larr; Настройки</a></p>
<h1>Газ</h1>
<form id="f">
<fieldset><legend>Калибровка (в реальных вольтах на проводах)</legend>
<p style="color:#888;font-size:13px">Это напряжение на самих проводах (ручка газа / вход контроллера), не на ножках ESP32 — делитель и усилитель уже всё пересчитывают сами.</p>
<label>Вход мин, В (ручка газа в покое)</label><input type="number" step="0.05" min="0" max="5" name="inMinV" value=")rawliteral"; html += String(throttleInMinV, 2);
  html += R"rawliteral(">
<label>Вход макс, В (ручка на полном газу)</label><input type="number" step="0.05" min="0" max="5" name="inMaxV" value=")rawliteral"; html += String(throttleInMaxV, 2);
  html += R"rawliteral(">
<label>Выход мин, В (контроллер, холостой ход)</label><input type="number" step="0.05" min="0" max="5" name="outMinV" value=")rawliteral"; html += String(throttleOutMinV, 2);
  html += R"rawliteral(">
<label>Выход макс, В (контроллер, полный газ)</label><input type="number" step="0.05" min="0" max="5" name="outMaxV" value=")rawliteral"; html += String(throttleOutMaxV, 2);
  html += R"rawliteral(">
</fieldset>
<fieldset><legend>Согласующие цепи (подстроить под фактические резисторы)</legend>
<label>Коэффициент делителя на входе (R2/(R1+R2))</label><input type="number" step="0.001" min="0.1" max="1" name="divRatio" value=")rawliteral"; html += String(throttleInputDividerRatio, 3);
  html += R"rawliteral(">
<p style="color:#888;font-size:13px">По умолчанию для R1=10к, R2=20к: 20/(10+20) &#8776; 0.667</p>
<label>Коэффициент усиления ОУ (1 + R4/R3)</label><input type="number" step="0.01" min="1" max="3" name="gain" value=")rawliteral"; html += String(throttleOutputGain, 2);
  html += R"rawliteral(">
<p style="color:#888;font-size:13px">По умолчанию для R3=10к, R4=3.3к: 1 + 3.3/10 = 1.33 (ОУ MCP6002, питание +5В).</p>
<p class="warn">Выше 3.3В на самом ЦАП ESP32 не поднимется — это аппаратный предел чипа. ОУ после ЦАП компенсирует это усилением, но выше напряжения питания ОУ (обычно 5В) выход тоже не поднимется физически.</p>
</fieldset>
<fieldset><legend>Мягкий старт/стоп</legend>
<div class="chk"><input type="checkbox" name="ssEn" )rawliteral"; html += throttleSoftStartEnabled?"checked":"";
  html += R"rawliteral(><label>Мягкий старт</label></div>
<label>Время разгона (мс)</label><input type="number" name="ssMs" value=")rawliteral"; html += String(throttleSoftStartMs);
  html += R"rawliteral(">
<div class="chk"><input type="checkbox" name="spEn" )rawliteral"; html += throttleSoftStopEnabled?"checked":"";
  html += R"rawliteral(><label>Мягкий стоп</label></div>
<label>Время торможения (мс)</label><input type="number" name="spMs" value=")rawliteral"; html += String(throttleSoftStopMs);
  html += R"rawliteral(">
</fieldset>
<fieldset><legend>Безопасность</legend>
<div class="chk"><input type="checkbox" name="brakeCut" )rawliteral"; html += ownBrakeCutoffEnabled?"checked":"";
  html += R"rawliteral(><label>Дублировать отключение газа по тормозу (доп. к моторконтроллеру)</label></div>
</fieldset>
<button type="submit">Сохранить</button>
</form>
<script>
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  const d=new FormData(this);
  fetch('/settings/throttle/save',{method:'POST',body:d}).then(()=>alert('Сохранено'));
});
</script>
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleThrottleSave() {
  throttleInMinV = server.arg("inMinV").toFloat();
  throttleInMaxV = server.arg("inMaxV").toFloat();
  throttleOutMinV = server.arg("outMinV").toFloat();
  throttleOutMaxV = server.arg("outMaxV").toFloat();
  float newDivRatio = server.arg("divRatio").toFloat();
  if (newDivRatio > 0.05f && newDivRatio <= 1.0f) throttleInputDividerRatio = newDivRatio;
  float newGain = server.arg("gain").toFloat();
  if (newGain >= 1.0f) throttleOutputGain = newGain;
  throttleSoftStartEnabled = server.hasArg("ssEn");
  throttleSoftStopEnabled = server.hasArg("spEn");
  throttleSoftStartMs = server.arg("ssMs").toInt();
  throttleSoftStopMs = server.arg("spMs").toInt();
  ownBrakeCutoffEnabled = server.hasArg("brakeCut");
  throttleSettingsSave();
  server.send(200, "text/plain", "OK");
}

void handlePasPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PAS</title>
<style>body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
label{display:block;margin-top:12px}input,select{width:100%;padding:6px;box-sizing:border-box;background:#222;color:#eee;border:1px solid #444}
button{margin-top:15px;padding:10px;width:100%;font-size:16px}
.chk{display:flex;gap:8px;align-items:center;margin-top:12px}.chk input{width:auto}
fieldset{border:1px solid #333;border-radius:8px;margin-top:15px;padding:10px}
#levels input{margin-top:4px}
</style></head><body>
<p><a href="/" style="color:#4a90d9">&larr; Настройки</a></p>
<h1>PAS (ассистент педалей)</h1>
<form id="f">
<fieldset><legend>Датчик</legend>
<label>Количество магнитов в диске</label>
<select name="magnets">
<option value="5" )rawliteral"; html += pasMagnetCount==5?"selected":""; html += R"rawliteral(>5 магнитов (стандарт 1)</option>
<option value="8" )rawliteral"; html += pasMagnetCount==8?"selected":""; html += R"rawliteral(>8 магнитов (стандарт 2)</option>
<option value="12" )rawliteral"; html += pasMagnetCount==12?"selected":""; html += R"rawliteral(>12 магнитов (KT-V12 / частый)</option>
</select>
<label>Срабатывание по фронту</label>
<select name="edge">
<option value=")rawliteral"; html += String(FALLING); html += R"rawliteral(" )rawliteral"; html += pasEdgeMode==FALLING?"selected":""; html += R"rawliteral(>FALLING (спад, по умолчанию)</option>
<option value=")rawliteral"; html += String(RISING); html += R"rawliteral(" )rawliteral"; html += pasEdgeMode==RISING?"selected":""; html += R"rawliteral(>RISING (нарастание)</option>
<option value=")rawliteral"; html += String(CHANGE); html += R"rawliteral(" )rawliteral"; html += pasEdgeMode==CHANGE?"selected":""; html += R"rawliteral(>CHANGE (оба фронта — в 2 раза чувствительнее)</option>
</select>
<label>Угол для активации ассиста</label>
<select name="angle">
<option value="45" )rawliteral"; html += pasActivationAngle==45?"selected":""; html += R"rawliteral(>45&deg; (1/8 оборота — мгновенный старт)</option>
<option value="90" )rawliteral"; html += pasActivationAngle==90?"selected":""; html += R"rawliteral(>90&deg; (1/4 оборота — комфортный)</option>
<option value="180" )rawliteral"; html += pasActivationAngle==180?"selected":""; html += R"rawliteral(>180&deg; (1/2 оборота — безопасный, по умолч.)</option>
<option value="360" )rawliteral"; html += pasActivationAngle==360?"selected":""; html += R"rawliteral(>360&deg; (полный оборот)</option>
</select>
<label>Тайм-аут импульса (мс)</label><input type="number" name="timeout" value=")rawliteral"; html += String(pasTimeoutMs);
  html += R"rawliteral(">
</fieldset>

<fieldset><legend>Уровни усилия (0-)rawliteral"; html += String(PAS_MAX_LEVELS); html += R"rawliteral()</legend>
<label>Количество уровней</label><input type="number" id="count" name="count" min="0" max=")rawliteral"; html += String(PAS_MAX_LEVELS);
  html += R"rawliteral(" value=")rawliteral"; html += String(pasLevelsCount);
  html += R"rawliteral(" oninput="renderLevels()">
<div id="levels"></div>
<button type="button" onclick="autoDistribute()">Автораспределение</button>
</fieldset>

<fieldset><legend>Мягкий старт/стоп ассиста</legend>
<div class="chk"><input type="checkbox" name="ssEn" )rawliteral"; html += pasSoftStartEnabled?"checked":"";
  html += R"rawliteral(><label>Мягкий старт</label></div>
<label>Время разгона (мс)</label><input type="number" name="ssMs" value=")rawliteral"; html += String(pasSoftStartMs);
  html += R"rawliteral(">
<div class="chk"><input type="checkbox" name="spEn" )rawliteral"; html += pasSoftStopEnabled?"checked":"";
  html += R"rawliteral(><label>Мягкий стоп</label></div>
<label>Время затухания при остановке педалей (мс)</label><input type="number" name="spMs" value=")rawliteral"; html += String(pasSoftStopMs);
  html += R"rawliteral(">
</fieldset>

<button type="submit">Сохранить</button>
</form>
<script>
const saved = [)rawliteral";
  for (int i = 0; i < PAS_MAX_LEVELS; i++) { html += String(pasLevelPercent[i]); if (i<PAS_MAX_LEVELS-1) html += ","; }
  html += R"rawliteral(];
function renderLevels(){
  const count = parseInt(document.getElementById('count').value)||0;
  const div = document.getElementById('levels'); div.innerHTML='';
  for(let i=0;i<count;i++){
    const val = saved[i]!==undefined?saved[i]:0;
    div.innerHTML += '<label>Уровень '+(i+1)+' — усилие (%)</label><input type="number" name="lvl'+i+'" value="'+val+'">';
  }
}
function autoDistribute(){
  const count = parseInt(document.getElementById('count').value)||0;
  for(let i=0;i<count;i++) saved[i]=Math.round((i+1)*100/count);
  renderLevels();
}
renderLevels();
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  const d=new FormData(this);
  fetch('/settings/pas/save',{method:'POST',body:d}).then(()=>alert('Сохранено'));
});
</script>
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
  pasSoftStopEnabled = server.hasArg("spEn");
  pasSoftStartMs = server.arg("ssMs").toInt();
  pasSoftStopMs = server.arg("spMs").toInt();

  pasSettingsSave();
  if (edgeChanged) reattachPasInterrupt();
  server.send(200, "text/plain", "OK");
}

void handleWifiPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WiFi</title>
<style>
body{background:#111;color:#eee;font-family:sans-serif;padding:15px;max-width:400px;margin:auto}
button{padding:10px;width:100%;font-size:15px;border-radius:8px;border:none;background:#333;color:#eee;margin-top:10px}
input{width:100%;padding:8px;box-sizing:border-box;background:#222;color:#eee;border:1px solid #444;margin-top:8px}
.net{padding:10px;background:#1a1a1a;border-radius:6px;margin-top:6px;cursor:pointer;display:flex;justify-content:space-between}
.net:active{background:#333}
a{color:#4a90d9}
.status{color:#888;font-size:13px;margin:10px 0}
</style></head><body>
<p><a href="/">&larr; Настройки</a></p>
<h1>WiFi</h1>
<div class="status" id="status">Сейчас: )rawliteral";
  html += (WiFi.status() == WL_CONNECTED) ? ("подключено к " + WiFi.SSID() + ", IP " + WiFi.localIP().toString()) : "не подключено";
  html += R"rawliteral(</div>

<button onclick="scan()">Найти сети</button>
<div id="nets"></div>

<form id="f" style="margin-top:20px">
<label>SSID</label><input type="text" id="ssid" name="ssid" value="">
<label>Пароль</label><input type="password" id="pass" name="pass" value="">
<button type="submit">Подключиться и сохранить</button>
</form>

<script>
function scan() {
  document.getElementById('nets').innerHTML = 'Ищу...';
  fetch('/wifi/scan').then(r=>r.json()).then(list=>{
    if (!list.length) { document.getElementById('nets').innerHTML = 'Ничего не нашлось'; return; }
    document.getElementById('nets').innerHTML = list.map(n =>
      '<div class="net" onclick="pick(\''+n.ssid.replace(/'/g,"")+'\')"><span>'+n.ssid+'</span><span>'+n.rssi+' dBm</span></div>'
    ).join('');
  });
}
function pick(ssid) {
  document.getElementById('ssid').value = ssid;
  document.getElementById('pass').focus();
}
document.getElementById('f').addEventListener('submit', function(e){
  e.preventDefault();
  const d = new FormData(this);
  document.getElementById('status').innerText = 'Подключаюсь...';
  fetch('/wifi/save', {method:'POST', body:d}).then(()=>{
    setTimeout(()=>location.reload(), 4000);
  });
});
</script>
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

void handleUpdatePage() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>OTA</title><style>body{font-family:sans-serif;background:#18181b;color:#f4f4f5;padding:20px;}";
  html += "button{background:#38bdf8;color:#000;padding:12px;border:0;border-radius:6px;font-weight:bold;width:100%;cursor:pointer;}";
  html += "</style></head><body><h2>OTA (.bin)</h2><form method='POST' action='/update' enctype='multipart/form-data'>";
  html += "<input type='file' name='update'><br><button type='submit'>Прошить</button></form><p><a href='/' style='color:#38bdf8;'>&larr; Меню</a></p></body></html>";
  server.send(200, "text/html", html);
}

void handleUpdateResult() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", (Update.hasError()) ? "ERROR" : "OK");
  delay(1000);
  ESP.restart();
}

void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
  else if (upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
  else if (upload.status == UPLOAD_FILE_END) Update.end(true);
}

void setup() {
  Serial.begin(115200);
  Serial.println("--- START SETUP ---");
  pinMode(BRAKE_PIN, INPUT_PULLUP);
  pinMode(PAS_SENSOR_PIN, INPUT_PULLUP);
  pinMode(PAS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(JOY_SW_PIN, INPUT_PULLUP);

  display.begin();
  display.control(MD_MAX72XX::INTENSITY, 5);
  display.clear();
  display.setChar(0, 'O');
  display.setChar(1, 'K');

  throttleSettingsLoad();
  pasSettingsLoad();
  setThrottleOutputSafeZero();
  reattachPasInterrupt();

  WiFi.mode(WIFI_STA); // Сначала пытаемся подключиться как клиент
  wifiCredsLoad();
  Serial.println("--- Before wifiConnect() ---");
  wifiConnect();
  Serial.println("--- After wifiConnect() ---");
  Serial.print("Initial WiFi Status: ");
  Serial.println(WiFi.status());

  // Ждем подключения до 10 секунд (20 итераций по 500мс)
  int wifi_retries = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retries < 20) {
    delay(500);
    Serial.print(".");
    wifi_retries++;
  }
  Serial.println(); // Новая строка после точек

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Успешно подключено к сети!");
    Serial.print("[WiFi] Веб-интерфейс: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] Ошибка подключения к WiFi сети. Переключаемся в режим точки доступа...");
    WiFi.mode(WIFI_AP); // Переключаемся в режим точки доступа
    WiFi.softAP("BikeControllerAP", "123456789");
    IPAddress myAP = WiFi.softAPIP();
    Serial.print("[WiFi] Точка доступа запущена: BikeControllerAP\n[WiFi] Пароль: 123456789\n[WiFi] Веб-интерфейс доступен по ссылке: http://");
    Serial.println(myAP);
  }

  server.on("/", handleHub);
  server.on("/settings/throttle", handleThrottlePage);
  server.on("/settings/throttle/save", HTTP_POST, handleThrottleSave);
  server.on("/settings/pas", handlePasPage);
  server.on("/settings/pas/save", HTTP_POST, handlePasSave);
  server.on("/wifi", handleWifiPage);
  server.on("/wifi/scan", handleWifiScan);
  server.on("/wifi/save", HTTP_POST, handleWifiSave);
  server.on("/debug", handleDebugPage);
  server.on("/debug/data", handleDebugData);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);
  Serial.println("--- SETUP COMPLETE ---");
  server.begin();

  Serial.println("Bike Controller Firmware Ready!");
}

void loop() {
  server.handleClient();
  updatePasDetection();
  updatePasButton();
  updateThrottle();
  updateDisplay();
  delay(5);
}
