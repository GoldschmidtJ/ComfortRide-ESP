#!/usr/bin/env python3
"""Разбиение web_handlers_settings.cpp (699 строк) на 4 файла по доменам.

Контент переносится 1:1 (строки 17-180, 183-315, 318-548, 551-699 исходника).
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src/web/web_handlers_settings.cpp"

INC_WIFI = '''#include "web/web_handlers_settings.h"
#include <WebServer.h>
#include <WiFi.h>

#include "system/storage.h"    // storedApSsid/Pass, apSettingsSave, wifiCredsSave
#include "system/network.h"    // wifiConnect, startConfiguredAp
#include "utils/utils.h"       // htmlEscape
#include "web/web_ui.h"        // getTopBarCss/getSettingsCss/getTopBarHtml/getBackMenuHtml
#include "web/html_pages.h"    // getTopBarJs
#include "web/web_routes.h"    // server
'''

INC_THROTTLE = '''#include "web/web_handlers_settings.h"
#include <WebServer.h>

#include "core/throttle.h"     // throttleInMinV/MaxV, hwThrottleInV
#include "system/storage.h"    // throttleSettingsSave
#include "system/inputs.h"     // ownBrakeCutoffEnabled
#include "web/web_ui.h"        // getTopBarCss/getSettingsCss/getTopBarHtml/getBackMenuHtml/getSettingsJs
#include "web/html_pages.h"    // getTopBarJs
#include "web/web_routes.h"    // server
'''

INC_PAS = '''#include "web/web_handlers_settings.h"
#include <WebServer.h>

#include "core/pas.h"          // pas*-настройки, pasPulseMux, PAS_MAX_LEVELS
#include "system/storage.h"    // pasSettingsSave
#include "web/web_ui.h"        // getTopBarCss/getSettingsCss/getTopBarHtml/getBackMenuHtml/getSettingsJs
#include "web/html_pages.h"    // getTopBarJs
#include "web/web_routes.h"    // server
'''

INC_CRUISE = '''#include "web/web_handlers_settings.h"
#include <WebServer.h>
#include <math.h>              // round

#include "core/cruise.h"       // cruise*-настройки, CRUISE_MAX_LEVELS
#include "system/storage.h"    // cruiseSettingsSave
#include "web/web_ui.h"        // getTopBarCss/getSettingsCss/getTopBarHtml/getBackMenuHtml/getSettingsJs
#include "web/html_pages.h"    // getTopBarJs
#include "web/web_routes.h"    // server
'''

# (имя файла, инклуды, первая строка-контент, последняя строка-контент) — 1-based включительно
SPLITS = [
    ("web_handlers_settings_wifi.cpp",     INC_WIFI,     17, 180),
    ("web_handlers_settings_throttle.cpp", INC_THROTTLE, 183, 315),
    ("web_handlers_settings_pas.cpp",      INC_PAS,      318, 548),
    ("web_handlers_settings_cruise.cpp",   INC_CRUISE,   551, 699),
]


def main() -> int:
    lines = SRC.read_text(encoding="utf-8").split("\n")  # 0-based; строка N файла = lines[N-1]
    covered = []
    for name, inc, a, b in SPLITS:
        body = "\n".join(lines[a - 1:b])
        out = ROOT / "src/web" / name
        out.write_text(inc + "\n" + body + "\n", encoding="utf-8")
        n = len(out.read_text(encoding="utf-8").splitlines())
        print(f"{name}: строки {a}-{b} -> {n} строк")
        covered.extend(range(a, b + 1))

    # Контроль: весь значимый контент перенесён, дыр нет
    content_lines = [i for i in range(1, len(lines) + 1) if lines[i - 1].strip() != ""]
    non_content = set(range(1, 16)) | {181, 182, 316, 317, 549, 550}
    missing = [i for i in content_lines if i not in covered and i not in non_content]
    print("Неперенесённые строки контента:", missing if missing else "нет")
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
