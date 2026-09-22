#include "web/html_pages_emulation.h"

// JS: boot-анимация Pac-Man (кадры, skip-обработчики, страховка по таймауту).
// Часть страницы эмулятора (этап D: разбиение matrix-части).
static const char part[] PROGMEM = R"rawliteral(function removeBootSkipListeners() {
  document.removeEventListener("keydown", handleBootSkipKeydown);
  document.removeEventListener("pointerdown", handleBootSkipPointerdown, true);
  document.removeEventListener("click", handleBootSkipClick, true);
}

function finishBootAnimation() {
  if (bootAnimationComplete) return;

  bootAnimationActive = false;
  bootAnimationComplete = true;
  if (bootAnimationTimeoutId !== null) {
    clearTimeout(bootAnimationTimeoutId);
    bootAnimationTimeoutId = null;
  }
  removeBootSkipListeners();

  // Сначала полностью убираем кадр заставки, затем рисуем первый кадр эмулятора.
  clearMatrix();
  if (ctx && canvas) ctx.clearRect(0, 0, canvas.width, canvas.height);
  updateMatrixDisplay();
}

function scheduleBootFrame(cb, delay) {
  bootAnimationTimeoutId = setTimeout(() => {
    bootAnimationTimeoutId = null;
    if (!bootAnimationActive || bootAnimationComplete) return;
    cb();
  }, delay);
}

function playBootAnimation() {
  if (bootAnimationActive || bootAnimationComplete) return;

  bootAnimationActive = true;
  let frame = 0;

  function nextFrame() {
    if (!bootAnimationActive || bootAnimationComplete) return;
    clearMatrix();

    switch(frame) {
      case 0: // Велосипедист справа
        drawBiker(4, 24, 0);
        renderCanvas();
        scheduleBootFrame(nextFrame, 500);
        break;

      case 1: // Велосипедист едет к центру влево
        drawBiker(4, 18, 1);
        renderCanvas();
        scheduleBootFrame(nextFrame, 400);
        break;

      case 2: // Велосипедист в центре, Pac-Man появляется слева (за краем, col -16)
        drawBiker(4, 12, 0);
        drawPacman(0, -10, true);
        renderCanvas();
        scheduleBootFrame(nextFrame, 300);
        break;

      case 3: // Pac-Man приближается слева (рот закрывается)
        drawBiker(4, 12, 1);
        drawPacman(0, 0, false);
        renderCanvas();
        scheduleBootFrame(nextFrame, 200);
        break;

      case 4: // Pac-Man съедает велосипедиста (рот открыт, велосипедист исчезает)
        drawPacman(0, 8, true);
        renderCanvas();
        scheduleBootFrame(nextFrame, 300);
        break;

      case 5: // Заливка экрана точками (Pac-Man в центре)
        drawPacman(0, 12, false);
        fillRandomDots(0.3);
        renderCanvas();
        scheduleBootFrame(nextFrame, 400);
        break;

      case 6: // Больше точек
        fillRandomDots(0.6);
        renderCanvas();
        scheduleBootFrame(nextFrame, 300);
        break;

      case 7: // Текст проявляется: "OPEN" (шрифт 4x5, вверху слева)
        clearMatrix();
        drawText4x5("OPEN", 0, 1);  // row 0 (самый верх), col 1 (сдвиг влево)
        renderCanvas();
        scheduleBootFrame(nextFrame, 600);
        break;

      case 8: // "BIKE" появляется (шрифт 7x10, ОГРОМНЫЙ внизу справа)
        drawText4x5("OPEN", 0, 1);
        drawText7x10("BIKE", 6, 2);  // row 6, col 2 (сдвиг вправо на 1)
        renderCanvas();
        scheduleBootFrame(finishBootAnimation, 1000);
        break;
    }

    frame++;
  }

  nextFrame();
}

function handleBootSkipKeydown() {
  finishBootAnimation();
}

function isBootScreenTarget(target) {
  return target === canvas || (target instanceof Element && target.closest("button"));
}

function handleBootSkipPointerdown(event) {
  if (isBootScreenTarget(event.target)) finishBootAnimation();
}

function handleBootSkipClick(event) {
  if (isBootScreenTarget(event.target)) finishBootAnimation();
}

// Любая клавиша или экранная кнопка пропускает заставку. Клик/тап по матрице также сохранён.
document.addEventListener("keydown", handleBootSkipKeydown, { once: true });
document.addEventListener("pointerdown", handleBootSkipPointerdown, true);
document.addEventListener("click", handleBootSkipClick, true);

// Таймаут-страховка: заставка в любом случае завершается максимум через 5 секунд.
setTimeout(() => {
  if (bootAnimationActive && !bootAnimationComplete) finishBootAnimation();
}, 5000);

)rawliteral";

PGM_P getEmulationMatrixBoot() { return part; }
size_t getEmulationMatrixBootLen() { return sizeof(part) - 1; }
