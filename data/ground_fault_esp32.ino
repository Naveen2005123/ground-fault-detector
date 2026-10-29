// ESP32 + ADS1115 + SSD1306 + ACS712 + RGB LED -> Firebase Realtime Database (v3, auto-calibrated)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "secrets.h"
#define DB_URL "https://ground-fault-detection-default-rtdb.asia-southeast1.firebasedatabase.app"

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_ADS1115 ads;
WiFiClientSecure client;

#define RED_PIN 25
#define GREEN_PIN 26
#define BLUE_PIN 27
const bool COMMON_ANODE = false;

// GREEN/YELLOW = stable (below 1.00), RED = voltage OR current >= 1.00
const float WARNING_LEVEL = 0.50;   // V or A -> yellow
const float ALERT_LEVEL   = 1.00;   // V or A -> red
const float CURRENT_DEADBAND_A = 0.05;
const float MAX_CURRENT_A = 20.0;   // ACS712-20A limit

float zeroCurrentV = 0.0;
float sensitivityVperA = 0.05;      // set automatically in calibration
bool sensorFault = false;
String lastStatus = "";
unsigned long lastSend = 0;
bool cloudOk = false;

void setRGB(bool r, bool g, bool b) {
  digitalWrite(RED_PIN,   (r ^ COMMON_ANODE) ? HIGH : LOW);
  digitalWrite(GREEN_PIN, (g ^ COMMON_ANODE) ? HIGH : LOW);
  digitalWrite(BLUE_PIN,  (b ^ COMMON_ANODE) ? HIGH : LOW);
}

float readCurrentSensorVoltage(int samples) {
  double total = 0.0;
  for (int i = 0; i < samples; i++) { total += ads.computeVolts(ads.readADC_SingleEnded(1)); delay(2); }
  return total / samples;
}

// ACS712 idles at VCC/2 and its sensitivity scales with VCC (100 mV/A at 5 V),
// so sensitivity = 0.04 x idle voltage. Works with or without a divider.
void calibrateCurrent() {
  display.clearDisplay(); display.setCursor(0, 0);
  display.println("CALIBRATING"); display.println("Current must be 0 A"); display.println("Please wait..."); display.display();
  unsigned long t0 = millis(); float z = 0;
  while (millis() - t0 < 15000) {                 // wait for sensor to power up
    z = readCurrentSensorVoltage(100);
    if (z >= 1.0) break;
    delay(500);
  }
  if (z < 1.0) { sensorFault = true; zeroCurrentV = z; Serial.println("ACS712 FAULT: A1 voltage too low"); return; }
  sensorFault = false;
  zeroCurrentV = readCurrentSensorVoltage(300);
  sensitivityVperA = 0.04 * zeroCurrentV;
  Serial.printf("Zero=%.4f V  Sensitivity=%.4f V/A\n", zeroCurrentV, sensitivityVperA);
}

bool firebaseSend(const char *method, const char *path, const char *json) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = String(DB_URL) + path + ".json";
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest(method, (uint8_t *)json, strlen(json));
  http.end();
  return code == 200;
}

void setup() {
  Serial.begin(115200);
  pinMode(RED_PIN, OUTPUT); pinMode(GREEN_PIN, OUTPUT); pinMode(BLUE_PIN, OUTPUT);
  setRGB(false, false, true);
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { Serial.println("OLED ERROR"); while (true) delay(1000); }
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  if (!ads.begin(0x48)) {
    display.clearDisplay(); display.setCursor(0, 0); display.println("ADS1115 ERROR"); display.println("Check connections"); display.display();
    while (true) { setRGB(true, false, false); delay(500); setRGB(false, false, false); delay(500); }
  }
  ads.setGain(GAIN_ONE);
  delay(1000);
  calibrateCurrent();

  display.clearDisplay(); display.setCursor(0, 0);
  display.println("Connecting WiFi..."); display.println(WIFI_SSID); display.display();
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(300); Serial.print("."); }
  client.setInsecure();
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nWiFi OK" : "\nWiFi FAILED (offline mode)");
}

void loop() {
  float voltageV = ads.computeVolts(ads.readADC_SingleEnded(0));
  float sensorV = readCurrentSensorVoltage(100);
  float currentA = sensorFault ? 0.0 : fabs(sensorV - zeroCurrentV) / sensitivityVperA;
  if (currentA < CURRENT_DEADBAND_A) currentA = 0.0;
  if (currentA > MAX_CURRENT_A) currentA = MAX_CURRENT_A;
  float currentmA = currentA * 1000.0;

  float level = max(voltageV, currentA);          // worst of voltage / current
  String status;
  if (level >= ALERT_LEVEL)        { status = "ALERT";   setRGB(true, false, false); }
  else if (level >= WARNING_LEVEL) { status = "WARNING"; setRGB(true, true, false); }
  else                             { status = "NORMAL";  setRGB(false, true, false); }

  if (millis() - lastSend > 600) {
    lastSend = millis();
    char body[160];
    snprintf(body, sizeof(body), "{\"voltage\":%.4f,\"currentA\":%.4f,\"sensorV\":%.4f,\"status\":\"%s\"}", voltageV, currentA, sensorV, status.c_str());
    cloudOk = firebaseSend("PUT", "/groundfault/live", body);
    if (cloudOk && status != lastStatus) {
      snprintf(body, sizeof(body), "{\"status\":\"%s\",\"currentA\":%.4f,\"voltage\":%.4f,\"t\":{\".sv\":\"timestamp\"}}", status.c_str(), currentA, voltageV);
      if (firebaseSend("POST", "/groundfault/events", body)) lastStatus = status;
    }
  }

  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(0, 0);
  display.println("ELECTRICAL MONITOR"); display.println("--------------------");
  display.print("Voltage: "); display.print(voltageV, 3); display.println(" V");
  display.print("Current: "); display.print(currentA, 3); display.println(" A");
  display.print("Status: "); display.println(status);
  display.println(sensorFault ? "Check ACS712 (A1)!" : "Sensor OK");
  display.print(cloudOk ? "Cloud: OK" : (WiFi.status() == WL_CONNECTED ? "Cloud: error" : "WiFi: offline"));
  display.display();

  Serial.printf("A0=%.4f V | A1=%.4f V | Zero=%.4f V | I=%.1f mA | %s | cloud=%d\n", voltageV, sensorV, zeroCurrentV, currentmA, status.c_str(), cloudOk);
  delay(200);
}
