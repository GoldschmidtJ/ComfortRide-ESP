#ifndef PERIPHERALS_H
#define PERIPHERALS_H

#include <Arduino.h>

// Buzzer, horn and temporary display indicators.
void buzzerClick(unsigned long durationMs);
void buzzerPattern(uint8_t beeps);
void updateBuzzer();

void displayShowHorn(unsigned long durationMs);
void displayShowBrake(unsigned long durationMs);
void updateDisplayIcons();

void hornBeep(unsigned long durationMs);
void updateHorn();

// Virtual button emulation used by the event engine and web handlers.
enum VirtualButtonSlot {
  VBTN_TURN_LEFT = 0,
  VBTN_TURN_RIGHT = 1,
  VBTN_LIGHT = 2,
  VBTN_COUNT = 3
};

extern volatile bool vbtnPressed[VBTN_COUNT];
extern volatile unsigned long vbtnAutoReleaseMs[VBTN_COUNT];

int emuButtonRead(int pin);
void updateVirtualButtons();

#endif // PERIPHERALS_H
