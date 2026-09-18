#include "system/peripherals.h"
#include "system/hardware_config.h"

// Рабочие GPIO объявлены в main.cpp и перенастраиваются через hardware_config.
extern int HORN_PIN;
extern int BUZZER_PIN;
extern int BTN_HORN_PIN;
extern int BTN_HEADLIGHT_PIN;
extern int BTN_TURN_LEFT_PIN;
extern int BTN_TURN_RIGHT_PIN;

// ================= Buzzer =================
bool buzzerOn = false;
unsigned long buzzerOffAtMs = 0;
uint8_t buzzerPatternRemaining = 0;
unsigned long buzzerPatternNextMs = 0;

void buzzerClick(unsigned long durationMs) {
  digitalWrite(BUZZER_PIN, HIGH);
  buzzerOn = true;
  buzzerOffAtMs = millis() + durationMs;
}

void buzzerPattern(uint8_t beeps) {
  buzzerPatternRemaining = beeps;
  buzzerPatternNextMs = 0;
}

void updateBuzzer() {
  unsigned long now = millis();
  if (buzzerOn && now >= buzzerOffAtMs) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerOn = false;
    if (buzzerPatternRemaining > 0) {
      buzzerPatternRemaining--;
      if (buzzerPatternRemaining > 0) buzzerPatternNextMs = now + 120;
    }
  }
  if (!buzzerOn && buzzerPatternRemaining > 0 &&
      (buzzerPatternNextMs == 0 || now >= buzzerPatternNextMs)) {
    buzzerClick(120);
    buzzerPatternNextMs = 1;
  }
}

// ================= Temporary display indicators =================
bool displayHornActive = false;
bool displayBrakeActive = false;
unsigned long displayHornHideAtMs = 0;
unsigned long displayBrakeHideAtMs = 0;

void displayShowHorn(unsigned long durationMs) {
  displayHornActive = true;
  displayHornHideAtMs = millis() + durationMs;
}

void displayShowBrake(unsigned long durationMs) {
  displayBrakeActive = true;
  displayBrakeHideAtMs = millis() + (durationMs > 0 ? durationMs : 1000);
}

void updateDisplayIcons() {
  unsigned long now = millis();
  if (displayHornActive && now >= displayHornHideAtMs) displayHornActive = false;
  if (displayBrakeActive && now >= displayBrakeHideAtMs) displayBrakeActive = false;
}

// ================= Horn =================
unsigned long hornOverrideUntilMs = 0;

void hornBeep(unsigned long durationMs) {
  hornOverrideUntilMs = millis() + durationMs;
}

void updateHorn() {
  if (millis() < hornOverrideUntilMs) {
    digitalWrite(HORN_PIN, HIGH);
    return;
  }
  bool hornPressed = (digitalRead(BTN_HORN_PIN) == LOW);
  digitalWrite(HORN_PIN, hornPressed ? HIGH : LOW);
}

// ================= Virtual buttons =================
volatile bool vbtnPressed[VBTN_COUNT] = {false, false, false};
volatile unsigned long vbtnAutoReleaseMs[VBTN_COUNT] = {0, 0, 0};

int emuButtonRead(int pin) {
  if (pin == BTN_TURN_LEFT_PIN && vbtnPressed[VBTN_TURN_LEFT]) return LOW;
  if (pin == BTN_TURN_RIGHT_PIN && vbtnPressed[VBTN_TURN_RIGHT]) return LOW;
  if (pin == BTN_HEADLIGHT_PIN && vbtnPressed[VBTN_LIGHT]) return LOW;
  return digitalRead(pin);
}

void updateVirtualButtons() {
  unsigned long now = millis();
  for (int i = 0; i < VBTN_COUNT; i++) {
    if (vbtnPressed[i] && (long)(now - vbtnAutoReleaseMs[i]) >= 0) {
      vbtnPressed[i] = false;
    }
  }
}
