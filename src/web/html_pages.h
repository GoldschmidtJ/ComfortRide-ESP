#ifndef HTML_PAGES_H
#define HTML_PAGES_H
#include <Arduino.h>
class WebServer;
String getTopBarJs();
String getUpdatePageHtml();
void sendDebugPage(WebServer &server);
void sendHubPage(WebServer &server);
void sendEmulationPage(WebServer &server);
#endif
