#include <Arduino.h>
#include <Preferences.h>
#include "system/storage.h"
#include "system/hardware_config.h"

// ================= NVS-хранилище (единственный экземпляр) =================
Preferences prefs;
#include "system/events_engine.h"
#include "core/throttle.h"
#include "system/inputs.h"
#include "core/pas.h"
#include "core/cruise.h"

// ================= WiFi учётные данные =================
// storedSsid/storedPass — сохранённая сеть (сверх дефолтной ssid/password из
// network.cpp). Если через веб выбрать другую сеть — она сохранится в NVS
// и будет использоваться вместо дефолтной.
String storedSsid = "";
String storedPass = "";
String storedApSsid = "BikeControllerAP";
String storedApPass = "";

// ================= Внешние переменные из main.cpp =================

// Общие константы
void enforceSystemRules(EventRule &r, int slot, bool forceActionsUpdate);
#define EVENT_CONFIG_VERSION 3

// --- WiFi ---
// (storedSsid, storedPass, storedApSsid, storedApPass объявлены в storage.h)

// ================= РЕАЛИЗАЦИЯ ФУНКЦИЙ =================

// ================= Распиновка GPIO =================

void pinSettingsSave() {
  Preferences p;
  p.begin("pins", false);
  p.putBytes("cfg", &pinConfig, sizeof(pinConfig));
  p.putBool("custom", pinConfigCustom);
  p.putBytes("names", pinRoleNames, sizeof(pinRoleNames));
  p.putBytes("extra", customPins, sizeof(customPins));
  p.end();
}

void pinSettingsLoad() {
  Preferences p;
  if (!p.begin("pins", true)) return;
  PinConfig c = PIN_CONFIG_DEFAULTS;
  size_t got = p.isKey("cfg") ? p.getBytes("cfg", &c, sizeof(c)) : 0;
  bool custom = p.getBool("custom", false);
  if (p.isKey("names") && p.getBytesLength("names") == sizeof(pinRoleNames)) 
    p.getBytes("names", pinRoleNames, sizeof(pinRoleNames));
  if (p.isKey("extra") && p.getBytesLength("extra") == sizeof(customPins)) 
    p.getBytes("extra", customPins, sizeof(customPins));
  p.end();
  if (got == sizeof(c)) {
    pinConfig = c;
    pinConfigCustom = custom;
  } else {
    pinConfig = PIN_CONFIG_DEFAULTS;
    pinConfigCustom = false;
  }
}

// ================= Газ (Throttle) =================

void throttleSettingsSave() {
  prefs.begin("throttle", false);
  prefs.putFloat("inMinV", throttleInMinV);
  prefs.putFloat("inMaxV", throttleInMaxV);
  prefs.putFloat("outMinV", throttleOutMinV);
  prefs.putFloat("outMaxV", throttleOutMaxV);
  prefs.putFloat("divRatio", throttleInputDividerRatio);
  prefs.putFloat("gain", throttleOutputGain);
  prefs.putInt("extRange", throttleExtendedRangeAllowed ? 1 : 0);
  prefs.putInt("ssEn", throttleSoftStartEnabled ? 1 : 0);
  prefs.putInt("spEn", throttleSoftStopEnabled ? 1 : 0);
  prefs.putULong("ssMs", throttleSoftStartMs);
  prefs.putULong("spMs", throttleSoftStopMs);
  prefs.putInt("brakeCut", ownBrakeCutoffEnabled ? 1 : 0);
  prefs.end();
}

void throttleSettingsLoad() {
  prefs.begin("throttle", true);
  throttleInMinV = prefs.getFloat("inMinV", 1.1f);
  throttleInMaxV = prefs.getFloat("inMaxV", 4.1f);
  throttleOutMinV = prefs.getFloat("outMinV", 1.1f);
  throttleOutMaxV = prefs.getFloat("outMaxV", 4.1f);
  throttleInputDividerRatio = prefs.getFloat("divRatio", 20.0f / 30.0f);
  throttleOutputGain = prefs.getFloat("gain", 1.33f);
  throttleExtendedRangeAllowed = prefs.getInt("extRange", 0) != 0;
  throttleSoftStartEnabled = prefs.getInt("ssEn", 0) != 0;
  throttleSoftStopEnabled = prefs.getInt("spEn", 0) != 0;
  throttleSoftStartMs = prefs.getULong("ssMs", 500);
  throttleSoftStopMs = prefs.getULong("spMs", 500);
  ownBrakeCutoffEnabled = true;
  prefs.end();
}

// ================= PAS (Педальный датчик) =================

