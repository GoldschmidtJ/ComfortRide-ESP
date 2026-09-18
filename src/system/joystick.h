#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <Arduino.h>

// ================= Физический джойстик (переключение режимов) =================
// Аналог веб-эмуляции /api/joystick/apply, но с реальных GPIO:
//   SW  — кнопка нажатия: цикл OFF → PAS → КРУИЗ → OFF
//   VRy — ось Y: изменение уровня внутри режима (0 = режим выключен)
//   VRx — ось X: резерв
// Пины совпадают с layout_perfboard_corrected.py (БЛОК 6).
extern int JOYSTICK_VRX_PIN; // ось X, вход ADC1 (только вход)
extern int JOYSTICK_VRY_PIN; // ось Y, вход ADC1 (только вход)
extern int JOYSTICK_SW_PIN;  // кнопка нажатия, вход с внутренним pull-up

void joystickInit();
void updateJoystick();

#endif // JOYSTICK_H
