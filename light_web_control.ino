#include <WiFi.h>
#include <WebServer.h>

// Домашний WiFi для теста на столе
const char* ssid = "Donut";
const char* password = "doughnut";

// Пины временные — финальную раскладку утвердим, когда дойдём до полной схемы
const int HEADLIGHT_PIN = 18;
const int DRL_PIN = 19;

const int HEADLIGHT_CHANNEL = 0;
const int DRL_CHANNEL = 1;
const int PWM_FREQ = 5000;
const int PWM_RES = 8; // бит, 0-255

WebServer server(80);

const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Свет велосипеда</title>
  <style>
    body { font-family: sans-serif; text-align: center; padding: 20px; }
    input[type=range] { width: 80%; }
    h2 { margin-top: 40px; }
  </style>
</head>
<body>
  <h1>Управление светом</h1>

  <h2>Фара</h2>
  <input type="range" min="0" max="255" value="0" id="headlight" oninput="send('headlight', this.value)">
  <p id="headlightVal">0</p>

  <h2>ДХО</h2>
  <input type="range" min="0" max="255" value="0" id="drl" oninput="send('drl', this.value)">
  <p id="drlVal">0</p>

  <script>
    function send(ch, val) {
      document.getElementById(ch + 'Val').innerText = val;
      fetch('/set?ch=' + ch + '&val=' + val);
    }
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

void handleSet() {
  if (server.hasArg("ch") && server.hasArg("val")) {
    String ch = server.arg("ch");
    int val = constrain(server.arg("val").toInt(), 0, 255);

    if (ch == "headlight") {
      ledcWrite(HEADLIGHT_CHANNEL, val);
    } else if (ch == "drl") {
      ledcWrite(DRL_CHANNEL, val);
    }
  }
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);

  ledcSetup(HEADLIGHT_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(HEADLIGHT_PIN, HEADLIGHT_CHANNEL);

  ledcSetup(DRL_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(DRL_PIN, DRL_CHANNEL);

  WiFi.begin(ssid, password);
  Serial.print("Подключение к WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Открой в браузере: http://");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.begin();
}

void loop() {
  server.handleClient();
}
