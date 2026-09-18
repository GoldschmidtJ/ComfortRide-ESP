#ifndef EVENTS_ENGINE_H
#define EVENTS_ENGINE_H

#include <Arduino.h>

#define EVENT_MAX_RULES 8
#define EVENT_MAX_ACTIONS 3

// ================= Event Triggers =================
enum EventTrigger : uint8_t {
  EV_BOOT = 0,
  EV_BTN_HEADLIGHT,
  EV_BTN_TURN_L,
  EV_BTN_TURN_R,
  EV_BTN_HORN,
  EV_BTN_PAS,
  EV_BRAKE_PRESS,
  EV_BRAKE_RELEASE,
  EV_BRAKE_HOLD,
  EV_PAS_ON,
  EV_PAS_OFF,
  EV_PAS_LEVEL,
  EV_CRUISE_ON,
  EV_CRUISE_OFF,
};
#define EV_TRIGGER_MAX EV_CRUISE_OFF

// ================= Event Conditions =================
enum EventCondition : uint8_t {
  EV_NONE = 0,
  EV_PRESS_COUNT,
  EV_HOLD_MS,
};

// ================= Event Actions =================
enum EventAction : uint8_t {
  EV_NO_ACTION = 0,
  EV_SERVICE_TOGGLE,
  EV_SERVICE_ON,
  EV_SERVICE_OFF,
  EV_LIGHT_TOGGLE,
  EV_DRL_TOGGLE,
  EV_TURN_L_TOGGLE,
  EV_TURN_R_TOGGLE,
  EV_HORN_BEEP,
  EV_BUZZER_BEEP,
  EV_PAS_SET_LEVEL,
  EV_PAS_TOGGLE,
  EV_LIGHT_BLINK,
  EV_DRL_BLINK,
  EV_LIGHT_SET_MODE,
  EV_LIGHT_CYCLE_UP,
  EV_DISPLAY_TURN_L,
  EV_DISPLAY_TURN_R,
  EV_DISPLAY_LIGHT,
  EV_DISPLAY_HORN,
  EV_DISPLAY_BRAKE,
};
#define EV_ACTION_MAX EV_DISPLAY_BRAKE

// ================= Event Configuration =================
#define EVENT_MAX_RULES 8
#define EVENT_MAX_ACTIONS 3
#define EVENT_DEBOUNCE_MS 40
#define EVENT_CONFIG_VERSION 3

struct EventRule {
  uint8_t enabled;
  uint8_t trigger;
  uint8_t condition;
  uint8_t priority;
  uint16_t count;
  uint32_t intervalMs;
  uint8_t actions[EVENT_MAX_ACTIONS];
  int16_t actionValues[EVENT_MAX_ACTIONS];
};

// Legacy format for migration
struct EventRuleV1 {
  uint8_t enabled;
  uint8_t trigger;
  uint8_t condition;
  uint8_t priority;
  uint16_t count;
  uint32_t intervalMs;
  uint8_t action;
  int16_t actionValue;
};

struct EventRuntime {
  uint16_t count;
  unsigned long lastPress;
  unsigned long lastFire;
  bool holdFired;
};

struct EventLog {
  unsigned long at;
  uint8_t rule;
};

// ================= State =================
#define USER_LABEL_SIZE 65

extern const EventRule EVENT_DEFAULTS[EVENT_MAX_RULES];
extern EventRule eventRules[EVENT_MAX_RULES];
extern char eventRuleNames[EVENT_MAX_RULES][USER_LABEL_SIZE];
extern EventRuntime eventRuntime[EVENT_MAX_RULES];
extern EventLog eventLog[10];
extern int eventLogHead;
extern int eventLogCount;

// ================= Functions =================
String eventRuleName(int i);
bool isSystemRule(int slot);
void enforceSystemRules(EventRule &r, int slot, bool forceActionsUpdate = false);
void eventLogAdd(uint8_t i);
void serviceModeApply(bool on);
void eventExecute(uint8_t action, int16_t value);
void eventFire(int i);
void updateEventEngine();

#endif // EVENTS_ENGINE_H
