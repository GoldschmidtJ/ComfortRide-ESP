#ifndef HTML_PAGES_DEBUG_H
#define HTML_PAGES_DEBUG_H

class WebServer;

// Страница отладки: осциллограф и сниффер шины (/debug.html)
void sendDebugPage(WebServer &server);

#endif // HTML_PAGES_DEBUG_H