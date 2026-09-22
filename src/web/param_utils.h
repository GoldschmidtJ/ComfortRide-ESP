#ifndef PARAM_UTILS_H
#define PARAM_UTILS_H

#include <WebServer.h>

// ================= Хелперы парсинга HTTP-аргументов (этап C) =================
// Семантика строго 1-в-1 с прежним кодом:
//   - String("").toInt() -> 0, String("").toFloat() -> 0.0f;
//   - отсутствующий аргумент = пустая строка, т.е. тоже 0 / 0.0f;
//   - default-логики нет: если вызывающий код различал «нет аргумента»,
//     он по-прежнему использует server.hasArg(...) сам.

inline int getArgInt(WebServer &server, const String &name) {
  return server.arg(name).toInt();
}

inline float getArgFloat(WebServer &server, const String &name) {
  return server.arg(name).toFloat();
}

// Чекбоксы: присутствие аргумента = true (форма шлёт поле только при галочке).
inline bool getArgBool(WebServer &server, const String &name) {
  return server.hasArg(name);
}

#endif // PARAM_UTILS_H
