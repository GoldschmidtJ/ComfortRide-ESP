#ifndef STORAGE_H
#define STORAGE_H

// Модуль для работы с энергонезависимой памятью (NVS) ESP32
// Содержит функции сохранения/загрузки всех настроек проекта

// ================= Распиновка GPIO =================
void pinSettingsSave();
void pinSettingsLoad();

// ================= Газ (Throttle) =================
void throttleSettingsSave();
void throttleSettingsLoad();

// ================= PAS (Педальный датчик) =================
void pasSettingsSave();
void pasSettingsLoad();

// ================= Круиз-контроль (Cruise) =================
void cruiseSettingsSave();
void cruiseSettingsLoad();

// ================= Система событий (Events) =================
bool eventSettingsSave();
void eventSettingsLoad();
void eventSettingsReset();

// ================= WiFi (STA режим) =================
void wifiCredsLoad();
void wifiCredsSave(const String &newSsid, const String &newPass);

// ================= WiFi (AP режим) =================
void apSettingsSave();
void apSettingsLoad();

#endif // STORAGE_H
