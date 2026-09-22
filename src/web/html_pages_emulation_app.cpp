#include "web/html_pages_emulation.h"

// JS: сим-входы, refreshHubData (FSM круиза), кнопки, renderJoystick, apply
// Часть страницы эмулятора (этап D: разбиение html_pages_emulation.cpp).
static const char part[] PROGMEM = R"rawliteral(
// Виртуальные входы эмуляции
let simGasPct = 0; // Значение виртуальной ручки газа, 0-100%
let simBrakeActive = false; // Virtual brake input (momentary, active only while held)
let simPedalActive = false; // Virtual pedal button state
let simPedalStartMs = 0;
const sliderGas = document.getElementById("simGas");
const lblGas = document.getElementById("lblGas");

// Слайдер газа — это именно виртуальный вход эмулятора, а не индикатор
// физической ручки. Телеметрия hardware обновляется отдельно ниже.
if (sliderGas) {
  sliderGas.addEventListener("input", (e) => {
    simGasPct = Math.max(0, Math.min(100, Number(e.target.value) || 0));
    if (lblGas) lblGas.innerText = simGasPct + "%";
    userInteractingUntil = Date.now() + 1000;
    renderJoystick();
  });
}

// Real-time data from hardware used by the virtual display
let hwBrakeActive = false;
let hwPasActive = false;

// Combined effective states
let effectiveBrake = false;
let effectivePedal = false;

// Variables for combined state update
let currentMode = "";
let currentPasLvl = 0;
let currentCruiseLvl = 0;
let currentCruiseEngaged = false;

// Обновление результирующих состояний по входам эмулятора и оборудования
function updateEffectiveStates() {
  effectiveBrake = simBrakeActive || hwBrakeActive || (hwDisplayBrakeActive && Date.now() < hwDisplayBrakeHideAtMs);
  effectivePedal = simPedalActive || hwPasActive;
}

