#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>

// Модуль для работы с сетью (WiFi, mDNS, Captive Portal)

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

#endif // NETWORK_H
