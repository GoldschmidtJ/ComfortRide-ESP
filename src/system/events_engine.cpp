#include "system/events_engine.h"
#include "core/lights.h"
#include "core/pas.h"
#include "core/cruise.h"
#include "system/inputs.h"
#include <Arduino.h>

// ================= External Dependencies =================
// Service mode
extern bool serviceModeActive;
extern int serviceThrottleLimitPct;

// Forward declarations for peripherals (buzzer, horn, display)
void buzzerClick(unsigned long durationMs);
void buzzerPattern(uint8_t count);
void hornBeep(unsigned long durationMs);
void displayShowHorn(unsigned long durationMs);
void displayShowBrake(unsigned long durationMs);
int emuButtonRead(int pin);

// ================= State Variables =================
const EventRule EVENT_DEFAULTS[EVENT_MAX_RULES] = {
  // Slot 0: Brake (5x presses = service mode) + brake icon
  {1, EV_BRAKE_PRESS, EV_PRESS_COUNT, 0, 5, 700, {EV_SERVICE_TOGGLE, EV_DISPLAY_BRAKE}, {30, 1000}},
  
  // Slot 1: Left turn signal → toggle
  {1, EV_BTN_TURN_L, EV_NONE, 1, 1, 0, {EV_TURN_L_TOGGLE}, {0}},
  
  // Slot 2: Right turn signal → toggle
  {1, EV_BTN_TURN_R, EV_NONE, 1, 1, 0, {EV_TURN_R_TOGGLE}, {0}},
  
  // Slot 3: Headlight (cycle modes: OFF → DRL → LOW → FULL → OFF)
  {1, EV_BTN_HEADLIGHT, EV_NONE, 2, 1, 0, {EV_LIGHT_CYCLE_UP}, {0}},
  
  // Slot 4: Horn → beep 300ms + icon 300ms
  {1, EV_BTN_HORN, EV_NONE, 3, 1, 0, {EV_HORN_BEEP, EV_DISPLAY_HORN}, {300, 300}},
};

EventRule eventRules[EVENT_MAX_RULES];
char eventRuleNames[EVENT_MAX_RULES][USER_LABEL_SIZE] = {};
EventRuntime eventRuntime[EVENT_MAX_RULES];
EventLog eventLog[10];
int eventLogHead = 0;
int eventLogCount = 0;

// ================= Functions =================

String eventRuleName(int i) {
  return eventRuleNames[i][0] ? String(eventRuleNames[i]) : "Правило " + String(i + 1);
}

bool isSystemRule(int slot) {
  return slot >= 0 && slot <= 4;
}

void enforceSystemRules(EventRule &r, int slot, bool forceActionsUpdate) {
  if (!isSystemRule(slot)) return;
  const EventRule &def = EVENT_DEFAULTS[slot];
  r.enabled = 1;
  r.trigger = def.trigger;
  r.condition = def.condition;
  r.count = def.count;
  r.intervalMs = def.intervalMs;
  r.priority = def.priority;
  
  if (forceActionsUpdate) {
    memcpy(r.actions, def.actions, sizeof(r.actions));
    memcpy(r.actionValues, def.actionValues, sizeof(r.actionValues));
  }
}

void eventLogAdd(uint8_t i) {
  eventLog[eventLogHead] = {(unsigned long)millis(), i};
  eventLogHead = (eventLogHead + 1) % 10;
  if (eventLogCount < 10) eventLogCount++;
}

void serviceModeApply(bool on) {
  if (serviceModeActive == on) return;
  serviceModeActive = on;
  
  if (on) {
    if (pasCurrentLevel > 1) pasCurrentLevel = 1;
    cruiseEnabled = false;
    cruiseCurrentLevel = 0;
    cruiseEngaged = false;
    cruisePendingResume = false;
    buzzerPattern(3);
  } else {
    buzzerPattern(2);
  }
}

