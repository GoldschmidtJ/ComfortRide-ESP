# ComfortRide-ESP 🚴⚡

**ComfortRide-ESP** — умный блок комфорта и управления для электровелосипеда на базе микроконтроллера **ESP32** (для связки с китайским мотор-колесом и контроллерами в KT-стиле). 

Проект превращает базовый электровелосипед в умный транспорт с мягким управлением газом, умным ассистентом педалирования (PAS), круиз-контролем, управлением светом/звуком, экранным интерфейсом и защитными функциями.

---

## ✨ Ключевые возможности проекта

- **Модульная прошивка ESP32 (v0.4.1)** — код разделён на слои: `src/core` (чистая логика throttle/PAS/cruise/света, покрывается юнит-тестами), `src/system` (network, storage/NVS, events_engine, joystick, inputs, peripherals, hardware_config), `src/web` (маршруты и HTTP-обработчики по доменам), `src/utils` (иконки, хелперы). Точка входа — `src/main.cpp`.
- **Throttle-by-Wire:** Мягкий старт/стоп (линейное сглаживание — рампа с гарантированным достижением цели за заданное время, отдельные таймауты старта/стопа), точная калибровка в вольтах, настраиваемая мертвая зона, удержание минимального выходного напряжения (`throttleOutMinV`) для мгновенного отклика мотора, живой захват мин/макс напряжений ручки газа из веб-интерфейса. Аппаратные коэффициенты согласования (делитель входа `divRatio`, усиление ОУ `gain` 1.33) редактируются и сохраняются в NVS.
- **Собственная реализация PAS & Cruise:** Адаптивная помощь педалирования (до 20 уровней, настраиваемый угол подхвата, 3 режима сглаживания: «по умолчанию» / «свои» / «выкл»), отдельный настраиваемый таймаут быстрой остановки PAS (`pasStopTimeoutMs`), интерактивный помощник калибровки магнитов PAS, круиз-контроль до 100 уровней с автораспределением процентов и конечным автоматом безопасных переходов.
- **Свет и звук (12V силовые ключи MOSFET):** Фара с режимом ДХО (ШИМ), поворотники с миганием и звуковым щелканьем зуммера, мощный звуковой сигнал (гудок).
- **Интерактивное управление:** Меню на джойстике и LED-экран 16×32 (два модуля 8×32 друг над другом) для отображения статуса, режимов, уровня PAS/круиза, шкал газа, поворотников, света и напряжения прямо на руле. Эмулятор экрана — на странице `/emulation`.
- **Секретные функции безопасности (Anti-Police / Стелс-режимы):** Возможность скрытого переключения профилей мощности и ограничения скорости (например, по 5 быстрым нажатиям на ручку тормоза).
- **Типизированный конструктор событий:** `/settings/events` хранит до 8 безопасных правил «триггер → условие → до 3 результатов» (например, поворотник включает индикацию и звуковое сопровождение одним событием), применяет их без перезагрузки и ведёт последние 10 срабатываний в RAM. Заводское правило: 5 быстрых нажатий тормоза (интервал до 700 мс) переключают сервисный режим (газ до 30%, PAS не выше уровня 1, круиз выключен; вход подтверждается 3 писками, выход — 2).
- **WiFi & Web-интерфейс:**
  - Единая, унифицированная темная тема (CSS-токены) для всех страниц.
  - Панель управления и живой калибровки всех параметров через браузер со смартфона.
  - `/debug` — Осциллограф/график телеметрии в реальном времени. Включает секционный профилировщик загрузки CPU (можно отключить для снижения нагрузки).
  - OTA-обновление прошивки по воздуху без кабеля (`/update`).

---

## 📦 Актуальная прошивка

- **`src/`** — модульная прошивка PlatformIO, версия **v0.4.1**: `core/` (throttle, pas, cruise, light_logic, lights), `system/` (network, storage, events_engine, joystick, inputs, peripherals, hardware_config), `web/` (routes + обработчики settings/pins/events/system/telemetry/emulation), `utils/`. Точка входа — `src/main.cpp`.

### История изменений (Changelog)

