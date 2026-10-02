import re
from pathlib import Path

root = Path("/Users/diego/Desktop/bike_controller_light")
out_dir = root / "ui_preview"
out_dir.mkdir(exist_ok=True)

# Читаем CSS и компоненты из web_ui.cpp
web_ui = (root / "src/web/web_ui.cpp").read_text(encoding="utf-8")
topbar_css = re.search(r'getTopBarCss\(\)\s*\{\s*return String\(R\"rawliteral\((.*?)\)rawliteral\"\);', web_ui, re.DOTALL).group(1)
settings_css = re.search(r'getSettingsCss\(\)\s*\{\s*return String\(R\"rawliteral\((.*?)\)rawliteral\"\);', web_ui, re.DOTALL).group(1)
topbar_html = re.search(r'getTopBarHtml\(\)\s*\{\s*return String\(R\"rawliteral\((.*?)\)rawliteral\"\);', web_ui, re.DOTALL).group(1)
settings_js = re.search(r'getSettingsJs\(\)\s*\{\s*return String\(R\"rawliteral\((.*?)\)rawliteral\"\);', web_ui, re.DOTALL).group(1)
i18n_js = re.search(r'getI18nJs\(\)\s*\{\s*return String\(R\"rawliteral\((.*?)\)rawliteral\"\);', web_ui, re.DOTALL).group(1)

topbar_mock_js = """<script>
const mockD = { cpu: 9, ram_pct: 37, ram_free_kb: 205, rom_sketch_kb: 1083, rom_total_kb: 1310, wifi_mode: "STA", wifi_ssid: "Donut", temp: 42 };
function updateTopBar(){
  const set=(id,val)=>{const el=document.getElementById(id);if(el)el.textContent=val};
  set('tbCpu',(mockD.cpu + Math.floor(Math.random()*4))+'%');
  set('tbRam',mockD.ram_pct+'%');
  set('tbRamKb','('+mockD.ram_free_kb+'k)');
  set('tbRom',mockD.rom_sketch_kb+'k/'+mockD.rom_total_kb+'k');
  set('tbTemp',(mockD.temp + Math.floor(Math.random()*2))+'°C');
  const dot=document.getElementById('tbWifiDot'),txt=document.getElementById('tbWifiTxt');
  if(dot&&txt){dot.className='tb-dot dot-green';txt.textContent=mockD.wifi_ssid;}
}
setInterval(updateTopBar, 2000); updateTopBar();
</script>"""

def wrap_static(title, body):
    body = body.replace('href="/settings/drive"', 'href="drive.html"')
    body = body.replace('href="/settings/pins"', 'href="pins.html"')
    body = body.replace('href="/settings/events"', 'href="events.html"')
    body = body.replace('href="/wifi"', 'href="wifi.html"')
    body = body.replace('href="/debug"', 'href="debug.html"')
    body = body.replace('href="/system"', 'href="system.html"')
    body = body.replace('href="/emulation"', 'href="emulation.html"')
    body = body.replace('href="/"', 'href="index.html"')
    return f"""<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title><style>{topbar_css}{settings_css}</style></head>
<body>{topbar_html}<p class="back-row"><a class="back-link" href="index.html" data-i18n="backMenu">&larr; Меню</a></p>
{body}
{i18n_js}
{topbar_mock_js}
</body></html>"""
# 1. index.html
hub_raw = (root / "data/index.html").read_text(encoding="utf-8")
hub_raw = hub_raw.replace('href="/settings/drive"', 'href="drive.html"')
hub_raw = hub_raw.replace('href="/settings/pins"', 'href="pins.html"')
hub_raw = hub_raw.replace('href="/settings/events"', 'href="events.html"')
hub_raw = hub_raw.replace('href="/wifi"', 'href="wifi.html"')
hub_raw = hub_raw.replace('href="/debug"', 'href="debug.html"')
hub_raw = hub_raw.replace('href="/system"', 'href="system.html"')
hub_raw = hub_raw.replace('href="/emulation"', 'href="emulation.html"')
hub_raw = hub_raw.replace('href="/"', 'href="index.html"')
hub_raw = re.sub(r'<script>.*?</script>', i18n_js + topbar_mock_js, hub_raw, flags=re.DOTALL)
(out_dir / "index.html").write_text(hub_raw, encoding="utf-8")

