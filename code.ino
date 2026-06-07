#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include "DHT.h"

// ======================================================
// Project Identity
// ======================================================

const char* DEVICE_NAME = "Autonomous Environmental Safety Turret";
const char* OTA_HOSTNAME = "environmental-turret";
const char* OTA_PASSWORD = "12345678";

// Setup access point fallback
const char* SETUP_AP_SSID = "Turret-Setup";
const char* SETUP_AP_PASSWORD = "12345678";

// ======================================================
// Pins
// ======================================================

#define SERVO_PIN 13

#define ULTRASONIC_TRIG_PIN 5
#define ULTRASONIC_ECHO_PIN 18

#define FLAME_DIGITAL_PIN 27
#define FLAME_ANALOG_PIN 32

#define SMOKE_PIN 34

#define DHT_PIN 4
#define DHT_TYPE DHT22

#define SOIL_PIN 35

#define PUMP_PIN 25
#define BUZZER_PIN 14

#define GREEN_LED_PIN 16
#define YELLOW_LED_PIN 17
#define RED_LED_PIN 19

// ======================================================
// Saved Config Defaults
// ======================================================

String wifiSsid = "";
String wifiPassword = "";

IPAddress laptopIp(192, 168, 1, 10);
uint16_t telemetryPort = 4210;
uint16_t commandPort = 4212;

// Sensor thresholds
int smokeWarningThreshold = 1200;
int smokeDangerThreshold = 2000;

float tempWarningThreshold = 40.0;
float tempDangerThreshold = 50.0;

int maxDistanceCm = 250;
int flameDetectedState = LOW;

// Radar scan
int scanMinAngle = 0;
int scanMaxAngle = 180;
int scanStep = 3;
unsigned long scanIntervalMs = 70;

// Fire search/extinguish
int fireSearchMinAngle = 0;
int fireSearchMaxAngle = 180;
int fireSearchStep = 5;

bool flameStrongIsLower = true;
int flameAnalogThreshold = 1600;

unsigned long firePumpBurstMs = 2500;
unsigned long fireSettleMs = 1200;
int maxFireAttempts = 5;

int fireRecheckRange = 25;
int fireRecheckStep = 5;

// Soil watering
int soilDryThreshold = 2500;
int soilWetTargetThreshold = 1800;
int soilWaterAngle = 90;

unsigned long soilPumpBurstMs = 2500;
unsigned long soilSettleMs = 2000;
int maxSoilAttempts = 3;

// Near object warning / optional deterrent
bool nearWarningEnabled = true;
bool nearPumpDeterrentEnabled = false;

int nearWarningDistanceCm = 40;
int nearDangerDistanceCm = 20;

unsigned long nearPumpBurstMs = 700;
unsigned long nearCooldownMs = 5000;

// Safety
unsigned long manualControlTimeoutMs = 8000;
unsigned long maxManualPumpRunMs = 10000;
unsigned long maxAutoPumpRunMs = 8000;

// ======================================================
// Timing
// ======================================================

const unsigned long ULTRASONIC_INTERVAL_MS = 80;
const unsigned long FAST_SENSOR_INTERVAL_MS = 100;
const unsigned long DHT_INTERVAL_MS = 2000;
const unsigned long TELEMETRY_INTERVAL_MS = 120;
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 7000;
const unsigned long SERIAL_PRINT_INTERVAL_MS = 4000;
const unsigned long EVENT_COOLDOWN_MS = 1000;

// ======================================================
// Objects
// ======================================================

Servo turretServo;
DHT dht(DHT_PIN, DHT_TYPE);

WebServer configServer(80);
Preferences prefs;

WiFiUDP telemetryUdp;
WiFiUDP commandUdp;

// ======================================================
// System States
// ======================================================

enum SystemStatus {
  STATUS_SAFE,
  STATUS_WARNING,
  STATUS_DANGER
};

enum AutoState {
  AUTO_SCAN,
  FIRE_SEARCH,
  FIRE_EXTINGUISH,
  FIRE_RECHECK,
  SOIL_WATER_MOVE,
  SOIL_WATERING,
  NEAR_DETERRENT,
  EMERGENCY_STOP_STATE
};

SystemStatus systemStatus = STATUS_SAFE;
AutoState autoState = AUTO_SCAN;

// ======================================================
// Runtime Data
// ======================================================

int currentAngle = 0;
int scanDirection = 1;

int distanceCm = 250;

float temperatureC = 0.0;
float humidity = 0.0;

int smokeValue = 0;
int soilValue = 0;

bool flameDetected = false;
int flameAnalogValue = 4095;

bool autoMode = true;
bool setupApRunning = false;
bool wifiConnected = false;
bool otaReady = false;
bool emergencyStopActive = false;

// Outputs
bool pumpOn = false;
bool buzzerOn = false;
bool greenLed = true;
bool yellowLed = false;
bool redLed = false;

// Manual safety
unsigned long manualControlLastSeen = 0;
unsigned long manualPumpStopAt = 0;

// Auto action runtime
int bestFlameAngle = 90;
int bestFlameScore = 0;
int fireSearchAngle = 0;
int fireAttempts = 0;

int fireRecheckMinAngle = 0;
int fireRecheckMaxAngle = 180;
int fireRecheckAngle = 0;

int soilAttempts = 0;

bool firePumpRunning = false;
bool soilPumpRunning = false;
bool nearPumpRunning = false;

unsigned long actionTimer = 0;
unsigned long stateStartedAt = 0;
unsigned long pumpStartedAt = 0;
unsigned long lastNearDeterrentTime = 0;

// Messages
char latestEvent[140] = "System started";
char lastCommand[160] = "none";

// Timers
unsigned long lastServoTime = 0;
unsigned long lastUltrasonicTime = 0;
unsigned long lastFastSensorTime = 0;
unsigned long lastDhtTime = 0;
unsigned long lastTelemetryTime = 0;
unsigned long lastWifiReconnectTime = 0;
unsigned long lastSerialPrintTime = 0;
unsigned long lastEventTime = 0;

// ======================================================
// Text Helpers
// ======================================================

const char* statusToText(SystemStatus status) {
  switch (status) {
    case STATUS_SAFE: return "SAFE";
    case STATUS_WARNING: return "WARNING";
    case STATUS_DANGER: return "DANGER";
    default: return "UNKNOWN";
  }
}

const char* autoStateToText(AutoState state) {
  switch (state) {
    case AUTO_SCAN: return "AUTO_SCAN";
    case FIRE_SEARCH: return "FIRE_SEARCH";
    case FIRE_EXTINGUISH: return "FIRE_EXTINGUISH";
    case FIRE_RECHECK: return "FIRE_RECHECK";
    case SOIL_WATER_MOVE: return "SOIL_WATER_MOVE";
    case SOIL_WATERING: return "SOIL_WATERING";
    case NEAR_DETERRENT: return "NEAR_DETERRENT";
    case EMERGENCY_STOP_STATE: return "EMERGENCY_STOP";
    default: return "UNKNOWN";
  }
}

void setEvent(const char* text) {
  unsigned long now = millis();

  if (now - lastEventTime < EVENT_COOLDOWN_MS) {
    return;
  }

  lastEventTime = now;

  strncpy(latestEvent, text, sizeof(latestEvent) - 1);
  latestEvent[sizeof(latestEvent) - 1] = '\0';

  Serial.print("[EVENT] ");
  Serial.println(latestEvent);
}

