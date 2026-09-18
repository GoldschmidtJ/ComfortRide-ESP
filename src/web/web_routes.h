#ifndef WEB_ROUTES_H
#define WEB_ROUTES_H

#include <WebServer.h>

// ================= Регистрация всех HTTP-маршрутов =================
// Инициализирует все маршруты веб-интерфейса и API.
// Обработчики должны быть объявлены в вызывающем модуле (main.cpp).
void initWebRoutes(WebServer& server);

#endif // WEB_ROUTES_H
