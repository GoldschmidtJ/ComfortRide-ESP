#include "utils/json_utils.h"
#include "core/cruise.h" // CRUISE_MAX_LEVELS (лимит для целочисленной перегрузки)
#include <math.h>

const size_t SETTINGS_IMPORT_MAX_BYTES = 16384;

double jsonNumberAfter(const String &src, const char *key, bool &ok) {
  ok = false;
  int p = src.indexOf(key);
  if (p == -1) return 0;
  p += strlen(key);
  int end = p;
  while (end < (int)src.length()) {
    char c = src.charAt(end);
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') { end++; continue; }
    break;
  }
  if (end == p) return 0;
  ok = true;
  return src.substring(p, end).toDouble();
}

int jsonIntAfter(const String &src, const char *key, bool &ok) {
  double v = jsonNumberAfter(src, key, ok);
  return (int)lround(v);
}

String jsonStringAfter(const String &src, const char *key, bool &ok) {
  ok = false;
  int p = src.indexOf(key);
  if (p == -1) return "";
  p += strlen(key);
  if (p >= (int)src.length() || src.charAt(p) != '"') return "";
  p++;
  String out;
  for (; p < (int)src.length(); p++) {
    char c = src.charAt(p);
    if (c == '\\') {
      if (p + 1 < (int)src.length()) {
        char n = src.charAt(p + 1);
        if (n == '"' || n == '\\' || n == '/') out += n;
        else if (n == 'n') out += '\n';
        else if (n == 'r') out += '\r';
        else if (n == 't') out += '\t';
        else out += n;
        p++;
      }
      continue;
    }
    if (c == '"') { ok = true; return out; }
    out += c;
  }
  return "";
}

bool extractJsonObject(const String &src, const char *key, String &out) {
  int keyPos = src.indexOf(key);
  if (keyPos == -1) return false;
  int open = src.indexOf('{', keyPos);
  if (open == -1) return false;
  int depth = 0;
  for (int i = open; i < (int)src.length(); i++) {
    char c = src.charAt(i);
    if (c == '{') depth++;
    else if (c == '}') {
      depth--;
      if (depth == 0) { out = src.substring(open + 1, i); return true; }
    }
  }
  return false;
}

// Читает "pct":[...] в массив float, обрезая значения до 0..100.
// Возвращает количество разобранных значений.
int readPercentArray(const String &src, float *dst, int maxCount) {
  int arrPos = src.indexOf('[');
  if (arrPos == -1) return 0;
  int arrEnd = src.indexOf(']', arrPos);
  if (arrEnd == -1) return 0;
  String body = src.substring(arrPos + 1, arrEnd);
  int count = 0;
  int pos = 0;
  while (pos <= (int)body.length() && count < maxCount) {
    int comma = body.indexOf(',', pos);
    if (comma == -1) comma = body.length();
    String item = body.substring(pos, comma);
    item.trim();
    if (item.length()) {
      bool parsed = false;
      jsonNumberAfter(String("v:") + item, "v:", parsed);
      if (parsed) dst[count++] = constrain((float)item.toDouble(), 0.0f, 100.0f);
    }
    if (comma >= (int)body.length()) break;
    pos = comma + 1;
  }
  return count;
}

// Обёртка над float-перегрузкой для целочисленных массивов (PAS).
// Лимит буфера — CRUISE_MAX_LEVELS (100), а не PAS_MAX_LEVELS (20): функция
// используется и для cruise-массива (импорт настроек круиза), у которого до
// 100 уровней. Раньше здесь стоял PAS_MAX_LEVELS и уровни круиза 21+ молча
// терялись при импорте.
int readPercentArray(const String &src, int *dst, int maxCount) {
  if (maxCount <= 0) return 0;
  if (maxCount > CRUISE_MAX_LEVELS) maxCount = CRUISE_MAX_LEVELS;
  float values[CRUISE_MAX_LEVELS];
  int count = readPercentArray(src, values, maxCount);
  for (int i = 0; i < count; i++) dst[i] = (int)lround(values[i]);
  return count;
}