void forceEvent(const char* text) {
  lastEventTime = 0;
  setEvent(text);
}

void setLastCommand(const char* text) {
  strncpy(lastCommand, text, sizeof(lastCommand) - 1);
  lastCommand[sizeof(lastCommand) - 1] = '\0';
}

// ======================================================
// Output Helpers
// ======================================================

void applyPump(bool state) {
  if (emergencyStopActive && state) {
    return;
  }

  pumpOn = state;
  digitalWrite(PUMP_PIN, state ? HIGH : LOW);

  if (state) {
    pumpStartedAt = millis();
  }
}

void applyBuzzer(bool state) {
  buzzerOn = state;
  digitalWrite(BUZZER_PIN, state ? HIGH : LOW);
}

void applyLeds(bool green, bool yellow, bool red) {
  greenLed = green;
  yellowLed = yellow;
  redLed = red;

  digitalWrite(GREEN_LED_PIN, green ? HIGH : LOW);
  digitalWrite(YELLOW_LED_PIN, yellow ? HIGH : LOW);
  digitalWrite(RED_LED_PIN, red ? HIGH : LOW);
}

void stopAllActuators() {
  applyPump(false);
  applyBuzzer(false);
}

void triggerEmergencyStop(const char* reason) {
  emergencyStopActive = true;
  autoMode = false;
  autoState = EMERGENCY_STOP_STATE;

  firePumpRunning = false;
  soilPumpRunning = false;
  nearPumpRunning = false;
  actionTimer = 0;
  manualPumpStopAt = 0;

  stopAllActuators();
  applyLeds(false, false, true);

  forceEvent(reason);
}

void clearEmergencyStop() {
  emergencyStopActive = false;
  autoMode = true;
  autoState = AUTO_SCAN;

  firePumpRunning = false;
  soilPumpRunning = false;
  nearPumpRunning = false;
  actionTimer = 0;

  stopAllActuators();
  applyLeds(true, false, false);

  forceEvent("Emergency stop cleared. Returned to AUTO.");
}

// ======================================================
// Sensor Logic Helpers
// ======================================================

int getFlameScore(int analogValue) {
  if (flameStrongIsLower) {
    return 4095 - analogValue;
  }

  return analogValue;
}

bool isFlameStrongEnough() {
  if (flameStrongIsLower) {
    return flameAnalogValue <= flameAnalogThreshold;
  }

  return flameAnalogValue >= flameAnalogThreshold;
}

bool isSmokeWarning() {
  return smokeValue >= smokeWarningThreshold;
}

bool isSmokeDanger() {
  return smokeValue >= smokeDangerThreshold;
}

bool isTempWarning() {
  return temperatureC >= tempWarningThreshold;
}

bool isTempDanger() {
  return temperatureC >= tempDangerThreshold;
}

bool isSoilDry() {
  return soilValue >= soilDryThreshold;
}

bool isSoilWetEnough() {
  return soilValue <= soilWetTargetThreshold;
}

bool isFireOrSmokeDetected() {
  return flameDetected || isFlameStrongEnough() || isSmokeDanger();
}

bool isNearWarning() {
  if (!nearWarningEnabled) return false;
  if (distanceCm <= 0) return false;
  return distanceCm <= nearWarningDistanceCm;
}

bool isNearDanger() {
  if (!nearWarningEnabled) return false;
  if (distanceCm <= 0) return false;
  return distanceCm <= nearDangerDistanceCm;
}

bool canRunNearDeterrent() {
  if (!nearPumpDeterrentEnabled) return false;

  unsigned long now = millis();
  return now - lastNearDeterrentTime >= nearCooldownMs;
}

void changeAutoState(AutoState newState, const char* eventText) {
  autoState = newState;
  stateStartedAt = millis();
  actionTimer = 0;

  firePumpRunning = false;
  soilPumpRunning = false;
  nearPumpRunning = false;

  forceEvent(eventText);
}

// ======================================================
// Preferences
// ======================================================

int prefInt(const char* key, int defaultValue) {
  return prefs.getInt(key, defaultValue);
}

unsigned long prefULong(const char* key, unsigned long defaultValue) {
  return prefs.getULong(key, defaultValue);
}

bool prefBool(const char* key, bool defaultValue) {
  return prefs.getBool(key, defaultValue);
}

void sanitizeConfig() {
  scanMinAngle = constrain(scanMinAngle, 0, 180);
  scanMaxAngle = constrain(scanMaxAngle, 0, 180);

  if (scanMinAngle > scanMaxAngle) {
    scanMinAngle = 0;
    scanMaxAngle = 180;
  }

  fireSearchMinAngle = constrain(fireSearchMinAngle, 0, 180);
  fireSearchMaxAngle = constrain(fireSearchMaxAngle, 0, 180);

  if (fireSearchMinAngle > fireSearchMaxAngle) {
    fireSearchMinAngle = 0;
    fireSearchMaxAngle = 180;
  }

  soilWaterAngle = constrain(soilWaterAngle, 0, 180);

  scanStep = constrain(scanStep, 1, 30);
  fireSearchStep = constrain(fireSearchStep, 1, 30);
  fireRecheckStep = constrain(fireRecheckStep, 1, 30);

  scanIntervalMs = constrain(scanIntervalMs, 25UL, 1000UL);

  maxDistanceCm = constrain(maxDistanceCm, 30, 500);

  nearWarningDistanceCm = constrain(nearWarningDistanceCm, 5, maxDistanceCm);
  nearDangerDistanceCm = constrain(nearDangerDistanceCm, 3, maxDistanceCm);

  if (nearDangerDistanceCm > nearWarningDistanceCm) {
    nearDangerDistanceCm = nearWarningDistanceCm;
  }

  nearPumpBurstMs = constrain(nearPumpBurstMs, 100UL, 3000UL);
  nearCooldownMs = constrain(nearCooldownMs, 1000UL, 60000UL);

  firePumpBurstMs = constrain(firePumpBurstMs, 300UL, maxAutoPumpRunMs);
  soilPumpBurstMs = constrain(soilPumpBurstMs, 300UL, maxAutoPumpRunMs);

  maxFireAttempts = constrain(maxFireAttempts, 1, 20);
  maxSoilAttempts = constrain(maxSoilAttempts, 1, 20);

  fireRecheckRange = constrain(fireRecheckRange, 5, 90);
}

