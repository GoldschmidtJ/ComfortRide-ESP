#ifndef WEB_HANDLERS_SETTINGS_H
#define WEB_HANDLERS_SETTINGS_H

#include <Arduino.h>

void handleThrottlePage();
void handleThrottleCalMin();
void handleThrottleCalMax();
void handleThrottleSave();

void handlePasPage();
void handlePasSave();
void handlePasCalStart();
void handlePasCalStatus();
void handlePasCalStop();

void handleCruisePage();
void handleCruiseSave();

void handleWifiPage();
void handleWifiScan();
void handleWifiSave();
void handleApSave();

// Helper для валидации AP credentials
bool validApCredentials(const String &ssidValue, const String &passValue, String &error);

#endif // WEB_HANDLERS_SETTINGS_H
