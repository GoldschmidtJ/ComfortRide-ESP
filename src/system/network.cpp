#include "system/network.h"
#include "system/storage.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>


// ================= Конфигурация WiFi =================
// ssid/password — дефолт "из коробки". Если через веб выбрать другую сеть,
// она сохранится в NVS (storage.cpp) и будет использоваться вместо дефолтной.
const char* ssid = "Donut";
const char* password = "doughnut";
const char* MDNS_HOST = "openbike";

// ================= Captive Portal DNS =================
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Статический IP для надежного подключения к точке доступа телефона (Android)
const IPAddress staticSTAIP(192, 168, 43, 88);
const IPAddress staticSTAGateway(192, 168, 43, 1);
const IPAddress staticSTASubnet(255, 255, 255, 0);
const IPAddress staticSTADNS(192, 168, 43, 1);

// ================= Внутренние переменные модуля =================

static bool mdnsStarted = false;
static unsigned long wifiConnectStartMs = 0;
static bool wifiApActive = false;
static unsigned long lastWifiRetryMs = 0;

// ================= Функции =================

void wifiConnect() {
  // Переход в STA должен немедленно снять состояние AP, иначе фоновая
  // машина состояний перестанет обрабатывать переподключение к Wi-Fi.
  wifiApActive = false;
  WiFi.softAPdisconnect(true);
  String useSsid = (storedSsid.length() > 0) ? storedSsid : String(ssid);
  String usePass = (storedSsid.length() > 0) ? storedPass : String(password);

  WiFi.mode(WIFI_STA);
  // Если подключение к хотспоту/внешней сети, настраиваем статический IP (если хотспот)
  if (useSsid.indexOf("Android") != -1 || useSsid.indexOf("Hotspot") != -1 || useSsid == "Redmi" || useSsid == "Donut") {
    WiFi.config(staticSTAIP, staticSTAGateway, staticSTASubnet, staticSTADNS);
  }

  Serial.printf("WiFi Connect -> SSID: '%s', Pass: <скрыт> (len=%d), Source: %s\n",
                useSsid.c_str(), usePass.length(),
                (storedSsid.length() > 0) ? "NVS" : "DEFAULT");
  WiFi.begin(useSsid.c_str(), usePass.c_str());
}

bool startConfiguredAp() {
  return storedApPass.length() == 0
    ? WiFi.softAP(storedApSsid.c_str())
    : WiFi.softAP(storedApSsid.c_str(), storedApPass.c_str());
}

void updateWifiStateMachine() {
  if (!wifiApActive) {
    if (WiFi.status() != WL_CONNECTED && millis() - lastWifiRetryMs > 10000) {
      lastWifiRetryMs = millis();
      if (storedSsid.length() > 0) {
        Serial.printf("WiFi Connect attempt: SSID='%s'\n", storedSsid.c_str());
        WiFi.begin(storedSsid.c_str(), storedPass.c_str());
      } else {
        Serial.println(F("Нет сохраненной WiFi сети. Запуск точки доступа AP..."));
        WiFi.mode(WIFI_AP);
        if (!startConfiguredAp()) {
          Serial.println(F("Ошибка запуска точки доступа"));
          return;
        }
        wifiApActive = true;
        Serial.printf("AP: %s, IP: http://%s\n", storedApSsid.c_str(), WiFi.softAPIP().toString().c_str());
        #ifdef ENABLE_CAPTIVE_PORTAL
        dnsServer.setErrorReplyCode(DNS_RCODE_NOERROR);
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
        #endif
      }
    } else if (WiFi.status() == WL_CONNECTED) {
      if (!mdnsStarted) {
        if (MDNS.begin(MDNS_HOST)) {
          MDNS.addService("http", "tcp", 80);
          mdnsStarted = true;
          Serial.printf("mDNS запущен: http://%s.local\n", MDNS_HOST);
        }
      }
    }
  }
}

void networkInit() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  wifiConnectStartMs = millis();
  wifiConnect();

  Serial.print("Ожидание подключения к WiFi");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 8000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Открой в браузере: http://"); Serial.println(WiFi.localIP());
    if (MDNS.begin(MDNS_HOST)) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
      Serial.printf("mDNS запущен: http://%s.local\n", MDNS_HOST);
    }
  } else {
    Serial.printf("WiFi статус при старте: %d (1=NoSSID, 4=Failed, 6=WrongPass, 7=Disconnected)\n", WiFi.status());
  }
}

bool isWifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool isApActive() {
  return wifiApActive;
}

String getWifiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    return "Подключено к " + WiFi.SSID() + " (IP: " + WiFi.localIP().toString() + ")";
  } else if ((WiFi.getMode() & WIFI_MODE_AP) != 0) {
    return "Точка доступа: " + storedApSsid + " (IP: " + WiFi.softAPIP().toString() + ")";
  }
  return "Не подключено";
}

void processCaptiveDns() {
  #ifdef ENABLE_CAPTIVE_PORTAL
  if (wifiApActive) {
    dnsServer.processNextRequest();
  }
  #endif
}
