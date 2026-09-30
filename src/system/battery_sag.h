#ifndef BATTERY_SAG_H
#define BATTERY_SAG_H

#include <Arduino.h>
#include "system/hardware_config.h" // PinConfig struct

// ================= Батарейный саг-гард ("последняя капля") =================
// Ограничивает throttle / PAS / cruise при низком напряжении батареи.
// Состояния:
//   NORMAL  — напряжение выше sag_threshold + hysteresis
//   SAG     — напряжение ниже sag_threshold (но выше cutoff) — ограничение мощности
//   CUTOFF  — напряжение ниже cutoff — принудительный ноль

enum class BatterySagState : uint8_t {
  NORMAL  = 0,
  SAG     = 1,
  CUTOFF  = 2,
};

struct BatterySagConfig {
  bool   enabled;
  int    sag_threshold_mv;    // Переход в SAG при падении ниже этого напряжения (мВ)
  int    sag_hysteresis_mv;   // Гистерезис при выходе из SAG обратно в NORMAL (мВ)
  int    cutoff_mv;           // Переход в CUTOFF при падении ниже этого напряжения (мВ)
  float  voltage_div;         // Делитель напряжения батареи (напр. 4.5f для 12V→2.67V)
};

struct BatterySagStateData {
  BatterySagState state;
  int battery_mv;              // Текущее напряжение батареи (мВ)
  int battery_mv_raw_adc;     // Сырое значение АЦП (для отладки)
  float voltage_raw;           // Сырое напряжение АЦП (вольты)
  unsigned long state_since_ms; // Когда вошли в текущее состояние
};

// Глобальные переменные
extern BatterySagConfig batterySagConfig;
extern BatterySagStateData batterySagState;

// ================= Функции =================
BatterySagConfig batterySagDefaultConfig();
void batterySagInit();
void updateBatterySag();
float batterySagLimitVoltage(float targetV);
BatterySagState batterySagGetState();
BatterySagStateData batterySagGetStateData();
void batterySagSave();
void batterySagLoad();

#endif // BATTERY_SAG_H
