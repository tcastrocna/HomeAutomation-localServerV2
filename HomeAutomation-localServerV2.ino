#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <DHT.h>
#include <Ticker.h>

#define DHTPIN 27
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);
Ticker sensorTicker;

const char* ssid = "REPLACE_WITH_YOUR_SSID";
const char* password = "REPLACE_WITH_YOUR_PASSWORD";

WebServer server(80);
WebSocketsServer webSocket(81);

const int numDevices = 4;
const int numTouchButtons = 3;

const int devicePins[numDevices] = { 18, 19, 23, 5 };
const int touchButtonPins[numTouchButtons] = { 13, 12, 14 };

bool deviceStates[numDevices] = { LOW, LOW, LOW, LOW };
bool lastTouchStates[numTouchButtons] = { LOW, LOW, LOW };

const int capacitanceThreshold = 20;
int touchMedians[numTouchButtons] = { 0, 0, 0 };

Preferences preferences;

bool loggedIn = false;
const char* defaultUsername = "admin";
const char* defaultPassword = "admin";

float currentTemperature = 0.0;
float currentHumidity = 0.0;

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 0; background-color: #f7f9fc; }
header { background-color: #4CAF50; color: white; padding: 20px; font-size: 24px; display: flex; justify-content: space-between; align-items: center; }
.grid { display: flex; flex-wrap: wrap; justify-content: center; padding: 10px; }
.grid-item { flex: 1 1 calc(50% - 20px); box-sizing: border-box; margin: 10px; }
button { width: 100%; padding: 15px; font-size: 18px; border: none; border-radius: 10px; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1); cursor: pointer; transition: all 0.3s ease; }
button:active { box-shadow: 0 2px 3px rgba(0, 0, 0, 0.2); }
.on { background-color: #4CAF50; color: white; }
.off { background-color: #f44336; color: white; }
.sensor { background-color: #ddd; padding: 10px; margin-bottom: 20px; }
</style>
</head>
<body>
<header>Controle de Dispositivos
<button onclick="location.href='/logout'">Sair</button>
</header>
<div class="sensor">
<h2>Temperatura e Umidade</h2>
<p>Temperatura: <span id="temperature">%TEMPERATURE%</span> °C</p>
<p>Umidade: <span id="humidity">%HUMIDITY%</span> %</p>
</div>
<div class="grid">
%BUTTONS%
</div>
<script>
let ws = new WebSocket('ws://' + window.location.hostname + ':81');
ws.onmessage = function(event) {
  let data = JSON.parse(event.data);
  for (let i = 0; i < %NUM_DEVICES%; i++) {
    let btn = document.getElementById('toggleBtn' + i);
    if (data.deviceStates[i]) {
      btn.classList.add('on');
      btn.classList.remove('off');
      btn.textContent = 'Desligar Dispositivo ' + (i+1);
    } else {
      btn.classList.add('off');
      btn.classList.remove('on');
      btn.textContent = 'Ligar Dispositivo ' + (i+1);
    }
  }
  document.getElementById('temperature').textContent = data.temperature.toFixed(1);
  document.getElementById('humidity').textContent = data.humidity.toFixed(1);
};
function toggleDevice(index) {
  ws.send(index.toString());
}
</script>
</body>
</html>
)rawliteral";

const char LOGIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 0; background-color: #f7f9fc; }
.login-container { position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%);
                   width: 300px; padding: 20px; background-color: #fff; border-radius: 10px; box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1); }
input[type=text], input[type=password] { width: 100%; padding: 12px 20px; margin: 8px 0; display: inline-block;
                                         border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }
input[type=submit] { width: 100%; background-color: #4CAF50; color: white; padding: 14px 20px; margin: 8px 0;
                    border: none; border-radius: 4px; cursor: pointer; }
input[type=submit]:hover { background-color: #45a049; }
</style>
</head>
<body>
<div class="login-container">
<h2>Login</h2>
<form action="/login" method="POST">
<input type="text" name="username" placeholder="Usuário">
<br>
<input type="password" name="password" placeholder="Senha">
<br>
<input type="submit" value="Entrar">
</form>
</div>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  dht.begin();
  setupDevices();
  setupTouchButtons();
  setupWiFi();
  setupServer();
  sensorTicker.attach(1, updateSensorData);
}

void loop() {
  server.handleClient();
  webSocket.loop();
  readTouchButtons();
}

void updateSensorData() {
  currentTemperature = dht.readTemperature();
  currentHumidity = dht.readHumidity();

  if (isnan(currentTemperature) || isnan(currentHumidity)) {
    Serial.println("Falha ao ler o sensor DHT11!");
    currentTemperature = 0;
    currentHumidity = 0;
  }

  if (loggedIn) {
    notifyClients();
  }
}

void setupDevices() {
  for (int i = 0; i < numDevices; i++) {
    pinMode(devicePins[i], OUTPUT);
    digitalWrite(devicePins[i], HIGH);
    deviceStates[i] = LOW;
  }

  preferences.begin("deviceStates", false);
  for (int i = 0; i < numDevices; i++) {
    deviceStates[i] = preferences.getBool(String("device" + String(i)).c_str(), false);
    digitalWrite(devicePins[i], deviceStates[i] ? LOW : HIGH);
  }
}

void setupTouchButtons() {
  for (int i = 0; i < numTouchButtons; i++) {
    touchMedians[i] = getTouchMedian(touchButtonPins[i]);
  }
}

void setupWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Conectando a ");
  Serial.println(ssid);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi conectado.");
  Serial.println("Endereço IP: ");
  Serial.println(WiFi.localIP());
}

void setupServer() {
  server.on("/", handleRoot);
  server.on("/login", HTTP_GET, handleLoginPage);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/logout", handleLogout);
  server.onNotFound(handleNotFound);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void handleRoot() {
  if (!loggedIn) {
    server.sendHeader("Location", "/login");
    server.send(303);
    return;
  }

  String html = String(HTML_PAGE);
  html.replace("%TEMPERATURE%", String(currentTemperature, 1));
  html.replace("%HUMIDITY%", String(currentHumidity, 1));
  html.replace("%NUM_DEVICES%", String(numDevices));

  String buttons = "";
  for (int i = 0; i < numDevices; i++) {
    String btnClass = deviceStates[i] ? "on" : "off";
    buttons += "<div class=\"grid-item\"><button id=\"toggleBtn" + String(i) + "\" class=\"" + btnClass + "\" onclick=\"toggleDevice(" + String(i) + ")\">" + String(deviceStates[i] ? "Desligar" : "Ligar") + " Dispositivo " + String(i + 1) + "</button></div>";
  }
  html.replace("%BUTTONS%", buttons);

  server.send(200, "text/html", html);
}

void handleLoginPage() {
  server.send(200, "text/html", LOGIN_PAGE);
}

void handleLogin() {
  if (server.hasArg("username") && server.hasArg("password")) {
    String username = server.arg("username");
    String password = server.arg("password");

    if (username == defaultUsername && password == defaultPassword) {
      loggedIn = true;
      server.sendHeader("Location", "/");
      server.send(303);
      return;
    }
  }
  server.send(401, "text/plain", "Login falhou!");
}

void handleLogout() {
  loggedIn = false;
  server.sendHeader("Location", "/login");
  server.send(303);
}

void handleNotFound() {
  server.send(404, "text/plain", "404 Not Found");
}

void notifyClients() {
  String message = "{\"deviceStates\":[";
  for (int i = 0; i < numDevices; i++) {
    message += deviceStates[i] ? "true" : "false";
    if (i < numDevices - 1) message += ",";
  }
  message += "],";
  message += "\"temperature\":" + String(currentTemperature) + ",";
  message += "\"humidity\":" + String(currentHumidity) + "}";
  webSocket.broadcastTXT(message);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_TEXT) {
    int index = atoi((char*)payload);
    if (index >= 0 && index < numDevices) {
      deviceStates[index] = !deviceStates[index];
      digitalWrite(devicePins[index], deviceStates[index] ? LOW : HIGH);
      preferences.putBool(String("device" + String(index)).c_str(), deviceStates[index]);
      notifyClients();
    }
  }
}

int getTouchMedian(int pin) {
  int sum = 0;
  for (int i = 0; i < 100; i++) {
    sum += touchRead(pin);
  }
  return sum / 100;
}

void readTouchButtons() {
  for (int i = 0; i < numTouchButtons; i++) {
    int touchValue = touchRead(touchButtonPins[i]);
    if (touchValue < touchMedians[i] - capacitanceThreshold && lastTouchStates[i] == HIGH) {
      deviceStates[i] = !deviceStates[i];
      digitalWrite(devicePins[i], deviceStates[i] ? LOW : HIGH);
      preferences.putBool(String("device" + String(i)).c_str(), deviceStates[i]);
      notifyClients();
    }
    lastTouchStates[i] = (touchValue < touchMedians[i] - capacitanceThreshold) ? LOW : HIGH;
  }
}