# 2. emulation.html
emu_raw = (root / "data/emulation.html").read_text(encoding="utf-8")
matrix_js = (root / "data/matrix_graphics.js").read_text(encoding="utf-8")
emu_raw = emu_raw.replace('<script src="/matrix_graphics.js"></script>', f'<script>\n{matrix_js}\n</script>')
emu_raw = emu_raw.replace('href="/"', 'href="index.html"')
emu_raw = emu_raw.replace('</body>', i18n_js + '</body>')
(out_dir / "emulation.html").write_text(emu_raw, encoding="utf-8")

# 3. debug.html
dbg_raw = (root / "data/debug.html").read_text(encoding="utf-8")
dbg_raw = dbg_raw.replace('href="/"', 'href="index.html"')
dbg_raw = dbg_raw.replace('</body>', i18n_js + '</body>')
(out_dir / "debug.html").write_text(dbg_raw, encoding="utf-8")

# 4. drive.html — rebuilt from CPP with correct raws[] mapping (57 elements)
drive_cpp = (root / "src/web/web_handlers_settings_drive.cpp").read_text(encoding="utf-8")
raws = re.findall(r'R\"rawliteral\((.*?)\)rawliteral\"', drive_cpp, re.DOTALL)
assert len(raws) == 57, f"Expected 57 raws, got {len(raws)}"
drive_html = (
    raws[0] + topbar_css + settings_css +
    raws[1] + topbar_html +
    '<p class="back-row"><a class="back-link" href="index.html" data-i18n="backMenu">&larr; Меню</a></p>' +
    raws[2] + "1.10" + raws[3] + "4.10" + raws[4] + "1.15" + raws[5] + "4.10" +
    raws[6] + "0.667" + raws[7] + "1.33" + raws[8] + "checked" + raws[9] + "500" +
    raws[10] + raws[11] + raws[12] +
    raws[13] + raws[14] + raws[15] + raws[16] + raws[17] + raws[18] +
    raws[19] + raws[20] + raws[21] + raws[22] + raws[23] + raws[24] + raws[25] + "20" + raws[26] + "5" + raws[27] +
    raws[28] + raws[29] + raws[30] + raws[31] + raws[32] + "500" + raws[33] + raws[34] +
    raws[35] + "4" + raws[36] + "25" + raws[37] + "100" + raws[38] + raws[39] + raws[40] + raws[41] +
    raws[42] + "500" + raws[43] + raws[44] + raws[45] + raws[46] + raws[47] + raws[48] + raws[49] +
    raws[50] + raws[51] + raws[52] + settings_js + raws[53] + "20,40,60,80,100" + raws[54] + "25,50,75,100" + raws[55] + topbar_mock_js + raws[56]
)
(out_dir / "drive.html").write_text(drive_html, encoding="utf-8")