void eventExecute(uint8_t action, int16_t value) {
  switch (action) {
    case EV_SERVICE_TOGGLE: serviceModeApply(!serviceModeActive); break;
    case EV_SERVICE_ON: serviceModeApply(true); break;
    case EV_SERVICE_OFF: serviceModeApply(false); break;
    case EV_LIGHT_TOGGLE: toggleHeadlight(); break;
    case EV_DRL_TOGGLE: toggleDrl(); break;
    case EV_TURN_L_TOGGLE: toggleTurnLeft(); break;
    case EV_TURN_R_TOGGLE: toggleTurnRight(); break;
    case EV_HORN_BEEP: hornBeep(value > 0 ? value : 300); break;
    case EV_BUZZER_BEEP: buzzerClick(value > 0 ? value : 150); break;
    case EV_PAS_SET_LEVEL:
      pasCurrentLevel = constrain(value, 0, pasLevelsCount);
      pasEnabled = pasCurrentLevel > 0;
      break;
    case EV_PAS_TOGGLE:
      pasEnabled = !pasEnabled;
      if (!pasEnabled) pasCurrentLevel = 0;
      else if (!pasCurrentLevel) pasCurrentLevel = 1;
      break;
    case EV_LIGHT_BLINK: toggleLightBlink(true, value > 0 ? value : 500); break;
    case EV_DRL_BLINK: toggleLightBlink(false, value > 0 ? value : 500); break;
    case EV_LIGHT_SET_MODE: setLightMode(constrain(value, 0, 3)); break;
    case EV_LIGHT_CYCLE_UP: cycleLightModeUp(); break;
    case EV_DISPLAY_HORN: displayShowHorn(value > 0 ? value : 300); break;
    case EV_DISPLAY_BRAKE: displayShowBrake(value > 0 ? value : 1000); break;
    default: break;
  }
}

void eventFire(int i) {
  unsigned long n = millis();
  if (n - eventRuntime[i].lastFire < 50) return;
  eventRuntime[i].lastFire = n;
  
  for (int k = 0; k < EVENT_MAX_ACTIONS; k++) {
    if (eventRules[i].actions[k] != EV_NO_ACTION) {
      eventExecute(eventRules[i].actions[k], eventRules[i].actionValues[k]);
    }
  }
  
  eventLogAdd(i);
}

static void eventHandlePressEdge(int i, unsigned long n) {
  EventRule &r = eventRules[i];
  if (r.condition == EV_HOLD_MS) return;
  
  if (r.condition == EV_PRESS_COUNT) {
    if (n - eventRuntime[i].lastPress > r.intervalMs) eventRuntime[i].count = 0;
    eventRuntime[i].lastPress = n;
    eventRuntime[i].count++;
    if (eventRuntime[i].count >= r.count) {
      eventFire(i);
      eventRuntime[i].count = 0;
    }
  } else {
    eventFire(i);
  }
}

static void eventHandleHold(int i, unsigned long n, bool active, unsigned long activeSince) {
  EventRule &r = eventRules[i];
  if (r.condition != EV_HOLD_MS) return;
  
  if (active) {
    if (!eventRuntime[i].holdFired && n - activeSince >= r.intervalMs) {
      eventRuntime[i].holdFired = true;
      eventFire(i);
    }
  } else {
    eventRuntime[i].holdFired = false;
  }
}

static void eventFireTrigger(uint8_t trig) {
  for (int i = 0; i < EVENT_MAX_RULES; i++) {
    EventRule &r = eventRules[i];
    if (r.enabled && r.trigger == trig && r.condition == EV_NONE) eventFire(i);
  }
}

