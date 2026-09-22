#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <Arduino.h>

// ================= GPIO Capabilities (ESP32-WROOM) =================
bool gpioExists(int g);
bool gpioInputOnly(int g);
bool gpioHasAdc(int g);
bool gpioIsAdc2(int g);
bool gpioHasDac(int g);
bool gpioIsFlash(int g);
bool gpioIsStrap(int g);
bool gpioIsUart(int g);

// ================= Pin Configuration =================
#define USER_LABEL_SIZE 65
#define CUSTOM_PIN_MAX 8
#define PIN_ROLE_COUNT 15

struct PinConfig {
  int16_t throttleAdc;
  int16_t throttleDac;
  int16_t brake;
  int16_t pasSensor;
  int16_t pasButton;
  int16_t headlight;
  int16_t drl;
  int16_t turnLeft;
  int16_t turnRight;
  int16_t horn;
  int16_t buzzer;
  int16_t btnHeadlight;
  int16_t btnTurnLeft;
  int16_t btnTurnRight;
  int16_t btnHorn;
};

struct PinRole {
  const char *key;
  const char *title;
  bool output;
  bool adc;
  bool dac;
  bool pullup;
  bool pwm;
};

enum CustomPinMode : uint8_t {
  CUSTOM_PIN_INPUT = 0,
  CUSTOM_PIN_INPUT_PULLUP = 1,
  CUSTOM_PIN_OUTPUT = 2
};

struct CustomPinRole {
  uint8_t used;
  uint8_t mode;
  int16_t gpio;
  char name[USER_LABEL_SIZE];
};

// Constants
extern const PinRole pinRoles[PIN_ROLE_COUNT];
extern const PinConfig PIN_CONFIG_DEFAULTS;

// State
extern PinConfig pinConfig;
extern bool pinConfigCustom;
extern char pinRoleNames[PIN_ROLE_COUNT][USER_LABEL_SIZE];
extern CustomPinRole customPins[CUSTOM_PIN_MAX];

// ================= Рабочие пины GPIO =================
// Определения — в hardware_config.cpp, рабочие значения задаёт applyPinConfig().
extern int THROTTLE_ADC_PIN;
extern int THROTTLE_DAC_PIN;
extern int BRAKE_PIN;
extern int PAS_SENSOR_PIN;
extern int PAS_BUTTON_PIN;
extern int HEADLIGHT_PIN;
extern int DRL_PIN;
extern int TURN_LEFT_PIN;
extern int TURN_RIGHT_PIN;
extern int HORN_PIN;
extern int BUZZER_PIN;
extern int BTN_HEADLIGHT_PIN;
extern int BTN_TURN_LEFT_PIN;
extern int BTN_TURN_RIGHT_PIN;
extern int BTN_HORN_PIN;

// Functions
String customPinModeName(uint8_t mode);
String pinRoleName(int i);
int16_t& pinField(PinConfig &c, int i);
String validatePinConfig(PinConfig &c, String *warnings = nullptr);
void applyPinConfig();

#endif // HARDWARE_CONFIG_H
