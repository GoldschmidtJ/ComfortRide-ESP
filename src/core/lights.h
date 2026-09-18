#pragma once

#include <Arduino.h>

// ================= Константы яркости =================
extern const int HEADLIGHT_DEFAULT_BRIGHTNESS; // 0-255
extern const int DRL_DEFAULT_BRIGHTNESS;       // 0-255

// ================= Состояние освещения =================
extern bool headlightOn;
extern bool drlOn;
extern bool headlightBlink, drlBlink;
extern bool headlightBlinkState, drlBlinkState;
extern unsigned long headlightBlinkAtMs, drlBlinkAtMs;
extern unsigned long headlightBlinkMs, drlBlinkMs;
extern uint8_t lightCycleMode;

// ================= Поворотники =================
extern bool turnLeftActive, turnRightActive;
extern unsigned long turnLastBlinkMs;
extern bool turnBlinkState;

// ================= API управления светом =================

// Инициализация PWM-каналов для фары и ДХО
void lightsInit(int headlightPin, int drlPin);

// Обновление пинов поворотников (вызывается при изменении распиновки)
void lightsSetTurnPins(int turnLeftPin, int turnRightPin);

// Управление фарой
void toggleHeadlight();
void toggleDrl();

// Циклический режим света (0=Выкл, 1=ДХО, 2=Ближний, 3=Ближний+ДХО)
void applyCycleLightMode();
void cycleLightMode();
void cycleLightModeUp();
void setLightMode(uint8_t mode);
uint8_t currentLightMode();
void syncLightCycleMode();

// Мигание фары/ДХО
void toggleLightBlink(bool headlight, unsigned long intervalMs);
void updateLightBlink(); // вызывать в loop

// Поворотники
void toggleTurnLeft();
void toggleTurnRight();
void updateTurnSignals(); // вызывать в loop
