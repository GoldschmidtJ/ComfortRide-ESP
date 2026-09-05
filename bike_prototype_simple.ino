#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <math.h>

// ================= НАСТРОЙКА WiFi =================
const char* ssid = "Donut";
const char* password = "doughnut";

// ================= ПИНЫ (распаять сюда) =================
// Газ: GND -> GND, +5В -> 5В, Сигнал -> GPIO34 (ВНИМАНИЕ: см. предупреждение по напряжению ниже)
const int THROTTLE_ADC_PIN = 34;
// Выход газа на моторконтроллер — настоящий ЦАП ESP32 + усилитель на ОУ
// (MCP6002, канал Б). ЦАП сам по себе уже даёт чистое напряжение 0-3.3В,
// ОУ просто поднимает его до нужных 0-4.2В.
const int THROTTLE_DAC_PIN = 25;
// Тормоз: один провод -> GND, второй -> GPIO27
const int BRAKE_PIN = 27;
// PAS-датчик: GND -> GND, +5В -> 5В, Сигнал -> GPIO14
const int PAS_SENSOR_PIN = 14;
// Кнопка переключения уровня PAS: один контакт -> GND, второй -> GPIO13
const int PAS_BUTTON_PIN = 13;

// ================= СВЕТ / ЗВУК (передняя группа, 12В) =================
// Все переключаются MOSFET AOD418 (низкая сторона — минус нагрузки на этот
// пин, плюс нагрузки — на общую 12В-шину напрямую). Задний блок (48В,
// неизвестной конструкции) пока НЕ трогаем — см. обсуждение в чате.
const int HEADLIGHT_PIN   = 18; // фара, ШИМ
const int DRL_PIN         = 19; // ДХО, ШИМ
const int TURN_LEFT_PIN   = 21; // поворотник левый
const int TURN_RIGHT_PIN  = 22; // поворотник правый
const int HORN_PIN        = 23; // гудок
const int BUZZER_PIN      = 4;  // пищалка (тик поворотника)

const int BTN_HEADLIGHT_PIN  = 16; // кнопка фары
const int BTN_TURN_LEFT_PIN  = 17; // кнопка поворотник влево
const int BTN_TURN_RIGHT_PIN = 32; // кнопка поворотник вправо
const int BTN_HORN_PIN       = 33; // кнопка гудка (отжимная)

const int HEADLIGHT_DEFAULT_BRIGHTNESS = 220; // 0-255
const int DRL_DEFAULT_BRIGHTNESS       = 60;  // 0-255, горит всегда

// Аппаратные пределы ESP32 — константы, не настройки. АЦП и ЦАП работают
// в диапазоне 0-3.3В. Если ручка газа реально выдаёт больше (многие выдают
// до 4.2В) — НАПРЯМУЮ подавать на ADC-пин нельзя, нужен делитель напряжения
// (два резистора), иначе есть риск спалить пин.
const float HW_MAX_VOLTAGE = 3.3f;

WebServer server(80);
Preferences prefs;

// ================= ТОРМОЗ =================
bool isBrakePressed() { return digitalRead(BRAKE_PIN) == LOW; }
bool ownBrakeCutoffEnabled = true; // дублировать отключение газа по тормозу (доп. к контроллеру)

// ================= ГАЗ: аппаратное согласование напряжений =================
// У ESP32 АЦП/ЦАП работают 0-3.3В, а ручка газа/контроллер — обычно 0-4.2В.
// На входе стоит резисторный делитель (понижает), на выходе — усилитель на
// ОУ (повышает). Эти коэффициенты описывают ЧТО РЕАЛЬНО СТОИТ в железе —
// дальше калибровка ниже работает уже в "настоящих" вольтах на проводах,
// а не на ножках ESP32.
float throttleInputDividerRatio = 20.0f / (10.0f + 20.0f); // R2/(R1+R2), R1=10к, R2=20к по умолчанию
float throttleOutputGain = 1.0f + 3.3f / 10.0f; // 1 + R4/R3 (MCP6002, канал Б), R3=10к, R4=3.3к по умолчанию

// ================= ГАЗ: калибровка (в РЕАЛЬНЫХ вольтах на проводах) =================
float throttleInMinV = 0.0f, throttleInMaxV = 4.2f;
float throttleOutMinV = 0.0f, throttleOutMaxV = 4.2f;
bool throttleExtendedRangeAllowed = false; // задел на будущее, сейчас ничего не меняет физически

