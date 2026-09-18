#ifndef WEB_HANDLERS_PINS_H
#define WEB_HANDLERS_PINS_H

#include <Arduino.h>

// Forward declarations для структур из main.cpp
struct CustomPinRole;

// Main pin configuration handlers
void handlePinsPage();
void handlePinsSave();
void handlePinRowSave();
void handleCustomPinSave();
void handleCustomPinDelete();
void handlePinsReset();

// Helper functions exported for use in other modules
String gpioCapabilityText(int g);
String pinOptionsHtml(int selected, bool output, bool needAdc, bool needDac, bool needPullup);
String customPinRowHtml(int i, const CustomPinRole &c);

#endif // WEB_HANDLERS_PINS_H
