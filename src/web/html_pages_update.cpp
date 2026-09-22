#include "web/html_pages_update.h"

#include <WebServer.h>
#include "web/web_ui.h"      // getTopBarCss/getSettingsCss/getTopBarHtml/getBackMenuHtml
#include "web/html_pages_topbar.h" // getTopBarJs
String getUpdatePageHtml() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Обновление прошивки</title>
<style>
)rawliteral";
  html += getTopBarCss();
  html += getSettingsCss();
  html += R"rawliteral(
body{max-width:400px}.update-hint{color:var(--ui-muted);font-size:var(--ui-fs-small)}input[type=file]{padding:8px}</style>
</head><body>
)rawliteral";
  html += getTopBarHtml();
  html += getBackMenuHtml();
  html += R"rawliteral(
<h1>Загрузить прошивку (.bin) или ФС (.fs.bin)</h1>
<p class="update-hint">файл прошивки .bin / файл ФС .fs.bin (образ LittleFS). Прошивка: в Arduino IDE Sketch &rarr; Export Compiled Binary — появится .bin рядом со скетчем. ФС: собери образ (<code>pio run -t buildfs</code>, переименуй в <code>*.fs.bin</code>) — он заменит страницы веб-интерфейса. Выбери файл тут и жми "Залить". Займёт секунд 20-30, плата сама перезагрузится.</p>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="update" accept=".bin,.fs.bin">
<button type="submit">Залить</button>
</form>
)rawliteral";
  html += getTopBarJs();
  html += R"rawliteral(
</body></html>
)rawliteral";
  return html;
}

