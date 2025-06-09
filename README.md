
# ESP32 Non-Blocking WiFiManager + MQTT + LittleFS + Home Assistant

## Prerequisites

- **Hardware:** ESP32 board, DHT22 sensor, air pump relay, status LED, push button.
- **Libraries:**
    - [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
    - [WiFiManager](https://github.com/tzapu/WiFiManager)
    - [PubSubClient](https://github.com/knolleary/pubsubclient)
    - [LittleFS_esp32](https://github.com/lorol/LITTLEFS)
    - [Button2](https://github.com/LennartHennigs/Button2)
    - [ezLED](https://github.com/raphaelbs/ezLED)
    - [DHT sensor library](https://github.com/adafruit/DHT-sensor-library)
    - [TickTwo](https://github.com/RobTillaart/TickTwo)
- **Platform:** Arduino IDE or PlatformIO

## Features

- Non-blocking WiFi configuration portal using WiFiManager.
- MQTT client with Home Assistant auto-discovery support.
- Persistent configuration storage using LittleFS.
- Air pump control and state persistence via Preferences.
- DHT22 temperature and humidity sensor integration.
- Status LED and air pump relay control via ezLED.
- Button2 for WiFi reset and air pump toggle.
- Modular, non-blocking task scheduling with TickTwo.
- Debug output with optional compile-time switch.
- Easy configuration backup and restore.
- Optional static IP configuration.
- Home Assistant entity removal support.