function refreshHubData(forceSync = false) {
  if (!forceSync && (isDirty || applyInProgress || Date.now() < applyInProgressUntil)) return;
  fetch("/status/sys")
    .then(r => r.json())
    .then(d => {
      // Игнорируем устаревший ответ, если OK был нажат после отправки запроса.
      if (applyInProgress && !forceSync) return;

      // Update hardware states used by the virtual display
      hwBrakeActive = d.brake || false;
      hwPasActive = d.pas_active || false; // Use 'pas_active' field
      hwTurnLeftActive = d.turn_left_act || false;
      hwTurnRightActive = d.turn_right_act || false;
      hwLightMode = d.light_mode === undefined ? 1 : (d.light_mode | 0);
      renderSimLightButtons();

      // Sync level limits from firmware config (NVS)
      if (d.pas_cnt !== undefined) pasMax = d.pas_cnt | 0;
      if (d.cruise_cnt !== undefined) cruiseMax = d.cruise_cnt | 0;

      // Sync cruise FSM behaviour config from firmware (cruise.cpp defaults)
      if (d.cruise_conf_thr !== undefined) cfgCruiseConfirmThrottle = !!d.cruise_conf_thr;
      if (d.cruise_brk_mode !== undefined) cfgCruiseAfterBraking = d.cruise_brk_mode | 0;
      if (d.cruise_thr_mode !== undefined) cfgCruiseAfterThrottle = d.cruise_thr_mode | 0;
      if (Array.isArray(d.cruise_pcts)) cruiseLevelPcts = d.cruise_pcts;

      // Для эмулятора мы не перезаписываем слайдер (simGasPct) данными физической
      // ручки, чтобы виртуальный интерфейс работал независимо (анимация и логика
      // UI продолжают реагировать на simGasPct).
      // Hardware-телеметрия только обновляет флаги, не трогая simGasPct.

      // Update effective states
      updateEffectiveStates();

      // State machine for cruise confirmation & engagement in UI
      // Логика 1:1 повторяет FSM в src/core/throttle.cpp (строки ~134–177):
      // те же условия, те же режимы 0/1/2, та же семантика cruiseReleaseSeen.
      if (activeMode === "cruise" && activeCruiseLvl > 0) {
        if (effectiveBrake) {
          if (simCruiseEngaged || !simCruisePendingResume) {
            if (cfgCruiseAfterBraking === 0) {
              simCruiseEngaged = false;
              simCruisePendingResume = false;
            } else if (cfgCruiseAfterBraking === 1) {
              // armCruisePending(true, throttlePct)
              simCruiseEngaged = false;
              simCruisePendingResume = true;
              simCruiseConfirmRequired = true;
              simCruiseReleaseSeen = (simGasPct <= 10);
            } else if (cfgCruiseAfterBraking === 2) {
              // armCruisePending(false, throttlePct)
              simCruiseEngaged = false;
              simCruisePendingResume = true;
              simCruiseConfirmRequired = false;
            }
          }
        } else {
          if (simCruiseEngaged && simGasPct > 10) {
            if (cfgCruiseAfterThrottle === 0) {
              simCruiseEngaged = false;
              simCruisePendingResume = false;
            } else if (cfgCruiseAfterThrottle === 1) {
              // armCruisePending(true, throttlePct)
              simCruiseEngaged = false;
              simCruisePendingResume = true;
              simCruiseConfirmRequired = true;
              simCruiseReleaseSeen = (simGasPct <= 10);
            } else if (cfgCruiseAfterThrottle === 2) {
              // armCruisePending(false, throttlePct)
              simCruiseEngaged = false;
              simCruisePendingResume = true;
              simCruiseConfirmRequired = false;
            }
          } else if (!simCruiseEngaged && simCruisePendingResume) {
            if (simCruiseConfirmRequired) {
              if (simGasPct <= 10) {
                simCruiseReleaseSeen = true;
              } else if (simCruiseReleaseSeen && simGasPct > 10) {
                simCruiseEngaged = true;
                simCruisePendingResume = false;
              }
            } else {
              if (simGasPct <= 10) {
                simCruiseEngaged = true;
                simCruisePendingResume = false;
              }
            }
          }
        }
      }

      // Update joystick mode and level display
      currentMode = d.pas_en ? "pas" : (d.cruise_en ? "cruise" : "off");
      currentPasLvl = d.pas_lvl || 0;
      currentCruiseLvl = d.cruise_lvl || 0;
      currentCruiseEngaged = d.cruise_engaged || false;
      let currentCruisePending = d.cruise_pending || false;
      let currentCruiseConfReq = d.cruise_conf_req || false;

      let serverMode = currentMode;
      let serverPasLvl = currentPasLvl;
      let serverCruiseLvl = currentCruiseLvl;
      let serverCruiseEngaged = currentCruiseEngaged;

      if (serverMode === "cruise") {
        if (serverCruiseEngaged) {
          simCruiseEngaged = true;
          simCruisePendingResume = false;
          simCruiseConfirmRequired = false;
        } else if (currentCruisePending) {
          // Прошивка в pending-режиме: зеркалим её FSM-состояние напрямую
          // вместо локального вывода — устраняет рассинхрон после тормоза/газа.
          simCruiseEngaged = false;
          simCruisePendingResume = true;
          simCruiseConfirmRequired = currentCruiseConfReq;
          simCruiseReleaseSeen = (simGasPct <= 10);
        } else if (simCruiseEngaged && !simCruisePendingResume) {
          // Прошивка сообщает: круиз активен, но тяга снята (pending),
          // при этом локальная FSM об этом не знает — синхронизируемся.
          simCruiseEngaged = false;
          simCruisePendingResume = true;
        }
      }

      if (serverMode !== activeMode || serverPasLvl !== activePasLvl || serverCruiseLvl !== activeCruiseLvl) {
        activeMode = serverMode;
        activePasLvl = serverPasLvl;
        activeCruiseLvl = serverCruiseLvl;
        let isUserBusy = isDirty || (Date.now() < userInteractingUntil) || (Date.now() < applyInProgressUntil);
        if (!isUserBusy || forceSync) {
          draftMode = (activeMode === "off") ? "pas" : activeMode;
          draftPasLvl = activePasLvl;
          draftCruiseLvl = activeCruiseLvl;
          if (forceSync) isDirty = false;
        }
        renderJoystick();
      }

      // Update visual feedback for simulated buttons based on effective states
      const btnBrake = document.getElementById("btnSimBrake");
      if (btnBrake) {
        if (effectiveBrake) btnBrake.classList.add("active");
        else btnBrake.classList.remove("active");
      }
      const btnPedal = document.getElementById("btnSimPedal");
      if (btnPedal) {
        if (effectivePedal) btnPedal.classList.add("active");
        else btnPedal.classList.remove("active");
      }

    })
    .catch(e => {
      console.error("Failed to fetch hub data:", e);
    });
}
setInterval(refreshHubData, 100);

