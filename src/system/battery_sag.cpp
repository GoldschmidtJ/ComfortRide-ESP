// ================= Батарейный саг-гард ("последняя капля") =================
// Ограничивает throttle / PAS / cruise при низком напряжении батареи.
// Состояния:
//   NORMAL  — напряжение выше sag_threshold + hysteresis
//   SAG     — напряжение ниже sag_threshold (но выше cutoff) — ограничение мощности
//   CUTOFF  — напряжение ниже cutoff — принудительный ноль

#include "system/battery_sag.h"
#include <math.h>
#include "system/storage.h" // Preferences prefs

// Глобальные переменные
BatterySagConfig batterySagConfig;
BatterySagStateData batterySagState;

// Внутренние таймеры для дебаунса переходов
static unsigned long sagLastTickMs = 0;

BatterySagConfig batterySagDefaultConfig() {
    BatterySagConfig cfg;
    cfg.enabled = true;
    cfg.sag_threshold_mv = 3500;
    cfg.sag_hysteresis_mv = 200;
    cfg.cutoff_mv = 3200;
    cfg.voltage_div = 4.5f;
    return cfg;
}

void batterySagInit() {
    batterySagConfig = batterySagDefaultConfig();
    batterySagState.state = BatterySagState::NORMAL;
    batterySagState.battery_mv = 0;
    batterySagState.battery_mv_raw_adc = 0;
    batterySagState.voltage_raw = 0.0f;
    batterySagState.state_since_ms = millis();
    sagLastTickMs = millis();
}

void updateBatterySag() {
    unsigned long now = millis();
    if (now - sagLastTickMs < 200) return;
    sagLastTickMs = now;

    int adcPin = pinConfig.batteryAdc;
    int adcVal = (adcPin >= 0) ? analogRead(adcPin) : 0;
    float batteryV = (adcVal / 4095.0f) * 3.3f * batterySagConfig.voltage_div;

    batterySagState.battery_mv = (int)(batteryV * 1000.0f);
    batterySagState.battery_mv_raw_adc = adcVal;
    batterySagState.voltage_raw = batteryV;
    batterySagState.state_since_ms = now;
}

float batterySagLimitVoltage(float targetV) {
    if (!batterySagConfig.enabled) return targetV;

    unsigned long now = millis();

    // Читаем напряжение батареи из ADC
    int adcPin = pinConfig.batteryAdc;
    int adcVal = (adcPin >= 0) ? analogRead(adcPin) : 0;
    float batteryV = (adcVal / 4095.0f) * 3.3f * batterySagConfig.voltage_div;
    batterySagState.battery_mv = (int)(batteryV * 1000.0f);
    batterySagState.voltage_raw = batteryV;
    batterySagState.battery_mv_raw_adc = adcVal;

    BatterySagState prevState = batterySagState.state;

    if (batterySagState.battery_mv < batterySagConfig.cutoff_mv) {
        batterySagState.state = BatterySagState::CUTOFF;
        batterySagState.state_since_ms = now;
        return 0.0f;
    } else if (batterySagState.battery_mv < batterySagConfig.sag_threshold_mv) {
        batterySagState.state = BatterySagState::SAG;
        batterySagState.state_since_ms = now;
        float ratio = (float)(batterySagState.battery_mv - batterySagConfig.cutoff_mv) /
                      (float)(batterySagConfig.sag_threshold_mv - batterySagConfig.cutoff_mv);
        ratio = constrain(ratio, 0.0f, 1.0f);
        return targetV * ratio;
    } else {
        if (prevState == BatterySagState::SAG &&
            batterySagState.battery_mv > batterySagConfig.sag_threshold_mv + batterySagConfig.sag_hysteresis_mv) {
            batterySagState.state = BatterySagState::NORMAL;
            batterySagState.state_since_ms = now;
        } else {
            batterySagState.state = BatterySagState::NORMAL;
        }
    }
    return targetV;
}

BatterySagState batterySagGetState() {
    return batterySagState.state;
}

BatterySagStateData batterySagGetStateData() {
    return batterySagState;
}

void batterySagSave() {
    prefs.begin("battery_sag", false);
    prefs.putInt("state", static_cast<int>(batterySagState.state));
    prefs.putInt("battery_mv", batterySagState.battery_mv);
    prefs.putInt("battery_mv_raw_adc", batterySagState.battery_mv_raw_adc);
    prefs.putFloat("voltage_raw", batterySagState.voltage_raw);
    prefs.putULong("state_since_ms", batterySagState.state_since_ms);
    prefs.end();
}

void batterySagLoad() {
    prefs.begin("battery_sag", true);
    batterySagState.state = static_cast<BatterySagState>(prefs.getInt("state", static_cast<int>(BatterySagState::NORMAL)));
    batterySagState.battery_mv = prefs.getInt("battery_mv", 0);
    batterySagState.battery_mv_raw_adc = prefs.getInt("battery_mv_raw_adc", 0);
    batterySagState.voltage_raw = prefs.getFloat("voltage_raw", 0.0f);
    batterySagState.state_since_ms = prefs.getULong("state_since_ms", 0);
    prefs.end();
}