#include "web/static_resources.h"
#include "web/html_pages.h"
#include "web/web_ui.h"
#include <WebServer.h>
#include <LittleFS.h>

// Резервная версия страницы, зашитая в прошивку. Используется, если LittleFS не
// прошита (или файл потерян) — иначе /. /debug. /emulation отдавали бы 404.
using PageFallback = void (*)(WebServer &);

// Страница «нет файла в LittleFS»: та же тема и та же кнопка возврата, что и на
// остальных страницах (getTopBarCss/getSettingsCss несут эталонный :root).
static void sendMissingFilePage(WebServer &server, const char *path) {
  String html = "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                "<title>Нет файла</title><style>" +
                getTopBarCss() + getSettingsCss() +
                "code{font-family:var(--ui-mono);background:var(--ui-button);padding:2px 6px;"
                "border-radius:6px}</style></head><body>" +
                getBackMenuHtml() + "<h1>Нет файла</h1><p>Файл <b>" + String(path) +
                "</b> не найден в LittleFS. Прошейте файловую систему:"
                " <code>pio run -t uploadfs</code>, затем вернитесь в меню.</p></body></html>";
  server.send(404, "text/html; charset=utf-8", html);
}

static void sendResource(WebServer &server, const char *path, const char *type,
                         PageFallback fallback = nullptr) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    if (fallback) {
      fallback(server);
      return;
    }
    sendMissingFilePage(server, path);
    return;
  }
  server.streamFile(file, type);
  file.close();
}

void initStaticResources(WebServer &server) {
  server.on("/", [&server]() {
    sendResource(server, "/index.html", "text/html; charset=utf-8", sendHubPage);
  });
  server.on("/index.html", [&server]() {
    sendResource(server, "/index.html", "text/html; charset=utf-8", sendHubPage);
  });
  server.on("/debug", [&server]() {
    sendResource(server, "/debug.html", "text/html; charset=utf-8", sendDebugPage);
  });
  server.on("/debug.html", [&server]() {
    sendResource(server, "/debug.html", "text/html; charset=utf-8", sendDebugPage);
  });
  // Для /emulation встроенную копию не держим: она стоит >60 КБ флеша, страница
  // живёт в LittleFS, а при её отсутствии sendResource подскажет, что прошить.
  server.on("/emulation", [&server]() {
    sendResource(server, "/emulation.html", "text/html; charset=utf-8");
  });
  server.on("/emulation.html", [&server]() {
    sendResource(server, "/emulation.html", "text/html; charset=utf-8");
  });
  server.on("/matrix_graphics.js", [&server]() {
    sendResource(server, "/matrix_graphics.js", "application/javascript; charset=utf-8");
  });
}
