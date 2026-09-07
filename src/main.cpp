#include <Arduino.h>

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <math.h>

// ================= НАСТРОЙКА WiFi =================
const char* ssid = "Donut";
const char* password = "doughnut";
const char* MDNS_HOST = "openbike";

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
String storedApSsid = "BikeControllerAP";
String storedApPass = "";

// Forward declarations
void wifiCredsSave(const String &newSsid, const String &newPass);
void wifiConnect();
void updateWifiStateMachine();
void setThrottleOutputSafeZero();
void throttleSettingsSave();
void throttleSettingsLoad();
void pasSettingsSave();
void pasSettingsLoad();
void reattachPasInterrupt();
void handleHub();
void handleThrottlePage();
void handleThrottleSave();
void handlePasPage();
void handlePasSave();
void handleApiPasSetLevel();
void handleApiPasToggleMode();
void handleApiCruiseToggleMode();
void handleWifiPage();
void handleWifiScan();
void handleWifiSave();
void handleDebugPage();
void handleDebugData();
void handleSystemPage();
void handleSettingsExport();
void handleSettingsImport();
void handleUpdatePage();
void handleSystemStatus();

// Forward declarations of state variables used in status endpoint
extern bool pasEnabled;
extern int pasCurrentLevel;
extern int pasLevelsCount;
extern bool cruiseEnabled;

// ================= System status tracking ================
unsigned long cpuMeasureStartMs = 0;
unsigned long cpuBusyTimeMicros = 0;
int cpuUsagePercent = 0;

// ================= Веб: статус системы (JSON endpoint) ================
void handleSystemStatus() {
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  int ramPct = totalHeap > 0 ? (int)(((totalHeap - freeHeap) * 100) / totalHeap) : 0;

  String wifiMode = "OFF";
  int rssi = 0;
  String ssidName = "";
  String ipStr = "";
  if (WiFi.status() == WL_CONNECTED) {
    wifiMode = "STA";
    rssi = WiFi.RSSI();
    ssidName = WiFi.SSID();
    ipStr = WiFi.localIP().toString();
  } else if ((WiFi.getMode() & WIFI_MODE_AP) != 0) {
    wifiMode = "AP";
    ssidName = storedApSsid;
    ipStr = WiFi.softAPIP().toString();
  }

  String json = "{";
  json += "\"cpu\":" + String(cpuUsagePercent) + ",";
  json += "\"ram_pct\":" + String(ramPct) + ",";
  json += "\"ram_free_kb\":" + String(freeHeap / 1024) + ",";
  json += "\"ram_total_kb\":" + String(totalHeap / 1024) + ",";
  json += "\"rom_sketch_kb\":" + String(ESP.getSketchSize() / 1024) + ","; // Added sketch size
  json += "\"rom_total_kb\":" + String(ESP.getFlashChipSize() / 1024) + ","; // Added total flash size
  json += "\"wifi_mode\":\"" + wifiMode + "\",";
  json += "\"wifi_ssid\":\"" + ssidName + "\",";
  json += "\"wifi_ip\":\"" + ipStr + "\",";
  json += "\"mdns_host\":\"" + String(MDNS_HOST) + ".local\",";
  json += "\"wifi_rssi\":" + String(rssi) + ",";
  json += "\"pas_en\":" + String(pasEnabled ? "true" : "false") + ",";
  json += "\"pas_lvl\":" + String(pasCurrentLevel) + ",";
  json += "\"pas_cnt\":" + String(pasLevelsCount) + ",";
  json += "\"cruise_en\":" + String(cruiseEnabled ? "true" : "false") + ",";
  json += "\"bt_active\":false";
  json += "}";
  server.send(200, "application/json", json);
}

// --- New Cruise Control forward declarations ---
void cruiseSettingsSave();
void cruiseSettingsLoad();
void handleCruisePage();
void handleCruiseSave();

void handleUpdateResult();
void handleUpdateUpload();

// AP Settings forward declarations
void handleApSave();
void apSettingsSave();
void apSettingsLoad();

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
float throttleInMinV = 1.1f, throttleInMaxV = 4.1f;
float throttleOutMinV = 1.1f, throttleOutMaxV = 4.1f;
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

// --- New Cruise Control configuration ---
const int CRUISE_MAX_LEVELS = 100;
int cruiseLevelsCount = 3;
float cruiseLevelPercent[CRUISE_MAX_LEVELS];
bool cruiseEnabled = true; // Cruise control enabled by default
bool cruiseSoftStartEnabled = false;
bool cruiseSoftStopEnabled = false;
unsigned long cruiseSoftStartMs = 500;
unsigned long cruiseSoftStopMs = 800;