void loadSettings() {
  prefs.begin("turret", true);

  wifiSsid = prefs.getString("wssid", "");
  wifiPassword = prefs.getString("wpass", "");

  String savedIp = prefs.getString("lip", "192.168.1.10");
  telemetryPort = prefs.getUShort("tport", 4210);
  commandPort = prefs.getUShort("cport", 4212);

  smokeWarningThreshold = prefInt("smkWarn", smokeWarningThreshold);
  smokeDangerThreshold = prefInt("smkDanger", smokeDangerThreshold);

  tempWarningThreshold = prefs.getFloat("tmpWarn", tempWarningThreshold);
  tempDangerThreshold = prefs.getFloat("tmpDanger", tempDangerThreshold);

  maxDistanceCm = prefInt("maxDist", maxDistanceCm);
  flameDetectedState = prefInt("flmDState", flameDetectedState);

  scanMinAngle = prefInt("scanMin", scanMinAngle);
  scanMaxAngle = prefInt("scanMax", scanMaxAngle);
  scanStep = prefInt("scanStep", scanStep);
  scanIntervalMs = prefULong("scanMs", scanIntervalMs);

  fireSearchMinAngle = prefInt("fireMin", fireSearchMinAngle);
  fireSearchMaxAngle = prefInt("fireMax", fireSearchMaxAngle);
  fireSearchStep = prefInt("fireStep", fireSearchStep);
  flameAnalogThreshold = prefInt("flmTh", flameAnalogThreshold);
  flameStrongIsLower = prefBool("flmLow", flameStrongIsLower);

  firePumpBurstMs = prefULong("firePump", firePumpBurstMs);
  fireSettleMs = prefULong("fireSettle", fireSettleMs);
  maxFireAttempts = prefInt("fireTry", maxFireAttempts);

  fireRecheckRange = prefInt("fReRange", fireRecheckRange);
  fireRecheckStep = prefInt("fReStep", fireRecheckStep);

  soilDryThreshold = prefInt("soilDry", soilDryThreshold);
  soilWetTargetThreshold = prefInt("soilWet", soilWetTargetThreshold);
  soilWaterAngle = prefInt("soilAngle", soilWaterAngle);
  soilPumpBurstMs = prefULong("soilPump", soilPumpBurstMs);
  soilSettleMs = prefULong("soilSettle", soilSettleMs);
  maxSoilAttempts = prefInt("soilTry", maxSoilAttempts);

  nearWarningEnabled = prefBool("nearEn", nearWarningEnabled);
  nearPumpDeterrentEnabled = prefBool("nearPump", nearPumpDeterrentEnabled);
  nearWarningDistanceCm = prefInt("nearWarn", nearWarningDistanceCm);
  nearDangerDistanceCm = prefInt("nearDanger", nearDangerDistanceCm);
  nearPumpBurstMs = prefULong("nearBurst", nearPumpBurstMs);
  nearCooldownMs = prefULong("nearCool", nearCooldownMs);

  manualControlTimeoutMs = prefULong("manTout", manualControlTimeoutMs);
  maxManualPumpRunMs = prefULong("manPumpMax", maxManualPumpRunMs);
  maxAutoPumpRunMs = prefULong("autoPumpMax", maxAutoPumpRunMs);

  prefs.end();

  IPAddress parsedIp;
  if (parsedIp.fromString(savedIp)) {
    laptopIp = parsedIp;
  }

  sanitizeConfig();

  Serial.println("Settings loaded.");
}

void saveSettingsFromForm() {
  String ip = configServer.arg("laptopIp");

  IPAddress testIp;
  if (!testIp.fromString(ip)) {
    configServer.send(400, "text/plain", "Invalid laptop IP");
    return;
  }

  String newSsid = configServer.arg("wifiSsid");
  String newPass = configServer.arg("wifiPassword");

  prefs.begin("turret", false);

  prefs.putString("wssid", newSsid);
  prefs.putString("wpass", newPass);

  prefs.putString("lip", ip);
  prefs.putUShort("tport", configServer.arg("telemetryPort").toInt());
  prefs.putUShort("cport", configServer.arg("commandPort").toInt());

  prefs.putInt("smkWarn", configServer.arg("smokeWarningThreshold").toInt());
  prefs.putInt("smkDanger", configServer.arg("smokeDangerThreshold").toInt());

  prefs.putFloat("tmpWarn", configServer.arg("tempWarningThreshold").toFloat());
  prefs.putFloat("tmpDanger", configServer.arg("tempDangerThreshold").toFloat());

  prefs.putInt("maxDist", configServer.arg("maxDistanceCm").toInt());
  prefs.putInt("flmDState", configServer.arg("flameDetectedState").toInt());

  prefs.putInt("scanMin", configServer.arg("scanMinAngle").toInt());
  prefs.putInt("scanMax", configServer.arg("scanMaxAngle").toInt());
  prefs.putInt("scanStep", configServer.arg("scanStep").toInt());
  prefs.putULong("scanMs", configServer.arg("scanIntervalMs").toInt());

  prefs.putInt("fireMin", configServer.arg("fireSearchMinAngle").toInt());
  prefs.putInt("fireMax", configServer.arg("fireSearchMaxAngle").toInt());
  prefs.putInt("fireStep", configServer.arg("fireSearchStep").toInt());
  prefs.putInt("flmTh", configServer.arg("flameAnalogThreshold").toInt());
  prefs.putBool("flmLow", configServer.arg("flameStrongIsLower").toInt() == 1);

  prefs.putULong("firePump", configServer.arg("firePumpBurstMs").toInt());
  prefs.putULong("fireSettle", configServer.arg("fireSettleMs").toInt());
  prefs.putInt("fireTry", configServer.arg("maxFireAttempts").toInt());

  prefs.putInt("fReRange", configServer.arg("fireRecheckRange").toInt());
  prefs.putInt("fReStep", configServer.arg("fireRecheckStep").toInt());

  prefs.putInt("soilDry", configServer.arg("soilDryThreshold").toInt());
  prefs.putInt("soilWet", configServer.arg("soilWetTargetThreshold").toInt());
  prefs.putInt("soilAngle", configServer.arg("soilWaterAngle").toInt());
  prefs.putULong("soilPump", configServer.arg("soilPumpBurstMs").toInt());
  prefs.putULong("soilSettle", configServer.arg("soilSettleMs").toInt());
  prefs.putInt("soilTry", configServer.arg("maxSoilAttempts").toInt());

  prefs.putBool("nearEn", configServer.arg("nearWarningEnabled").toInt() == 1);
  prefs.putBool("nearPump", configServer.arg("nearPumpDeterrentEnabled").toInt() == 1);
  prefs.putInt("nearWarn", configServer.arg("nearWarningDistanceCm").toInt());
  prefs.putInt("nearDanger", configServer.arg("nearDangerDistanceCm").toInt());
  prefs.putULong("nearBurst", configServer.arg("nearPumpBurstMs").toInt());
  prefs.putULong("nearCool", configServer.arg("nearCooldownMs").toInt());

  prefs.putULong("manTout", configServer.arg("manualControlTimeoutMs").toInt());
  prefs.putULong("manPumpMax", configServer.arg("maxManualPumpRunMs").toInt());
  prefs.putULong("autoPumpMax", configServer.arg("maxAutoPumpRunMs").toInt());

  prefs.end();

  loadSettings();

  commandUdp.stop();
  commandUdp.begin(commandPort);

  forceEvent("Configuration saved");
}

// ======================================================
// Wi-Fi / AP / OTA
// ======================================================

void startSetupAp() {
  if (setupApRunning) return;

  WiFi.mode(WIFI_AP_STA);

  bool ok = WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASSWORD);

  if (ok) {
    setupApRunning = true;

    Serial.println("Setup AP started.");
    Serial.print("AP SSID: ");
    Serial.println(SETUP_AP_SSID);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());

    forceEvent("Setup AP active. Open 192.168.4.1");
  } else {
    Serial.println("Failed to start setup AP.");
  }
}

void connectWiFiInitial() {
  if (wifiSsid.length() == 0) {
    Serial.println("No saved Wi-Fi. Starting setup AP.");
    startSetupAp();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(wifiSsid);

  unsigned long startedAt = millis();
  unsigned long lastDot = 0;

  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
    if (millis() - lastDot >= 500) {
      lastDot = millis();
      Serial.print(".");
    }

    yield();
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    setupApRunning = false;

    Serial.print("Wi-Fi connected. ESP32 IP: ");
    Serial.println(WiFi.localIP());

    forceEvent("Wi-Fi connected");
  } else {
    wifiConnected = false;

    Serial.println("Wi-Fi connection failed. Starting setup AP.");
    startSetupAp();
  }
}

