/*
 * ============================================================================
 * AIR QUALITY MONITORING SYSTEM | ESP32
 * ============================================================================
 * Repository: https://github.com/jops26/--Air-Quality-Monitoring-System
 * Project type: Environmental monitoring / IoT / embedded systems
 * Firmware: ESP32 (Arduino framework)
 *
 * PROJECT OVERVIEW
 * ----------------
 * This project monitors indoor environmental conditions using an ESP32 and
 * three sensor modules. Readings can be viewed over USB Serial, accessed as
 * JSON through a local web API, and optionally sent to an AirWatch server.
 *
 * MAIN FUNCTIONS
 * --------------
 * 1. Read temperature and relative humidity from a DHT11 sensor.
 * 2. Read eCO2 (estimated/equivalent CO2) and TVOC from a CJMCU-811 / CCS811.
 * 3. Read the uncalibrated analog response of a MiCS gas-sensor module.
 * 4. Display the readings and connection status in the Serial Monitor.
 * 5. Provide local JSON data at GET http://<ESP32-IP>/api/sensors.
 * 6. Optionally POST sensor readings to an external AirWatch HTTP API.
 * 7. Connect to Wi-Fi with a fixed IP address and automatic reconnection.
 *
 * COMPONENTS
 * ----------
 * - 1 x ESP32 DevKit V1 or compatible ESP32 board
 * - 1 x DHT11 temperature / humidity sensor
 * - 1 x CJMCU-811 (CCS811) air-quality sensor module
 * - 1 x MiCS analog gas-sensor module (e.g., MiCS-5524 board)
 * - Jumper wires, USB cable, and appropriate power supply
 * - Wi-Fi router; optional laptop/server for the AirWatch API
 *
 * PIN CONNECTIONS
 * ---------------
 * DHT11 DATA           -> ESP32 GPIO32
 * CCS811 SDA           -> ESP32 GPIO21
 * CCS811 SCL           -> ESP32 GPIO22
 * MiCS analog output   -> ESP32 GPIO35 (ADC input)
 * All module grounds   -> ESP32 GND (common ground)
 *
 * Power modules according to their SPECIFIC breakout-board requirements.
 * ESP32 GPIO inputs are NOT 5 V tolerant. Ensure the MiCS analog output and
 * I2C signal levels do not exceed 3.3 V; use level shifting/dividers where
 * needed. GPIO35 is input-only and belongs to ADC1, which works with Wi-Fi.
 *
 * SOFTWARE REQUIREMENTS (install in Arduino IDE Library Manager)
 * -------------------------------------------------------------
 * - DHT sensor library by Adafruit
 * - Adafruit Unified Sensor (required by DHT library)
 * - Adafruit CCS811 Library
 * - The ESP32 board support package by Espressif Systems
 *
 * CONFIGURATION BEFORE FLASHING
 * -----------------------------
 * 1. Insert your Wi-Fi network details ONLY in your private local copy below.
 *    Never commit personal Wi-Fi credentials to a public repository.
 * 2. Previous fixed network setup (change to match your own router):
 *      ESP32 IP: 192.168.0.150
 *      Gateway:  192.168.0.1
 *      Subnet:   255.255.255.0
 * 3. Previous AirWatch server endpoint (change if laptop/server IP differs):
 *      http://192.168.0.100:4000/api/sensors/add
 *    Set ENABLE_REMOTE_POST = false if there is no external server.
 * 4. Select an ESP32 board and set Serial Monitor baud to 115200.
 *
 * DATA LIMITATIONS / SCIENTIFIC ACCURACY
 * --------------------------------------
 * - CCS811 reports eCO2 (an estimate), NOT direct CO2 concentration.
 * - MiCS analog counts/voltage are NOT calibrated carbon-monoxide (CO) ppm.
 *   Calibrating that measurement requires sensor-specific procedures and
 *   reference measurements. For this reason, the legacy server field "co"
 *   is sent as JSON null rather than a fabricated concentration.
 * - The legacy server field "co2" contains CCS811 eCO2 for compatibility.
 * - This device is a development prototype, not a certified safety alarm.
 *
 * DATA FORMAT
 * -----------
 * GET /api/sensors example:
 * {
 *   "temperature_c": 26.0,
 *   "humidity_percent": 33.0,
 *   "eco2_ppm": 637,
 *   "tvoc_ppb": 36,
 *   "mics_raw": 500,
 *   "mics_voltage_v": 0.400,
 *   "co_ppm": null,
 *   "dht_ok": true,
 *   "ccs_ok": true,
 *   "wifi_connected": true
 * }
 * Example numbers above are illustrative, not actual live measurements.
 *
 * The legacy remote API payload has these fields:
 *   "tvoc", "co", "co2", "temperature", "humidity"
 * The remote backend must allow JSON null for missing/uncalibrated values.
 *
 * NOTE ON CODE ORIGIN
 * -------------------
 * Reconstructed and documented from the project's earlier ESP32 sketch and
 * saved configuration. It preserves the prior sensor pins, fixed-IP scheme,
 * local API path, and remote API address, but is not a verbatim archive of
 * every line of the original sketch.
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_CCS811.h>

// ============================================================================
// CONFIGURATION -- NO PRIVATE WI-FI CREDENTIALS INCLUDED
// ============================================================================
const char* WIFI_SSID     = ""; // Fill in locally; do not upload actual value
const char* WIFI_PASSWORD = ""; // Fill in locally; do not upload actual value

const bool ENABLE_STATIC_IP  = true;
const bool ENABLE_REMOTE_POST = true;

IPAddress local_IP(192, 168, 0, 150);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(1, 1, 1, 1);

const char* SERVER_URL = "http://192.168.0.100:4000/api/sensors/add";

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
#define DHT_PIN   32
#define DHT_TYPE  DHT11
#define CCS_SDA   21
#define CCS_SCL   22
#define MICS_PIN  35

// ============================================================================
// SCHEDULING
// ============================================================================
const unsigned long SENSOR_INTERVAL_MS = 5000UL;
const unsigned long POST_INTERVAL_MS   = 15000UL;
const unsigned long STATUS_INTERVAL_MS = 30000UL;

DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_CCS811 ccs;
WebServer server(80);

// Most recent readings
float temperatureC = NAN;
float humidityPercent = NAN;
uint16_t eco2Ppm = 0;
uint16_t tvocPpb = 0;
uint16_t micsRaw = 0;
float micsVoltageV = 0.0f;

bool dhtOk = false;
bool ccsInitialized = false;
bool ccsOk = false;

unsigned long lastSensorRead = 0;
unsigned long lastPost = 0;
unsigned long lastStatus = 0;

// Format absent readings as JSON null, rather than invalid numeric values.
String jsonFloat(float value, unsigned int decimals = 1) {
  return isnan(value) ? String("null") : String(value, decimals);
}

String jsonUnsignedOrNull(unsigned long value, bool valid) {
  return valid ? String(value) : String("null");
}

const char* jsonBool(bool value) {
  return value ? "true" : "false";
}

// ============================================================================
// SENSOR ACQUISITION
// ============================================================================
void readSensors() {
  const float newHumidity = dht.readHumidity();
  const float newTemperature = dht.readTemperature();

  dhtOk = !isnan(newHumidity) && !isnan(newTemperature);
  if (dhtOk) {
    temperatureC = newTemperature;
    humidityPercent = newHumidity;
  } else {
    temperatureC = NAN;
    humidityPercent = NAN;
  }

  if (ccsInitialized) {
    if (dhtOk) {
      ccs.setEnvironmentalData(humidityPercent, temperatureC);
    }

    if (ccs.available()) {
      if (ccs.readData() == 0) {
        eco2Ppm = ccs.geteCO2();
        tvocPpb = ccs.getTVOC();
        ccsOk = true;
      } else {
        ccsOk = false;
        Serial.println("[CCS811] Read error");
      }
    }
  }

  // The analog value is sensor response only, not a calibrated CO reading.
  micsRaw = static_cast<uint16_t>(analogRead(MICS_PIN));
  micsVoltageV = analogReadMilliVolts(MICS_PIN) / 1000.0f;
}

// ============================================================================
// LOCAL JSON API: GET /api/sensors
// ============================================================================
String buildLocalJson() {
  String json;
  json.reserve(320);
  json += "{";
  json += "\"temperature_c\":" + jsonFloat(temperatureC, 1) + ",";
  json += "\"humidity_percent\":" + jsonFloat(humidityPercent, 1) + ",";
  json += "\"eco2_ppm\":" + jsonUnsignedOrNull(eco2Ppm, ccsOk) + ",";
  json += "\"tvoc_ppb\":" + jsonUnsignedOrNull(tvocPpb, ccsOk) + ",";
  json += "\"mics_raw\":" + String(micsRaw) + ",";
  json += "\"mics_voltage_v\":" + String(micsVoltageV, 3) + ",";
  json += "\"co_ppm\":null,";
  json += "\"dht_ok\":" + String(jsonBool(dhtOk)) + ",";
  json += "\"ccs_ok\":" + String(jsonBool(ccsOk)) + ",";
  json += "\"wifi_connected\":" + String(jsonBool(WiFi.status() == WL_CONNECTED));
  json += "}";
  return json;
}

void handleSensors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json; charset=utf-8", buildLocalJson());
}

void handleHome() {
  server.send(200, "text/plain; charset=utf-8",
              "ESP32 Air Quality Monitoring System\n"
              "GET /api/sensors for current JSON readings.\n");
}

// ============================================================================
// LEGACY REMOTE AIRWATCH SERVER
// ============================================================================
String buildRemoteJson() {
  String json;
  json.reserve(180);
  json += "{";
  json += "\"tvoc\":" + jsonUnsignedOrNull(tvocPpb, ccsOk) + ",";
  json += "\"co\":null,"; // No validated CO ppm measurement available.
  json += "\"co2\":" + jsonUnsignedOrNull(eco2Ppm, ccsOk) + ","; // eCO2.
  json += "\"temperature\":" + jsonFloat(temperatureC, 1) + ",";
  json += "\"humidity\":" + jsonFloat(humidityPercent, 1);
  json += "}";
  return json;
}

void postToAirWatch() {
  if (!ENABLE_REMOTE_POST || WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.setTimeout(4000);
  http.addHeader("Content-Type", "application/json");

  const String payload = buildRemoteJson();
  const int responseCode = http.POST(payload);

  if (responseCode > 0) {
    Serial.printf("[AirWatch] HTTP %d\n", responseCode);
    if (responseCode >= 400) {
      Serial.println("[AirWatch] Check endpoint, API schema, and null handling.");
    }
  } else {
    Serial.printf("[AirWatch] POST failed (%d): %s\n",
                  responseCode, http.errorToString(responseCode).c_str());
  }
  http.end();
}

// ============================================================================
// WI-FI
// ============================================================================
void startWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("[WiFi] SSID is blank. Add credentials to your PRIVATE copy.");
    Serial.println("[WiFi] Sensor readings will still appear on Serial Monitor.");
    return;
  }

  if (ENABLE_STATIC_IP) {
    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
      Serial.println("[WiFi] Static-IP configuration failed; verify router settings.");
    }
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("[WiFi] Connection initiated; auto-reconnect enabled.");
  // IMPORTANT: Do not call WiFi.reconnect() in a tight loop. That previously
  // caused 'sta is connecting, return error' messages during connection.
}

// ============================================================================
// SERIAL REPORT
// ============================================================================
void printReadings() {
  Serial.println("\n----- SENSOR DATA -----");
  Serial.print("Temperature: ");
  Serial.print(jsonFloat(temperatureC, 1));
  Serial.println(" C");

  Serial.print("Humidity: ");
  Serial.print(jsonFloat(humidityPercent, 1));
  Serial.println(" %");

  Serial.print("eCO2 (estimated): ");
  Serial.print(jsonUnsignedOrNull(eco2Ppm, ccsOk));
  Serial.println(" ppm");

  Serial.print("TVOC: ");
  Serial.print(jsonUnsignedOrNull(tvocPpb, ccsOk));
  Serial.println(" ppb");

  Serial.print("MiCS Raw: ");
  Serial.println(micsRaw);
  Serial.print("MiCS Voltage: ");
  Serial.print(micsVoltageV, 3);
  Serial.println(" V");
}

// ============================================================================
// ARDUINO ENTRY POINTS
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nESP32 Air Quality Monitoring System");

  dht.begin();
  Wire.begin(CCS_SDA, CCS_SCL);
  ccsInitialized = ccs.begin();
  if (ccsInitialized) {
    Serial.println("[CCS811] Sensor initialized.");
  } else {
    Serial.println("[CCS811] Not detected; check SDA/SCL, power, and address.");
  }

  analogReadResolution(12);
  // GPIO35 is an ADC1 input: usable while the ESP32 Wi-Fi is running.

  readSensors();
  printReadings();

  server.on("/", HTTP_GET, handleHome);
  server.on("/api/sensors", HTTP_GET, handleSensors);
  server.onNotFound([]() {
    server.send(404, "application/json", "{\"error\":\"Not found\"}");
  });

  startWiFi();
  server.begin();
  Serial.println("[HTTP] Local API registered at /api/sensors");
}

void loop() {
  server.handleClient();
  const unsigned long now = millis();

  if (now - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = now;
    readSensors();
    printReadings();
  }

  if (now - lastPost >= POST_INTERVAL_MS) {
    lastPost = now;
    postToAirWatch();
  }

  if (now - lastStatus >= STATUS_INTERVAL_MS) {
    lastStatus = now;
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WiFi] Connected. API: http://");
      Serial.print(WiFi.localIP());
      Serial.println("/api/sensors");
    } else {
      Serial.println("[WiFi] Not connected; sensor data available on Serial.");
    }
  }

  delay(2);
}
