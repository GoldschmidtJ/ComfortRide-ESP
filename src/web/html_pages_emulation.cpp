#include "web/html_pages_emulation.h"

#include <WebServer.h>

// Сборка страницы эмулятора из частей (этап D: разбиение монолита 1607 строк).
// Контент отдаётся кусками sendContent_P из PROGMEM; Content-Length точный,
// суммарный поток байт байт-в-байт совпадает с прежним единым raw-литералом.
void sendEmulationPage(WebServer &server) {
  const size_t total = getEmulationHeadLen() + getEmulationStatusLen() +
      getEmulationStateLen() + getEmulationMatrixCoreLen() + getEmulationMatrixBootLen() +
      getEmulationMatrixDisplayLen() + getEmulationAppLen();
  server.setContentLength(total);
  server.send_P(200, PSTR("text/html; charset=utf-8"), getEmulationHead(), getEmulationHeadLen());
  server.sendContent_P(getEmulationStatus(), getEmulationStatusLen());
  server.sendContent_P(getEmulationState(), getEmulationStateLen());
  server.sendContent_P(getEmulationMatrixCore(), getEmulationMatrixCoreLen());
  server.sendContent_P(getEmulationMatrixBoot(), getEmulationMatrixBootLen());
  server.sendContent_P(getEmulationMatrixDisplay(), getEmulationMatrixDisplayLen());
  server.sendContent_P(getEmulationApp(), getEmulationAppLen());
}