void updateWiFiReconnect() {
  if (wifiSsid.length() == 0) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    return;
  }

  wifiConnected = false;

  unsigned long now = millis();

  if (now - lastWifiReconnectTime < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }

  lastWifiReconnectTime = now;

  Serial.println("Wi-Fi disconnected. Reconnecting...");
  WiFi.disconnect();
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

  startSetupAp();
}

void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA update started");

    emergencyStopActive = true;
    stopAllActuators();
    applyLeds(false, true, false);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("OTA update finished");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\n", (progress * 100) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error [%u]\n", error);
  });

  ArduinoOTA.begin();
  otaReady = true;

  Serial.println("OTA ready");
}

// ======================================================
// Config Web Page
// ======================================================

String htmlInput(const String& label, const String& name, const String& value, const String& type = "text") {
  String html = "";
  html += "<label>" + label + "</label>";
  html += "<input type='" + type + "' name='" + name + "' value='" + value + "'>";
  return html;
}

String htmlSection(const String& title, const String& subtitle = "") {
  String html = "";
  html += "<section class='card'>";
  html += "<h3>" + title + "</h3>";

  if (subtitle.length() > 0) {
    html += "<p class='muted'>" + subtitle + "</p>";
  }

  return html;
}

String getConfigPageHtml() {
  String html = "";

  html += "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Turret Configuration</title>";

  html += "<style>";
  html += "*{box-sizing:border-box}body{margin:0;font-family:Arial;background:#070b12;color:#eef4ff;padding:16px}";
  html += ".wrap{max-width:980px;margin:auto}.hero{background:linear-gradient(135deg,#132035,#0b111d);border:1px solid #263244;border-radius:18px;padding:18px;margin-bottom:14px}";
  html += "h1{margin:0;font-size:26px}p{color:#aab7c8;line-height:1.5}.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}";
  html += ".card{background:#111827;border:1px solid #263244;border-radius:16px;padding:16px;margin-bottom:14px}";
  html += "h3{margin:0 0 8px}.muted{font-size:13px;margin-top:0}";
  html += "label{display:block;margin-top:10px;color:#c6d3e3;font-weight:bold;font-size:13px}";
  html += "input{width:100%;padding:11px;margin-top:5px;background:#060b12;color:white;border:1px solid #344258;border-radius:10px}";
  html += ".status{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:12px}";
  html += ".pill{background:#07101c;border:1px solid #263244;border-radius:12px;padding:10px}.pill b{display:block;color:white}.pill span{color:#91a1b6;font-size:12px}";
  html += "button,.btn{display:inline-block;border:0;border-radius:12px;padding:12px 16px;margin:6px 6px 0 0;font-weight:bold;text-decoration:none;cursor:pointer}";
  html += ".save{background:#2ecc71;color:#06110a}.danger{background:#ef4444;color:white}.warn{background:#facc15;color:#111827}.neutral{background:#263244;color:white}";
  html += ".wide{grid-column:1/-1}@media(max-width:760px){.grid{grid-template-columns:1fr}.status{grid-template-columns:1fr}}";
  html += "</style>";

  html += "</head><body><div class='wrap'>";

  html += "<div class='hero'>";
  html += "<h1>Autonomous Environmental Safety Turret</h1>";
  html += "<p>Configuration portal for Wi-Fi, telemetry, autonomous response thresholds, safety limits, and OTA-ready operation.</p>";

  html += "<div class='status'>";
  html += "<div class='pill'><span>STA IP</span><b>" + WiFi.localIP().toString() + "</b></div>";
  html += "<div class='pill'><span>Setup AP IP</span><b>" + WiFi.softAPIP().toString() + "</b></div>";
  html += "<div class='pill'><span>Wi-Fi</span><b>" + String(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "NOT CONNECTED") + "</b></div>";
  html += "<div class='pill'><span>Current State</span><b>" + String(autoStateToText(autoState)) + "</b></div>";
  html += "</div></div>";

  html += "<form method='POST' action='/save'><div class='grid'>";

  html += htmlSection("Wi-Fi", "Leave password unchanged only by typing it again. ESP32 uses setup AP if Wi-Fi fails.");
  html += htmlInput("Wi-Fi SSID", "wifiSsid", wifiSsid);
  html += htmlInput("Wi-Fi Password", "wifiPassword", wifiPassword, "password");
  html += "</section>";

  html += htmlSection("Laptop / Node Dashboard", "Set your laptop IPv4 address printed by the Node dashboard terminal.");
  html += htmlInput("Laptop IP", "laptopIp", laptopIp.toString());
  html += htmlInput("Telemetry UDP Port", "telemetryPort", String(telemetryPort));
  html += htmlInput("Command UDP Port", "commandPort", String(commandPort));
  html += "</section>";

  html += htmlSection("Basic Sensor Thresholds");
  html += htmlInput("Smoke Warning Threshold", "smokeWarningThreshold", String(smokeWarningThreshold));
  html += htmlInput("Smoke Danger Threshold", "smokeDangerThreshold", String(smokeDangerThreshold));
  html += htmlInput("Temperature Warning °C", "tempWarningThreshold", String(tempWarningThreshold, 1));
  html += htmlInput("Temperature Danger °C", "tempDangerThreshold", String(tempDangerThreshold, 1));
  html += htmlInput("Max Ultrasonic Distance cm", "maxDistanceCm", String(maxDistanceCm));
  html += htmlInput("Flame Digital Detected State: 0=LOW, 1=HIGH", "flameDetectedState", String(flameDetectedState));
  html += "</section>";

  html += htmlSection("Radar Scan");
  html += htmlInput("Scan Min Angle", "scanMinAngle", String(scanMinAngle));
  html += htmlInput("Scan Max Angle", "scanMaxAngle", String(scanMaxAngle));
  html += htmlInput("Scan Step", "scanStep", String(scanStep));
  html += htmlInput("Scan Interval ms", "scanIntervalMs", String(scanIntervalMs));
  html += "</section>";

  html += htmlSection("Fire Search / Extinguish", "Normal radar scan stops during fire response. Recheck scan is only for flame source confirmation.");
  html += htmlInput("Fire Search Min Angle", "fireSearchMinAngle", String(fireSearchMinAngle));
  html += htmlInput("Fire Search Max Angle", "fireSearchMaxAngle", String(fireSearchMaxAngle));
  html += htmlInput("Fire Search Step", "fireSearchStep", String(fireSearchStep));
  html += htmlInput("Flame Analog Threshold", "flameAnalogThreshold", String(flameAnalogThreshold));
  html += htmlInput("Flame Strong Is Lower? 1=yes, 0=no", "flameStrongIsLower", flameStrongIsLower ? "1" : "0");
  html += htmlInput("Fire Pump Burst ms", "firePumpBurstMs", String(firePumpBurstMs));
  html += htmlInput("Fire Settle ms", "fireSettleMs", String(fireSettleMs));
  html += htmlInput("Max Fire Attempts", "maxFireAttempts", String(maxFireAttempts));
  html += htmlInput("Fire Recheck Range ± degrees", "fireRecheckRange", String(fireRecheckRange));
  html += htmlInput("Fire Recheck Step", "fireRecheckStep", String(fireRecheckStep));
  html += "</section>";

  html += htmlSection("Soil Watering");
  html += htmlInput("Soil Dry Threshold", "soilDryThreshold", String(soilDryThreshold));
  html += htmlInput("Soil Wet Target Threshold", "soilWetTargetThreshold", String(soilWetTargetThreshold));
  html += htmlInput("Soil Watering Angle", "soilWaterAngle", String(soilWaterAngle));
  html += htmlInput("Soil Pump Burst ms", "soilPumpBurstMs", String(soilPumpBurstMs));
  html += htmlInput("Soil Settle ms", "soilSettleMs", String(soilSettleMs));
  html += htmlInput("Max Soil Attempts", "maxSoilAttempts", String(maxSoilAttempts));
  html += "</section>";

  html += htmlSection("Near Object Warning / Deterrent", "Pump deterrent is disabled by default for safety. Enable only in controlled demo.");
  html += htmlInput("Near Warning Enabled? 1=yes, 0=no", "nearWarningEnabled", nearWarningEnabled ? "1" : "0");
  html += htmlInput("Near Pump Deterrent Enabled? 1=yes, 0=no", "nearPumpDeterrentEnabled", nearPumpDeterrentEnabled ? "1" : "0");
  html += htmlInput("Near Warning Distance cm", "nearWarningDistanceCm", String(nearWarningDistanceCm));
  html += htmlInput("Near Danger Distance cm", "nearDangerDistanceCm", String(nearDangerDistanceCm));
  html += htmlInput("Near Pump Burst ms", "nearPumpBurstMs", String(nearPumpBurstMs));
  html += htmlInput("Near Cooldown ms", "nearCooldownMs", String(nearCooldownMs));
  html += "</section>";

  html += htmlSection("Safety Limits", "Emergency stop is available through UDP command and this page.");
  html += htmlInput("Manual Control Timeout ms", "manualControlTimeoutMs", String(manualControlTimeoutMs));
  html += htmlInput("Max Manual Pump Run ms", "maxManualPumpRunMs", String(maxManualPumpRunMs));
  html += htmlInput("Max Auto Pump Run ms", "maxAutoPumpRunMs", String(maxAutoPumpRunMs));
  html += "</section>";

  html += "<section class='card wide'>";
  html += "<button class='save' type='submit'>Save Configuration</button>";
  html += "<a class='btn danger' href='/estop'>Emergency Stop</a>";
  html += "<a class='btn warn' href='/clear-estop'>Clear Emergency Stop</a>";
  html += "<a class='btn neutral' href='/restart'>Restart ESP32</a>";
  html += "<a class='btn neutral' href='/clear-settings'>Clear Saved Settings</a>";
  html += "</section>";

  html += "</div></form>";

  html += "</div></body></html>";

  return html;
}