void updateEventEngine() {
  static bool lastBrake = false, booted = false, stateInitialized = false;
  static int lastPasLevel = 0;
  static bool lastPasEnabled = false, lastCruiseEnabled = false;
  static unsigned long down = 0;
  
  // Button pins array (updated each cycle to reflect current pin config)
  extern int BTN_HEADLIGHT_PIN, BTN_TURN_LEFT_PIN, BTN_TURN_RIGHT_PIN, BTN_HORN_PIN, PAS_BUTTON_PIN;
  const int btnPins[5] = {BTN_HEADLIGHT_PIN, BTN_TURN_LEFT_PIN, BTN_TURN_RIGHT_PIN, BTN_HORN_PIN, PAS_BUTTON_PIN};
  
  static int btnLast[5] = {HIGH, HIGH, HIGH, HIGH, HIGH};
  static int btnStable[5] = {HIGH, HIGH, HIGH, HIGH, HIGH};
  static bool btnPrevActive[5] = {false, false, false, false, false};
  static unsigned long btnDeb[5] = {0, 0, 0, 0, 0};
  unsigned long n = millis();

  // Boot event fires once
  if (!booted) {
    booted = true;
    eventFireTrigger(EV_BOOT);
  }

  // --- Brake ---
  bool brake = isBrakePressed();
  if (brake && !lastBrake) {
    down = n;
    for (int i = 0; i < EVENT_MAX_RULES; i++) {
      EventRule &r = eventRules[i];
      if (r.enabled && r.trigger == EV_BRAKE_PRESS) eventHandlePressEdge(i, n);
    }
  }
  if (!brake && lastBrake) eventFireTrigger(EV_BRAKE_RELEASE);
  
  for (int i = 0; i < EVENT_MAX_RULES; i++) {
    EventRule &r = eventRules[i];
    if (r.enabled && r.trigger == EV_BRAKE_HOLD) eventHandleHold(i, n, brake, down);
  }
  lastBrake = brake;

  // --- Physical buttons (headlight, turns, horn, PAS) with debounce ---
  for (int b = 0; b < 5; b++) {
    uint8_t trig = EV_BTN_HEADLIGHT + b;
    int reading = emuButtonRead(btnPins[b]);
    if (reading != btnLast[b]) btnDeb[b] = n;
    
    bool active = (btnStable[b] == LOW);
    if (n - btnDeb[b] > EVENT_DEBOUNCE_MS && reading != btnStable[b]) {
      btnStable[b] = reading;
      active = (reading == LOW);
      
      if (active) {
        for (int i = 0; i < EVENT_MAX_RULES; i++) {
          EventRule &r = eventRules[i];
          if (r.enabled && r.trigger == trig) eventHandlePressEdge(i, n);
        }
      }
    }
    btnLast[b] = reading;
    
    for (int i = 0; i < EVENT_MAX_RULES; i++) {
      EventRule &r = eventRules[i];
      if (r.enabled && r.trigger == trig) eventHandleHold(i, n, active, btnDeb[b]);
    }
    btnPrevActive[b] = active;
  }

  // Don't treat initial state as enable/disable event
  if (!stateInitialized) {
    lastPasLevel = pasCurrentLevel;
    lastPasEnabled = pasEnabled;
    lastCruiseEnabled = cruiseEnabled;
    stateInitialized = true;
  }

  // --- PAS: level, on, off ---
  if (pasCurrentLevel != lastPasLevel) {
    int lvl = pasCurrentLevel;
    lastPasLevel = lvl;
    for (int i = 0; i < EVENT_MAX_RULES; i++) {
      EventRule &r = eventRules[i];
      if (r.enabled && r.trigger == EV_PAS_LEVEL && (int)r.count == lvl && r.condition == EV_NONE) {
        eventFire(i);
      }
    }
  }
  if (pasEnabled && !lastPasEnabled) eventFireTrigger(EV_PAS_ON);
  if (!pasEnabled && lastPasEnabled) eventFireTrigger(EV_PAS_OFF);
  lastPasEnabled = pasEnabled;

  // --- Cruise: on, off ---
  if (cruiseEnabled && !lastCruiseEnabled) eventFireTrigger(EV_CRUISE_ON);
  if (!cruiseEnabled && lastCruiseEnabled) eventFireTrigger(EV_CRUISE_OFF);
  lastCruiseEnabled = cruiseEnabled;
}
