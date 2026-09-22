#ifndef HTML_PAGES_TOPBAR_H
#define HTML_PAGES_TOPBAR_H

#include <Arduino.h>

// JS топ-бара (CPU/RAM/WiFi, опрос /status/sys). Вставляется в страницы,
// которые собираются динамически из String (events, pins, settings, system).
String getTopBarJs();

#endif // HTML_PAGES_TOPBAR_H