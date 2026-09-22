#include "web/web_handlers_emulation.h"
#include "web/html_pages.h" // sendEmulationPage
#include "system/peripherals.h"
#include "core/pas.h"
#include "core/cruise.h"
#include <WebServer.h>

#include "web/param_utils.h"   // getArgInt/getArgFloat (семантика toInt/toFloat)

#include "web/web_routes.h"

void handleEmulationPage() { sendEmulationPage(server); }

void handleApiButtonPress() {
  String btn = server.hasArg("btn") ? server.arg("btn") : "";
  String state = server.hasArg("state") ? server.arg("state") : "pulse";
  int slot;
  if (btn == "turnL") slot = VBTN_TURN_LEFT;
  else if (btn == "turnR") slot = VBTN_TURN_RIGHT;
  else if (btn == "light") slot = VBTN_LIGHT;
  else { server.send(400, "text/plain", "Bad button"); return; }
  unsigned long now = millis();
  if (state == "down") {
    vbtnPressed[slot] = true;
    vbtnAutoReleaseMs[slot] = now + 10000; // страховка от залипания
  } else if (state == "up") {
    vbtnPressed[slot] = false;
  } else { // pulse — короткое нажатие
    vbtnPressed[slot] = true;
    vbtnAutoReleaseMs[slot] = now + 150;
  }
  server.send(200, "text/plain", "OK");
}
void handleApiJoystickApply() {
  if (!server.hasArg("mode")) {
    server.send(400, "text/plain", "Missing mode");
    return;
  }
  String mode = server.arg("mode");
  int level = server.hasArg("level") ? getArgInt(server, "level") : 0;

  if (mode == "pas") {
    if (level >= 0 && level <= pasLevelsCount) {
      pasCurrentLevel = level;
      pasEnabled = (level > 0);
      if (pasEnabled) {
        cruiseEnabled = false; // Взаимное исключение
        cruiseCurrentLevel = 0;
        cruiseEngaged = false;
        cruisePendingResume = false;
      }
      server.send(200, "text/plain", "OK");
      return;
    }
  } else if (mode == "cruise") {
    if (level >= 0 && level <= cruiseLevelsCount) {
      cruiseCurrentLevel = level;
      cruiseEnabled = (level > 0);
      if (cruiseEnabled) {
        pasEnabled = false; // Взаимное исключение
        pasCurrentLevel = 0;
        if (cruiseConfirmThrottleAfterStart) {
          armCruisePending(true, 0.0f);
        } else {
          cruiseEngaged = true;
          cruisePendingResume = false;
        }
      } else {
        cruiseEngaged = false;
        cruisePendingResume = false;
      }
      server.send(200, "text/plain", "OK");
      return;
    }
  } else if (mode == "off") {
    pasEnabled = false;
    pasCurrentLevel = 0;
    cruiseEnabled = false;
    cruiseCurrentLevel = 0;
    cruiseEngaged = false;
    cruisePendingResume = false;
    server.send(200, "text/plain", "OK");
    return;
  }
  server.send(400, "text/plain", "Bad Request");
}void handleApiPasSetLevel() {
  if (server.hasArg("level")) {
    int newLevel = getArgInt(server, "level");
    // Уровень 0 means PAS off, levels 1..pasLevelsCount are valid
    if (newLevel >= 0 && newLevel <= pasLevelsCount) {
      pasCurrentLevel = newLevel;
      pasEnabled = (newLevel > 0);
      // Взаимное исключение: если PAS включен, круиз отключается
      if (pasEnabled) {
        cruiseEnabled = false;
        cruiseCurrentLevel = 0;
        cruiseEngaged = false;
        cruisePendingResume = false;
      }
      server.send(200, "text/plain", "OK");
      return;
    }
  }
  server.send(400, "text/plain", "Bad Request");
}
void handleApiPasToggleMode() {
  pasEnabled = !pasEnabled;
  if (!pasEnabled) {
    pasCurrentLevel = 0;
  } else {
    if (pasCurrentLevel == 0) pasCurrentLevel = 1;
    // Взаимное исключение: если PAS включен, круиз отключается
    cruiseEnabled = false;
    cruiseCurrentLevel = 0;
    cruiseEngaged = false;
    cruisePendingResume = false;
  }
  server.send(200, "text/plain", "OK");
}
void handleApiCruiseToggleMode() {
  cruiseEnabled = !cruiseEnabled;
  // Взаимное исключение: если круиз включен, PAS отключается
  if (cruiseEnabled) {
    pasEnabled = false;
    pasCurrentLevel = 0;
    if (cruiseCurrentLevel == 0) cruiseCurrentLevel = 1;
    if (cruiseConfirmThrottleAfterStart) {
      armCruisePending(true, 0.0f);
    } else {
      cruiseEngaged = true;
      cruisePendingResume = false;
    }
  } else {
    cruiseCurrentLevel = 0;
    cruiseEngaged = false;
    cruisePendingResume = false;
  }
  server.send(200, "text/plain", "OK");
}
