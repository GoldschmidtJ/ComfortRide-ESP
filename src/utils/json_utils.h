#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <Arduino.h>

// ================= Безопасный разбор импортируемых настроек =================
// Импортируемый файл приходит от пользователя, поэтому все строки и числа
// разбираются с проверкой границ: отсутствующая запятая, кавычка или '}'
// не должны приводить к substring(-1) или выходу за границы массивов.

// Лимит размера импортируемого файла настроек
extern const size_t SETTINGS_IMPORT_MAX_BYTES; // 16384

// Число после key (собирает цифры/знаки/точку/экспоненту), ok=false если не найдено
double jsonNumberAfter(const String &src, const char *key, bool &ok);

// Целое число после key (округление double)
int jsonIntAfter(const String &src, const char *key, bool &ok);

// Строка в кавычках после key с разбором escape-последовательностей
String jsonStringAfter(const String &src, const char *key, bool &ok);

// Содержимое JSON-объекта после key (между сбалансированными { })
bool extractJsonObject(const String &src, const char *key, String &out);

// Читает "pct":[...] в массив float, обрезая значения до 0..100.
// Возвращает количество разобранных значений.
int readPercentArray(const String &src, float *dst, int maxCount);

// Обёртка над float-перегрузкой для целочисленных массивов (PAS)
int readPercentArray(const String &src, int *dst, int maxCount);

#endif // JSON_UTILS_H