// Auto-distribution for cruise levels
void cruiseAutoDistribute() {
  for (int i = 0; i < cruiseLevelsCount; i++) {
    cruiseLevelPercent[i] = (float)(i + 1) * 100.0f / (float)cruiseLevelsCount;
  }
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
  throttleInMinV = prefs.getFloat("inMinV", 1.1f);
  throttleInMaxV = prefs.getFloat("inMaxV", 4.1f);
  throttleOutMinV = prefs.getFloat("outMinV", 1.1f);
  throttleOutMaxV = prefs.getFloat("outMaxV", 4.1f);
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
bool pasEnabled = true; // PAS enabled by default
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
  if (!pasEnabled) return 0; // PAS fully disabled
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
  prefs.putInt("enabled", pasEnabled ? 1 : 0);
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
  pasEnabled = (prefs.getInt("enabled", 1) != 0);
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
  ledcWrite(0, headlightOn ? HEADLIGHT_DEFAULT_BRIGHTNESS : 0);
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
const int DEBUG_BUFFER_SIZE = 200; // уменьшено для стабильности JSON-ответа (около 1-2 сек истории при семпле 20мс)

struct DebugSample {
  unsigned long tMs;
  float throttleInV;
  float throttleOutV;
  bool brake;
  bool pasActive;
  int pasLevel;
  bool btnPasPressed;
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
  s.btnPasPressed = (digitalRead(PAS_BUTTON_PIN) == LOW);
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

// ================= Общий компонент: Статус-бар в шапке (Функции-генераторы) =================
String getTopBarCss() {
  return String(R"rawliteral(
.top-bar-sticky{position:sticky;top:0;left:0;right:0;z-index:9999;background:#181818;border-bottom:1px solid #333;padding:8px 12px;margin:-20px -20px 15px -20px;font-size:12px;color:#bbb;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;box-shadow:0 2px 8px rgba(0,0,0,0.5)}
.tb-item{display:inline-flex;align-items:center;gap:4px;white-space:nowrap}
.tb-link{color:#4a90d9;text-decoration:none;padding:2px 6px;border-radius:4px;background:#242424;border:1px solid #3a3a3a;transition:background .2s, border-color .2s}
.tb-link:hover{background:#303030;border-color:#555;color:#70b0ff}
.tb-dot{width:8px;height:8px;border-radius:50%;display:inline-block}
.dot-green{background:#2ecc71;box-shadow:0 0 5px #2ecc71}
.dot-yellow{background:#f1c40f;box-shadow:0 0 5px #f1c40f}
.dot-red{background:#e74c3c}
.dot-gray{background:#666}
)rawliteral");
}

String getTopBarHtml() {
  return String(R"rawliteral(
<div class="top-bar-sticky">
  <div class="tb-item" title="Нагрузка процессора ESP32">
    <span>CPU:</span> <b id="tbCpu">0%</b>
  </div>
  <div class="tb-item" title="Оперативная память (занято / свободно)">
    <span>RAM:</span> <b id="tbRam">0%</b> <span id="tbRamKb" style="color:#888;font-size:11px">(0k)</span>
  </div>
  <div class="tb-item" title="Flash память (прошивка / всего)">
    <span>ROM:</span> <span id="tbRom" style="color:#bbb">0k</span>
  </div>
  <a href="/wifi" class="tb-item tb-link" title="Настройки Wi-Fi">
    <span>WiFi:</span>
    <span class="tb-dot dot-gray" id="tbWifiDot"></span>
    <span id="tbWifiTxt">...</span>
  </a>
  <div class="tb-item" title="Bluetooth (не используется)" style="opacity:0.7">
    <span>BT:</span>
    <span class="tb-dot dot-gray"></span>
    <span style="color:#777">Выкл</span>
  </div>
</div>
)rawliteral");
}

String getTopBarJs() {
  return String(R"rawliteral(
<script>
function updateSysStatus(){
  fetch("/status/sys").then(r=>r.json()).then(d=>{
    const cpuEl = document.getElementById("tbCpu");
    if(cpuEl) cpuEl.innerText = (d.cpu || 0) + "%";

    const ramEl = document.getElementById("tbRam");
    if(ramEl) ramEl.innerText = (d.ram_pct || 0) + "%";
    const ramKbEl = document.getElementById("tbRamKb");
    if(ramKbEl && d.ram_free_kb !== undefined) ramKbEl.innerText = "(" + d.ram_free_kb + "k)";

    const romEl = document.getElementById("tbRom");
    if(romEl && d.rom_sketch_kb !== undefined) romEl.innerText = d.rom_sketch_kb + "k/" + d.rom_total_kb + "k";

    const dot = document.getElementById("tbWifiDot");
    const txt = document.getElementById("tbWifiTxt");
    if(dot && txt){
      dot.className = "tb-dot ";
      if(d.wifi_mode === "STA"){
        dot.className += "dot-green";
        let display = d.wifi_ssid || "WiFi";
        if(d.wifi_ip) display += " (" + d.wifi_ip + ")";
        txt.innerText = display;
        txt.title = "Доступен по http://" + (d.mdns_host || "openbike.local") + " или http://" + (d.wifi_ip || "");
      } else if(d.wifi_mode === "AP"){
        dot.className += "dot-yellow";
        txt.innerText = "AP: " + (d.wifi_ssid || "Bike") + " (" + (d.wifi_ip || "192.168.4.1") + ")";
      } else {
        dot.className += "dot-red";
        txt.innerText = "Подключение...";
      }
    }
  }).catch(e=>{});
}
setInterval(updateSysStatus, 2000);
updateSysStatus();
</script>
)rawliteral");
}

// ================= Веб: отладочный график =================
void handleDebugPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Отладка</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(

body{background:#111;color:#eee;font-family:sans-serif;padding:15px}
canvas{background:#000;border-radius:6px;width:100%;max-width:700px;display:block}
.tbtn{background:#333;color:#eee;border:1px solid #555;padding:5px 12px;border-radius:4px;cursor:pointer;margin-right:5px;font-size:13px;transition:background .2s}
.tbtn:hover{background:#444}
.legend{display:flex;gap:15px;flex-wrap:wrap;margin:10px 0;font-size:13px}
.legend span{display:inline-flex;align-items:center;gap:5px}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block}
#vals{font-size:14px;margin-top:10px;line-height:1.6}
a{color:#4a90d9}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a href="/">&larr; Настройки</a></p>
<h1>Отладка (мини-осциллограф)</h1>
<div style="margin-bottom:10px;display:flex;align-items:center;flex-wrap:wrap;gap:8px;">
  <span style="font-size:14px;">Масштаб времени:</span>
  <button type="button" class="tbtn" onclick="setTimeScale(1)" id="tb1">1с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(3)" id="tb3">3с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(5)" id="tb5" style="background:#4a90d9;color:#fff;">5с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(10)" id="tb10">10с</button>
  <button type="button" class="tbtn" onclick="setTimeScale(30)" id="tb30">30с</button>
  <label style="margin-left:10px;font-weight:bold;font-size:14px;">
    <input type="checkbox" id="chkOscilloscope" onchange="updateOscilloscopeState()" checked> Запускать осциллограф
  </label>
</div>
<canvas id="chart" width="700" height="320"></canvas>
<div class="legend">
<span><span class="dot" style="background:#4a90d9"></span>Газ вход, В</span>
<span><span class="dot" style="background:#e74c3c"></span>Газ выход, В</span>
<span><span class="dot" style="background:#f39c12"></span>Тормоз</span>
<span><span class="dot" style="background:#2ecc71"></span>PAS активен</span>
<span><span class="dot" style="background:#9b59b6"></span>Кнопка PAS</span>
</div>
<div id="vals">Загрузка...</div>

<script>
const canvas = document.getElementById('chart');
const ctx = canvas.getContext('2d');
const W = canvas.width, H = canvas.height;
const VMAX = 5.0; // шкала по напряжению, В
let currentTimeScaleSec = 5;
let isOscRunning = true;

function setTimeScale(sec) {
  currentTimeScaleSec = sec;
  [1, 3, 5, 10, 30].forEach(s => {
    const el = document.getElementById('tb' + s);
    if (el) {
      if (s === sec) { el.style.background = '#4a90d9'; el.style.color = '#fff'; }
      else { el.style.background = '#333'; el.style.color = '#eee'; }
    }
  });
}

function updateOscilloscopeState() {
  const checkbox = document.getElementById('chkOscilloscope');
  isOscRunning = checkbox.checked;
  if (isOscRunning) {
    refresh();
  } else {
    document.getElementById('vals').innerText = 'Осциллограф отключен.';
    ctx.clearRect(0,0,W,H);
  }
}

function draw(rawHistory) {
  ctx.clearRect(0,0,W,H);
  if (!rawHistory || !rawHistory.length) return;
  const now = rawHistory[rawHistory.length - 1].t;
  const cutoff = now - currentTimeScaleSec * 1000;
  let data = rawHistory.filter(d => d.t >= cutoff);
  if (data.length < 2) data = rawHistory.slice(-2);
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
  plotBool('btn', '#9b59b6', H-65);
}

function refresh() {
  if (!isOscRunning) return;
  fetch('/debug/data').then(r=>r.json()).then(data=>{
    if (!isOscRunning) return;
    draw(data);
    if (data.length) {
      const last = data[data.length-1];
      document.getElementById('vals').innerHTML =
        'Газ вход: <b>' + last.in.toFixed(2) + 'В</b> &nbsp;|&nbsp; ' +
        'Газ выход: <b>' + last.out.toFixed(2) + 'В</b> &nbsp;|&nbsp; ' +
        'Тормоз: <b>' + (last.brake ? 'НАЖАТ' : 'отпущен') + '</b> &nbsp;|&nbsp; ' +
        'PAS: <b>' + (last.pas ? ('активен, уровень '+last.lvl) : 'неактивен') + '</b> &nbsp;|&nbsp; ' +
        'Кнопка: <b>' + (last.btn ? 'НАЖАТА' : 'отпущена') + '</b>';
    }
  }).catch(e=>{});
}
setInterval(refresh, 200);
refresh();
</script>
)rawliteral" + getTopBarJs() + R"rawliteral(
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleDebugData() {
  String json = "[";
  bool first = true;
  for (int i = 0; i < DEBUG_BUFFER_SIZE; i++) {
    int idx = (debugBufferHead + i) % DEBUG_BUFFER_SIZE;
    DebugSample &s = debugBuffer[idx];
    if (s.tMs == 0) continue; // пропускаем неинициализированные сэмплы
    if (!first) json += ",";
    first = false;
    json += "{\"t\":" + String(s.tMs) +
            ",\"in\":" + String(s.throttleInV, 2) +
            ",\"out\":" + String(s.throttleOutV, 2) +
            ",\"brake\":" + (s.brake ? "1" : "0") +
            ",\"pas\":" + (s.pasActive ? "1" : "0") +
            ",\"btn\":" + (s.btnPasPressed ? "1" : "0") +
            ",\"lvl\":" + String(s.pasLevel) + "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

// ================= Веб: WiFi и Точка Доступа =================
void handleWifiPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WiFi и Точка доступа</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(

body{background:#111;color:#eee;font-family:sans-serif;padding:15px;max-width:420px;margin:auto}
.card{background:#222;border:1px solid #333;border-radius:8px;padding:15px;margin-bottom:20px}
h2{font-size:18px;margin-top:0;color:#4a90d9;border-bottom:1px solid #333;padding-bottom:8px}
button{padding:10px;width:100%;font-size:15px;border-radius:8px;border:none;background:#333;color:#eee;margin-top:10px;cursor:pointer}
button:hover{background:#444}
button.primary{background:#2980b9;color:#fff}
button.primary:hover{background:#3498db}
input{width:100%;padding:9px;box-sizing:border-box;background:#181818;color:#eee;border:1px solid #444;border-radius:4px;margin-top:6px;margin-bottom:10px;font-size:14px}
.net{padding:10px;background:#1a1a1a;border-radius:6px;margin-top:6px;cursor:pointer;display:flex;justify-content:space-between;border:1px solid #2a2a2a}
.net:active{background:#333}
a{color:#4a90d9;text-decoration:none}
.status{color:#aaa;font-size:13px;margin:8px 0;line-height:1.4}
.hint{color:#888;font-size:12px;margin-top:-6px;margin-bottom:10px;display:block}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a href="/">&larr; Меню</a></p>
<h1>Связь и сеть</h1>

<div class="card">
  <h2>Подключение к Wi-Fi (Клиент)</h2>
  <div class="status" id="wifi_status">Текущий статус: )rawliteral";
  html += (WiFi.status() == WL_CONNECTED) ? ("<b>Подключено к " + WiFi.SSID() + "</b> (IP: " + WiFi.localIP().toString() + ")") : "<i>Не подключено к внешней сети</i>";
  html += R"rawliteral(</div>

  <button type="button" onclick="scan()">Найти доступные сети</button>
  <div id="nets" style="margin-top:10px"></div>

  <form id="f_wifi" style="margin-top:15px">
    <label>Имя сети (SSID):</label>
    <input type="text" id="ssid" name="ssid" placeholder="Выберите сеть или введите вручную">
    <label>Пароль сети:</label>
    <input type="password" id="pass" name="pass" placeholder="Пароль Wi-Fi">
    <button type="submit" class="primary">Подключиться к Wi-Fi</button>
  </form>
</div>

<div class="card">
  <h2>Настройки точки доступа (AP)</h2>
  <div class="status">
    Режим точки доступа: <b>активен</b><br>
    IP-адрес точки: <b>)rawliteral";
  html += WiFi.softAPIP().toString();
  html += R"rawliteral(</b>
  </div>

  <form id="f_ap" style="margin-top:15px">
    <label>Имя точки доступа (SSID):</label>
    <input type="text" id="ap_ssid" name="ap_ssid" value=")rawliteral";
  html += storedApSsid;
  html += R"rawliteral(">

    <label>Пароль точки доступа (открыто):</label>
    <input type="text" id="ap_pass" name="ap_pass" value=")rawliteral";
  html += storedApPass;
  html += R"rawliteral(" placeholder="Оставьте пустым для открытой сети">
    <span class="hint">Пароль отображается открыто. Оставьте пустым для открытой точки (без пароля). Для WPA2 нужно минимум 8 символов.</span>

    <button type="submit" class="primary">Сохранить настройки точки доступа</button>
  </form>
  <div id="ap_status" class="status" style="margin-top:8px"></div>
</div>

<script>
function scan() {
  document.getElementById('nets').innerHTML = '<i>Поиск сетей...</i>';
  fetch('/wifi/scan').then(r=>r.json()).then(list=>{
    if (!list || !list.length) { document.getElementById('nets').innerHTML = '<i>Сети не найдены</i>'; return; }
    document.getElementById('nets').innerHTML = list.map(n =>
      '<div class="net" onclick="pick(\''+n.ssid.replace(/'/g,"")+'\')"><span>'+n.ssid+'</span><span style="color:#888">'+n.rssi+' dBm</span></div>'
    ).join('');
  }).catch(() => {
    document.getElementById('nets').innerHTML = '<span style="color:#e74c3c">Ошибка сканирования</span>';
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
  fetch('/wifi/ap/save', {method:'POST', body:d}).then(r=>r.text()).then(txt=>{
    st.innerHTML = '<b style="color:#2ecc71">Настройки точки доступа сохранены и применены!</b>';
  }).catch(e=>{
    st.innerHTML = '<span style="color:#e74c3c">Ошибка: ' + e.message + '</span>';
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

void handleApSave() {
  if (server.hasArg("ap_ssid")) {
    storedApSsid = server.arg("ap_ssid");
  }
  if (server.hasArg("ap_pass")) {
    storedApPass = server.arg("ap_pass");
  }
  apSettingsSave();
  WiFi.softAP(storedApSsid.c_str(), storedApPass.c_str());
  server.send(200, "text/plain", "OK");
}

// ================= Веб: хаб =================
void handleHub() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenBike Controller v0.1.5-alpha</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
.header{text-align:center;margin-bottom:16px;}
.header h1{margin:0;font-size:24px;}
.header .version{color:#888;font-size:14px;}
a.card{display:block;background:#333;color:#fff;padding:15px;border-radius:8px;margin-bottom:10px;text-decoration:none;transition:background .2s}
a.card:active{background:#444}
.warn{background:#5c1a1a;padding:12px;border-radius:8px;margin-bottom:16px;font-weight:bold;font-size:13px}

/* Блок кнопок управления P и C */
.btn-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:16px;}
.mode-btn{background:#222;border:2px solid #444;border-radius:12px;padding:15px 10px;text-align:center;cursor:pointer;user-select:none;-webkit-user-select:none;touch-action:manipulation;transition:all .15s}
.mode-btn:active{transform:scale(0.96)}
.mode-btn.active-pas{background:#1b382b;border-color:#2ecc71;box-shadow:0 0 10px rgba(46,204,113,0.3)}
.mode-btn.active-cruise{background:#1b2f4c;border-color:#3498db;box-shadow:0 0 10px rgba(52,152,219,0.3)}
.mode-btn .btn-letter{font-size:32px;font-weight:900;line-height:1}
.mode-btn .btn-label{font-size:12px;color:#888;margin-top:6px;text-transform:uppercase;letter-spacing:1px}
.mode-btn .btn-val{font-size:15px;font-weight:bold;margin-top:4px;color:#fff}
.mode-btn.active-pas .btn-letter{color:#2ecc71}
.mode-btn.active-cruise .btn-letter{color:#3498db}
.btn-hint{font-size:10px;color:#666;margin-top:4px}
</style>
</head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(
<div class="header">
  <h1>OpenBike Controller</h1>
  <div class="version">v0.1.5-alpha</div>
</div>

<div class="btn-grid">
  <div class="mode-btn" id="pasBtn">
    <div class="btn-letter">P</div>
    <div class="btn-label">PAS Ассистент</div>
    <div class="btn-val" id="pasTxt">ВЫКЛ</div>
    <div class="btn-hint">Клик: уровень 0 &rarr; 1 ... &rarr; N</div>
  </div>

  <div class="mode-btn" id="cruiseBtn">
    <div class="btn-letter">C</div>
    <div class="btn-label">Круиз-контроль</div>
    <div class="btn-val" id="cruiseTxt">ВЫКЛ</div>
    <div class="btn-hint">Клик: вкл/выкл</div>
  </div>
</div>

<div class="warn">&#9888; Тормоз продублирован сюда как приоритет 0 (можно отключить в настройках газа).</div>
<a class="card" href="/settings/throttle">Газ &rarr;</a>
<a class="card" href="/settings/pas">PAS &rarr;</a>
<a class="card" href="/settings/cruise">Круиз-контроль &rarr;</a>
<a class="card" href="/wifi">Связь и сеть (WiFi / AP) &rarr;</a>
<a class="card" href="/debug">Отладка (график) &rarr;</a>
<a class="card" href="/system">Система &rarr;</a>
)rawliteral" + getTopBarJs() + R"rawliteral(
<script>
let pasEn = )rawliteral" + String(pasEnabled ? "true" : "false") + R"rawliteral(;
let pasLvl = )rawliteral" + String(pasCurrentLevel) + R"rawliteral(;
let pasMax = )rawliteral" + String(pasLevelsCount) + R"rawliteral(;
let cruiseEn = )rawliteral" + String(cruiseEnabled ? "true" : "false") + R"rawliteral(;

function renderUI() {
  const pBtn = document.getElementById("pasBtn");
  const pTxt = document.getElementById("pasTxt");
  if (!pasEn || pasLvl === 0) {
    pBtn.className = "mode-btn";
    pTxt.innerText = "ВЫКЛ";
  } else {
    pBtn.className = "mode-btn active-pas";
    pTxt.innerText = "УРОВЕНЬ " + pasLvl + " / " + pasMax;
  }

  const cBtn = document.getElementById("cruiseBtn");
  const cTxt = document.getElementById("cruiseTxt");
  if (!cruiseEn) {
    cBtn.className = "mode-btn";
    cTxt.innerText = "ВЫКЛ";
  } else {
    cBtn.className = "mode-btn active-cruise";
    cTxt.innerText = "АКТИВЕН";
  }
}

document.getElementById("pasBtn").addEventListener("click", () => {
  if (navigator.vibrate) navigator.vibrate(40);
  // Циклический выбор уровня: 0 -> 1 -> 2 -> ... -> N -> 0
  if (!pasEn || pasLvl === 0) {
    pasEn = true;
    pasLvl = 1;
  } else {
    pasLvl++;
    if (pasLvl > pasMax) {
      pasLvl = 0;
      pasEn = false;
    }
  }
  renderUI();
  fetch("/api/pas/set_level?level=" + pasLvl);
});

document.getElementById("cruiseBtn").addEventListener("click", () => {
  if (navigator.vibrate) navigator.vibrate(40);
  cruiseEn = !cruiseEn;
  renderUI();
  fetch("/api/cruise/toggle");
});

renderUI();

// Периодическая синхронизация с физической кнопкой на руле
setInterval(() => {
  fetch("/status/sys").then(r=>r.json()).then(d => {
    if (d.pas_lvl !== undefined && (d.pas_lvl !== pasLvl || d.pas_en !== pasEn || d.cruise_en !== cruiseEn)) {
      pasEn = d.pas_en;
      pasLvl = d.pas_lvl;
      if (d.pas_cnt) pasMax = d.pas_cnt;
      cruiseEn = d.cruise_en;
      renderUI();
    }
  }).catch(()=>{});
}, 1000);
</script>
</body>
</html>)rawliteral";
  server.send(200, "text/html", html);
}

// ================= Веб: API для кнопок PAS/Cruise =================
void handleApiPasSetLevel() {
  if (server.hasArg("level")) {
    int newLevel = server.arg("level").toInt();
    // Уровень 0 means PAS off, levels 1..pasLevelsCount are valid
    if (newLevel >= 0 && newLevel <= pasLevelsCount) {
      pasCurrentLevel = newLevel;
      // Автоматически включить/выключить PAS в зависимости от уровня
      pasEnabled = (newLevel > 0);
      server.send(200, "text/plain", "OK");
      return;
    }
  }
  server.send(400, "text/plain", "Bad Request");
}

void handleApiPasToggleMode() {
  pasEnabled = !pasEnabled;
  if (!pasEnabled) {
    pasCurrentLevel = 0;
  } else if (pasCurrentLevel == 0) {
    pasCurrentLevel = 1; // По умолчанию уровень 1 при включении
  }
  server.send(200, "text/plain", "OK");
}

void handleApiCruiseToggleMode() {
  cruiseEnabled = !cruiseEnabled;
  server.send(200, "text/plain", "OK");
}

// ================= Веб: Газ =================
void handleThrottlePage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Газ</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
label{display:block;margin-top:12px}input{width:100%;padding:6px;box-sizing:border-box;background:#222;color:#eee;border:1px solid #444}
button{margin-top:15px;padding:10px;width:100%;font-size:16px}
.chk{display:flex;gap:8px;align-items:center;margin-top:12px}.chk input{width:auto}
fieldset{border:1px solid #333;border-radius:8px;margin-top:15px;padding:10px}
.warn{color:#ff8888;font-size:13px;margin-top:6px}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

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
)rawliteral" + getTopBarJs() + R"rawliteral(
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
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
label{display:block;margin-top:12px}input,select{width:100%;padding:6px;box-sizing:border-box;background:#222;color:#eee;border:1px solid #444}
button{margin-top:15px;padding:10px;width:100%;font-size:16px}
.chk{display:flex;gap:8px;align-items:center;margin-top:12px}.chk input{width:auto}
fieldset{border:1px solid #333;border-radius:8px;margin-top:15px;padding:10px}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a href="/" style="color:#4a90d9">&larr; Настройки</a></p>
<h1>PAS</h1>
<form id="f">
<fieldset><legend>Ассистент PAS</legend>
<div class="chk"><input type="checkbox" name="en" )rawliteral"; html += pasEnabled?"checked":"";
  html += R"rawliteral(><label>Включить PAS</label></div>
</fieldset>
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
  pasEnabled = server.hasArg("en");

  pasSettingsSave();
  if (edgeChanged) reattachPasInterrupt();
  server.send(200, "text/plain", "OK");
}

// ================= Веб: заливка прошивки прямо через браузер =================
void handleUpdatePage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Обновление прошивки</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
button{margin-top:15px;padding:10px;width:100%;font-size:16px}</style>
</head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a href="/" style="color:#4a90d9">&larr; Настройки</a></p>
<h1>Загрузить прошивку (.bin)</h1>
<p style="color:#888;font-size:13px">В Arduino IDE: Sketch &rarr; Export Compiled Binary — появится .bin рядом со скетчем. Выбери его тут и жми "Залить". Займёт секунд 20-30, плата сама перезагрузится.</p>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="update" accept=".bin">
<button type="submit">Залить</button>
</form>
)rawliteral" + getTopBarJs() + R"rawliteral(
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
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
bool mdnsStarted = false;
unsigned long wifiConnectStartMs = 0;
bool wifiApActive = false;

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

void apSettingsSave() {
  prefs.begin("ap", false);
  prefs.putString("ssid", storedApSsid);
  prefs.putString("pass", storedApPass);
  prefs.end();
}

void apSettingsLoad() {
  prefs.begin("ap", true);
  storedApSsid = prefs.getString("ssid", "BikeControllerAP");
  storedApPass = prefs.getString("pass", "");
  prefs.end();
}

void wifiConnect() {
  String useSsid = storedSsid.length() > 0 ? storedSsid : String(ssid);
  String usePass = storedSsid.length() > 0 ? storedPass : String(password);
  WiFi.disconnect();
  delay(50);
  wifiConnectStartMs = millis();
  WiFi.begin(useSsid.c_str(), usePass.c_str());
  Serial.print("Подключение к "); Serial.println(useSsid);
}

// Автомат состояний WiFi:
// 1. При успешном подключении к роутеру (STA) гасим точку доступа (AP) и поднимаем mDNS.
// 2. Если связи с роутером нет, поднимаем аварийную точку доступа (AP).
void updateWifiStateMachine() {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiApActive) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      wifiApActive = false;
      Serial.print("WiFi подключен: "); Serial.println(WiFi.localIP());
    }
    if (!mdnsStarted) {
      if (MDNS.begin(MDNS_HOST)) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
        Serial.printf("mDNS запущен: http://%s.local\n", MDNS_HOST);
      }
    }
  } else {
    // Не подключены к STA
    if (!wifiApActive) {
      if (wifiConnectStartMs == 0) wifiConnectStartMs = millis();
      // Если прошло больше 7 секунд попытки подключения к роутеру — поднимаем AP
      if (millis() - wifiConnectStartMs > 7000) {
        Serial.println("Роутер недоступен. Запуск точки доступа (AP)...");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(storedApSsid.c_str(), storedApPass.c_str());
        wifiApActive = true;
        Serial.print("Точка доступа: "); Serial.println(storedApSsid);
        Serial.print("IP адрес AP: "); Serial.println(WiFi.softAPIP());
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("--- OpenBike Controller v0.1.5-alpha ---"));

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

  ledcSetup(0, 5000, 8);
  ledcAttachPin(HEADLIGHT_PIN, 0);
  ledcSetup(1, 5000, 8);
  ledcAttachPin(DRL_PIN, 1);
  ledcWrite(1, DRL_DEFAULT_BRIGHTNESS); // ДХО горит всегда, пока плата включена

  // Настоящий ЦАП ESP32 — ledcAttach больше не нужен, dacWrite() работает сразу

  throttleSettingsLoad();
  pasSettingsLoad();
  apSettingsLoad();

  setThrottleOutputSafeZero();

  reattachPasInterrupt();

  // Инициализация WiFi
  WiFi.mode(WIFI_STA);
  wifiCredsLoad();
  wifiConnectStartMs = millis();
  wifiConnect();

  Serial.print("Подключение к WiFi");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 4000) {
    delay(250); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    wifiApActive = false;
    Serial.print("Открой в браузере: http://"); Serial.println(WiFi.localIP());
    if (MDNS.begin(MDNS_HOST)) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
      Serial.printf("mDNS запущен: http://%s.local\n", MDNS_HOST);
    }
  } else {
    // Если роутер сразу не ответил — поднимаем AP
    Serial.println("WiFi не найден. Запускаю режим точки доступа (AP).");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(storedApSsid.c_str(), storedApPass.c_str());
    wifiApActive = true;
    IPAddress apIP = WiFi.softAPIP();
    Serial.print("Точка доступа: "); Serial.println(storedApSsid);
    Serial.print("IP адрес AP: "); Serial.println(apIP);
  }

  cruiseSettingsLoad();

  server.on("/", handleHub);
  server.on("/api/pas/set_level", HTTP_GET, handleApiPasSetLevel);
  server.on("/api/pas/toggle_mode", HTTP_GET, handleApiPasToggleMode);
  server.on("/api/cruise/toggle", HTTP_GET, handleApiCruiseToggleMode);
  server.on("/settings/throttle", handleThrottlePage);
  server.on("/settings/throttle/save", HTTP_POST, handleThrottleSave);
  server.on("/settings/pas", handlePasPage);
  server.on("/settings/pas/save", HTTP_POST, handlePasSave);
  server.on("/wifi/ap/save", HTTP_POST, handleApSave);
  server.on("/wifi", handleWifiPage);
  server.on("/wifi/scan", handleWifiScan);
  server.on("/wifi/save", HTTP_POST, handleWifiSave);
server.on("/settings/cruise", handleCruisePage);
server.on("/settings/cruise/save", HTTP_POST, handleCruiseSave);
  server.on("/debug", handleDebugPage);
  server.on("/debug/data", handleDebugData);
  server.on("/system", handleSystemPage);
  server.on("/system/export", handleSettingsExport);
  server.on("/system/import", HTTP_POST, handleSettingsImport);
  server.on("/status/sys", HTTP_GET, handleSystemStatus);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);
  server.begin();

  Serial.println("Готово. Упрощённый прототип запущен.");
}

void loop() {
  unsigned long loopStart = micros();
  
  server.handleClient();
  updateWifiStateMachine();
  updatePasDetection();
  updatePasButton();
  updateThrottle();
  updateLightButtons();
  updateTurnSignals();
  updateHorn();
  updateBuzzer();
  
  cpuBusyTimeMicros += (micros() - loopStart);
  
  if (millis() - cpuMeasureStartMs >= 1000) {
    cpuUsagePercent = (int)constrain((cpuBusyTimeMicros * 100ULL) / ((millis() - cpuMeasureStartMs) * 1000ULL), 0ULL, 100ULL);
    cpuBusyTimeMicros = 0;
    cpuMeasureStartMs = millis();
  }
  
  delay(5);
}

// --- Cruise Control Module Implementation ---

void cruiseSettingsSave() {
  prefs.begin("cruise", false);
  prefs.putInt("cnt", cruiseLevelsCount);
  prefs.putBytes("pct", cruiseLevelPercent, sizeof(cruiseLevelPercent));
  prefs.putInt("ssEn", cruiseSoftStartEnabled ? 1 : 0);
  prefs.putInt("spEn", cruiseSoftStopEnabled ? 1 : 0);
  prefs.putULong("ssMs", cruiseSoftStartMs);
  prefs.putULong("spMs", cruiseSoftStopMs);
  prefs.putBool("cen", cruiseEnabled); // Save cruiseEnabled state
  prefs.end();
}

void cruiseSettingsLoad() {
  prefs.begin("cruise", true);
  cruiseLevelsCount = prefs.getInt("cnt", 3);
  size_t got = prefs.getBytes("pct", cruiseLevelPercent, sizeof(cruiseLevelPercent));
  cruiseSoftStartEnabled = prefs.getInt("ssEn", 0) != 0;
  cruiseSoftStopEnabled = prefs.getInt("spEn", 0) != 0;
  cruiseEnabled = prefs.getBool("cen", true); // Load cruiseEnabled state
  cruiseSoftStartMs = prefs.getULong("ssMs", 500);
  cruiseSoftStopMs = prefs.getULong("spMs", 800);
  prefs.end();
  if (got != sizeof(cruiseLevelPercent)) {
    cruiseAutoDistribute();
  }
}

// ================= Веб: Cruise Control =================
void handleCruisePage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Cruise Control</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(

body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
label{display:block;margin-top:12px}
input,select{width:100%;padding:6px;box-sizing:border-box;background:#222;color:#eee;border:1px solid #444}
button{margin-top:15px;padding:10px;width:100%;font-size:16px}
.chk{display:flex;gap:8px;align-items:center;margin-top:12px}
.chk input{width:auto}
fieldset{border:1px solid #333;border-radius:8px;margin-top:15px;padding:10px}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a href="/" style="color:#4a90d9">&larr; Настройки</a></p>
<h1>Cruise Control</h1>
<form id="f">
<fieldset><legend>Круиз-контроль</legend>
<div class="chk"><input type="checkbox" name="cen" )rawliteral";
  html += cruiseEnabled ? "checked" : "";
  html += R"rawliteral(><label>Включить Cruise Control</label></div>
</fieldset>
<fieldset><legend>Настройки Cruise Control</legend>
<label>Количество уровней</label>
<input type="number" id="count" name="count" min="0" max="100" value=")rawliteral"; 
  html += String(cruiseLevelsCount);
  html += R"rawliteral(" oninput="renderLevels()">
<div id="levels"></div>
<button type="button" onclick="autoDistribute()">Автораспределение</button>
</fieldset>
<fieldset><legend>Мягкий старт/стоп</legend>
<div class="chk"><input type="checkbox" name="ssEn" )rawliteral";
  html += cruiseSoftStartEnabled ? "checked" : "";
  html += R"rawliteral(><label>Мягкий старт</label></div>
<label>Время разгона (мс)</label><input type="number" name="ssMs" value=")rawliteral";
  html += String(cruiseSoftStartMs);
  html += R"rawliteral(">
<div class="chk"><input type="checkbox" name="spEn" )rawliteral";
  html += cruiseSoftStopEnabled ? "checked" : "";
  html += R"rawliteral(><label>Мягкий стоп</label></div>
<label>Время торможения (мс)</label><input type="number" name="spMs" value=")rawliteral";
  html += String(cruiseSoftStopMs);
  html += R"rawliteral(">
</fieldset>
<button type="submit">Сохранить</button>
</form>
<script>
const saved = [)rawliteral";
  for (int i = 0; i < CRUISE_MAX_LEVELS; i++) {
    html += String(cruiseLevelPercent[i]);
    if (i < CRUISE_MAX_LEVELS - 1) html += ",";
  }
  html += R"rawliteral(];
function renderLevels(){
  const count = parseInt(document.getElementById('count').value)||0;
  const div = document.getElementById('levels'); div.innerHTML='';
  for(let i=0;i<count;i++){
    const val = saved[i]!==undefined?saved[i]:0;
    div.innerHTML += '<label>Уровень '+(i+1)+' — значение (%)</label><input type="number" step="0.01" class="lvl-input" data-idx="'+i+'" value="'+val+'">';
  }
}
function autoDistribute(){
  const count = parseInt(document.getElementById('count').value)||0;
  for(let i=0;i<count;i++) saved[i]=Math.round((i+1)*10000/count)/100;
  renderLevels();
}
renderLevels();
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  const d=new FormData(this);
  const count = parseInt(document.getElementById('count').value)||0;
  const lvls = [];
  document.querySelectorAll('.lvl-input').forEach(inp => {
    lvls.push(parseFloat(inp.value)||0);
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
  cruiseEnabled = server.hasArg("cen");
  cruiseLevelsCount = server.arg("count").toInt();
  if (cruiseLevelsCount < 0) cruiseLevelsCount = 0;
  if (cruiseLevelsCount > CRUISE_MAX_LEVELS) cruiseLevelsCount = CRUISE_MAX_LEVELS;
  
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
          float v = item.toFloat();
          if (v < 0) v = 0;
          if (v > 100) v = 100;
          cruiseLevelPercent[idx++] = v;
        }
        currentPos = commaPos + 1;
      }
    }
  } else {
    for (int i = 0; i < cruiseLevelsCount; i++) {
      String key = "lvl" + String(i);
      if (server.hasArg(key)) {
        float v = server.arg(key).toFloat();
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        cruiseLevelPercent[i] = v;
      }
    }
  }

  cruiseSoftStartEnabled = server.hasArg("ssEn");
  cruiseSoftStopEnabled = server.hasArg("spEn");
  cruiseSoftStartMs = server.arg("ssMs").toInt();
  cruiseSoftStopMs = server.arg("spMs").toInt();
  cruiseSettingsSave();
  server.send(200, "text/plain", "OK");
}

// ================= Веб: Система (экспорт/импорт настроек, OTA) =================
void handleSystemPage() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Система</title>
<style>
)rawliteral" + getTopBarCss() + R"rawliteral(
body{font-family:sans-serif;padding:20px;max-width:400px;margin:auto;background:#111;color:#eee}
a.card, label.card{display:block;background:#333;color:#fff;padding:15px;border-radius:8px;margin-bottom:10px;text-decoration:none;box-sizing:border-box;text-align:center;cursor:pointer}
a.card:hover, label.card:hover{background:#444}
.warn{background:#5c1a1a;padding:12px;border-radius:8px;margin-bottom:20px;font-weight:bold;font-size:14px}
a.back{color:#4a90d9;text-decoration:none;display:inline-block;margin-top:20px;}
</style></head><body>
)rawliteral" + getTopBarHtml() + R"rawliteral(

<p><a class="back" href="/">&larr; Меню</a></p>
<h1>Система</h1>
<a class="card" href="/update">Обновить прошивку (.bin) &rarr;</a>
<a class="card" href="#" onclick="exportSettings(); return false;">Экспортировать настройки (.json) &rarr;</a>
<form id="importForm" enctype="multipart/form-data" method="post" action="/system/import" style="margin-top:10px">
  <label for="settingsFile" class="card">Импортировать настройки (.json) &rarr;</label>
  <input type="file" id="settingsFile" name="settingsFile" accept=".json" onchange="importSettings(this)" style="display:none;">
</form>
  <input type="file" id="settingsFile" name="settingsFile" accept=".json" onchange="importSettings(this)" style="display:none;">
</form>

<script>
function exportSettings() {
  fetch('/system/export').then(r => {
    if (!r.ok) throw new Error('Export failed');
    return r.json();
  }).then(data => {
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'bike_settings.json';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }).catch(err => {
    alert('Ошибка экспорта: ' + err);
  });
}

function importSettings(input) {
  if (!input.files || !input.files[0]) return;
  const file = input.files[0];
  const reader = new FileReader();
  reader.onload = function(e) {
    const content = e.target.result;
    const form = new FormData();
    form.append('settingsFile', content);
    fetch('/system/import', { method: 'POST', body: form })
      .then(r => r.text())
      .then(msg => {
        alert(msg);
        setTimeout(() => location.href = '/', 2000);
      })
      .catch(err => alert('Ошибка импорта: ' + err));
  };
  reader.readAsText(file);
}
</script>
)rawliteral" + getTopBarJs() + R"rawliteral(
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
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
  json += "\"brakeCut\":" + String(ownBrakeCutoffEnabled ? 1 : 0);
  json += "},";
  json += "\"pas\":{";
  json += "\"magnets\":" + String(pasMagnetCount) + ",";
  json += "\"edge\":" + String(pasEdgeMode) + ",";
  json += "\"angle\":" + String(pasActivationAngle) + ",";
  json += "\"timeout\":" + String(pasTimeoutMs) + ",";
  json += "\"cnt\":" + String(pasLevelsCount) + ",";
  json += "\"curLvl\":" + String(pasCurrentLevel) + ",";
  json += "\"ssEn\":" + String(pasSoftStartEnabled ? 1 : 0) + ",";
  json += "\"spEn\":" + String(pasSoftStopEnabled ? 1 : 0) + ",";
  json += "\"ssMs\":" + String(pasSoftStartMs) + ",";
  json += "\"spMs\":" + String(pasSoftStopMs) + ",";
  json += "\"enabled\":" + String(pasEnabled ? 1 : 0) + ",";
  json += "\"pct\":[";
  for (int i = 0; i < pasLevelsCount; i++) {
    json += String(pasLevelPercent[i]);
    if (i < pasLevelsCount - 1) json += ",";
  }
  json += "]";
  json += "},";
  json += "\"wifi\":{";
  json += "\"ssid\":\"" + storedSsid + "\",";
  json += "\"pass\":\"" + storedPass + "\""; // Add storedPass
  json += "},";
  json += "\"ap\":{"; // New AP settings section
  json += "\"ssid\":\"" + storedApSsid + "\",";
  json += "\"pass\":\"" + storedApPass + "\"";
  json += "},";
  json += "\"cruise\":{"; // New Cruise Control settings section
  json += "\"cnt\":" + String(cruiseLevelsCount) + ",";
  json += "\"pct\":[";
  for (int i = 0; i < cruiseLevelsCount; i++) {
    json += String(cruiseLevelPercent[i]);
    if (i < cruiseLevelsCount - 1) json += ",";
  }
  json += "],";
  json += "\"ssEn\":" + String(cruiseSoftStartEnabled ? 1 : 0) + ",";
  json += "\"spEn\":" + String(cruiseSoftStopEnabled ? 1 : 0) + ",";
  json += "\"ssMs\":" + String(cruiseSoftStartMs) + ",";
  json += "\"spMs\":" + String(cruiseSoftStopMs);
  json += "}";
  json += "}";
  server.send(200, "application/json", json);
}

void handleSettingsImport() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  if (!server.hasArg("settingsFile")) {
    server.send(400, "text/plain", "No settings file uploaded");
    return;
  }
  String fileContent = server.arg("settingsFile");
  int idx = fileContent.indexOf("\"throttle\":{");
  if (idx != -1) {
    String throttleJson = fileContent.substring(fileContent.indexOf('{', idx) + 1, fileContent.indexOf('}', idx));
    if (throttleJson.indexOf("\"inMinV\":") != -1)
      throttleInMinV = throttleJson.substring(throttleJson.indexOf("\"inMinV\":") + 9, throttleJson.indexOf(',', throttleJson.indexOf("\"inMinV\":"))).toFloat();
    if (throttleJson.indexOf("\"inMaxV\":") != -1)
      throttleInMaxV = throttleJson.substring(throttleJson.indexOf("\"inMaxV\":") + 9, throttleJson.indexOf(',', throttleJson.indexOf("\"inMaxV\":"))).toFloat();
    if (throttleJson.indexOf("\"outMinV\":") != -1)
      throttleOutMinV = throttleJson.substring(throttleJson.indexOf("\"outMinV\":") + 10, throttleJson.indexOf(',', throttleJson.indexOf("\"outMinV\":"))).toFloat();
    if (throttleJson.indexOf("\"outMaxV\":") != -1)
      throttleOutMaxV = throttleJson.substring(throttleJson.indexOf("\"outMaxV\":") + 10, throttleJson.indexOf(',', throttleJson.indexOf("\"outMaxV\":"))).toFloat();
    if (throttleJson.indexOf("\"divRatio\":") != -1)
      throttleInputDividerRatio = throttleJson.substring(throttleJson.indexOf("\"divRatio\":") + 11, throttleJson.indexOf(',', throttleJson.indexOf("\"divRatio\":"))).toFloat();
    if (throttleJson.indexOf("\"gain\":") != -1)
      throttleOutputGain = throttleJson.substring(throttleJson.indexOf("\"gain\":") + 7, throttleJson.indexOf(',', throttleJson.indexOf("\"gain\":"))).toFloat();
    if (throttleJson.indexOf("\"ssEn\":") != -1)
      throttleSoftStartEnabled = throttleJson.substring(throttleJson.indexOf("\"ssEn\":") + 7, throttleJson.indexOf(',', throttleJson.indexOf("\"ssEn\":"))).toInt() != 0;
    if (throttleJson.indexOf("\"spEn\":") != -1)
      throttleSoftStopEnabled = throttleJson.substring(throttleJson.indexOf("\"spEn\":") + 7, throttleJson.indexOf(',', throttleJson.indexOf("\"spEn\":"))).toInt() != 0;
    if (throttleJson.indexOf("\"ssMs\":") != -1)
      throttleSoftStartMs = throttleJson.substring(throttleJson.indexOf("\"ssMs\":") + 7, throttleJson.indexOf(',', throttleJson.indexOf("\"ssMs\":"))).toInt();
    if (throttleJson.indexOf("\"spMs\":") != -1)
      throttleSoftStopMs = throttleJson.substring(throttleJson.indexOf("\"spMs\":") + 7, throttleJson.indexOf(',', throttleJson.indexOf("\"spMs\":"))).toInt();
    if (throttleJson.indexOf("\"brakeCut\":") != -1) {
      int bcIdx = throttleJson.indexOf("\"brakeCut\":") + 11;
      int bcEnd = throttleJson.indexOf(',', bcIdx);
      if (bcEnd == -1) bcEnd = throttleJson.length();
      ownBrakeCutoffEnabled = throttleJson.substring(bcIdx, bcEnd).toInt() != 0;
    }
    throttleSettingsSave();
  }
  idx = fileContent.indexOf("\"pas\":{");
  if (idx != -1) {
    String pasJson = fileContent.substring(fileContent.indexOf('{', idx) + 1, fileContent.indexOf('}', idx));
    if (pasJson.indexOf("\"magnets\":") != -1)
      pasMagnetCount = pasJson.substring(pasJson.indexOf("\"magnets\":") + 10, pasJson.indexOf(',', pasJson.indexOf("\"magnets\":"))).toInt();
    if (pasJson.indexOf("\"edge\":") != -1)
      pasEdgeMode = pasJson.substring(pasJson.indexOf("\"edge\":") + 7, pasJson.indexOf(',', pasJson.indexOf("\"edge\":"))).toInt();
    if (pasJson.indexOf("\"angle\":") != -1)
      pasActivationAngle = pasJson.substring(pasJson.indexOf("\"angle\":") + 8, pasJson.indexOf(',', pasJson.indexOf("\"angle\":"))).toInt();
    if (pasJson.indexOf("\"timeout\":") != -1)
      pasTimeoutMs = pasJson.substring(pasJson.indexOf("\"timeout\":") + 10, pasJson.indexOf(',', pasJson.indexOf("\"timeout\":"))).toInt();
    if (pasJson.indexOf("\"cnt\":") != -1)
      pasLevelsCount = pasJson.substring(pasJson.indexOf("\"cnt\":") + 6, pasJson.indexOf(',', pasJson.indexOf("\"cnt\":"))).toInt();
    if (pasJson.indexOf("\"curLvl\":") != -1)
      pasCurrentLevel = pasJson.substring(pasJson.indexOf("\"curLvl\":") + 9, pasJson.indexOf(',', pasJson.indexOf("\"curLvl\":"))).toInt();
    if (pasJson.indexOf("\"ssEn\":") != -1)
      pasSoftStartEnabled = pasJson.substring(pasJson.indexOf("\"ssEn\":") + 7, pasJson.indexOf(',', pasJson.indexOf("\"ssEn\":"))).toInt() != 0;
    if (pasJson.indexOf("\"spEn\":") != -1)
      pasSoftStopEnabled = pasJson.substring(pasJson.indexOf("\"spEn\":") + 7, pasJson.indexOf(',', pasJson.indexOf("\"spEn\":"))).toInt() != 0;
    if (pasJson.indexOf("\"ssMs\":") != -1)
      pasSoftStartMs = pasJson.substring(pasJson.indexOf("\"ssMs\":") + 7, pasJson.indexOf(',', pasJson.indexOf("\"ssMs\":"))).toInt();
    if (pasJson.indexOf("\"spMs\":") != -1)
      pasSoftStopMs = pasJson.substring(pasJson.indexOf("\"spMs\":") + 7, pasJson.indexOf(',', pasJson.indexOf("\"spMs\":"))).toInt();
    int pctStart = pasJson.indexOf("\"pct\":[");
    if (pctStart != -1) {
      pctStart += 7;
      int pctEnd = pasJson.indexOf("]", pctStart);
      String pctJson = pasJson.substring(pctStart, pctEnd);
      int currentPct = 0;
      int commaPos = -1;
      for (int i = 0; i < pasLevelsCount; i++) {
        commaPos = pctJson.indexOf(',', currentPct);
        if (commaPos == -1) commaPos = pctJson.length();
        pasLevelPercent[i] = pctJson.substring(currentPct, commaPos).toInt();
        currentPct = commaPos + 1;
      }
    }
    pasSettingsSave();
    reattachPasInterrupt();
  }
  idx = fileContent.indexOf("\"wifi\":{");
  if (idx != -1) {
    String wifiJson = fileContent.substring(fileContent.indexOf('{', idx) + 1, fileContent.indexOf('}', idx));
    int ssidPos = wifiJson.indexOf("\"ssid\":\"");
    if (ssidPos != -1) {
      String importedSsid = wifiJson.substring(ssidPos + 8);
      importedSsid = importedSsid.substring(0, importedSsid.indexOf("\""));
      if (importedSsid.length() > 0) {
        wifiCredsSave(importedSsid, "");
      }
    }
  }

  // --- AP Settings ---
  idx = fileContent.indexOf("\"ap\":{");
  if (idx != -1) {
    String apJson = fileContent.substring(fileContent.indexOf('{', idx) + 1, fileContent.indexOf('}', idx));
    int apSsidPos = apJson.indexOf("\"ssid\":\"");
    if (apSsidPos != -1) {
      String importedApSsid = apJson.substring(apSsidPos + 8);
      importedApSsid = importedApSsid.substring(0, importedApSsid.indexOf("\""));
      if (importedApSsid.length() > 0) {
        storedApSsid = importedApSsid;
        storedApPass = ""; // Clear pass if SSID is changed
        int apPassPos = apJson.indexOf("\"pass\":\"");
        if (apPassPos != -1) {
          String importedApPass = apJson.substring(apPassPos + 8);
          importedApPass = importedApPass.substring(0, importedApPass.indexOf("\""));
          storedApPass = importedApPass;
        }
        apSettingsSave(); // Save AP settings
      }
    }
  }

  // --- Cruise Control Settings ---
  idx = fileContent.indexOf("\"cruise\":{");
  if (idx != -1) {
    String cruiseJson = fileContent.substring(fileContent.indexOf('{', idx) + 1, fileContent.indexOf('}', idx));
    if (cruiseJson.indexOf("\"cnt\":") != -1) cruiseLevelsCount = cruiseJson.substring(cruiseJson.indexOf("\"cnt\":") + 6, cruiseJson.indexOf(',', cruiseJson.indexOf("\"cnt\":"))).toInt();
    if (cruiseJson.indexOf("\"ssEn\":") != -1) cruiseSoftStartEnabled = cruiseJson.substring(cruiseJson.indexOf("\"ssEn\":") + 7, cruiseJson.indexOf(',', cruiseJson.indexOf("\"ssEn\":"))).toInt() != 0;
    if (cruiseJson.indexOf("\"spEn\":") != -1) cruiseSoftStopEnabled = cruiseJson.substring(cruiseJson.indexOf("\"spEn\":") + 7, cruiseJson.indexOf(',', cruiseJson.indexOf("\"spEn\":"))).toInt() != 0;
    if (cruiseJson.indexOf("\"ssMs\":") != -1) cruiseSoftStartMs = cruiseJson.substring(cruiseJson.indexOf("\"ssMs\":") + 7, cruiseJson.indexOf(',', cruiseJson.indexOf("\"ssMs\":"))).toInt();
    if (cruiseJson.indexOf("\"spMs\":") != -1) cruiseSoftStopMs = cruiseJson.substring(cruiseJson.indexOf("\"spMs\":") + 7, cruiseJson.indexOf(',', cruiseJson.indexOf("\"spMs\":"))).toInt();
    
    int pctStart = cruiseJson.indexOf("\"pct\":[");
    if (pctStart != -1) {
      pctStart += 7;
      int pctEnd = cruiseJson.indexOf("]", pctStart);
      String pctJson = cruiseJson.substring(pctStart, pctEnd);
      int currentPct = 0;
      int commaPos = -1;
      for (int i = 0; i < cruiseLevelsCount; i++) {
        commaPos = pctJson.indexOf(',', currentPct);
        if (commaPos == -1) commaPos = pctJson.length();
        cruiseLevelPercent[i] = pctJson.substring(currentPct, commaPos).toFloat();
        currentPct = commaPos + 1;
      }
    }
    cruiseSettingsSave();
    cruiseAutoDistribute(); // Recalculate levels if count changed or for safety
  }

  server.send(200, "text/plain", "Настройки успешно импортированы. Перезагрузка...");
  delay(1500);
  ESP.restart();
}