void handleRoot() {
  configServer.send(200, "text/html", getConfigPageHtml());
}

void handleSave() {
  saveSettingsFromForm();

  String html = "<html><body style='font-family:Arial;background:#070b12;color:white;padding:20px'>";
  html += "<h2>Configuration Saved</h2>";
  html += "<p>Settings saved to ESP32 flash memory.</p>";
  html += "<p>For Wi-Fi changes, restart the ESP32.</p>";
  html += "<a style='color:#2ecc71' href='/'>Back to Config</a>";
  html += "</body></html>";

  configServer.send(200, "text/html", html);
}

void handleEmergencyStopPage() {
  triggerEmergencyStop("Emergency stop triggered from config page");
  configServer.send(200, "text/html", "<html><body style='font-family:Arial;background:#070b12;color:white;padding:20px'><h2>Emergency Stop Active</h2><a style='color:#facc15' href='/'>Back</a></body></html>");
}

void handleClearEmergencyPage() {
  clearEmergencyStop();
  configServer.send(200, "text/html", "<html><body style='font-family:Arial;background:#070b12;color:white;padding:20px'><h2>Emergency Stop Cleared</h2><a style='color:#2ecc71' href='/'>Back</a></body></html>");
}

void handleRestartPage() {
  configServer.send(200, "text/html", "<html><body style='font-family:Arial;background:#070b12;color:white;padding:20px'><h2>Restarting ESP32...</h2></body></html>");
  delay(500);
  ESP.restart();
}

void handleClearSettingsPage() {
  prefs.begin("turret", false);
  prefs.clear();
  prefs.end();

  configServer.send(200, "text/html", "<html><body style='font-family:Arial;background:#070b12;color:white;padding:20px'><h2>Settings Cleared</h2><p>Restarting...</p></body></html>");
  delay(700);
  ESP.restart();
}

void setupConfigServer() {
  configServer.on("/", HTTP_GET, handleRoot);
  configServer.on("/save", HTTP_POST, handleSave);
  configServer.on("/estop", HTTP_GET, handleEmergencyStopPage);
  configServer.on("/clear-estop", HTTP_GET, handleClearEmergencyPage);
  configServer.on("/restart", HTTP_GET, handleRestartPage);
  configServer.on("/clear-settings", HTTP_GET, handleClearSettingsPage);

  configServer.begin();
  Serial.println("Config web server ready.");
}

// ======================================================
// Sensors
// ======================================================

int readUltrasonicCm() {
  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(ULTRASONIC_TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ULTRASONIC_ECHO_PIN, HIGH, 25000);

  if (duration == 0) {
    return maxDistanceCm;
  }

  int distance = duration * 0.0343 / 2.0;

  if (distance <= 0 || distance > maxDistanceCm) {
    return maxDistanceCm;
  }

  return distance;
}

void updateUltrasonic() {
  unsigned long now = millis();

  if (now - lastUltrasonicTime < ULTRASONIC_INTERVAL_MS) {
    return;
  }

  lastUltrasonicTime = now;
  distanceCm = readUltrasonicCm();
}

void updateFastSensors() {
  unsigned long now = millis();

  if (now - lastFastSensorTime < FAST_SENSOR_INTERVAL_MS) {
    return;
  }

  lastFastSensorTime = now;

  flameDetected = digitalRead(FLAME_DIGITAL_PIN) == flameDetectedState;
  flameAnalogValue = analogRead(FLAME_ANALOG_PIN);

  smokeValue = analogRead(SMOKE_PIN);
  soilValue = analogRead(SOIL_PIN);
}

void updateDht() {
  unsigned long now = millis();

  if (now - lastDhtTime < DHT_INTERVAL_MS) {
    return;
  }

  lastDhtTime = now;

  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t)) temperatureC = t;
  if (!isnan(h)) humidity = h;
}

// ======================================================
// Threat Status
// ======================================================

void updateThreatStatus() {
  if (emergencyStopActive) {
    systemStatus = STATUS_DANGER;
    return;
  }

  bool danger = isFireOrSmokeDetected() || isTempDanger();
  bool warning = isSmokeWarning() || isTempWarning() || isSoilDry() || isNearWarning();

  SystemStatus previous = systemStatus;

  if (danger) {
    systemStatus = STATUS_DANGER;
  } else if (warning) {
    systemStatus = STATUS_WARNING;
  } else {
    systemStatus = STATUS_SAFE;
  }

  if (previous != systemStatus) {
    if (systemStatus == STATUS_SAFE) {
      setEvent("Status changed to SAFE");
    } else if (systemStatus == STATUS_WARNING) {
      setEvent("Status changed to WARNING");
    } else if (systemStatus == STATUS_DANGER) {
      setEvent("Status changed to DANGER");
    }
  }
}

