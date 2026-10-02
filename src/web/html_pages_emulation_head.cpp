#include "web/html_pages_emulation.h"

// HTML: head, CSS-тема и разметка страницы эмулятора
// Часть страницы эмулятора (этап D: разбиение html_pages_emulation.cpp).
static const char part[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenBike Controller v0.4.0</title>
<style>

:root{--ui-bg:#101214;--ui-card:#191c20;--ui-button:#252a30;--ui-border:#3b424a;--ui-hover:#30363d;--ui-active:#383f47;--ui-text:#eee;--ui-muted:#8b949e;--ui-focus:#aeb6bf;--ui-accent:#4a90d9;--ui-success:#2ecc71;--ui-warning:#f39c12;--ui-danger:#e74c3c;--ui-purple:#9b59b6;--ui-dark:#0a0f0d;--ui-black:#000;--ui-led:#ff8c00;--ui-led-glow:#ff7700;--ui-led-off:#1e140a;--ui-radius:8px;--ui-control-height:44px;--ui-on-accent:#fff;--ui-success-soft:rgba(46,204,113,.12);--ui-warning-soft:rgba(243,156,18,.15);--ui-danger-soft:rgba(231,76,60,.12);--ui-font:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;--ui-mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;--ui-fs-h1:24px;--ui-fs-h2:18px;--ui-fs-body:16px;--ui-fs-small:13px;--ui-fs-mid:14px;--ui-fs-tiny:12px}
.back-link:hover,a.back:hover{text-decoration:underline;background:var(--ui-button)}
.back-row{margin:0 0 12px}
.u-muted{color:var(--ui-muted)}.u-small{font-size:var(--ui-fs-tiny)}.u-dim{opacity:.7}.back-link,a.back{display:inline-block;color:var(--ui-accent);text-decoration:none;font-size:var(--ui-fs-small);padding:6px 10px;margin:0 0 0 -10px;border-radius:6px}.hint{color:var(--ui-muted);font-size:var(--ui-fs-small)}.is-hidden{display:none!important}
.top-bar-sticky{position:sticky;top:0;top:env(safe-area-inset-top);z-index:9999;background:var(--ui-card);border-bottom:1px solid var(--ui-border);padding:8px 12px;margin:-20px -20px 15px;font-size:var(--ui-fs-tiny);color:var(--ui-muted);display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;box-shadow:0 2px 8px rgba(0,0,0,.5)}
.tb-item{display:inline-flex;align-items:center;gap:4px;white-space:nowrap}
.tb-link{color:var(--ui-accent);text-decoration:none;padding:2px 6px;border-radius:6px;background:var(--ui-button);border:1px solid var(--ui-border);transition:background .2s,border-color .2s}
.tb-link:hover{background:var(--ui-hover);border-color:var(--ui-muted);color:var(--ui-text)}
.tb-dot{width:8px;height:8px;border-radius:50%;display:inline-block}
.dot-green{background:var(--ui-success);box-shadow:0 0 5px var(--ui-success)}
.dot-yellow{background:var(--ui-warning);box-shadow:0 0 5px var(--ui-warning)}
.dot-red{background:var(--ui-danger)}
.dot-gray{background:var(--ui-muted)}

body{font-family:var(--ui-font);font-size:var(--ui-fs-body);line-height:1.45;padding:20px;max-width:560px;margin:auto;background:var(--ui-bg);color:var(--ui-text)}
.header{text-align:center;margin-bottom:16px}.header h1{margin:0;font-size:var(--ui-fs-h1);line-height:1.2}.header .version{color:var(--ui-muted);font-size:var(--ui-fs-mid)}
a.card{display:block;background:var(--ui-button);color:var(--ui-text);padding:15px;border:1px solid var(--ui-border);border-radius:var(--ui-radius);margin-bottom:10px;text-decoration:none;transition:background .2s,border-color .2s}
a.card:hover{background:var(--ui-hover)}a.card:active{background:var(--ui-active)}
.warn{background:var(--ui-card);border:1px solid var(--ui-danger);color:var(--ui-text);padding:12px;border-radius:var(--ui-radius);margin-bottom:16px;font-weight:bold;font-size:var(--ui-fs-small)}

/* LED Matrix Simulator */
.matrix-card{background:var(--ui-card);border:1px solid var(--ui-border);border-radius:12px;padding:10px;margin-bottom:12px;text-align:center;box-shadow:0 4px 12px rgba(0,0,0,.4);transition:all .2s}
.matrix-title{font-size:11px;font-weight:bold;color:var(--ui-muted);letter-spacing:1px;text-transform:uppercase;margin-bottom:8px}
.sim-matrix-area{display:flex;justify-content:center;align-items:center;margin-bottom:8px}
#ledMatrixCanvas{background:var(--ui-black);border:2px solid var(--ui-border);border-radius:6px;box-shadow:inset 0 0 8px rgba(0,0,0,.7);display:block;max-width:100%;height:auto}

/* Компактная панель эмуляции */
.simulator-layout{display:grid;grid-template-columns:minmax(0,1fr) 64px;gap:8px;align-items:stretch;margin-bottom:12px}
.sim-left-col{min-width:0;display:flex;flex-direction:column;gap:8px}
.sim-section-title{font-size:10px;font-weight:900;letter-spacing:1px;text-transform:uppercase;color:var(--ui-muted);margin-bottom:2px}
.sim-btns-group{display:flex;flex-direction:row;gap:6px;width:100%}
.sim-side-btn{flex:1;display:flex;align-items:center;justify-content:center;gap:6px;padding:10px 6px;font-size:var(--ui-fs-small);font-weight:bold;border-radius:8px;border:1px solid var(--ui-border);background:var(--ui-button);color:var(--ui-text);cursor:pointer;user-select:none;-webkit-user-select:none;touch-action:manipulation;transition:background .1s,border-color .1s}
.sim-btn-ico{font-size:var(--ui-fs-h2);line-height:1}.sim-side-btn:hover{background:var(--ui-hover)}.sim-side-btn:active{background:var(--ui-active)}
.sim-side-btn:focus-visible,.dpad-btn:focus-visible{outline:2px solid var(--ui-focus);outline-offset:2px}
.sim-side-btn.active{background:var(--ui-card);border-color:var(--ui-danger);color:var(--ui-text)}
.sim-side-btn.pedal-btn.active{background:var(--ui-card);border-color:var(--ui-success);color:var(--ui-text)}

.sim-throttle-group{flex:1 1 auto;min-width:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;background:var(--ui-card);border:1px solid var(--ui-border);border-radius:10px;padding:8px 6px}
.sim-throttle-label{font-size:10px;font-weight:900;color:var(--ui-muted);text-transform:uppercase;letter-spacing:1px}
.sim-slider-vert{writing-mode:vertical-lr;direction:rtl;-webkit-appearance:slider-vertical;appearance:slider-vertical;width:24px;height:126px;cursor:pointer;accent-color:var(--ui-accent)}
.sim-throttle-val{font-size:var(--ui-fs-tiny);font-weight:bold;color:var(--ui-accent);min-width:36px;text-align:center}

/* D-Pad Джойстик */
.joystick-panel{align-self:center;background:var(--ui-card);border:1px solid var(--ui-border);border-radius:12px;padding:7px;width:fit-content;max-width:100%;min-width:0;box-sizing:border-box;margin-bottom:0;box-shadow:0 4px 12px rgba(0,0,0,.35);display:flex;flex-direction:column;justify-content:flex-start}.joystick-panel .sim-section-title,.joystick-panel .joy-screen{width:193px;max-width:100%;box-sizing:border-box}
.joy-screen{background:var(--ui-dark);border:1px solid var(--ui-border);border-radius:10px;padding:8px 10px;margin-bottom:10px;text-align:center;font-family:var(--ui-mono)}
.screen-mode{font-size:var(--ui-fs-tiny);font-weight:bold;letter-spacing:1px;color:var(--ui-muted);text-transform:uppercase}.screen-mode.mode-pas{color:var(--ui-success)}.screen-mode.mode-cruise{color:var(--ui-accent)}.screen-mode.mode-off{color:var(--ui-danger)}
.screen-val{font-size:34px;font-weight:900;color:var(--ui-text);margin:4px 0}.screen-status{font-size:10px;color:var(--ui-muted);font-weight:bold;text-transform:uppercase}.screen-status.dirty{color:var(--ui-warning);animation:blink 1s infinite}
@keyframes blink{50%{opacity:0.4}}

.dpad-container{display:grid;grid-template-columns:repeat(3,60px);grid-template-rows:repeat(3,50px);gap:6px;justify-content:center;margin:2px auto 0}
.dpad-btn{background:var(--ui-button);color:var(--ui-text);border:1px solid var(--ui-border);border-bottom-width:2px;border-radius:10px;display:flex;align-items:center;justify-content:center;font-size:19px;font-weight:900;cursor:pointer;user-select:none;-webkit-user-select:none;touch-action:manipulation;transition:background .08s,transform .08s}
.dpad-btn:hover{background:var(--ui-hover)}.dpad-btn:active{transform:translateY(2px);border-bottom-width:2px;background:var(--ui-active)}
.btn-up{grid-column:2;grid-row:1}
.btn-left{grid-column:1;grid-row:2}
.btn-ok{grid-column:2;grid-row:2;background:var(--ui-card);border-color:var(--ui-success);border-bottom-color:var(--ui-success);color:var(--ui-success);font-size:19px}
.btn-ok:hover{background:var(--ui-hover)}.btn-ok:active{background:var(--ui-active)}
.btn-ok.dirty-pulse{background:var(--ui-card);border-color:var(--ui-warning);border-bottom-color:var(--ui-warning);color:var(--ui-warning);animation:pulse 1s infinite}
@keyframes pulse{50%{box-shadow:0 0 14px rgba(243,156,18,0.7)}}
.btn-right{grid-column:3;grid-row:2}
.btn-down{grid-column:2;grid-row:3}
.joy-legend{display:none}
@media(max-width:520px){body{padding:12px}.top-bar-sticky{margin:-12px -12px 12px}.sim-side-btn{padding:8px 5px}.matrix-card{padding:8px}.screen-val{font-size:23px}}
@media(max-width:360px){.simulator-layout{grid-template-columns:minmax(0,1fr) 54px}.joystick-panel .sim-section-title,.joystick-panel .joy-screen{width:158px}.dpad-container{grid-template-columns:repeat(3,49px);grid-template-rows:repeat(3,43px);gap:5px}}
</style>
</head><body>

<div class="top-bar-sticky">
  <div class="tb-item" title="Нагрузка процессора ESP32">
    <span>CPU:</span> <b id="tbCpu">0%</b>
  </div>
  <div class="tb-item" title="Оперативная память (занято / свободно)">
    <span>RAM:</span> <b id="tbRam">0%</b> <span id="tbRamKb" class="u-muted u-small">(0k)</span>
  </div>
  <div class="tb-item" title="Flash память (прошивка / всего)">
    <span>ROM:</span> <span id="tbRom" class="u-muted">0k</span>
  </div>
  <a href="/wifi" class="tb-item tb-link" title="Настройки Wi-Fi">
    <span>WiFi:</span>
    <span class="tb-dot dot-gray" id="tbWifiDot"></span>
    <span id="tbWifiTxt">...</span>
  </a>
  <div class="tb-item" title="Температура процессора">
    <span>Temp:</span> <b id="tbTemp">--°C</b>
  </div>
  <div class="tb-item u-dim" title="Bluetooth (не используется)">
    <span>BT:</span>
    <span class="tb-dot dot-gray"></span>
    <span class="u-muted">Выкл</span>
  </div>
  <select id="tbLangSel" onchange="setUiLang(this.value)" style="margin-left:auto;font-size:var(--ui-fs-tiny);min-height:28px;padding:2px 6px;background:var(--ui-button);color:var(--ui-text);border:1px solid var(--ui-border);border-radius:6px;cursor:pointer">
    <option value="ru">Русский</option>
    <option value="en">English</option>
  </select>
</div>

<div class="header">
  <p class="back-row"><a class="back-link" href="/" data-i18n="backMenu">&larr; Меню</a></p>
  <h1 data-i18n="emulation">Эмуляция управления</h1>
  <div class="version">v0.4.0 &bull; 16&times;32 LED Matrix</div>
</div>

<div class="matrix-card" id="simMatrixCard">
  <div class="matrix-title">Эмулятор дисплея 16&times;32</div>
  <div class="sim-matrix-area">
    <canvas id="ledMatrixCanvas" width="320" height="160"></canvas>
  </div>
  <div class="simulator-layout">
    <div class="sim-left-col">
      <div class="joystick-panel" id="joystickPanel">
        <div class="sim-section-title" data-i18n="joystick">Джойстик</div>
        <div class="joy-screen" id="joyScreen">
          <div class="screen-mode" id="joyMode">PAS</div>
          <div class="screen-val" id="joyVal">УРОВЕНЬ 1</div>
          <div class="screen-status" id="joyStatus">ПОДТВЕРЖДЕНО</div>
        </div>
        <div class="dpad-container" id="dpadContainer">
          <button type="button" class="dpad-btn btn-up" id="btnUp" title="Увеличить">&#9650;</button>
          <button type="button" class="dpad-btn btn-left" id="btnLeft" title="Режим влево">&#9664;</button>
          <button type="button" class="dpad-btn btn-ok" id="btnOk" title="Применить">OK</button>
          <button type="button" class="dpad-btn btn-right" id="btnRight" title="Режим вправо">&#9654;</button>
          <button type="button" class="dpad-btn btn-down" id="btnDown" title="Уменьшить">&#9660;</button>
        </div>
      </div>
      <div class="sim-btns-group" id="simControlsPanel">
        <button type="button" class="sim-side-btn" id="btnSimBrake">
          <span class="sim-btn-ico">[ • ]</span><span data-i18n="brake">Тормоз</span>
        </button>
        <button type="button" class="sim-side-btn pedal-btn" id="btnSimPedal">
          <span class="sim-btn-ico">&#129461;</span><span data-i18n="pedals">Педали</span>
        </button>
      </div>
      <div class="sim-btns-group" id="simLightsPanel">
        <button type="button" class="sim-side-btn" id="btnSimTurnL" title="Левый поворотник">
          <span class="sim-btn-ico">&#8626;</span><span data-i18n="turnL">Пов. L</span>
        </button>
        <button type="button" class="sim-side-btn" id="btnSimTurnR" title="Правый поворотник">
          <span class="sim-btn-ico">&#8627;</span><span data-i18n="turnR">Пов. R</span>
        </button>
        <button type="button" class="sim-side-btn" id="btnSimLight" title="Циклический выбор режима света">
          <span class="sim-btn-ico">&#9788;</span><span id="lblSimLight" data-i18n="light">Свет</span>
        </button>
      </div>
    </div>
    <div class="sim-throttle-group">
      <span class="sim-throttle-label" data-i18n="throttle">Газ</span>
      <input type="range" min="0" max="100" value="0" orient="vertical" class="sim-slider-vert" id="simGas">
      <span class="sim-throttle-val" id="lblGas">0%</span>
    </div>
  </div>
</div>

)rawliteral";

PGM_P getEmulationHead() { return part; }
size_t getEmulationHeadLen() { return sizeof(part) - 1; }
