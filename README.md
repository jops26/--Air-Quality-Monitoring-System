# ESP32 Air Quality Monitoring System

**Environmental monitoring prototype | ESP32 • IoT • Embedded Systems**

An ESP32-based monitoring system for collecting indoor environmental readings and making them available over a local Wi-Fi API. The device combines temperature and humidity measurements with estimated air-quality indicators from a CCS811 and an analog gas-sensor signal from a MiCS module.

> **Project status:** Development prototype. This repository documents an ESP32 firmware version reconstructed from an earlier project configuration. It has not been independently validated against reference instruments and is **not** a certified environmental monitor or gas-safety alarm.

## What the device does

- Reads **air temperature** (°C) and **relative humidity** (%) using a DHT11.
- Reads **equivalent CO₂ (eCO₂)** in ppm and **total volatile organic compounds (TVOC)** in ppb using a CJMCU-811 / CCS811 module.
- Samples the **raw analog output** and voltage of a MiCS gas-sensor breakout. These values are sensor responses **not calibrated CO concentrations**.
- Prints readings and connection diagnostics to the **Arduino Serial Monitor**.
- Provides a lightweight **local HTTP JSON endpoint** for dashboards or mobile/web applications.
- Optionally sends readings to an external HTTP server (**AirWatch API**).
- Supports a configurable **static IP address** and Wi-Fi auto-reconnection.

## Hardware components

| Component | Purpose | Quantity |
|---|---|---:|
| ESP32 DevKit V1 (or compatible) | Microcontroller, Wi-Fi, and data processing | 1 |
| DHT11 | Temperature and humidity | 1 |
| CJMCU-811 / CCS811 | eCO₂ and TVOC estimates | 1 |
| MiCS analog gas-sensor module (board-dependent) | Uncalibrated analog gas-sensor response | 1 |
| Jumper wires, USB cable, suitable power supply | Wiring and power | As needed |
| Wi-Fi router | Local network and API access | 1 |
| Computer or server (optional) | Receives remote HTTP data | 1 |

## ESP32 wiring

| Sensor/module | Signal | ESP32 pin |
|---|---|---|
| DHT11 | DATA | GPIO 32 |
| CCS811 | SDA | GPIO 21 |
| CCS811 | SCL | GPIO 22 |
| MiCS module | Analog output | GPIO 35 (ADC1) |
| All modules | GND | Common GND |

**Power and voltage safety:** Supply each breakout according to its own board specifications. ESP32 inputs are **not 5 V tolerant**; make sure analog and I²C signal levels do not exceed 3.3 V. Use an appropriate divider or level shifter where necessary. GPIO35 is input-only.

## Firmware

The main Arduino sketch is:

[`Air_Quality_Monitoring_System.ino`](./Air_Quality_Monitoring_System.ino)

### Libraries and development environment

1. Install **Arduino IDE** and the **ESP32 board package by Espressif Systems**.
2. Install **DHT sensor library by Adafruit**.
3. Install **Adafruit Unified Sensor** (dependency).
4. Install **Adafruit CCS811 Library**.
5. Select your ESP32 board and a working USB serial port.

### Configuration

Open the `.ino` file and edit these values **only in your private local copy**:

```cpp
const char* WIFI_SSID     = "";  // Your Wi-Fi network name
const char* WIFI_PASSWORD = "";  // Your Wi-Fi password

const bool ENABLE_STATIC_IP   = true;
const bool ENABLE_REMOTE_POST = true;
```

The sketch's example network configuration is:

```cpp
IPAddress local_IP(192, 168, 0, 150);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

const char* SERVER_URL = "http://192.168.0.100:4000/api/sensors/add";
```

These addresses are **examples inherited from the prototype**; adapt them to your router and server. Set `ENABLE_REMOTE_POST = false` if you are not using the external server. If the static address is not on your network, update it or set `ENABLE_STATIC_IP = false` to use DHCP.

**Security:** Never upload real Wi-Fi credentials, tokens, passwords, or personal data to a public repository. If you enter credentials directly into the sketch, keep that edited file private and publish only the sanitized version.

### Run the device

1. Connect the modules using the wiring table above.
2. Configure the sketch locally and upload it to the ESP32.
3. Open **Serial Monitor** at **115200 baud**.
4. Confirm that DHT11 and CCS811 readings are displayed. The first CCS811 readings may need time to stabilize.
5. After Wi-Fi connects, open the local API address in your browser or app.

## Local API

**Endpoint:** `GET http://<ESP32-IP>/api/sensors`

With the sample static IP, the endpoint would be:

```text
http://192.168.0.150/api/sensors
```

Example response (illustrative values only):

```json
{
  "temperature_c": 26.0,
  "humidity_percent": 33.0,
  "eco2_ppm": 637,
  "tvoc_ppb": 36,
  "mics_raw": 500,
  "mics_voltage_v": 0.400,
  "co_ppm": null,
  "dht_ok": true,
  "ccs_ok": true,
  "wifi_connected": true
}
```

Readings are refreshed about every **5 seconds**. JSON `null` is used for unavailable/uncalibrated values rather than inventing measurements.

### Optional remote reporting

When enabled, the ESP32 posts readings approximately every **15 seconds** to the configured AirWatch server endpoint. The compatibility payload uses these fields:

```json
{
  "tvoc": 36,
  "co": null,
  "co2": 637,
  "temperature": 26.0,
  "humidity": 33.0
}
```

**Important:** The legacy field `co2` contains the CCS811 **eCO₂ estimate**, not a direct CO₂ measurement. The `co` field remains `null`, because the MiCS signal has not been calibrated into a reliable CO concentration. Your backend must accept `null` values.

## Measurement limitations

- **CCS811 eCO₂ is an estimate**, inferred from air-quality sensing; it is not a nondispersive infrared (NDIR) CO₂ measurement.
- **MiCS raw ADC counts and voltage are not gas concentration values.** Reliable concentration estimates require the correct sensor model, warm-up, calibration, and reference measurements.
- **DHT11 is a basic sensor** with limited precision and range compared with laboratory equipment.
- This prototype must **not** be used for safety-critical gas detection, health decisions, regulatory compliance, or emergency alerts.
- The optional HTTP server and local endpoint are intended for a **trusted network**; the sketch does not implement API authentication or transport encryption.

## Planned improvements

- Validate measurements against calibrated reference instruments.
- Add recorded calibration procedures for the installed MiCS sensor model.
- Improve error reporting, local data logging, and disconnected operation.
- Add secure authenticated telemetry for deployments outside a trusted network.
- Publish photographs, schematic diagrams, and field-test findings once available.

## Repository

[GitHub — Air Quality Monitoring System](https://github.com/jops26/--Air-Quality-Monitoring-System)

**Documentation note:** Features and pins in this README describe the accompanying firmware. Claims of deployment performance or certified accuracy are not made.
