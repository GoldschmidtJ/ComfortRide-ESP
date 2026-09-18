#include "utils/utils.h"

String htmlEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '\"') out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else out += c;
  }
  return out;
}

String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '\\' || c == '\"') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if ((uint8_t)c >= 0x20) out += c;
  }
  return out;
}

bool normalizeUserLabel(String &value) {
  value.trim();
  if (!value.length() || value.length() >= USER_LABEL_SIZE) return false;
  for (size_t i = 0; i < value.length(); i++) {
    if ((uint8_t)value[i] < 0x20 || value[i] == '<' || value[i] == '>') return false;
  }
  return true;
}

void setUserLabel(char *dest, const String &value) {
  memset(dest, 0, USER_LABEL_SIZE);
  value.substring(0, USER_LABEL_SIZE - 1).toCharArray(dest, USER_LABEL_SIZE);
}