// ======================================================
// Pump Safety
// ======================================================

void enforcePumpSafety() {
  if (!pumpOn) {
    return;
  }

  unsigned long now = millis();
  unsigned long maxRun = autoMode ? maxAutoPumpRunMs : maxManualPumpRunMs;

  if (now - pumpStartedAt > maxRun) {
    applyPump(false);

    firePumpRunning = false;
    soilPumpRunning = false;
    nearPumpRunning = false;
    actionTimer = 0;

    forceEvent("Pump stopped by maximum runtime safety limit");
  }
}

// ======================================================
// AUTO Logic
// ======================================================

void updateRadarScanServo() {
  unsigned long now = millis();

  if (now - lastServoTime < scanIntervalMs) {
    return;
  }

  lastServoTime = now;

  currentAngle += scanDirection * scanStep;

  if (currentAngle >= scanMaxAngle) {
    currentAngle = scanMaxAngle;
    scanDirection = -1;
  }

  if (currentAngle <= scanMinAngle) {
    currentAngle = scanMinAngle;
    scanDirection = 1;
  }

  turretServo.write(currentAngle);
}

void handleAutoScanState() {
  applyPump(false);
  applyBuzzer(false);

  if (systemStatus == STATUS_SAFE) {
    applyLeds(true, false, false);
  } else if (systemStatus == STATUS_WARNING) {
    applyLeds(false, true, false);
  } else {
    applyLeds(false, false, true);
  }

  if (isFireOrSmokeDetected()) {
    bestFlameScore = 0;
    bestFlameAngle = currentAngle;
    fireSearchAngle = fireSearchMinAngle;
    fireAttempts = 0;

    changeAutoState(FIRE_SEARCH, "Smoke/fire detected. Searching flame source.");
    return;
  }

  if (isSoilDry()) {
    soilAttempts = 0;
    changeAutoState(SOIL_WATER_MOVE, "Soil dry. Moving to watering angle.");
    return;
  }

  if (isNearDanger() && canRunNearDeterrent()) {
    changeAutoState(NEAR_DETERRENT, "Near object detected. Deterrent activated.");
    return;
  }

  if (isNearWarning()) {
    applyLeds(false, true, false);
    applyBuzzer(true);
  }

  updateRadarScanServo();
}

void handleFireSearchState() {
  unsigned long now = millis();

  applyPump(false);
  applyBuzzer(true);
  applyLeds(false, false, true);

  if (now - lastServoTime < scanIntervalMs) {
    return;
  }

  lastServoTime = now;

  currentAngle = constrain(fireSearchAngle, fireSearchMinAngle, fireSearchMaxAngle);
  turretServo.write(currentAngle);

  int score = getFlameScore(flameAnalogValue);

  if (score > bestFlameScore) {
    bestFlameScore = score;
    bestFlameAngle = currentAngle;
  }

  fireSearchAngle += fireSearchStep;

  if (fireSearchAngle > fireSearchMaxAngle) {
    currentAngle = constrain(bestFlameAngle, 0, 180);
    turretServo.write(currentAngle);

    if (bestFlameScore > 0 || flameDetected || isSmokeDanger()) {
      changeAutoState(FIRE_EXTINGUISH, "Flame source locked. Extinguishing.");
    } else {
      changeAutoState(AUTO_SCAN, "No flame source found. Returning to radar scan.");
    }
  }
}

void startFireRecheck() {
  fireRecheckMinAngle = constrain(bestFlameAngle - fireRecheckRange, fireSearchMinAngle, fireSearchMaxAngle);
  fireRecheckMaxAngle = constrain(bestFlameAngle + fireRecheckRange, fireSearchMinAngle, fireSearchMaxAngle);
  fireRecheckAngle = fireRecheckMinAngle;

  bestFlameScore = 0;

  changeAutoState(FIRE_RECHECK, "Rechecking flame source after pump burst.");
}

void handleFireExtinguishState() {
  unsigned long now = millis();

  currentAngle = constrain(bestFlameAngle, 0, 180);
  turretServo.write(currentAngle);

  applyLeds(false, false, true);
  applyBuzzer(true);

  bool fireStillPresent = isFireOrSmokeDetected();

  if (!fireStillPresent) {
    applyPump(false);
    applyBuzzer(false);
    changeAutoState(AUTO_SCAN, "Flame appears off. Returning to radar scan.");
    return;
  }

  if (fireAttempts >= maxFireAttempts) {
    applyPump(false);
    applyBuzzer(true);
    changeAutoState(AUTO_SCAN, "Max fire attempts reached. Returning to radar scan.");
    return;
  }

  if (!firePumpRunning && actionTimer == 0) {
    firePumpRunning = true;
    actionTimer = now + firePumpBurstMs;
    fireAttempts++;

    applyPump(true);
    forceEvent("Fire pump burst started");
    return;
  }

  if (firePumpRunning && (long)(now - actionTimer) >= 0) {
    firePumpRunning = false;
    actionTimer = now + fireSettleMs;

    applyPump(false);
    forceEvent("Fire pump paused for settle/recheck");
    return;
  }

  if (!firePumpRunning && actionTimer != 0 && (long)(now - actionTimer) >= 0) {
    actionTimer = 0;
    startFireRecheck();
  }
}

void handleFireRecheckState() {
  unsigned long now = millis();

  applyPump(false);
  applyBuzzer(true);
  applyLeds(false, false, true);

  if (now - lastServoTime < scanIntervalMs) {
    return;
  }

  lastServoTime = now;

  currentAngle = constrain(fireRecheckAngle, fireRecheckMinAngle, fireRecheckMaxAngle);
  turretServo.write(currentAngle);

  int score = getFlameScore(flameAnalogValue);

  if (score > bestFlameScore) {
    bestFlameScore = score;
    bestFlameAngle = currentAngle;
  }

  fireRecheckAngle += fireRecheckStep;

  if (fireRecheckAngle > fireRecheckMaxAngle) {
    currentAngle = constrain(bestFlameAngle, 0, 180);
    turretServo.write(currentAngle);

    if (!isFireOrSmokeDetected()) {
      applyBuzzer(false);
      changeAutoState(AUTO_SCAN, "Fire recheck clear. Returning to radar scan.");
      return;
    }

    changeAutoState(FIRE_EXTINGUISH, "Fire still present. Continuing extinguish sequence.");
  }
}

void handleSoilWaterMoveState() {
  if (isFireOrSmokeDetected()) {
    bestFlameScore = 0;
    bestFlameAngle = currentAngle;
    fireSearchAngle = fireSearchMinAngle;
    fireAttempts = 0;

    changeAutoState(FIRE_SEARCH, "Fire detected during soil action. Switching to fire response.");
    return;
  }

  currentAngle = constrain(soilWaterAngle, 0, 180);
  turretServo.write(currentAngle);

  applyPump(false);
  applyBuzzer(false);
  applyLeds(false, true, false);

  changeAutoState(SOIL_WATERING, "Soil watering started");
}