#### v0.4.1
- **TopBar language switcher:** кнопку «RU» заменили на dropdown «Русский / English»; выбор сохраняется в `localStorage` (`bike_ui_lang`). Все страницы, включая `data/index.html` и `data/emulation.html`, получили `<select id="tbLangSel">` с i18n-словарём и `data-i18n` атрибутами.
- **i18n coverage на статических HTML:** `data/index.html` и `data/emulation.html` теперь содержат полный I18N-словарь и все `data-i18n` атрибуты (hub, emulation, drive, pins, events, network, debug, system, backMenu, joystick, brake, pedals, turnL, turnR, light, throttle), как и страницы из прошивки.
- **Экспорт тестового HTML (`tools/export_ui_preview.py`):** скрипт теперь извлекает `getI18nJs()` из `web_ui.cpp` и внедряет его в `ui_preview/*.html`, обеспечивая полный перевод UI на страницах-preview.
- **Версия прошивки:** `FIRMWARE_VERSION` → `0.4.1`.

#### v0.3.1
- **Веб-интерфейс:** Эмуляция вынесена с главной страницы в отдельный раздел `/emulation`; главная страница стала компактным меню.
- **Дисплей (LED Matrix):** Обновлен дизайн символов (P/C, напряжение "V"), добавлены новые иконки фар (БС, ДХО). Анимация педалей и иконка тормоза сдвинуты для лучшего визуального баланса.
- **Интерфейс Джойстика:** При уровне 0 добавлена индикация "пустой" нижней стрелки.
- **Исправления:** Устранено состояние гонки при нажатии кнопки "OK" в эмуляторе пульта (уровни больше не "сбрасываются" случайным образом при медленном отклике сети).

#### v0.3.0 и ранее
- См. историю коммитов репозитория (`git log`).

---

## 🚀 Начало работы и прошивка (Getting Started)

Проект собирается через **PlatformIO** — отдельная установка Arduino IDE не нужна.

### 1. Установка PlatformIO

- **VS Code:** установи расширение [PlatformIO IDE](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide) — оно само поставит PlatformIO Core.
- **Только CLI (без VS Code):**
  ```bash
  pip install platformio        # или: brew install platformio
  ```

### 2. Сборка и прошивка

Из корня проекта:

```bash
pio run                # собрать прошивку (env: esp32dev)
pio run -t upload      # прошить плату по USB
pio run -t uploadfs    # прошить образ файловой системы (data/ → LittleFS)
pio device monitor     # монитор порта, 115200 бод
pio test -e native     # юнит-тесты чистой логики на хосте (16 тестов)
```

Первый запуск скачает toolchain ESP32 и библиотеки из `lib_deps` — это занимает несколько минут, дальше сборка занимает ~15 секунд.

### 3. Первый запуск платы

1. Прошей плату (`pio run -t upload`).
2. ESP32 поднимет точку доступа **BikeControllerAP** — подключись к ней со смартфона.
3. Открой веб-интерфейс: `http://192.168.4.1` (или `http://bike-controller.local`).
4. В разделе «Связь» задай SSID/пароль домашней Wi-Fi-сети, в разделе «Распиновка (GPIO)» — проверь/переназначь пины под свою разводку.
5. Обновление по воздуху — страница `/update` (файлы `firmware.bin` и, при необходимости, `littlefs.bin` из `.pio/build/esp32dev/`).

### 4. CI

