#include "system/inputs.h"

// ================= Конфигурация тормоза =================
bool ownBrakeCutoffEnabled = true; // Всегда включено; настройка скрыта из веб-интерфейса

// ================= Состояние дебаунсера кнопки PAS =================
static int btnLastReading = HIGH;
static int btnStable = HIGH;
static unsigned long btnLastDebounce = 0;
static const unsigned long DEBOUNCE_MS = 50;

// ================= API тормоза =================
bool isBrakePressed() {
  return digitalRead(BRAKE_PIN) == LOW;
}

// ================= API кнопки PAS =================
bool updatePasButton() {
  int reading = digitalRead(PAS_BUTTON_PIN);
  
  // Сброс таймера дебаунсинга при изменении сигнала
  if (reading != btnLastReading) {
    btnLastDebounce = millis();
  }
  
  // Проверка стабильности сигнала
  if (millis() - btnLastDebounce > DEBOUNCE_MS) {
    if (reading != btnStable) {
      btnStable = reading;
      btnLastReading = reading;
      
      // Детекция нажатия (переход HIGH → LOW)
      if (btnStable == LOW) {
        return true; // Кнопка нажата
      }
    }
  }
  
  btnLastReading = reading;
  return false; // Кнопка не нажата
}