void handleSoilWateringState() {
  unsigned long now = millis();

  if (isFireOrSmokeDetected()) {
    applyPump(false);

    soilPumpRunning = false;
    actionTimer = 0;

    bestFlameScore = 0;
    bestFlameAngle = currentAngle;
    fireSearchAngle = fireSearchMinAngle;
    fireAttempts = 0;

    changeAutoState(FIRE_SEARCH, "Fire detected during soil watering. Switching to fire response.");
    return;
  }

  currentAngle = constrain(soilWaterAngle, 0, 180);
  turretServo.write(currentAngle);

  applyBuzzer(false);
  applyLeds(false, true, false);

  if (isSoilWetEnough()) {
    applyPump(false);

    soilPumpRunning = false;
    actionTimer = 0;

    changeAutoState(AUTO_SCAN, "Soil moisture restored. Returning to radar scan.");
    return;
  }

  if (soilAttempts >= maxSoilAttempts) {
    applyPump(false);

    soilPumpRunning = false;
    actionTimer = 0;

    changeAutoState(AUTO_SCAN, "Max soil watering attempts reached. Returning to radar scan.");
    return;
  }

  if (!soilPumpRunning && actionTimer == 0) {
    soilPumpRunning = true;
    actionTimer = now + soilPumpBurstMs;
    soilAttempts++;

    applyPump(true);
    forceEvent("Soil pump burst started");
    return;
  }

  if (soilPumpRunning && (long)(now - actionTimer) >= 0) {
    soilPumpRunning = false;
    actionTimer = now + soilSettleMs;

    applyPump(false);
    forceEvent("Soil pump paused for moisture recheck");
    return;
  }

  if (!soilPumpRunning && actionTimer != 0 && (long)(now - actionTimer) >= 0) {
    actionTimer = 0;
  }
}

void handleNearDeterrentState() {
  unsigned long now = millis();

  if (isFireOrSmokeDetected()) {
    applyPump(false);

    nearPumpRunning = false;
    actionTimer = 0;

    bestFlameScore = 0;
    bestFlameAngle = currentAngle;
    fireSearchAngle = fireSearchMinAngle;
    fireAttempts = 0;

    changeAutoState(FIRE_SEARCH, "Fire detected during near deterrent. Switching to fire response.");
    return;
  }

  applyLeds(false, true, false);
  applyBuzzer(true);

  turretServo.write(currentAngle);

  if (!nearPumpRunning && actionTimer == 0) {
    nearPumpRunning = true;
    actionTimer = now + nearPumpBurstMs;
    lastNearDeterrentTime = now;

    applyPump(true);
    forceEvent("Near deterrent pump burst started");
    return;
  }

  if (nearPumpRunning && (long)(now - actionTimer) >= 0) {
    nearPumpRunning = false;
    actionTimer = 0;

    applyPump(false);
    applyBuzzer(false);

    changeAutoState(AUTO_SCAN, "Near deterrent finished. Returning to radar scan.");
    return;
  }
}

void handleEmergencyState() {
  stopAllActuators();
  applyLeds(false, false, true);
}

void updateAutoStateMachine() {
  if (!autoMode) return;

  if (emergencyStopActive) {
    autoState = EMERGENCY_STOP_STATE;
  }

  switch (autoState) {
    case AUTO_SCAN:
      handleAutoScanState();
      break;

    case FIRE_SEARCH:
      handleFireSearchState();
      break;

    case FIRE_EXTINGUISH:
      handleFireExtinguishState();
      break;

    case FIRE_RECHECK:
      handleFireRecheckState();
      break;

    case SOIL_WATER_MOVE:
      handleSoilWaterMoveState();
      break;

    case SOIL_WATERING:
      handleSoilWateringState();
      break;

    case NEAR_DETERRENT:
      handleNearDeterrentState();
      break;

    case EMERGENCY_STOP_STATE:
      handleEmergencyState();
      break;
  }
}

// ======================================================
// Manual Mode
// ======================================================

void updateManualTimeout() {
  if (autoMode) return;
  if (emergencyStopActive) return;
  if (manualControlLastSeen == 0) return;

  unsigned long now = millis();

  if (now - manualControlLastSeen > manualControlTimeoutMs) {
    autoMode = true;
    autoState = AUTO_SCAN;

    applyPump(false);
    applyBuzzer(false);

    manualPumpStopAt = 0;

    forceEvent("Manual timeout. Returned to AUTO.");
  }
}

void updateManualPumpSafety() {
  if (autoMode) return;
  if (!pumpOn) return;
  if (manualPumpStopAt == 0) return;

  if ((long)(millis() - manualPumpStopAt) >= 0) {
    applyPump(false);
    manualPumpStopAt = 0;

    forceEvent("Manual pump stopped by safety timer");
  }
}

// ======================================================
// UDP Telemetry
// ======================================================

void sendTelemetry() {
  unsigned long now = millis();

  if (now - lastTelemetryTime < TELEMETRY_INTERVAL_MS) {
    return;
  }

  lastTelemetryTime = now;

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  char json[1600];

  snprintf(
    json,
    sizeof(json),
    "{"
    "\"type\":\"telemetry\","
    "\"device\":\"environmental_turret\","
    "\"millis\":%lu,"
    "\"ip\":\"%s\","
    "\"wifi_connected\":%s,"
    "\"setup_ap\":%s,"
    "\"emergency_stop\":%s,"
    "\"ota_ready\":%s,"
    "\"auto\":%s,"
    "\"auto_state\":\"%s\","
    "\"status\":\"%s\","
    "\"angle\":%d,"
    "\"distance_cm\":%d,"
    "\"near_warning\":%s,"
    "\"near_danger\":%s,"
    "\"near_warning_enabled\":%s,"
    "\"near_pump_deterrent_enabled\":%s,"
    "\"near_warning_distance_cm\":%d,"
    "\"near_danger_distance_cm\":%d,"
    "\"temperature\":%.1f,"
    "\"humidity\":%.1f,"
    "\"smoke\":%d,"
    "\"soil\":%d,"
    "\"flame\":%s,"
    "\"flame_analog\":%d,"
    "\"flame_score\":%d,"
    "\"best_flame_angle\":%d,"
    "\"best_flame_score\":%d,"
    "\"pump\":%s,"
    "\"buzzer\":%s,"
    "\"green\":%s,"
    "\"yellow\":%s,"
    "\"red\":%s,"
    "\"soil_water_angle\":%d,"
    "\"fire_attempts\":%d,"
    "\"soil_attempts\":%d,"
    "\"event\":\"%s\","
    "\"last_command\":\"%s\""
    "}",
    millis(),
    WiFi.localIP().toString().c_str(),
    WiFi.status() == WL_CONNECTED ? "true" : "false",
    setupApRunning ? "true" : "false",
    emergencyStopActive ? "true" : "false",
    otaReady ? "true" : "false",
    autoMode ? "true" : "false",
    autoStateToText(autoState),
    statusToText(systemStatus),
    currentAngle,
    distanceCm,
    isNearWarning() ? "true" : "false",
    isNearDanger() ? "true" : "false",
    nearWarningEnabled ? "true" : "false",
    nearPumpDeterrentEnabled ? "true" : "false",
    nearWarningDistanceCm,
    nearDangerDistanceCm,
    temperatureC,
    humidity,
    smokeValue,
    soilValue,
    flameDetected ? "true" : "false",
    flameAnalogValue,
    getFlameScore(flameAnalogValue),
    bestFlameAngle,
    bestFlameScore,
    pumpOn ? "true" : "false",
    buzzerOn ? "true" : "false",
    greenLed ? "true" : "false",
    yellowLed ? "true" : "false",
    redLed ? "true" : "false",
    soilWaterAngle,
    fireAttempts,
    soilAttempts,
    latestEvent,
    lastCommand
  );

  telemetryUdp.beginPacket(laptopIp, telemetryPort);
  telemetryUdp.write((const uint8_t*)json, strlen(json));
  telemetryUdp.endPacket();
}