float calibrateThrottleV(float rawV) {
  float inMin = throttleInMinV;
  float inMax = (throttleInMaxV > inMin + 0.01f) ? throttleInMaxV : (inMin + 0.01f);
  float outMin = throttleOutMinV;
  float outMax = (throttleOutMaxV > outMin + 0.01f) ? throttleOutMaxV : (outMin + 0.01f);
  float clamped = rawV;
  if (clamped < inMin) clamped = inMin;
  if (clamped > inMax) clamped = inMax;
  float norm = (clamped - inMin) / (inMax - inMin);
  float outV = outMin + norm * (outMax - outMin);
  return outV; // это ЦЕЛЕВОЕ напряжение на проводе к контроллеру, ещё не значение для ЦАП
}

// ================= ГАЗ: мягкий старт / мягкий стоп (в вольтах) =================
bool throttleSoftStartEnabled = false;
bool throttleSoftStopEnabled = false;
unsigned long throttleSoftStartMs = 500;
unsigned long throttleSoftStopMs  = 500;

float throttleSmoothOutV = 0;
unsigned long throttleSmoothLastMs = 0;

float applyThrottleSmoothing(float targetV) {
  unsigned long now = millis();
  unsigned long dt = now - throttleSmoothLastMs;
  if (dt == 0) dt = 1;
  throttleSmoothLastMs = now;

  float diff = targetV - throttleSmoothOutV;
  bool rising = diff > 0;
  bool enabled = rising ? throttleSoftStartEnabled : throttleSoftStopEnabled;
  unsigned long tau = rising ? throttleSoftStartMs : throttleSoftStopMs;

  if (!enabled || tau == 0) { throttleSmoothOutV = targetV; return targetV; }

  float alpha = 1.0f - expf(-(float)dt / (float)tau);
  throttleSmoothOutV += diff * alpha;
  if (fabs(targetV - throttleSmoothOutV) < 0.01f) throttleSmoothOutV = targetV;
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
  prefs.putInt("extRange", throttleExtendedRangeAllowed ? 1 : 0);
  prefs.putInt("ssEn", throttleSoftStartEnabled ? 1 : 0);
  prefs.putInt("spEn", throttleSoftStopEnabled ? 1 : 0);
  prefs.putULong("ssMs", throttleSoftStartMs);
  prefs.putULong("spMs", throttleSoftStopMs);
  prefs.putInt("brakeCut", ownBrakeCutoffEnabled ? 1 : 0);
  prefs.end();
}

void throttleSettingsLoad() {
  prefs.begin("throttle", true);
  throttleInMinV = prefs.getFloat("inMinV", 0.0f);
  throttleInMaxV = prefs.getFloat("inMaxV", 4.2f);
  throttleOutMinV = prefs.getFloat("outMinV", 0.0f);
  throttleOutMaxV = prefs.getFloat("outMaxV", 4.2f);
  throttleInputDividerRatio = prefs.getFloat("divRatio", 24.0f / 34.0f);
  throttleOutputGain = prefs.getFloat("gain", 1.27f);
  throttleExtendedRangeAllowed = prefs.getInt("extRange", 0) != 0;
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
  if (required < 1) required = 1; // без max() — раньше тут была ошибка компиляции из-за смешения типов
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
int pasCurrentLevel = 0; // 0 = выключен (edge case "PAS 0 = 0%" — заложено по умолчанию)

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
  if (pasCurrentLevel <= 0 || pasCurrentLevel > pasLevelsCount) return 0;
  if (!pasConfirmedActive) return 0;
  return (pasLevelPercent[pasCurrentLevel - 1] / 100.0f) * throttleOutMaxV;
}

void pasSettingsSave() {
  prefs.begin("pas", false);
  prefs.putInt("magnets", pasMagnetCount);
  prefs.putInt("edge", pasEdgeMode);
  prefs.putInt("angle", pasActivationAngle);
  prefs.putULong("timeout", pasTimeoutMs);
  prefs.putInt("cnt", pasLevelsCount);
  prefs.putBytes("pct", pasLevelPercent, sizeof(pasLevelPercent));
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
  pasSoftStartEnabled = prefs.getInt("ssEn", 0) != 0;
  pasSoftStopEnabled = prefs.getInt("spEn", 0) != 0;
  pasSoftStartMs = prefs.getULong("ssMs", 500);
  pasSoftStopMs = prefs.getULong("spMs", 800);
  prefs.end();
  if (got != sizeof(pasLevelPercent)) {
    pasAutoDistribute();
  }
}

// ================= Кнопка переключения уровня PAS =================
int btnLastReading = HIGH, btnStable = HIGH;
unsigned long btnLastDebounce = 0;
const unsigned long DEBOUNCE_MS = 50;