При пуше в репозиторий GitHub Actions автоматически проверяет сборку (`pio run -v` с логом в `build.log` и grep-проверкой warning'ов компиляции) и запускает юнит-тесты (`pio test -e native`) — см. `.github/workflows/ci.yml`. Логика света/поворотников дополнительно покрыта юнит-тестами (`pio test -e native`).

---


## 📐 Схемы и подключение (Hardware Schematics)

- **[docs/full_schematic_mcp6002.svg](./docs/full_schematic_mcp6002.svg)** — Актуальная схема согласования газа (вход/выход на ОУ MCP6002 Rail-to-Rail).
- **[docs/esp32_board_wiring.svg](./docs/esp32_board_wiring.svg)** — Полная карта назначения пинов ESP32.
- **[docs/light_sound_wiring.svg](./docs/light_sound_wiring.svg)** — Силовая часть света, поворотников и звука на MOSFET AOD418.
- **[docs/simple_wire_routing.svg](./docs/simple_wire_routing.svg)** — Быстрая схема для монтажа "куда какой провод".
- **[docs/hall_speed_sensor.svg](./docs/hall_speed_sensor.svg)** — План подключения одного Холла мотора для скорости и пробега (через делитель).
- **[docs/display_bus_sniffer.svg](./docs/display_bus_sniffer.svg)** — План пассивного сниффера шины дисплея контроллера (RX-only).

**Справочная разводка перфборда и джойстик** (текстовая схема, генерируется скриптом):

- **[layout_perfboard_corrected.py](./layout_perfboard_corrected.py)** — Полная справочная разводка перфборда 60×80 мм (ESP32 DevKit + MCP6002): питание, буфер/усилитель газа, PAS, тормоз, дисплей MAX7219 и **джойстик** (БЛОК 6): VRx → GPIO35, VRy → GPIO36, SW → GPIO15 (внутренний pull-up), питание осей строго от 3.3V. Назначения пинов соответствуют прошивке 1:1 (`src/system/joystick.cpp`, дефолтная распиновка редактируется на `/settings/pins`).
- **[perfboard_layout.kicad_pcb](./perfboard_layout.kicad_pcb)** — Макет той же перфборд-платы в KiCad (открывается в KiCad; сопутствующие `*.kicad_prl` / `*.kicad_pro` — служебные файлы проекта).

Схемы лежат в каталоге `docs/`, генераторы схем/превью и макет перфборда — в корне (`layout_perfboard*.py`, `perfboard_layout.kicad_pcb`, `gen.py`, `mkpreview.py`, `extract_html.py`).

---

## 🌐 Веб-интерфейс и эмулятор

В прошивку встроен веб-сервер с возможностями:
- `/` — Главное меню настроек.
- `/emulation` — Отдельная страница эмуляции LED-матрицы, джойстика, газа, тормоза и педалей.
- `/settings/pins` — Конструктор распиновки: переназначение GPIO с проверкой конфликтов, картой свободных пинов и сохранением в NVS (применяется после перезагрузки).
- `/settings/events` — Конструктор событий: 8 слотов правил (до 3 результатов в каждом), заводское правило 5 быстрых нажатий тормоза, безопасные действия света/звука/PAS и сервисный ограничитель газа. Изменения применяются сразу; раздел `events` входит в экспорт/импорт настроек.
- `/debug` — Осциллограф/график телеметрии и пассивный сниффер неизвестной цифровой линии на GPIO36: кольцевой буфер фронтов, старт/стоп/очистка, график и экспорт CSV. GPIO36 используется только как вход без подтяжки; требуется внешний согласователь уровня до 3,3 В.
- `/update` — Обновление по воздуху: прошивка (`.bin`) и образ файловой системы (`.fs.bin`, с валидацией littlefs).
- `/system` — Экспорт/импорт всех настроек в JSON (включая пины и события), сброс к заводским, статус системы (`/status/sys`).

---

## 🐞 Отладка (Debug)

В прошивку встроен полноценный отладочный раздел — полезен при отладке шины дисплея/пульта и контура управления:

- **`/debug`** — страница осциллографа (`data/debug.html`): живой график телеметрии (вход/выход газа, PAS, тормоз) и пассивный сниффер цифровой линии.
- **Захват шины (bus capture):** RX-only пассивный захват фронтов на GPIO36 (кольцевой буфер на 4096 фронтов, ISR). Управление через API:
  - `POST /debug/bus/control` — старт/стоп/очистка захвата;
  - `GET /debug/bus/status` — состояние захвата (идёт ли запись, счётчики фронтов, переполнения);
  - `GET /debug/bus/data` — снимок буфера в JSON;
  - `GET /debug/bus/csv` — экспорт захвата в CSV для внешнего анализа.
- **`/debug/data`** — телеметрия для графика в JSON.
- GPIO36 используется только как вход без подтяжки; при подаче внешнего сигнала требуется согласователь уровня до 3,3 В. Если GPIO36 занят системной ролью в конструкторе распиновки, старт захвата возвращит ошибку 409 с пояснением.

---

## 📜 История ключевых инженерных решений

**Платформа.** Начинали с Arduino Nano, перешли на ESP32 (не хватало прерываний, ШИМ-каналов, нужен WiFi и встроенный ЦАП). Финальная плата — ESP32 DevKit.

**Согласование газа (Throttle-by-Wire):**
- Прямое подключение невозможно: ручка и контроллер работают до ~4.2В, а логика ESP32 — 3.3В.
- **Итоговое решение:** сдвоенный Rail-to-Rail операционный усилитель **MCP6002** с питанием от 5В:
  - Канал А: буферизует вход ручки газа через делитель (R1=10k, R2=20k).
  - Канал Б: усиливает сигнал с встроенного ЦАП ESP32 (R3=10k, R4=3.3k, K ≈ 1.33). Сигнал получается низкоомным и помехоустойчивым.

**Тормоз и PAS:**
- Тормоз читается параллельно сухому контакту.
- Датчик PAS питается от 5В и подключается к пину с внутренней подтяжкой ESP32 (для датчиков с открытым коллектором).

---

## ⚠️ Важные заметки по безопасности

- **Тормоз:** Линия концевика тормоза обязательно должна идти параллельно напрямую в заводской мотор-контроллер! Считывание на ESP32 выполняет сервисную и дублирующую функцию.
- **Fail-safe:** При подаче питания выходной сигнал газа принудительно устанавливается в `0.0V` до завершения инициализации ядра.
- **Напряжения ESP32:** Пины ESP32 не толерантны к 5V. Не подавайте на GPIO напряжение выше 3.3V без согласующих делителей.

---
## 🔮 Планы на будущее (Бэклог)

- [ ] **Аппаратные улучшения:**
  - Добавить источник скорости и пробега: один Холл мотора через согласование уровней (с учётом возможного фривила) **или** пассивное чтение шины дисплея контроллера после идентификации протокола.
  - Сделать второй (правый) мини-джойстик для оперативного управления светом/поворотниками, оставив левый для меню и круиза.
  - Интегрировать вольтметр батареи (хотя штатная ручка газа часто имеет свой индикатор).

- [ ] **Программные улучшения (по мере добавления датчиков):**
  - **Автокалибровка старта:** По датчику скорости определять реальное напряжение `throttleOutMinV`, при котором велосипед трогается с места.
  - **Умный круиз-контроль:** Переход от разомкнутого управления (удержание заданного % газа) к ПИД-регулятору с обратной связью по реальной скорости.
  - **Шина дисплея:** Определить протокол, скорость и кадры конкретного контроллера; реализовать RX-only парсер скорости, напряжения и кодов ошибок.
  - **Бортовой компьютер:** Подсчет общего пробега (хранение в NVS, защита от износа flash-памяти), средней скорости, времени в пути.
  - **Сервисный режим / Anti-Police:** "Стелс-режим" с аппаратным ограничением мощности и скорости, активируемый, например, 5 короткими нажатиями тормоза.


## 📁 Структура репозитория

```text
.
├── README.md                     # Документация проекта
├── LICENSE                       # Лицензия MIT
├── tz_comfort_block_v0.1.md      # Исходное ТЗ и планы
├── platformio.ini                # Конфиг PlatformIO (env: esp32dev + native для тестов)
├── src/                          # Прошивка
│   ├── main.cpp                  # Точка входа, инициализация модулей (батарейный саг-гард)
│   ├── core/                     # Логика throttle/PAS/cruise/света (без Arduino-зависимостей)
│   ├── system/                   # Системные подсистемы
│   │   ├── joystick.*            # Физический джойстик (VRx/VRy/SW)
│   │   ├── events_engine.*       # Конструктор событий (8 правил × 3 действия)
│   │   ├── hardware_config.*     # Распиновка GPIO (NVS, /settings/pins)
│   │   ├── debug_capture.*       # Отладочный буфер + пассивный сниффер шины (pin 35/36/39)
│   │   ├── battery_sag.*         # Саг-гард батареи (NORMAL/SAG/CUTOFF)
│   │   ├── inputs.* storage.* network.* peripherals.* cpu_profile.* version.*
│   ├── utils/                    # Общие утилиты и иконки (icons/)
│   └── web/                      # Веб-сервер: маршруты, обработчики, HTML-страницы
│       ├── web_handlers_settings_wifi.cpp
│       ├── web_handlers_settings_throttle.cpp
│       ├── web_handlers_settings_pas.cpp
│       ├── web_handlers_settings_cruise.cpp
├── data/                         # HTML-страницы и иконки для LittleFS
│   ├── emulation.html            # Страница эмуляции (генерируется из PROGMEM-частей)
│   ├── index.html                # Главная страница (LittleFS fallback → /system)
│   ├── debug.html                # Осциллограф / сниффер
│   └── matrix_graphics.js        # Иконки/спрайты для LED-матрицы 16×32
├── test/                         # Юнит-тесты (pio test -e native)
├── docs/                         # Схемы (*.svg)
├── tools/                        # Служебные скрипты (см. полный список ниже)
│   ├── gen_emulation_page.py       # Сборка data/emulation.html из PROGMEM-частей
│   ├── split_settings_handlers.py  # Разбиение web_handlers_settings.cpp на 4 файла
│   ├── apply_param_utils.py        # Замена server.arg().toInt/toFloat → getArgInt/getArgFloat
│   ├── verify_param_utils.py       # Контроль замен (RESULT: OK)
│   ├── check_ui_theme.py           # Проверка единообразия UI-темы
│   ├── normalize_back_link.py      # Приведение кнопок «назад» к единому виду
│   ├── mkpreview.py                # Генератор превью-анимации (preview_animation.html)
│   ├── gen.py                      # Скрипт генерации preview_animation.html
│   ├── extract_html.py             # Извлечение HTML из прошивки (для data/)
│   └── tools_extract_resources.py  # Рефактор-скрипт: вынос ресурсов в data/
├── preview_animation.html        # Сгенерированное превью анимации матрицы
└── *.svg (в корне)               # Копии сгенерированных схем рядом с генераторами
```

---

## 📄 Лицензия

Распространяется под лицензией MIT. Подробности в файле [LICENSE](./LICENSE).

---

## 🤖 Cline Instructions

**Project:** ComfortRide-ESP — ESP32 e-bike controller firmware + web UI.

### Architecture
- `src/core/` — Pure logic (throttle, PAS, cruise, lights). Unit-tested with Unity (`pio test -e native`).
- `src/system/` — Network, NVS storage, events_engine, joystick, inputs, peripherals, hardware_config.
- `src/web/` — HTTP routes + handlers. HTML pages live as PROGMEM strings in `html_pages_*.cpp`, with `data/*.html` as LittleFS fallback.
- `src/utils/` — Icons, helpers.
- `data/` — Static HTML for LittleFS (mirrors PROGMEM pages): `index.html`, `emulation.html`, `debug.html`, `matrix_graphics.js`.
- `tools/` — Build/export scripts: `export_ui_preview.py`, `gen_emulation_page.py`, `split_settings_handlers.py`, `check_ui_theme.py`, `normalize_back_link.py`, `mkpreview.py`, `extract_html.py`.

### Key Commands
```bash
pio run                  # build firmware (esp32dev)
pio run -t upload        # flash via USB
pio run -t uploadfs      # flash LittleFS (data/)
pio run -v               # verbose build
pio test -e native       # unit tests (16 tests for light_logic)
python3 tools/export_ui_preview.py   # regenerate ui_preview/ from src/
python3 tools/check_i18n.py          # check i18n dictionary consistency
```

### i18n (Internationalization)
- Dictionary lives in `src/web/web_ui.cpp` → `getI18nJs()`. Keys: `ru` + `en` (always identical key set).
- `data-i18n` attributes on HTML elements are auto-applied by `applyI18n()` on DOMContentLoaded.
- Language preference persisted in `localStorage.getItem('bike_ui_lang')`.
- TopBar language switcher: `<select id="tbLangSel" onchange="setUiLang(this.value)">` — present on all pages via `getTopBarHtml()` (C++) and injected into static HTML by `export_ui_preview.py`.
- Adding a new translable string: add key to both `ru` and `en` blocks in `getI18nJs()`; add `data-i18n="key"` to the element.

### Versioning
- Single source of truth: `src/system/version.h` → `FIRMWARE_VERSION`.
- Bump version here, then update `README.md` (v0.x.x entry), `data/index.html` title/version, `data/emulation.html` title/version, `src/web/html_pages_hub.cpp`, `src/web/html_pages_emulation_head.cpp`.

### Testing / QA
- After any HTML/i18n change, run `python3 tools/export_ui_preview.py` and open `ui_preview/index.html` / `ui_preview/emulation.html` in a browser to verify language switching.
- Run `python3 tools/check_i18n.py` to verify ru/en key symmetry and no empty values.
- Run `python3 tools/check_i18n.py` to verify ru/en key symmetry and no empty values.