// ======================================================
// UDP Commands
// ======================================================

void receiveCommands();

void handleCommandJson(const char* packet) {
  StaticJsonDocument<512> doc;

  DeserializationError error = deserializeJson(doc, packet);

  if (error) {
    setLastCommand("invalid json");
    return;
  }

  const char* type = doc["type"] | "";

  if (strcmp(type, "emergency_stop") == 0 || strcmp(type, "estop") == 0) {
    triggerEmergencyStop("Emergency stop triggered from dashboard");
    setLastCommand("emergency_stop");
    return;
  }

  if (strcmp(type, "clear_emergency") == 0 || strcmp(type, "clear_estop") == 0) {
    clearEmergencyStop();
    setLastCommand("clear_emergency");
    return;
  }

  if (emergencyStopActive) {
    setLastCommand("ignored: emergency stop active");
    return;
  }

  manualControlLastSeen = millis();

  if (strcmp(type, "mode") == 0) {
    autoMode = doc["auto"] | autoMode;

    if (autoMode) {
      autoState = AUTO_SCAN;
      stopAllActuators();
      applyLeds(true, false, false);

      forceEvent("Mode changed to AUTO");
      setLastCommand("mode auto");
    } else {
      stopAllActuators();
      autoState = AUTO_SCAN;

      forceEvent("Mode changed to MANUAL");
      setLastCommand("mode manual");
    }

    return;
  }

  if (strcmp(type, "servo") == 0) {
    autoMode = false;

    int angle = doc["angle"] | currentAngle;
    angle = constrain(angle, 0, 180);

    currentAngle = angle;
    turretServo.write(currentAngle);

    setLastCommand("servo");
    return;
  }

  if (strcmp(type, "pump") == 0) {
    autoMode = false;

    bool value = doc["value"] | false;
    unsigned long ttl = doc["ttl"] | 3000;

    ttl = constrain(ttl, 100UL, maxManualPumpRunMs);

    applyPump(value);

    if (value) {
      manualPumpStopAt = millis() + ttl;
    } else {
      manualPumpStopAt = 0;
    }

    setLastCommand("pump");
    return;
  }

  if (strcmp(type, "buzzer") == 0) {
    autoMode = false;

    bool value = doc["value"] | false;
    applyBuzzer(value);

    setLastCommand("buzzer");
    return;
  }

  if (strcmp(type, "led") == 0) {
    autoMode = false;

    bool g = doc["green"] | false;
    bool y = doc["yellow"] | false;
    bool r = doc["red"] | false;

    applyLeds(g, y, r);

    setLastCommand("led");
    return;
  }

  if (strcmp(type, "all") == 0) {
    autoMode = doc["auto"] | false;

    if (doc.containsKey("servo")) {
      currentAngle = constrain((int)doc["servo"], 0, 180);
      turretServo.write(currentAngle);
    }

    if (doc.containsKey("pump")) {
      bool p = doc["pump"];
      applyPump(p);

      if (p) {
        unsigned long ttl = doc["ttl"] | 3000;
        ttl = constrain(ttl, 100UL, maxManualPumpRunMs);
        manualPumpStopAt = millis() + ttl;
      } else {
        manualPumpStopAt = 0;
      }
    }

    if (doc.containsKey("buzzer")) {
      applyBuzzer((bool)doc["buzzer"]);
    }

    if (doc.containsKey("green") || doc.containsKey("yellow") || doc.containsKey("red")) {
      bool g = doc["green"] | greenLed;
      bool y = doc["yellow"] | yellowLed;
      bool r = doc["red"] | redLed;

      applyLeds(g, y, r);
    }

    setLastCommand("all");
    return;
  }

  if (strcmp(type, "ping") == 0) {
    setLastCommand("ping");
    return;
  }

  setLastCommand("unknown command");
}

void receiveCommands() {
  int packetSize = commandUdp.parsePacket();

  if (packetSize <= 0) {
    return;
  }

  char packet[512];
  int len = commandUdp.read(packet, sizeof(packet) - 1);

  if (len <= 0) {
    return;
  }

  packet[len] = '\0';

  handleCommandJson(packet);
}

// ======================================================
// Serial Debug
// ======================================================

void printStatus() {
  unsigned long now = millis();

  if (now - lastSerialPrintTime < SERIAL_PRINT_INTERVAL_MS) {
    return;
  }

  lastSerialPrintTime = now;

  Serial.println("------ Turret Status ------");
  Serial.print("Wi-Fi: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "NOT CONNECTED");

  Serial.print("STA IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  Serial.print("Laptop IP: ");
  Serial.println(laptopIp);

  Serial.print("Mode: ");
  Serial.println(autoMode ? "AUTO" : "MANUAL");

  Serial.print("Emergency: ");
  Serial.println(emergencyStopActive ? "YES" : "NO");

  Serial.print("Auto State: ");
  Serial.println(autoStateToText(autoState));

  Serial.print("Status: ");
  Serial.println(statusToText(systemStatus));

  Serial.print("Angle: ");
  Serial.println(currentAngle);

  Serial.print("Distance: ");
  Serial.println(distanceCm);

  Serial.print("Temp: ");
  Serial.println(temperatureC);

  Serial.print("Humidity: ");
  Serial.println(humidity);

  Serial.print("Smoke: ");
  Serial.println(smokeValue);

  Serial.print("Soil: ");
  Serial.println(soilValue);

  Serial.print("Flame digital: ");
  Serial.println(flameDetected ? "YES" : "NO");

  Serial.print("Flame analog: ");
  Serial.println(flameAnalogValue);

  Serial.print("Pump: ");
  Serial.println(pumpOn ? "ON" : "OFF");

  Serial.println("---------------------------");
}

// ======================================================
// Setup
// ======================================================

void setup() {
  Serial.begin(115200);

  pinMode(ULTRASONIC_TRIG_PIN, OUTPUT);
  pinMode(ULTRASONIC_ECHO_PIN, INPUT);

  pinMode(FLAME_DIGITAL_PIN, INPUT);
  pinMode(FLAME_ANALOG_PIN, INPUT);

  pinMode(PUMP_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);

  stopAllActuators();
  applyLeds(true, false, false);

  dht.begin();

  turretServo.setPeriodHertz(50);
  turretServo.attach(SERVO_PIN, 500, 2400);
  turretServo.write(currentAngle);

  loadSettings();

  connectWiFiInitial();

  setupOTA();
  setupConfigServer();

  telemetryUdp.begin(4211);
  commandUdp.begin(commandPort);

  Serial.print("Command UDP listening on port: ");
  Serial.println(commandPort);

  forceEvent("System ready. Presentation firmware active.");
}

// ======================================================
// Loop
// ======================================================

void loop() {
  ArduinoOTA.handle();
  configServer.handleClient();

  updateWiFiReconnect();

  receiveCommands();

  updateUltrasonic();
  updateFastSensors();
  updateDht();

  updateThreatStatus();

  if (emergencyStopActive) {
    handleEmergencyState();
  } else {
    updateManualTimeout();

    if (autoMode) {
      updateAutoStateMachine();
    } else {
      updateManualPumpSafety();
    }
  }

  enforcePumpSafety();

  sendTelemetry();

  printStatus();
}