void pasSettingsSave() {
  prefs.begin("pas", false);
  prefs.putInt("magnets", pasMagnetCount);
  prefs.putInt("edge", pasEdgeMode);
  prefs.putInt("angle", pasActivationAngle);
  prefs.putULong("timeout", pasTimeoutMs);
  prefs.putULong("stopTO", pasStopTimeoutMs);
  prefs.putInt("cnt", pasLevelsCount);
  prefs.putBytes("pct", pasLevelPercent, sizeof(pasLevelPercent));
  prefs.putInt("ssEn", pasSoftStartEnabled ? 1 : 0);
  prefs.putInt("spEn", pasSoftStopEnabled ? 1 : 0);
  prefs.putULong("ssMs", pasSoftStartMs);
  prefs.putULong("spMs", pasSoftStopMs);
  prefs.putInt("enabled", pasEnabled ? 1 : 0);
  prefs.end();
}

void pasSettingsLoad() {
  prefs.begin("pas", false);
  pasMagnetCount = prefs.getInt("magnets", 12);
  pasEdgeMode = prefs.getInt("edge", FALLING);
  pasActivationAngle = prefs.getInt("angle", 180);
  pasTimeoutMs = prefs.getULong("timeout", 350);
  pasStopTimeoutMs = prefs.getULong("stopTO", 200);
  if (pasStopTimeoutMs < 20) pasStopTimeoutMs = 20;
  pasLevelsCount = constrain(prefs.getInt("cnt", 3), 0, PAS_MAX_LEVELS);
  size_t got = prefs.isKey("pct") && prefs.getBytesLength("pct") == sizeof(pasLevelPercent)
                 ? prefs.getBytes("pct", pasLevelPercent, sizeof(pasLevelPercent)) : 0;
  pasSoftStartEnabled = prefs.getInt("ssEn", 0) != 0;
  pasSoftStopEnabled = prefs.getInt("spEn", 0) != 0;
  pasSoftStartMs = prefs.getULong("ssMs", 500);
  pasSoftStopMs = prefs.getULong("spMs", 800);
  pasEnabled = (prefs.getInt("enabled", 1) != 0);
  prefs.end();
  if (got != sizeof(pasLevelPercent)) {
    pasAutoDistribute();
  }
  for (int i = 0; i < PAS_MAX_LEVELS; i++) 
    pasLevelPercent[i] = constrain(pasLevelPercent[i], 0, 100);
  pasCurrentLevel = constrain(pasCurrentLevel, 0, pasLevelsCount);
}

// ================= Круиз-контроль =================

void cruiseSettingsSave() {
  prefs.begin("cruise", false);
  prefs.putInt("cnt", cruiseLevelsCount);
  prefs.putBytes("pct", cruiseLevelPercent, sizeof(cruiseLevelPercent));
  prefs.putFloat("stPct", cruiseStartPercent);
  prefs.putFloat("endPct", cruiseEndPercent);
  prefs.putBool("confThr", cruiseConfirmThrottleAfterStart);
  prefs.putInt("brkMode", cruiseAfterBrakingMode);
  prefs.putInt("thrMode", cruiseAfterThrottleMode);
  prefs.putInt("ssEn", cruiseSoftStartEnabled ? 1 : 0);
  prefs.putInt("spEn", cruiseSoftStopEnabled ? 1 : 0);
  prefs.putULong("ssMs", cruiseSoftStartMs);
  prefs.putULong("spMs", cruiseSoftStopMs);
  prefs.putBool("cen", cruiseEnabled);
  prefs.end();
}

void cruiseSettingsLoad() {
  prefs.begin("cruise", false);
  cruiseLevelsCount = constrain(prefs.getInt("cnt", 3), 1, CRUISE_MAX_LEVELS);
  cruiseStartPercent = constrain(round(prefs.getFloat("stPct", 20.0f)), 0.0f, 100.0f);
  cruiseEndPercent = constrain(round(prefs.getFloat("endPct", 100.0f)), 0.0f, 100.0f);
  size_t got = prefs.isKey("pct") && prefs.getBytesLength("pct") == sizeof(cruiseLevelPercent)
                 ? prefs.getBytes("pct", cruiseLevelPercent, sizeof(cruiseLevelPercent)) : 0;
  cruiseSoftStartEnabled = prefs.getInt("ssEn", 0) != 0;
  cruiseSoftStopEnabled = prefs.getInt("spEn", 0) != 0;
  cruiseEnabled = prefs.getBool("cen", false);
  cruiseSoftStartMs = prefs.getULong("ssMs", 500);
  cruiseSoftStopMs = prefs.getULong("spMs", 800);
  cruiseConfirmThrottleAfterStart = prefs.getBool("confThr", false);
  cruiseAfterBrakingMode = prefs.getInt("brkMode", 1);
  cruiseAfterThrottleMode = prefs.getInt("thrMode", 2);
  prefs.end();
  if (got != sizeof(cruiseLevelPercent)) {
    cruiseAutoDistribute();
  }
  for (int i = 0; i < CRUISE_MAX_LEVELS; i++) 
    cruiseLevelPercent[i] = constrain(cruiseLevelPercent[i], 0.0f, 100.0f);
  cruiseCurrentLevel = constrain(cruiseCurrentLevel, 0, cruiseLevelsCount);
}