void updatePasButton() {
  int reading = digitalRead(PAS_BUTTON_PIN);
  if (reading != btnLastReading) btnLastDebounce = millis();
  if (millis() - btnLastDebounce > DEBOUNCE_MS) {
    if (reading != btnStable) {
      btnStable = reading;
      if (btnStable == LOW) {
        pasCurrentLevel = (pasCurrentLevel + 1) % (pasLevelsCount + 1);
        Serial.printf("PAS уровень: %d/%d\n", pasCurrentLevel, pasLevelsCount);
      }
    }
  }
  btnLastReading = reading;
}

// ================= Свет: фара, ДХО =================
bool headlightOn = false;

void toggleHeadlight() {
  headlightOn = !headlightOn;
  ledcWrite(HEADLIGHT_PIN, headlightOn ? HEADLIGHT_DEFAULT_BRIGHTNESS : 0);
  Serial.println(headlightOn ? "Фара: ВКЛ" : "Фара: ВЫКЛ");
}

// ================= Пищалка (тик поворотника) =================
bool buzzerOn = false;
unsigned long buzzerOffAtMs = 0;

void buzzerClick(unsigned long durationMs) {
  digitalWrite(BUZZER_PIN, HIGH);
  buzzerOn = true;
  buzzerOffAtMs = millis() + durationMs;
}

void updateBuzzer() {
  if (buzzerOn && millis() >= buzzerOffAtMs) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerOn = false;
  }
}

// ================= Поворотники =================
bool turnLeftActive = false, turnRightActive = false;
unsigned long turnLastBlinkMs = 0;
bool turnBlinkState = false;
const unsigned long TURN_BLINK_MS = 500;

void toggleTurnLeft() {
  turnLeftActive = !turnLeftActive;
  if (turnLeftActive) turnRightActive = false;
}
void toggleTurnRight() {
  turnRightActive = !turnRightActive;
  if (turnRightActive) turnLeftActive = false;
}

void updateTurnSignals() {
  if (!turnLeftActive && !turnRightActive) {
    digitalWrite(TURN_LEFT_PIN, LOW);
    digitalWrite(TURN_RIGHT_PIN, LOW);
    return;
  }
  if (millis() - turnLastBlinkMs >= TURN_BLINK_MS) {
    turnLastBlinkMs = millis();
    turnBlinkState = !turnBlinkState;
    buzzerClick(50); // тик на каждой смене состояния мигания
  }
  digitalWrite(TURN_LEFT_PIN,  (turnLeftActive  && turnBlinkState) ? HIGH : LOW);
  digitalWrite(TURN_RIGHT_PIN, (turnRightActive && turnBlinkState) ? HIGH : LOW);
}

// ================= Гудок =================
void updateHorn() {
  bool hornPressed = (digitalRead(BTN_HORN_PIN) == LOW);
  digitalWrite(HORN_PIN, hornPressed ? HIGH : LOW);
}

// ================= Физические кнопки: фара, поворотники (дебаунс) =================
struct DebouncedButton {
  int pin; int lastReading; int stableState; unsigned long lastDebounceMs; void (*action)();
};

DebouncedButton lightButtons[] = {
  { BTN_HEADLIGHT_PIN,  HIGH, HIGH, 0, toggleHeadlight },
  { BTN_TURN_LEFT_PIN,  HIGH, HIGH, 0, toggleTurnLeft },
  { BTN_TURN_RIGHT_PIN, HIGH, HIGH, 0, toggleTurnRight },
};
const int lightButtonsCount = sizeof(lightButtons) / sizeof(lightButtons[0]);

void updateLightButtons() {
  for (int i = 0; i < lightButtonsCount; i++) {
    DebouncedButton &b = lightButtons[i];
    int reading = digitalRead(b.pin);
    if (reading != b.lastReading) b.lastDebounceMs = millis();
    if (millis() - b.lastDebounceMs > DEBOUNCE_MS) {
      if (reading != b.stableState) {
        b.stableState = reading;
        if (b.stableState == LOW) b.action();
      }
    }
    b.lastReading = reading;
  }
}

// ================= Отладочный буфер (для графиков в браузере) =================
// Копим последние несколько секунд значений — газ вход/выход, тормоз, PAS.
// Страница /debug рисует это как бегущий график, вроде мини-осциллографа.
const int DEBUG_BUFFER_SIZE = 200; // при семпле раз в 20мс — это ~4 секунды истории

struct DebugSample {
  unsigned long tMs;
  float throttleInV;
  float throttleOutV;
  bool brake;
  bool pasActive;
  int pasLevel;
};

DebugSample debugBuffer[DEBUG_BUFFER_SIZE];
int debugBufferHead = 0;
unsigned long lastDebugSampleMs = 0;
const unsigned long DEBUG_SAMPLE_INTERVAL_MS = 20;

