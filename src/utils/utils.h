#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

#define USER_LABEL_SIZE 65

// String utilities
String htmlEscape(const String &value);
String jsonEscape(const String &value);
bool normalizeUserLabel(String &value);
void setUserLabel(char *dest, const String &value);

#endif // UTILS_H
