#include "web/html_pages_emulation.h"

// JS: состояние UI, wake lock, видимость, script matrix_graphics.js
// Часть страницы эмулятора (этап D: разбиение html_pages_emulation.cpp).
static const char part[] PROGMEM = R"rawliteral(<script>
let activeMode = "off";
let activePasLvl = 0;
let activeCruiseLvl = 0;
let pasMax = 5;
let cruiseMax = 5;

const cfgThrottleInMin = 1.10;
const cfgThrottleInMax = 4.10;
const cfgThrottleOutMin = 1.10;
const cfgThrottleOutMax = 4.10;

// Поведение круиза: по умолчанию совпадает с прошивкой (cruise.cpp),
// реальные значения подтягиваются из /status/sys (cruise_conf_thr и т.д.)
let cfgCruiseConfirmThrottle = false;
let cfgCruiseAfterBraking = 1;
let cfgCruiseAfterThrottle = 2;
let cruiseLevelPcts = []; // Цели уровней круиза в % (из прошивки)

let draftMode = (activeMode === "off") ? "pas" : activeMode;
let draftPasLvl = activePasLvl;
let draftCruiseLvl = activeCruiseLvl;
let draftSettingIdx = 0;
let draftSettingEditing = false;
let isDirty = false;
let userInteractingUntil = 0;
let applyInProgressUntil = 0;
let applyInProgress = false;
let simCruiseEngaged = (activeMode === "cruise" && activeCruiseLvl > 0);
let simCruisePendingResume = false;
let simCruiseConfirmRequired = false;
let simCruiseReleaseSeen = false;
let arrowBounceUntil = 0;
let arrowBounceDir = 0;

// Screen Wake Lock API
let wakeLock = null;
async function requestWakeLock() {
  if ('wakeLock' in navigator) {
    try {
      wakeLock = await navigator.wakeLock.request('screen');
      wakeLock.addEventListener('release', () => { wakeLock = null; });
    } catch (err) {
      console.log('WakeLock error:', err);
    }
  }
}
requestWakeLock();
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible') requestWakeLock();
});

function vib(pattern = 20) {
  if (navigator.vibrate) {
    try { navigator.vibrate(pattern); } catch (e) {}
  }
}

function checkUiVisibility() {
  const showMatrix = localStorage.getItem("ui_show_matrix") !== "false";
  const showControls = localStorage.getItem("ui_show_controls") !== "false";
  const showScreen = localStorage.getItem("ui_show_screen") !== "false";
  const showDpad = localStorage.getItem("ui_show_dpad") !== "false";

  const matrixEl = document.getElementById("simMatrixCard");
  const controlsEl = document.getElementById("simControlsPanel");
  const screenEl = document.getElementById("joyScreen");
  const dpadEl = document.getElementById("dpadContainer");
  const joyPanel = document.getElementById("joystickPanel");

  if (matrixEl) matrixEl.style.display = showMatrix ? "block" : "none";
  if (controlsEl) controlsEl.style.display = showControls ? "flex" : "none";
  if (screenEl) screenEl.style.display = showScreen ? "block" : "none";
  if (dpadEl) dpadEl.style.display = showDpad ? "grid" : "none";
  if (joyPanel) {
    joyPanel.style.display = (!showScreen && !showDpad) ? "none" : "block";
  }
}
document.addEventListener("DOMContentLoaded", checkUiVisibility);
checkUiVisibility();

// ================= 16x32 LED MATRIX RENDERING ENGINE =================
const canvas = document.getElementById("ledMatrixCanvas");
const ctx = canvas ? canvas.getContext("2d") : null;
const MATRIX_ROWS = 16, MATRIX_COLS = 32;
let matrixGrid = [];
for (let r = 0; r < MATRIX_ROWS; r++) matrixGrid[r] = new Uint8Array(MATRIX_COLS);

// Центральная зона: P/C — единая сетка 6x11 и равномерный штрих 2 пикселя.
const BIG_GLYPHS = {
  'P': [
    [0,1,1,1,1,0],
    [1,1,1,1,1,1],
    [1,1,0,0,1,1],
    [1,1,0,0,1,1],
    [1,1,1,1,1,1],
    [1,1,1,1,1,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0]
  ],
  'C': [
    [0,1,1,1,1,0],
    [1,1,1,1,1,1],
    [1,1,0,0,1,1],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,0,0],
    [1,1,0,0,1,1],
    [1,1,1,1,1,1],
    [0,1,1,1,1,0]
  ]
};

// Компактные цифры 3x5: три столбца, пять строк.
</script>
<script src="/matrix_graphics.js"></script>
)rawliteral";

PGM_P getEmulationState() { return part; }
size_t getEmulationStateLen() { return sizeof(part) - 1; }