void updateDebugBuffer(float throttleInV, float throttleOutV) {
  unsigned long now = millis();
  if (now - lastDebugSampleMs < DEBUG_SAMPLE_INTERVAL_MS) return;
  lastDebugSampleMs = now;

  DebugSample &s = debugBuffer[debugBufferHead];
  s.tMs = now;
  s.throttleInV = throttleInV;
  s.throttleOutV = throttleOutV;
  s.brake = isBrakePressed();
  s.pasActive = pasConfirmedActive;
  s.pasLevel = pasCurrentLevel;
  debugBufferHead = (debugBufferHead + 1) % DEBUG_BUFFER_SIZE;
}

// ================= Throttle-by-wire =================
void setThrottleOutputSafeZero() {
  dacWrite(THROTTLE_DAC_PIN, 0);
}

void updateThrottle() {
  if (ownBrakeCutoffEnabled && isBrakePressed()) {
    setThrottleOutputSafeZero();
    throttleSmoothOutV = 0;
    pasSmoothOutV = 0;
    updateDebugBuffer(0, 0);
    return;
  }

  int raw = analogRead(THROTTLE_ADC_PIN);
  float adcPinV = raw * HW_MAX_VOLTAGE / 4095.0f;
  // На ножке ESP32 напряжение уже ослаблено делителем — пересчитываем
  // обратно в реальное напряжение на проводе ручки газа.
  float realGripV = adcPinV / throttleInputDividerRatio;

  float throttleTargetV = calibrateThrottleV(realGripV); // целевое напряжение на проводе К КОНТРОЛЛЕРУ
  float throttleOutV = applyThrottleSmoothing(throttleTargetV);

  float pasTargetV = getPasTargetV();
  float pasOutV = applyPasSmoothing(pasTargetV);

  float combinedV = (throttleOutV > pasOutV) ? throttleOutV : pasOutV; // целевое реальное напряжение на выходе
  // ОУ (канал Б) поднимает напряжение в throttleOutputGain раз — значит на
  // сам ЦАП нужно подать МЕНЬШЕ, чтобы после усиления получить цель.
  float dacTargetV = combinedV / throttleOutputGain;
  if (dacTargetV > HW_MAX_VOLTAGE) dacTargetV = HW_MAX_VOLTAGE; // физический предел ЦАП — не обойти
  if (dacTargetV < 0) dacTargetV = 0;
  int dacVal = (int)round(dacTargetV / HW_MAX_VOLTAGE * 255.0f);
  if (dacVal < 0) dacVal = 0;
  if (dacVal > 255) dacVal = 255;
  dacWrite(THROTTLE_DAC_PIN, dacVal);

  updateDebugBuffer(realGripV, combinedV);
}

// ================= Веб: отладочный график =================
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
const VMAX = 5.0; // шкала по напряжению, В

function draw(data) {
  ctx.clearRect(0,0,W,H);
  if (data.length < 2) return;

  // сетка
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

void handleDebugData() {
  String json = "[";
  for (int i = 0; i < DEBUG_BUFFER_SIZE; i++) {
    int idx = (debugBufferHead + i) % DEBUG_BUFFER_SIZE;
    DebugSample &s = debugBuffer[idx];
    if (i > 0) json += ",";
    json += "{\"t\":" + String(s.tMs) +
            ",\"in\":" + String(s.throttleInV, 2) +
            ",\"out\":" + String(s.throttleOutV, 2) +
            ",\"brake\":" + (s.brake ? "1" : "0") +
            ",\"pas\":" + (s.pasActive ? "1" : "0") +
            ",\"lvl\":" + String(s.pasLevel) + "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

// ================= Веб: WiFi (поиск и подключение) =================
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

// ================= Веб: хаб =================
void handleHub() {
  server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Настройки</title>
<style>body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
a.card{display:block;background:#333;color:#fff;padding:15px;border-radius:8px;margin-bottom:10px;text-decoration:none}
.warn{background:#5c1a1a;padding:12px;border-radius:8px;margin-bottom:20px;font-weight:bold}</style>
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

// ================= Веб: Газ =================
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
<p style="color:#888;font-size:13px">По умолчанию для R1=10к, R2=24к: 24/(10+24) &#8776; 0.706</p>
<label>Коэффициент усиления ОУ (1 + R4/R3)</label><input type="number" step="0.01" min="1" max="3" name="gain" value=")rawliteral"; html += String(throttleOutputGain, 2);
  html += R"rawliteral(">
<p style="color:#888;font-size:13px">По умолчанию для R3=10к, R4=2.7к: 1 + 2.7/10 = 1.27 (ОУ MCP6002, питание +5В).</p>
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

// ================= Веб: PAS =================
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
</style></head><body>
<p><a href="/" style="color:#4a90d9">&larr; Настройки</a></p>
<h1>PAS</h1>
<form id="f">
<fieldset><legend>Датчик</legend>
<label>Количество магнитов</label><input type="number" name="magnets" value=")rawliteral"; html += String(pasMagnetCount);
  html += R"rawliteral(">
<label>Направление срабатывания</label>
<select name="edge">
<option value="2")rawliteral"; html += (pasEdgeMode==FALLING?" selected":"");
  html += R"rawliteral(>При уходе магнита (FALLING)</option>
<option value="3")rawliteral"; html += (pasEdgeMode==RISING?" selected":"");
  html += R"rawliteral(>При появлении магнита (RISING)</option>
<option value="1")rawliteral"; html += (pasEdgeMode==CHANGE?" selected":"");
  html += R"rawliteral(>При любом изменении (CHANGE)</option>
</select>
<label>Угол активации</label>
<select name="angle">
<option value="90")rawliteral"; html += (pasActivationAngle==90?" selected":"");
  html += R"rawliteral(>90&deg;</option>
<option value="180")rawliteral"; html += (pasActivationAngle==180?" selected":"");
  html += R"rawliteral(>180&deg;</option>
<option value="270")rawliteral"; html += (pasActivationAngle==270?" selected":"");
  html += R"rawliteral(>270&deg;</option>