// Update effective states whenever sim states change.
// Тормоз — momentary: активен только пока кнопка удерживается.
const btnSimBrakeEl = document.getElementById("btnSimBrake");
function setSimBrake(active) {
  if (simBrakeActive === active) return;
  simBrakeActive = active;
  updateEffectiveStates();
  renderJoystick();
}
if (btnSimBrakeEl) {
  btnSimBrakeEl.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    if (btnSimBrakeEl.setPointerCapture) btnSimBrakeEl.setPointerCapture(e.pointerId);
    setSimBrake(true);
  });
  btnSimBrakeEl.addEventListener("pointerup", (e) => {
    setSimBrake(false);
    if (btnSimBrakeEl.releasePointerCapture && btnSimBrakeEl.hasPointerCapture(e.pointerId)) btnSimBrakeEl.releasePointerCapture(e.pointerId);
  });
  btnSimBrakeEl.addEventListener("pointerleave", () => {});
  btnSimBrakeEl.addEventListener("lostpointercapture", () => setSimBrake(false));
  btnSimBrakeEl.addEventListener("pointercancel", () => setSimBrake(false));
  btnSimBrakeEl.addEventListener("contextmenu", (e) => e.preventDefault());
}

document.getElementById("btnSimPedal").addEventListener("click", () => {
  simPedalActive = !simPedalActive;
  updateEffectiveStates();
  renderJoystick(); // Re-render to reflect state changes visually
});

// ================= Виртуальные кнопки света и поворотников =================
// Нажатие отправляется как "pulse": на контроллере пин прижимается на 150 мс,
// дальше штатный дебаунс отрабатывает нажатие как физическое.
let hwTurnLeftActive = false, hwTurnRightActive = false, hwLightMode = 1;
let hwDisplayTurnLeftActive = false, hwDisplayTurnRightActive = false, hwDisplayLightActive = false;
let hwDisplayHornActive = false, hwDisplayHornHideAtMs = 0;
let hwDisplayBrakeActive = false, hwDisplayBrakeHideAtMs = 0;
const LIGHT_MODE_NAMES = ["ВЫКЛ", "ДХО", "БЛИЖНИЙ", "БЛ+ДХО"];
function pressVirtualButton(btn) {
  vib();
  fetch("/api/buttons/press?btn=" + encodeURIComponent(btn) + "&state=pulse", { cache: "no-store" }).catch(() => {});
}
const btnSimTurnLEl = document.getElementById("btnSimTurnL");
const btnSimTurnREl = document.getElementById("btnSimTurnR");
const btnSimLightEl = document.getElementById("btnSimLight");
const lblSimLightEl = document.getElementById("lblSimLight");
if (btnSimTurnLEl) btnSimTurnLEl.addEventListener("click", () => pressVirtualButton("turnL"));
if (btnSimTurnREl) btnSimTurnREl.addEventListener("click", () => pressVirtualButton("turnR"));
if (btnSimLightEl) btnSimLightEl.addEventListener("click", () => pressVirtualButton("light"));
function renderSimLightButtons() {
  if (btnSimTurnLEl) btnSimTurnLEl.classList.toggle("active", hwTurnLeftActive);
  if (btnSimTurnREl) btnSimTurnREl.classList.toggle("active", hwTurnRightActive);
  if (btnSimLightEl) btnSimLightEl.classList.toggle("active", hwLightMode !== 0);
  if (lblSimLightEl) lblSimLightEl.innerText = LIGHT_MODE_NAMES[hwLightMode] || "Свет";
}

// Initial setup and interval
// Set initial effective states
updateEffectiveStates();
// Set initial slider value and label
if (sliderGas && lblGas) {
  lblGas.innerText = simGasPct + "%";
  sliderGas.value = simGasPct;
}