# 5. pins.html
pins_body = """<h1>Распиновка GPIO</h1>
<p class="hint">Назначение ролей выводам ESP32 с проверкой конфликтов.</p>
<fieldset><legend>Системные функции</legend>
<div class="frow"><label>Вход газа (АЦП1)</label><select><option selected>GPIO34 (только вход)</option></select></div>
<div class="frow"><label>Выход газа (ЦАП)</label><select><option selected>GPIO25 (ЦАП1)</option></select></div>
<div class="frow"><label>Вход тормоза</label><select><option selected>GPIO27 (подтяжка)</option></select></div>
<div class="frow"><label>Датчик PAS</label><select><option selected>GPIO14 (подтяжка)</option></select></div>
<div class="frow"><label>Кнопка PAS</label><select><option selected>GPIO13 (подтяжка)</option></select></div>
<div class="frow"><label>Фара (ШИМ LEDC)</label><select><option selected>GPIO18 (выход ШИМ)</option></select></div>
<div class="frow"><label>ДХО (ШИМ LEDC)</label><select><option selected>GPIO19 (выход ШИМ)</option></select></div>
<div class="frow"><label>Поворотник левый</label><select><option selected>GPIO21 (выход)</option></select></div>
<div class="frow"><label>Поворотник правый</label><select><option selected>GPIO22 (выход)</option></select></div>
<div class="frow"><label>Гудок</label><select><option selected>GPIO23 (выход)</option></select></div>
<div class="frow"><label>Зуммер</label><select><option selected>GPIO4 (выход)</option></select></div>
<div class="frow"><label>АЦП батареи</label><select><option selected>GPIO35 (АЦП1, делитель)</option></select></div>
</fieldset>
<div style="background:var(--ui-card);border:1px solid var(--ui-border);border-radius:8px;padding:12px;margin-top:14px">
  <div style="color:var(--ui-success);font-weight:600;font-size:13px">✓ Конфликтов не обнаружено</div>
  <div class="u-muted u-small" style="margin-top:4px">Свободные пины: GPIO32, GPIO33, GPIO39</div>
</div>
<button type="button" onclick="alert('Сохранено в NVS')">Сохранить распиновку</button>"""
(out_dir / "pins.html").write_text(wrap_static("Распиновка GPIO", pins_body), encoding="utf-8")

# 6. events.html — 3 правила: системное (Anti-Police), левый поворотник, правый поворотник
events_body = """<style>
.rule{background:var(--ui-card);border:1px solid var(--ui-border);padding:9px;border-radius:8px;margin:7px 0}
.rule-head{display:flex;align-items:center;gap:8px;min-height:26px;cursor:pointer}
.rule-head strong{flex:1}
.badge{font-size:11px;color:var(--ui-muted);background:var(--ui-button);padding:3px 6px;border-radius:4px}
.sys-badge{font-size:10px;color:var(--ui-accent);background:rgba(255,193,7,.15);border:1px solid var(--ui-accent);padding:2px 5px;border-radius:3px;font-weight:600}
.rule-body{display:none;padding-top:8px}
.rule.open .rule-body{display:block}
</style>
<h1>Конструктор событий</h1>
<p class="hint">Правила автоматизации «Триггер &rarr; Условие &rarr; До 3 действий». Без перезагрузки.</p>
<div class="rule open">
  <div class="rule-head" onclick="this.parentElement.classList.toggle('open')">
    <span class="badge sys-badge">СИСТЕМА</span><strong>1. Сервисный режим (Anti-Police)</strong><span>▼</span>
  </div>
  <div class="rule-body">
    <div class="fhint">5 быстрых нажатий на тормоз за 700 мс активируют ограничение газа до 30%.</div>
    <div class="frow"><label>Триггер</label><select><option selected>Тормоз: нажатие</option></select></div>
    <div class="frow"><label>Условие</label><select><option selected>Серия нажатий (5 раз за 700 мс)</option></select></div>
    <div class="frow"><label>Действие 1</label><select><option selected>Сервисный режим: Переключить</option></select></div>
    <div class="frow"><label>Действие 2</label><select><option selected>Дисплей: Показать значок тормоза</option></select></div>
  </div>
</div>
<div class="rule">
  <div class="rule-head" onclick="this.parentElement.classList.toggle('open')">
    <span class="badge">ВКЛ</span><strong>2. Левый поворотник</strong><span>▼</span>
  </div>
  <div class="rule-body">
    <div class="frow"><label>Триггер</label><select><option selected>Кнопка: левый поворотник</option></select></div>
    <div class="frow"><label>Действие</label><select><option selected>Левый поворотник: Переключить</option></select></div>
  </div>
</div>
<fieldset><legend>Журнал последних срабатываний</legend>
  <div style="font-size:12px;font-family:var(--ui-mono);color:var(--ui-muted);line-height:1.7">
    <div>[14:20:05] Правило 3: Кнопка фары &rarr; Свет: ДХО</div>
    <div>[14:18:42] Правило 2: Поворотник влево &rarr; Мигание + Стрелка</div>
    <div>[14:15:30] Правило 1: Сервисный режим &rarr; Газ ограничен 30%</div>
  </div>
</fieldset>
<button type="button" onclick="alert('Сохранено в NVS')">Сохранить правила</button>"""
(out_dir / "events.html").write_text(wrap_static("Конструктор событий", events_body), encoding="utf-8")

