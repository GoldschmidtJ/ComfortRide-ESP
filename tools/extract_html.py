from pathlib import Path
import re
p=Path('/Users/diego/Desktop/bike_controller_light/src')
main=(p/'main.cpp').read_text(); emu=(p/'web_handlers_emulation.cpp').read_text()
def cut(s,a,b):
    i=s.index(a); return s[i:s.index(b,i)]
def raw(s): return re.search(r'R"rawliteral\((.*?)\)rawliteral"',s,re.S).group(1)
def rr(s): return 'R"rawliteral('+s+')rawliteral"'
top=cut(main,'String getTopBarJs() {','// ================= Веб: отладочный график')
debug=cut(main,'void handleDebugPage() {','// ================= Веб: хаб')
hub=cut(main,'void handleHub() {','// Виртуальные кнопки страницы эмуляции')
upd=cut(main,'void handleUpdatePage() {','void handleUpdateUpload()')
epage=cut(emu,'void handleEmulationPage() {','void handleApiButtonPress()')
(p/'html_pages.h').write_text('''#ifndef HTML_PAGES_H\n#define HTML_PAGES_H\n#include <Arduino.h>\nclass WebServer;\nString getTopBarJs();\nString getUpdatePageHtml();\nvoid sendDebugPage(WebServer &server);\nvoid sendHubPage(WebServer &server);\nvoid sendEmulationPage(WebServer &server);\n#endif\n''')
cpp='#include "html_pages.h"\n#include "web_ui.h"\n#include <WebServer.h>\n\nString getTopBarJs() { return String('+rr(raw(top))+'); }\n\nString getUpdatePageHtml() { return String('+rr(raw(upd))+'); }\n\nvoid sendDebugPage(WebServer &server) {\n  static const char page[] PROGMEM = '+rr(raw(debug))+';\n  server.send_P(200, PSTR("text/html; charset=utf-8"), page, sizeof(page)-1);\n}\n\nvoid sendHubPage(WebServer &server) {\n  static const char page[] PROGMEM = '+rr(raw(hub))+';\n  server.send_P(200, PSTR("text/html; charset=utf-8"), page, sizeof(page)-1);\n}\n\nvoid sendEmulationPage(WebServer &server) {\n  static const char page[] PROGMEM = '+rr(raw(epage))+';\n  server.send_P(200, PSTR("text/html; charset=utf-8"), page, sizeof(page)-1);\n}\n'
(p/'html_pages.cpp').write_text(cpp)
a=main.index('String getTopBarJs() {'); b=main.index('// ================= Веб: отладочный график',a); main=main[:a]+main[b:]
for a0,b0,r in [('void handleDebugPage() {','// ================= Веб: хаб','void handleDebugPage() { sendDebugPage(server); }\n\n'),('void handleHub() {','// Виртуальные кнопки страницы эмуляции','void handleHub() { sendHubPage(server); }\n\n'),('void handleUpdatePage() {','void handleUpdateUpload','void handleUpdatePage() { server.send(200, "text/html", getUpdatePageHtml()); }\n\n')]:
 a=main.index(a0); b=main.index(b0,a); main=main[:a]+r+main[b:]
main=main.replace('#include "web_ui.h"       // UI компоненты (CSS, HTML, JS)','#include "web_ui.h"       // UI компоненты (CSS, HTML, JS)\n#include "html_pages.h"')
(p/'main.cpp').write_text(main)
a=emu.index('void handleEmulationPage() {'); b=emu.index('void handleApiButtonPress()',a); emu=emu[:a]+'void handleEmulationPage() { sendEmulationPage(server); }\n\n'+emu[b:]
emu=emu.replace('#include "peripherals.h"','#include "peripherals.h"\n#include "html_pages.h"'); (p/'web_handlers_emulation.cpp').write_text(emu)
