from pathlib import Path
import re

root = Path('/Users/diego/Desktop/bike_controller_light')
src = root / 'src'
data = root / 'data'
data.mkdir(exist_ok=True)

# 1) Fix compile errors left from the directory refactor
hc = src / 'system' / 'hardware_config.cpp'
s = hc.read_text()
s = s.replace('const int PIN_ROLE_COUNT = 15;\n\n', '')
hc.write_text(s)

pins = src / 'web' / 'web_handlers_pins.cpp'
s = pins.read_text()
s = s.replace('enum CustomPinMode : uint8_t { CUSTOM_PIN_INPUT=0, CUSTOM_PIN_INPUT_PULLUP=1, CUSTOM_PIN_OUTPUT=2 };\n\n', '')
s = s.replace('extern PinConfig PIN_CONFIG_DEFAULTS;', 'extern const PinConfig PIN_CONFIG_DEFAULTS;')
pins.write_text(s)

st = src / 'system' / 'storage.cpp'
s = st.read_text()
if '#include "system/inputs.h"' not in s:
    s = s.replace('#include "core/throttle.h"', '#include "core/throttle.h"\n#include "system/inputs.h"')
st.write_text(s)

# 2) LittleFS init in setup()
main = src / 'main.cpp'
s = main.read_text()
if '#include <LittleFS.h>' not in s:
    s = s.replace('#include <Update.h>', '#include <Update.h>\n#include <LittleFS.h>')
    s = s.replace('  // Инициализация NVS для работы с настройками',
                  '  if (!LittleFS.begin(true)) {\n    Serial.println(F("LittleFS: ошибка монтирования"));\n  }\n\n  // Инициализация NVS для работы с настройками')
main.write_text(s)

pio = root / 'platformio.ini'
s = pio.read_text()
if 'board_build.filesystem' not in s:
    s = s.replace('monitor_speed = 115200', 'monitor_speed = 115200\nboard_build.filesystem = littlefs')
pio.write_text(s)

# 3) Extract matrix graphics (font, arrows, icons) into data/matrix_graphics.js
hp = src / 'web' / 'html_pages.cpp'
s = hp.read_text()
a = s.index('const DIGIT_GLYPHS = {')
b = s.index('const SETTINGS_MENU', a)
graphics = s[a:b]
(data / 'matrix_graphics.js').write_text(
    '// Матрица: шрифты, стрелки и иконки эмулятора (вынесено из html_pages.cpp).\n'
    '// Синхронизировано с src/light_logic.h (lightIconRow / turnArrowRow / hornIconRow).\n'
    + graphics)
# The block sits inside an inline <script>; close it, load the external file, reopen.
s = s[:a] + '</script>\n<script src="/matrix_graphics.js"></script>\n<script>\n' + s[b:]

# 4) Extract PROGMEM pages into data/*.html
pages = [
    ('debug.html', 'void sendDebugPage(WebServer &server) {', 'void sendHubPage(WebServer &server) {'),
    ('index.html', 'void sendHubPage(WebServer &server) {', 'void sendEmulationPage(WebServer &server) {'),
    ('emulation.html', 'void sendEmulationPage(WebServer &server) {', None),
]
for name, marker, nxt in pages:
    a = s.index(marker)
    b = s.index(nxt, a) if nxt else len(s)
    chunk = s[a:b]
    m = re.search(r'R"rawliteral\((.*?)\)rawliteral"', chunk, re.S)
    if not m:
        raise SystemExit(name + ': raw HTML not found')
    (data / name).write_text(m.group(1))
hp.write_text(s)
print('ok: resources extracted')