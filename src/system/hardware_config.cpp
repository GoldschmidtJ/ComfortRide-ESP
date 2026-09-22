#include "system/hardware_config.h"
#include "core/lights.h"
#include "core/pas.h"
#include "core/throttle.h"
#include "system/inputs.h"

// ================= Рабочие пины GPIO (определения) =================
// Значения по умолчанию; перезаписываются из NVS в applyPinConfig().
int THROTTLE_ADC_PIN   = 34;
int THROTTLE_DAC_PIN   = 25;
int BRAKE_PIN          = 27;
int PAS_SENSOR_PIN     = 14;
int PAS_BUTTON_PIN     = 13;
int HEADLIGHT_PIN      = 18;
int DRL_PIN            = 19;
int TURN_LEFT_PIN      = 21;
int TURN_RIGHT_PIN     = 22;
int HORN_PIN           = 23;
int BUZZER_PIN         = 4;
int BTN_HEADLIGHT_PIN  = 16;
int BTN_TURN_LEFT_PIN  = 17;
int BTN_TURN_RIGHT_PIN = 32;
int BTN_HORN_PIN       = 33;

// ================= GPIO Capabilities (ESP32-WROOM) =================
bool gpioExists(int g)    { return (g >= 0 && g <= 19) || (g >= 21 && g <= 23) || (g >= 25 && g <= 27) || (g >= 32 && g <= 39); }
bool gpioInputOnly(int g) { return g >= 34 && g <= 39; }
bool gpioHasAdc(int g)    { return g == 0 || g == 2 || g == 4 || (g >= 12 && g <= 15) || (g >= 25 && g <= 27) || (g >= 32 && g <= 39); }
bool gpioIsAdc2(int g)    { return g == 0 || g == 2 || g == 4 || (g >= 12 && g <= 15) || (g >= 25 && g <= 27); }
bool gpioHasDac(int g)    { return g == 25 || g == 26; }
bool gpioIsFlash(int g)   { return g >= 6 && g <= 11; }
bool gpioIsStrap(int g)   { return g == 0 || g == 2 || g == 5 || g == 12 || g == 15; }
bool gpioIsUart(int g)    { return g == 1 || g == 3; }

// ================= Pin Roles =================
const PinRole pinRoles[PIN_ROLE_COUNT] = {
  {"tAdc",  "Вход газа",           false, true,  false, false, false},
  {"tDac",  "Выход газа (ЦАП)",    true,  false, true,  false, false},
  {"brake", "Тормоз",              false, false, false, true,  false},
  {"pas",   "PAS-датчик",          false, false, false, true,  false},
  {"pasB",  "Кнопка PAS",          false, false, false, true,  false},
  {"light", "Фара",                true,  false, false, false, true},
  {"drl",   "ДХО",                 true,  false, false, false, true},
  {"turnL", "Поворотник левый",    true,  false, false, false, false},
  {"turnR", "Поворотник правый",   true,  false, false, false, false},
  {"horn",  "Гудок",               true,  false, false, false, false},
  {"buzz",  "Пищалка",             true,  false, false, false, false},
  {"bLgt",  "Кнопка фары",         false, false, false, true,  false},
  {"bTL",   "Кнопка пов. влево",   false, false, false, true,  false},
  {"bTR",   "Кнопка пов. вправо",  false, false, false, true,  false},
  {"bHrn",  "Кнопка гудка",        false, false, false, true,  false},
};

const PinConfig PIN_CONFIG_DEFAULTS = {34, 25, 27, 14, 13, 18, 19, 21, 22, 23, 4, 16, 17, 32, 33};

// ================= State Variables =================
PinConfig pinConfig = PIN_CONFIG_DEFAULTS;
bool pinConfigCustom = false;
char pinRoleNames[PIN_ROLE_COUNT][USER_LABEL_SIZE] = {};

#define CUSTOM_PIN_MAX 8
CustomPinRole customPins[CUSTOM_PIN_MAX] = {};

// ================= Functions =================