# 7. wifi.html
wifi_body = """<h1>Настройки Wi-Fi</h1>
<p class="hint">Подключение к домашней сети или смартфону, либо собственная точка доступа.</p>
<fieldset><legend>Подключение к роутеру / хотспоту</legend>
<div class="frow"><label>SSID сети</label><input type="text" value="Donut"></div>
<div class="frow"><label>Пароль</label><input type="password" value="doughnut"></div>
<div style="margin-top:10px">
  <button type="button" class="secondary" onclick="alert('Сканирование... Найдено 4 сети: Donut (-54dB), HomeNet (-72dB)')">Сканировать сети</button>
</div>
</fieldset>
<fieldset><legend>Точка доступа контроллера (AP)</legend>
<div class="frow"><label>Имя AP (SSID)</label><input type="text" value="BikeControllerAP"></div>
<div class="frow"><label>Пароль (пусто = открытая)</label><input type="text" placeholder="Без пароля"></div>
<div class="frow"><label>IP-адрес контроллера</label><input type="text" value="192.168.4.1" readonly></div>
</fieldset>
<button type="button" onclick="alert('Настройки Wi-Fi сохранены')">Сохранить и подключиться</button>"""
(out_dir / "wifi.html").write_text(wrap_static("Настройки Wi-Fi", wifi_body), encoding="utf-8")

# 8. system.html
sys_body = """<h1>Система</h1>
<fieldset><legend>Файл настроек (JSON)</legend>
<p class="fhint">Экспорт сохраняет все параметры одним файлом: газ, PAS, круиз, распиновку, Wi-Fi и события.</p>
<div style="margin-top:10px">
  <button type="button" class="secondary" onclick="alert('Экспорт JSON настроек...')">⇩ Скачать бэкап настроек (.json)</button>
</div>
<div style="margin-top:12px">
  <label>Загрузить файл настроек</label>
  <input type="file" style="margin-top:4px" onchange="alert('Файл загружен и проверен!')">
</div>
</fieldset>
<fieldset><legend>Обновление прошивки (OTA)</legend>
<p class="fhint">Загрузка бинарника .bin по воздуху без кабеля.</p>
<button type="button" class="secondary" onclick="alert('Страница OTA /update')">Перейти к обновлению прошивки &rarr;</button>
</fieldset>
<fieldset><legend>Сброс к заводским установкам</legend>
<p class="fhint">Очистка NVS flash-памяти: сброс параметров газа, PAS, круиза, сети и правил.</p>
<button type="button" class="danger" onclick="if(confirm('Сбросить все настройки к заводским?')) alert('Заводской сброс выполнен. Плата перезагружена.')">Заводской сброс</button>
</fieldset>"""
(out_dir / "system.html").write_text(wrap_static("Система", sys_body), encoding="utf-8")

print(f"Готово! Все файлы сохранены в папку: {out_dir}")
for f in sorted(out_dir.glob("*.html")):
    print(f" - {f.name} ({f.stat().st_size} байт)")

