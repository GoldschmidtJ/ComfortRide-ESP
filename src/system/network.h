#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>

// Модуль для работы с сетью (WiFi, mDNS, Captive Portal)

// ================= mDNS =================
extern const char* MDNS_HOST; // определение в network.cpp ("openbike")

// ================= Инициализация и подключение =================
void networkInit();
void wifiConnect();
bool startConfiguredAp();

// ================= Состояние сети =================
void updateWifiStateMachine();
bool isWifiConnected();
bool isApActive();
String getWifiStatus();  // Для веб-интерфейса

// ================= Captive Portal DNS =================
void processCaptiveDns();

// ================= Статический IP для STA-режима =================
// Надежное подключение к точке доступа телефона (Android)
extern const IPAddress staticSTAIP;
extern const IPAddress staticSTAGateway;
extern const IPAddress staticSTASubnet;
extern const IPAddress staticSTADNS;

#endif // NETWORK_H