function renderJoystick() {
  const modeEl = document.getElementById("joyMode");
  const valEl = document.getElementById("joyVal");
  const statusEl = document.getElementById("joyStatus");
  const okBtn = document.getElementById("btnOk");

  modeEl.className = "screen-mode mode-" + draftMode;
  if (draftMode === "pas") {
    modeEl.innerText = "РЕЖИМ: PAS АССИСТЕНТ";
    if (draftPasLvl === 0) {
      valEl.innerText = "ВЫКЛ (0 / " + pasMax + ")";
    } else {
      valEl.innerText = "УРОВЕНЬ " + draftPasLvl + " / " + pasMax;
    }
  } else if (draftMode === "cruise") {
    modeEl.innerText = "РЕЖИМ: КРУИЗ-КОНТРОЛЬ";
    if (draftCruiseLvl === 0) {
      valEl.innerText = "ВЫКЛ (0 / " + cruiseMax + ")";
    } else {
      valEl.innerText = "УРОВЕНЬ " + draftCruiseLvl + " / " + cruiseMax;
    }
  } else if (draftMode === "settings") {
    const cur = SETTINGS_MENU[draftSettingIdx];
    modeEl.innerText = "НАСТРОЙКИ: " + cur.name;
    if (draftSettingEditing) {
      valEl.innerText = "РЕДАКТИРОВАНИЕ: " + cur.val.toFixed(2) + cur.unit;
    } else {
      valEl.innerText = "ЗНАЧЕНИЕ: " + cur.val.toFixed(2) + cur.unit + " (OK - правка)";
    }
  }

  let modified = false;
  if (draftMode === "settings") {
    modified = draftSettingEditing;
  } else if (activeMode === "off") {
    let draftLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
    modified = (draftLvl !== 0);
  } else if (draftMode !== activeMode) {
    modified = true;
  } else if (draftMode === "pas" && draftPasLvl !== activePasLvl) {
    modified = true;
  } else if (draftMode === "cruise" && draftCruiseLvl !== activeCruiseLvl) {
    modified = true;
  }

  isDirty = modified;
  if (draftMode === "settings") {
    if (draftSettingEditing) {
      statusEl.innerText = "НАЖМИТЕ OK ДЛЯ СОХРАНЕНИЯ ЗНАЧЕНИЯ";
      statusEl.className = "screen-status dirty";
      okBtn.className = "dpad-btn btn-ok dirty-pulse";
    } else {
      statusEl.innerText = "UP/DOWN: ПУНКТ, OK: ИЗМЕНИТЬ";
      statusEl.className = "screen-status";
      okBtn.className = "dpad-btn btn-ok";
    }
  } else if (isDirty) {
    statusEl.innerText = "НАЖМИТЕ OK ДЛЯ ПРИМЕНЕНИЯ";
    statusEl.className = "screen-status dirty";
    okBtn.className = "dpad-btn btn-ok dirty-pulse";
  } else {
    let curLvl = (activeMode === "pas") ? activePasLvl : ((activeMode === "cruise") ? activeCruiseLvl : 0);
    statusEl.innerText = (activeMode === "off" || curLvl === 0) ? "ОБЫЧНАЯ ЕЗДА (БЕЗ МОТОРА)" : "АКТИВНО И ПРИМЕНЕНО";
    statusEl.className = "screen-status";
    okBtn.className = "dpad-btn btn-ok";
  }
}

function toggleMode(dir = 1) {
  vib();
  userInteractingUntil = Date.now() + 4000;
  const modes = ["pas", "cruise", "settings"];
  let curIdx = modes.indexOf(draftMode);
  if (curIdx === -1) curIdx = 0;
  if (dir > 0) {
    curIdx = (curIdx + 1) % modes.length;
  } else {
    curIdx = (curIdx - 1 + modes.length) % modes.length;
  }
  draftMode = modes[curIdx];
  draftSettingEditing = false;
  renderJoystick();
}

document.getElementById("btnLeft").addEventListener("click", () => toggleMode(-1));
document.getElementById("btnRight").addEventListener("click", () => toggleMode(1));

document.getElementById("btnUp").addEventListener("click", () => {
  vib();
  userInteractingUntil = Date.now() + 4000;
  if (draftMode === "settings") {
    const cur = SETTINGS_MENU[draftSettingIdx];
    if (draftSettingEditing) {
      cur.val = Math.min(cur.max, +(cur.val + cur.step).toFixed(2));
    } else {
      if (draftSettingIdx > 0) draftSettingIdx--;
      else draftSettingIdx = SETTINGS_MENU.length - 1;
    }
  } else {
    let curLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
    let maxLvl = (draftMode === "pas") ? pasMax : cruiseMax;
    if (curLvl < maxLvl) {
      if (draftMode === "pas") draftPasLvl++;
      else if (draftMode === "cruise") draftCruiseLvl++;
      arrowBounceUntil = Date.now() + 500;
      arrowBounceDir = 1;
    }
  }
  renderJoystick();
});