<option value="360")rawliteral"; html += (pasActivationAngle==360?" selected":"");
  html += R"rawliteral(>360&deg;</option>
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

// ================= Веб: заливка прошивки прямо через браузер =================
void handleUpdatePage() {
  server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Обновление прошивки</title>
<style>body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
button{margin-top:15px;padding:10px;width:100%;font-size:16px}</style>
</head><body>
<p><a href="/" style="color:#4a90d9">&larr; Настройки</a></p>
<h1>Загрузить прошивку (.bin)</h1>
<p style="color:#888;font-size:13px">В Arduino IDE: Sketch &rarr; Export Compiled Binary — появится .bin рядом со скетчем. Выбери его тут и жми "Залить". Займёт секунд 20-30, плата сама перезагрузится.</p>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="update" accept=".bin">
<button type="submit">Залить</button>
</form>
</body></html>
)rawliteral");
}

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

// ================= Setup / Loop =================
// ================= WiFi: сохранённая сеть (сверх дефолтной из кода) =================
// ssid/password вверху файла — это дефолт "из коробки". Если через веб выберешь
// другую сеть — она сохранится в NVS и будет использоваться вместо дефолтной.
String storedSsid = "";
String storedPass = "";

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
  Serial.print("Подключение к "); Serial.println(useSsid);
}

void setup() {
  Serial.begin(115200);

  pinMode(BRAKE_PIN, INPUT_PULLUP);
  pinMode(PAS_SENSOR_PIN, INPUT_PULLUP);
  pinMode(PAS_BUTTON_PIN, INPUT_PULLUP);

  pinMode(BTN_HEADLIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_TURN_LEFT_PIN, INPUT_PULLUP);
  pinMode(BTN_TURN_RIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_HORN_PIN, INPUT_PULLUP);
  pinMode(TURN_LEFT_PIN, OUTPUT);
  pinMode(TURN_RIGHT_PIN, OUTPUT);
  pinMode(HORN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  ledcAttach(HEADLIGHT_PIN, 5000, 8);
  ledcAttach(DRL_PIN, 5000, 8);
  ledcWrite(DRL_PIN, DRL_DEFAULT_BRIGHTNESS); // ДХО горит всегда, пока плата включена

  // Настоящий ЦАП ESP32 — ledcAttach больше не нужен, dacWrite() работает сразу

  throttleSettingsLoad();
  pasSettingsLoad();

  setThrottleOutputSafeZero();

  reattachPasInterrupt();

  WiFi.mode(WIFI_STA);
  wifiCredsLoad();
  wifiConnect();
  Serial.print("Подключение к WiFi");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 8000) {
    delay(300); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Открой в браузере: http://"); Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi не найден — едем без веб-панели, газ/тормоз/PAS работают штатно.");
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
  server.begin();

  Serial.println("Готово. Упрощённый прототип запущен.");
}

void loop() {
  server.handleClient();
  updatePasDetection();
  updatePasButton();
  updateThrottle();
  updateLightButtons();
  updateTurnSignals();
  updateHorn();
  updateBuzzer();
  delay(5);
}
