#include "web/html_pages_emulation.h"

// JS: updateMatrixDisplay — отрисовка состояний на LED-матрице 16x32.
// Часть страницы эмулятора (этап D: разбиение matrix-части).
static const char part[] PROGMEM = R"rawliteral(function updateMatrixDisplay() {
  clearMatrix();
  const now = Date.now();
  const blinkOn = Math.floor(now / 400) % 2 === 0;

  if (draftMode === "settings") {
    // Gear icon top-left rows 1..7, cols 1..7
    drawGear7x7(1, 1);

    // Fixed checkmark on the right: rows 10..14, cols 24..28
    if (!draftSettingEditing) {
      drawIcon5x5(ICON_CHECK_5X5, 10, 24);
    } else if (blinkOn) {
      drawIcon5x5(ICON_CHECK_5X5, 10, 24);
    }

    // Current setting item
    const curItem = SETTINGS_MENU[draftSettingIdx];
    const fullText = curItem.name; // e.g. "THROTTLE IN MIN"
    const words = fullText.split(' ');
    let lines = [];
    let curLine = "";
    for (let w of words) {
      if (!curLine) {
        curLine = w;
      } else if ((curLine + " " + w).length <= 5) {
        curLine += " " + w;
      } else {
        lines.push(curLine);
        curLine = w;
      }
    }
    if (curLine) lines.push(curLine);

    // Scrolling logic for setting title (clipped in rows 0..9, cols 10..29)
    // Display window fits 2 lines at height 5 with 1px gap: line 0 at r=0, line 1 at r=5 (0..9)
    const lineHeight = 5;
    const lineSpacing = 1;
    const totalLinePitch = lineHeight + lineSpacing; // 6px
    const maxScroll = Math.max(0, (lines.length - 2) * totalLinePitch);

    let scrollY = 0;
    if (maxScroll > 0) {
      const scrollCycleMs = 3000;
      const t = (now % scrollCycleMs) / scrollCycleMs;
      if (t < 0.35) {
        scrollY = 0;
      } else if (t < 0.5) {
        scrollY = ((t - 0.35) / 0.15) * maxScroll;
      } else if (t < 0.85) {
        scrollY = maxScroll;
      } else {
        scrollY = maxScroll * (1 - (t - 0.85) / 0.15);
      }
    }

    for (let i = 0; i < lines.length; i++) {
      const y = Math.round(i * totalLinePitch - scrollY);
      if (y + 5 >= 0 && y <= 9) {
        drawText3x5(lines[i], y, 10, 0, 9, 10, 29);
      }
    }

    // Value area at bottom: rows 11..15, cols 2..22
    const valStr = curItem.val.toFixed(2) + curItem.unit;
    if (!draftSettingEditing || blinkOn) {
      drawText3x5(valStr, 11, 2, 10, 15, 0, 23);
    }
  } else {
    let modeToDraw = draftMode;
    let lvlToDraw = (modeToDraw === "pas") ? draftPasLvl : ((modeToDraw === "cruise") ? draftCruiseLvl : 0);
    let maxLvl = (modeToDraw === "pas") ? pasMax : cruiseMax;

    if (lvlToDraw !== animTargetLvl) {
      animStartLvl = animTargetLvl;
      animTargetLvl = lvlToDraw;
      animStartTime = now;
    }
    let animProgress = 1;
    if (now - animStartTime < ANIM_DURATION_MS) {
      let t = (now - animStartTime) / ANIM_DURATION_MS;
      animProgress = 1 - Math.pow(1 - t, 3);
    }

    // 1. Draw Big Letter P or C (6x11, rows 0..10, cols 0..5) - blink when draft mode differs from active mode
    let modeSwitchPending = isDirty && (draftMode !== activeMode);
    if (!modeSwitchPending || blinkOn) {
      if (modeToDraw === "pas") {
        drawBigLetter('P', 0, 0);
      } else if (modeToDraw === "cruise") {
        drawBigLetter('C', 0, 0);
      }
    }

    // 2. Стрелки центрированы относительно блока цифр 3x5 (две цифры: cols 7..13, одна: cols 8..12).
    let isTwoDigits = (lvlToDraw >= 10);
    let arrowCol = isTwoDigits ? 8 : 7;
    let arrowUpVOffset = 0;
    let arrowDownVOffset = 0;
    if (now < arrowBounceUntil) {
      let remaining = arrowBounceUntil - now;
      let bPhase = Math.sin((500 - remaining) / 500 * Math.PI);
      if (arrowBounceDir > 0) {
        arrowUpVOffset = -Math.round(bPhase * 1.2);
      } else if (arrowBounceDir < 0) {
        arrowDownVOffset = Math.round(bPhase * 1.2);
      }
    }

    if (lvlToDraw < maxLvl) {
      drawArrow5x2(ARROW_UP, 0 + arrowUpVOffset, arrowCol);
    }
    // Нижняя стрелка показывается всегда, но при уровне 0 — без центрального пикселя.
    drawArrow5x2(ARROW_DOWN, 9 + arrowDownVOffset, arrowCol, lvlToDraw === 0);

    // 3. Elevator Animation inside digit window: rows 3..7 (height 5px), strictly clipped
    let isCruiseWaiting = (modeToDraw === "cruise" && activeCruiseLvl > 0 && !simCruiseEngaged && !isDirty);
    let showDigits = true;
    if ((isDirty || isCruiseWaiting) && !blinkOn) showDigits = false;

    if (showDigits) {
      let fromLvl = animStartLvl;
      let toLvl = animTargetLvl;
      let dir = (toLvl >= fromLvl) ? 1 : -1;

      const renderLevelToMatrix = (lvlVal, rowOffset) => {
        let tD = Math.floor(lvlVal / 10);
        let oD = lvlVal % 10;
        let twoD = (lvlVal >= 10);
        let startC = twoD ? 7 : 8;

        let buf1 = Array.from({length: 5}, () => new Uint8Array(3));
        let buf2 = Array.from({length: 5}, () => new Uint8Array(3));

        if (twoD) {
          drawDigit3x5ToBuffer(tD.toString(), buf1);
          drawDigit3x5ToBuffer(oD.toString(), buf2);
        } else {
          drawDigit3x5ToBuffer(oD.toString(), buf1);
        }

        for (let r = 0; r < 5; r++) {
          let targetRow = Math.round(3 + r + rowOffset);
          if (targetRow >= 3 && targetRow <= 7) {
            for (let c = 0; c < 3; c++) {
              if (buf1[r][c]) setMatrixPixel(targetRow, startC + c, 1);
              if (twoD && buf2[r][c]) setMatrixPixel(targetRow, startC + 4 + c, 1);
            }
          }
        }
      };

      if (animProgress < 1 && fromLvl !== toLvl) {
        renderLevelToMatrix(Math.round(fromLvl), Math.round(-dir * animProgress * 5));
        renderLevelToMatrix(Math.round(toLvl), Math.round(dir * (1 - animProgress) * 5));
      } else {
        renderLevelToMatrix(lvlToDraw, 0);
      }
    }
  }

  // Нижний ряд: иконки управления (cols: 0=левый поворотник, 6=фара, 12=гудок, 21=тормоз, 27=правый поворотник)
  // Рисуются ДО шкал газа/выхода, чтобы шкалы (col 30/31, rows 13..15) перекрывали хвост правой стрелки.
  
  // Поворотники: мигание синхронно с физическими светодиодами (500мс)
  const lr = hwTurnLeftActive || hwDisplayTurnLeftActive;
  const rr = hwTurnRightActive || hwDisplayTurnRightActive;
  const lb = lr && (now % 1000 < 500);
  const rb = rr && (now % 1000 < 500);
  if (lb) drawTurnArrow5x5(true, 11, 0);
  if (rb) drawTurnArrow5x5(false, 11, 27);
  
  // Фара: показываем иконку текущего режима света (0=ВЫКЛ, 1=ДХО, 2=БЛИЖНИЙ, 3=БЛ+ДХО)
  if (draftMode !== "settings" && (hwLightMode > 0 || hwDisplayLightActive)) {
    drawIcon5x5(ICON_LIGHT_MODES[hwLightMode] || ICON_LIGHT_MODES[0], 11, 6);
  }
  
  // Гудок: временная иконка (автоматически скрывается через 300мс)
  if (hwDisplayHornActive && Date.now() < hwDisplayHornHideAtMs) {
    drawIcon5x5(ICON_HORN, 11, 12);
  }

  // 4. Scales calculation
  let inMin = cfgThrottleInMin, inMax = Math.max(inMin + 0.01, cfgThrottleInMax);
  let outMin = cfgThrottleOutMin, outMax = Math.max(outMin + 0.01, cfgThrottleOutMax);
  let rawGripV = inMin + (simGasPct / 100) * (inMax - inMin);
  let clampedV = Math.max(inMin, Math.min(inMax, rawGripV));
  let calibOutV = outMin + ((clampedV - inMin) / (inMax - inMin)) * (outMax - outMin);
  let calibOutPct = Math.max(0, Math.min(100, ((calibOutV - outMin) / (outMax - outMin)) * 100));

  let inGasLeds = Math.round((simGasPct / 100) * 16);
  for (let r = 0; r < inGasLeds; r++) {
    const row = 15 - r;
    if (row < 13 || row > 15) setMatrixPixel(row, 30, 1);
  }

  let effectiveOutPct = 0;
  if (!effectiveBrake) {
    let baseMotorPct = calibOutPct;
    if (activeMode === "cruise" && activeCruiseLvl > 0 && simCruiseEngaged) {
      // Цель уровня берём из конфигурации прошивки (cruiseLevelPercent[]),
      // как это делает getCruiseTargetV() в core/cruise.cpp
      let targetCruisePct = cruiseLevelPcts[activeCruiseLvl - 1];
      if (targetCruisePct === undefined) {
        targetCruisePct = Math.min(100, activeCruiseLvl * (100 / Math.max(1, cruiseMax)));
      }
      baseMotorPct = Math.max(baseMotorPct, targetCruisePct);
    } else if (activeMode === "pas" && activePasLvl > 0 && effectivePedal) {
      let targetPasPct = Math.min(100, activePasLvl * (100 / Math.max(1, pasMax)));
      baseMotorPct = Math.max(baseMotorPct, targetPasPct);
    }
    effectiveOutPct = baseMotorPct;
  }
  let outGasLeds = Math.round((effectiveOutPct / 100) * 16);
  for (let r = 0; r < outGasLeds; r++) {
    const row = 15 - r;
    if (row < 13 || row > 15) setMatrixPixel(row, 31, 1);
  }

  // Индикатор педалирования/тормоза 5x5: cols 21..25, rows 11..15
  // (сдвинут на 3px влево, чтобы освободить правый нижний угол под стрелку поворотника).
  if (effectiveBrake) {
    const brakeBlink = Math.floor(now / 90) % 2 === 0;
    if (brakeBlink) drawIcon5x5(ICON_BRAKE, 11, 21);
  } else if (effectivePedal) {
    let frame = Math.floor((now - simPedalStartMs) / 75) % 8;
    drawIcon5x5(ICON_PEDAL_FRAMES[frame], 11, 21);
  }

  // Отрисовка матрицы на canvas
  renderCanvas();
}

// Запуск анимации при загрузке страницы (1 раз)
if (canvas && !bootAnimationComplete) {
  playBootAnimation();
}

setInterval(() => {
  if (!bootAnimationActive) {
    updateEffectiveStates(); // Обновляем состояния для синхронизации временных иконок
    updateMatrixDisplay();
  }
}, 40);
)rawliteral";

PGM_P getEmulationMatrixDisplay() { return part; }
size_t getEmulationMatrixDisplayLen() { return sizeof(part) - 1; }
