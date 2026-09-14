#include "web/static_resources.h"
#include <WebServer.h>
#include <LittleFS.h>

static void sendResource(WebServer &server, const char *path, const char *type) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    server.send(404, "text/plain; charset=utf-8", "Ресурс не найден");
    return;
  }
  server.streamFile(file, type);
  file.close();
}

void initStaticResources(WebServer &server) {
  server.on("/", [&server]() {
    sendResource(server, "/index.html", "text/html; charset=utf-8");
  });
  server.on("/index.html", [&server]() {
    sendResource(server, "/index.html", "text/html; charset=utf-8");
  });
  server.on("/debug", [&server]() {
    sendResource(server, "/debug.html", "text/html; charset=utf-8");
  });
  server.on("/debug.html", [&server]() {
    sendResource(server, "/debug.html", "text/html; charset=utf-8");
  });
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
