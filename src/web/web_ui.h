#ifndef WEB_UI_H
#define WEB_UI_H

#include <Arduino.h>

// ================= CSS СТИЛИ =================

// Общие стили для топ-бара (системная панель с CPU/RAM/WiFi)
String getTopBarCss();

// Общие стили для страниц настроек
String getSettingsCss();

// JavaScript для динамического рендеринга полей настроек
String getSettingsJs();

// ================= HTML КОМПОНЕНТЫ =================

// Топ-бар с системной информацией (CPU, RAM, WiFi, температура)
String getTopBarHtml();

// Одинокая ссылка возврата в меню: одинаковая метка, класс и отступ на всех
// страницах. Вставляется сразу после getTopBarHtml(), перед <h1>.
String getBackMenuHtml();

#endif // WEB_UI_H
