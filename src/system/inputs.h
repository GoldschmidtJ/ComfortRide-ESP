#ifndef INPUTS_H
#define INPUTS_H

#include <Arduino.h>

// ================= Пины (из main.cpp) =================
extern int BRAKE_PIN;
extern int PAS_BUTTON_PIN;

// ================= Конфигурация тормоза =================
extern bool ownBrakeCutoffEnabled; // Включена ли отсечка газа при торможении

// ================= API тормоза =================
// Проверить, нажат ли тормоз (LOW = нажат)
bool isBrakePressed();

// ================= API кнопки PAS =================
// Обновить состояние кнопки PAS (вызывать в loop)
// Возвращает true, если кнопка была нажата (edge detection с дебаунсингом)
bool updatePasButton();

#endif // INPUTS_H
