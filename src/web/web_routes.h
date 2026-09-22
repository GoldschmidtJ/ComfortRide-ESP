#ifndef WEB_ROUTES_H
#define WEB_ROUTES_H

#include <WebServer.h>

// Единственный экземпляр веб-сервера (определён в web_routes.cpp).
// Все HTTP-обработчики и main.cpp используют его через этот extern.
extern WebServer server;

// ================= Регистрация всех HTTP-маршрутов =================
// Инициализирует все маршруты веб-интерфейса и API.
// Обработчики должны быть объявлены в вызывающем модуле (main.cpp).
void initWebRoutes(WebServer& server);

#endif // WEB_ROUTES_H