String customPinModeName(uint8_t mode) {
  if (mode == CUSTOM_PIN_INPUT_PULLUP) return "вход + подтяжка";
  if (mode == CUSTOM_PIN_OUTPUT) return "выход";
  return "вход";
}

String pinRoleName(int i) {
  return pinRoleNames[i][0] ? String(pinRoleNames[i]) : String(pinRoles[i].title);
}

int16_t& pinField(PinConfig &c, int i) {
  switch (i) {
    case 0:  return c.throttleAdc;
    case 1:  return c.throttleDac;
    case 2:  return c.brake;
    case 3:  return c.pasSensor;
    case 4:  return c.pasButton;
    case 5:  return c.headlight;
    case 6:  return c.drl;
    case 7:  return c.turnLeft;
    case 8:  return c.turnRight;
    case 9:  return c.horn;
    case 10: return c.buzzer;
    case 11: return c.btnHeadlight;
    case 12: return c.btnTurnLeft;
    case 13: return c.btnTurnRight;
    default: return c.btnHorn;
  }
}

String validatePinConfig(PinConfig &c, String *warnings) {
  String errors = "";
  if (warnings) *warnings = "";
  
  for (int i = 0; i < PIN_ROLE_COUNT; i++) {
    int g = pinField(c, i);
    const PinRole &r = pinRoles[i];
    String where = pinRoleName(i) + " (GPIO" + String(g) + "): ";
    
    if (!gpioExists(g)) { errors += where + "пин не существует\n"; continue; }
    if (gpioIsFlash(g)) { errors += where + "занят SPI Flash\n"; continue; }
    if (gpioIsUart(g))  { errors += where + "занят UART0 (USB/отладка)\n"; continue; }
    if (r.output && gpioInputOnly(g)) { errors += where + "пин работает только на вход\n"; continue; }
    if (r.pullup && gpioInputOnly(g)) { errors += where + "нет внутренней подтяжки (нужен внешний резистор)\n"; continue; }
    if (r.adc && !gpioHasAdc(g)) { errors += where + "на пине нет АЦП\n"; continue; }
    if (r.dac && !gpioHasDac(g)) { errors += where + "ЦАП есть только на GPIO25/GPIO26\n"; continue; }
    
    if (warnings) {
      if (gpioIsStrap(g)) *warnings += where + "страппинг-пин — влияет на режим загрузки платы\n";
      if (r.adc && gpioIsAdc2(g)) *warnings += where + "ADC2 — нестабилен при активном Wi-Fi\n";
    }
  }
  
  // Check for duplicates
  for (int i = 0; i < PIN_ROLE_COUNT; i++)
    for (int j = i + 1; j < PIN_ROLE_COUNT; j++)
      if (pinField(c, i) == pinField(c, j))
        errors += pinRoleName(j) + " и " + pinRoleName(i) + " на одном пине (GPIO" + String(pinField(c, i)) + ")\n";
  
  return errors;
}

void applyPinConfig() {
  THROTTLE_ADC_PIN   = pinConfig.throttleAdc;
  THROTTLE_DAC_PIN   = pinConfig.throttleDac;
  BRAKE_PIN          = pinConfig.brake;
  PAS_SENSOR_PIN     = pinConfig.pasSensor;
  PAS_BUTTON_PIN     = pinConfig.pasButton;
  HEADLIGHT_PIN      = pinConfig.headlight;
  DRL_PIN            = pinConfig.drl;
  TURN_LEFT_PIN      = pinConfig.turnLeft;
  TURN_RIGHT_PIN     = pinConfig.turnRight;
  HORN_PIN           = pinConfig.horn;
  BUZZER_PIN         = pinConfig.buzzer;
  BTN_HEADLIGHT_PIN  = pinConfig.btnHeadlight;
  BTN_TURN_LEFT_PIN  = pinConfig.btnTurnLeft;
  BTN_TURN_RIGHT_PIN = pinConfig.btnTurnRight;
  BTN_HORN_PIN       = pinConfig.btnHorn;

  // Update turn pins in lights module
  lightsSetTurnPins(TURN_LEFT_PIN, TURN_RIGHT_PIN);
}
