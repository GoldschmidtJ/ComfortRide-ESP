#ifndef HTML_PAGES_EMULATION_H
#define HTML_PAGES_EMULATION_H

#include <Arduino.h>

class WebServer;

// Страница эмуляции (джойстик, LED, опрос состояния)
void sendEmulationPage(WebServer &server);

// Части страницы (PROGMEM), этап D: head/status/state/matrix/app.
// Каждая часть лежит в отдельном .cpp как static const char part[] PROGMEM;
// доступ только через accessor'ы (без extern).
PGM_P getEmulationHead();
size_t getEmulationHeadLen();
PGM_P getEmulationStatus();
size_t getEmulationStatusLen();
PGM_P getEmulationState();
size_t getEmulationStateLen();
PGM_P getEmulationMatrixCore();
size_t getEmulationMatrixCoreLen();
PGM_P getEmulationMatrixBoot();
size_t getEmulationMatrixBootLen();
PGM_P getEmulationMatrixDisplay();
size_t getEmulationMatrixDisplayLen();
PGM_P getEmulationApp();
size_t getEmulationAppLen();

#endif // HTML_PAGES_EMULATION_H
