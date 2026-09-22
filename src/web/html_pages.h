#ifndef HTML_PAGES_H
#define HTML_PAGES_H

// Зонтичный заголовок: страницы HTML разнесены по тематическим файлам
// (рефакторинг C). Включайте конкретный заголовок или этот — как раньше.

#include "web/html_pages_topbar.h"    // getTopBarJs
#include "web/html_pages_update.h"    // getUpdatePageHtml
#include "web/html_pages_debug.h"     // sendDebugPage
#include "web/html_pages_hub.h"       // sendHubPage
#include "web/html_pages_emulation.h" // sendEmulationPage

#endif // HTML_PAGES_H
