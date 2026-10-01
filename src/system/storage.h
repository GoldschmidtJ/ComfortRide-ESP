#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <Preferences.h>

// Модуль для работы с энергонезависимой памятью (NVS) ESP32
// Содержит функции сохранения/загрузки настроек проекта

// Единственный экземпляр NVS-хранилища, используется всеми Save/Load
extern Preferences prefs;

// Пробег (odometer)
extern float odometerKm;

// ================= Сохранённые сетевые реквизиты (определения в storage.cpp) =================
extern String storedSsid;
extern String storedPass;
extern String storedApSsid;
extern String storedApPass;

// Модуль для работы с энергонезависимой памятью (NVS) ESP32
// Содержит функции сохранения/загрузки всех настроек проекта

// ================= Распиновка GPIO =================
void pinSettingsSave();
void pinSettingsLoad();

// ================= Газ (Throttle) =================
void throttleSettingsSave();
void throttleSettingsLoad();

// ================= PAS =================
void pasSettingsSave();
void pasSettingsLoad();

// ================= Круиз (Cruise) =================
void cruiseSettingsSave();
void cruiseSettingsLoad();

// ================= Система событий (Events) =================
bool eventSettingsSave();
void eventSettingsLoad();
void eventSettingsReset();

// ================= Сервисный режим (serviceThrottleLimitPct) =================
void serviceSettingsSave();
void serviceSettingsLoad();

// ================= Пробег (odometer) =================
void odometerLoad();
void odometerSave();
void odometerReset();

// ================= WiFi (STA режим) =================
void wifiCredsLoad();
void wifiCredsSave(const String &newSsid, const String &newPass);

// ================= WiFi (AP режим) =================
void apSettingsSave();
void apSettingsLoad();

// ================= Общие =================
void loadAllSettings();
void saveAllSettings();

#endif // STORAGE_H
