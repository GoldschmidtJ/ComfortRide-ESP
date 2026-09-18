#ifndef WEB_HANDLERS_SYSTEM_H
#define WEB_HANDLERS_SYSTEM_H

#include <Arduino.h>

// System page handlers
void handleHub();
void handleSystemPage();
void handleSettingsExport();
void handleSettingsImport();
void handleSettingsUpload();
void handleSystemFactoryReset();
void handleUpdatePage();
void handleUpdateUpload();
void handleUpdateResult();

#endif // WEB_HANDLERS_SYSTEM_H