document.getElementById("btnDown").addEventListener("click", () => {
  vib();
  userInteractingUntil = Date.now() + 4000;
  if (draftMode === "settings") {
    const cur = SETTINGS_MENU[draftSettingIdx];
    if (draftSettingEditing) {
      cur.val = Math.max(cur.min, +(cur.val - cur.step).toFixed(2));
    } else {
      if (draftSettingIdx < SETTINGS_MENU.length - 1) draftSettingIdx++;
      else draftSettingIdx = 0;
    }
  } else {
    let curLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
    if (curLvl > 0) {
      if (draftMode === "pas") draftPasLvl--;
      else if (draftMode === "cruise") draftCruiseLvl--;
      arrowBounceUntil = Date.now() + 500;
      arrowBounceDir = -1;
    }
  }
  renderJoystick();
});

function applyJoystickDraft(event) {
  if (event) {
    event.preventDefault();
    event.stopPropagation();
  }
  if (applyInProgress) return;

  vib();
  if (navigator.vibrate) navigator.vibrate([40, 30, 40]);
  if (draftMode === "settings") {
    draftSettingEditing = !draftSettingEditing;
    renderJoystick();
    return;
  }

  let targetMode = draftMode;
  let targetLvl = (draftMode === "pas") ? draftPasLvl : draftCruiseLvl;
  let requestMode = targetLvl === 0 ? "off" : targetMode;
  const previousActiveMode = activeMode;
  const previousActivePasLvl = activePasLvl;
  const previousActiveCruiseLvl = activeCruiseLvl;

  applyInProgress = true;
  applyInProgressUntil = Date.now() + 2000;

  if (targetLvl === 0) {
    activeMode = "off";
    activePasLvl = 0;
    activeCruiseLvl = 0;
  } else {
    activeMode = targetMode;
    if (targetMode === "pas") {
      activePasLvl = targetLvl;
      activeCruiseLvl = 0;
    } else if (targetMode === "cruise") {
      activeCruiseLvl = targetLvl;
      activePasLvl = 0;
    }
  }

  if (targetMode === "cruise" && targetLvl > 0) {
    if (cfgCruiseConfirmThrottle) {
      // Точно как в handleApiJoystickApply: armCruisePending(true, 0.0f)
      simCruiseEngaged = false;
      simCruisePendingResume = true;
      simCruiseConfirmRequired = true;
      simCruiseReleaseSeen = true; // прошивка вызывает armCruisePending(true, 0.0) → газ = 0
    } else {
      // Точно как в handleApiJoystickApply: cruiseEngaged = true
      simCruiseEngaged = true;
      simCruisePendingResume = false;
      simCruiseConfirmRequired = false;
    }
  }

  draftMode = (activeMode === "off") ? targetMode : activeMode;
  draftPasLvl = activePasLvl;
  draftCruiseLvl = activeCruiseLvl;
  isDirty = false;
  renderJoystick();

  fetch("/api/joystick/apply?mode=" + encodeURIComponent(requestMode) + "&level=" + targetLvl)
    .then(res => {
      if (!res.ok) throw new Error("HTTP " + res.status);
      return new Promise(resolve => setTimeout(resolve, 300));
    })
    .then(() => {
      applyInProgress = false;
      applyInProgressUntil = 0;
      refreshHubData(true);
    })
    .catch(err => {
      console.warn("Apply mode error:", err);
      applyInProgress = false;
      applyInProgressUntil = 0;
      activeMode = previousActiveMode;
      activePasLvl = previousActivePasLvl;
      activeCruiseLvl = previousActiveCruiseLvl;
      draftMode = targetMode;
      draftPasLvl = (targetMode === "pas") ? targetLvl : 0;
      draftCruiseLvl = (targetMode === "cruise") ? targetLvl : 0;
      renderJoystick();
      const statusEl = document.getElementById("joyStatus");
      if (statusEl) {
        statusEl.innerText = "ОШИБКА ПРИМЕНЕНИЯ — НАЖМИТЕ OK ЕЩЁ РАЗ: " + err.message;
        statusEl.className = "screen-status dirty";
      }
    });
}

document.getElementById("btnOk").addEventListener("click", applyJoystickDraft, false);

// Remove the duplicate function definition

document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible") {
    if (!applyInProgress) {
      applyInProgressUntil = 0;
      refreshHubData(true);
    }
    if (typeof updateSysStatus === "function") updateSysStatus();
  }
});

window.addEventListener("focus", () => {
  if (!applyInProgress) {
    applyInProgressUntil = 0;
    refreshHubData(true);
  }
  if (typeof updateSysStatus === "function") updateSysStatus();
});

renderJoystick();
</script>
</body>
</html>)rawliteral";

PGM_P getEmulationApp() { return part; }
size_t getEmulationAppLen() { return sizeof(part) - 1; }