// ================= События =================

bool eventSettingsSave(){
  Preferences p;
  if(!p.begin("events",false)) return false;
  size_t rulesWritten=p.putBytes("rules",eventRules,sizeof(eventRules));
  size_t namesWritten=p.putBytes("names",eventRuleNames,sizeof(eventRuleNames));
  p.putUInt("version", EVENT_CONFIG_VERSION);
  bool ok=rulesWritten==sizeof(eventRules)&&namesWritten==sizeof(eventRuleNames)&&
          p.getBytesLength("rules")==sizeof(eventRules)&&p.getBytesLength("names")==sizeof(eventRuleNames);
  p.end();
  return ok;
}

void eventSettingsLoad(){
  Preferences p;
  if(!p.begin("events",true)){memcpy(eventRules,EVENT_DEFAULTS,sizeof(eventRules));return;}
  size_t n=p.isKey("rules")?p.getBytesLength("rules"):0;
  uint32_t savedVersion = p.isKey("version") ? p.getUInt("version", 0) : 0;
  if(p.isKey("names")&&p.getBytesLength("names")==sizeof(eventRuleNames))
    p.getBytes("names",eventRuleNames,sizeof(eventRuleNames));
  
  if(n==sizeof(eventRules)){
    p.getBytes("rules",eventRules,sizeof(eventRules));
    p.end();
    
    bool needMigration = (savedVersion < EVENT_CONFIG_VERSION);
    for(int i=0;i<EVENT_MAX_RULES;i++){
      enforceSystemRules(eventRules[i], i, needMigration);
    }
    
    if(needMigration) {
      eventSettingsSave();
    }
    return;
  }
  
  if(n==sizeof(EventRuleV1)*EVENT_MAX_RULES){
    EventRuleV1 old[EVENT_MAX_RULES];
    p.getBytes("rules",old,sizeof(old));p.end();
    memset(eventRules,0,sizeof(eventRules));
    for(int i=0;i<EVENT_MAX_RULES;i++){
      EventRule &r=eventRules[i];EventRuleV1 &o=old[i];
      r.enabled=o.enabled;r.trigger=o.trigger;r.condition=o.condition;r.priority=o.priority;
      r.count=o.count;r.intervalMs=o.intervalMs;
      r.actions[0]=o.action;r.actionValues[0]=o.actionValue;
      enforceSystemRules(r,i,true);
    }
    eventSettingsSave();
    return;
  }
  p.end();
  memcpy(eventRules,EVENT_DEFAULTS,sizeof(eventRules));
}

void eventSettingsReset(){
  memcpy(eventRules,EVENT_DEFAULTS,sizeof(eventRules));
  memset(eventRuleNames,0,sizeof(eventRuleNames));
  eventSettingsSave();
}

// ================= WiFi (STA режим) =================

void wifiCredsLoad() {
  prefs.begin("wifi", false);
  if (prefs.isKey("ssid")) storedSsid = prefs.getString("ssid", "");
  if (prefs.isKey("pass")) storedPass = prefs.getString("pass", "");
  prefs.end();
}

void wifiCredsSave(const String &newSsid, const String &newPass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", newSsid);
  prefs.putString("pass", newPass);
  prefs.end();
  storedSsid = newSsid;
  storedPass = newPass;
}

// ================= WiFi (AP режим) =================

void apSettingsSave() {
  prefs.begin("ap", false);
  prefs.putString("ssid", storedApSsid);
  prefs.putString("pass", storedApPass);
  prefs.end();
}

void apSettingsLoad() {
  prefs.begin("ap", false);
  if (prefs.isKey("ssid")) storedApSsid = prefs.getString("ssid", "BikeControllerAP");
  if (prefs.isKey("pass")) storedApPass = prefs.getString("pass", "");
  prefs.end();
}
