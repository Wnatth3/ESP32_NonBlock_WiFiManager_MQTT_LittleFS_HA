/*
0.1 - Update ArduinoJson to 7.2.1
    - Update ezLED to 2.3.2
0.0 - Base on ESP32_WiFiManager_MQTT_SPIFFS_NonBlock
    - Change Serial.print to DebugMode
    - You can use wifi without MQTT Broker by removing the MQTT Broker field on web UI.
*/

#include <Arduino.h>
#include <FS.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>  //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>
#include <Button2.h>
#include <ezLED.h>
#include <DHT.h>
#include <TickTwo.h>

//******************************** Configulation ****************************//
#define FORMAT_LITTLEFS_IF_FAILED true

#define _DEBUG_  // Comment this line if you don't want to debug
// #define _RemoveEntity  // Uncomment this line if you want to remove the entity from Home Assistant

//******************************** Variables & Objects **********************//
#define deviceName "MyESP32"

const char* filename = "/config.txt";  // Config file name

bool mqttParameter;
//----------------- esLED ---------------------//
#define ledPin     LED_BUILTIN
#define testLedPin 4

ezLED statusLed(ledPin);
ezLED testLed(testLedPin);

bool testLedState = false;

//----------------- Preferences ---------------//
#define kLed "ledSt"
bool        testledState;
Preferences pf;

//----------------- Reset WiFi Button ---------//
#define resetWifiBtPin 0
Button2 resetWifiBt;

//----------------- WiFi Manager --------------//
char mqttBroker[16] = "192.168.0.10";
char mqttPort[6]    = "1883";
char mqttUser[10];
char mqttPass[10];

WiFiManager wifiManager;

WiFiManagerParameter customMqttBroker("broker", "mqtt server", mqttBroker, 16);
WiFiManagerParameter customMqttPort("port", "mqtt port", mqttPort, 6);
WiFiManagerParameter customMqttUser("user", "mqtt user", mqttUser, 10);
WiFiManagerParameter customMqttPass("pass", "mqtt pass", mqttPass, 10);

//----------------- MQTT ----------------------//
#define stateTopicDht22Temp   "dht22/temp"
#define configTopicDht22Temp  "homeassistant/sensor/dht22Temp/config"
#define stateTopicDht22Humi   "dht22/humi"
#define configTopicDht22Humi  "homeassistant/sensor/dht22Humi/config"
#define stateTopicAirPumpSw   "airPump/switch"
#define commandTopicAirPumpSw "airPump/switch/set"
#define configTopicAirPumpSw  "homeassistant/switch/airPumpSw/config"

WiFiClient   espClient;
PubSubClient mqtt(espClient);

//----------------- DHT22 ---------------------//
#define DHTPIN  33
#define DHTTYPE DHT22

float temp;
float humi;

DHT dht(DHTPIN, DHTTYPE);

//******************************** Tasks ************************************//
void    connectMqtt();
void    reconnectMqtt();
TickTwo tConnectMqtt(connectMqtt, 0, 0, MILLIS);  // (function, interval, iteration, interval unit)
TickTwo tReconnectMqtt(reconnectMqtt, 3000, 0, MILLIS);

void    readSendData();
TickTwo tReadSendData(readSendData, 1000, 0, MILLIS);  // (function, interval, iteration, interval unit)

//******************************** Functions ********************************//
//----------------- LittleFS ------------------//
// Loads the configuration from a file
void loadConfiguration(fs::FS& fs, const char* filename) {
    // Open file for reading
    File file = fs.open(filename, "r");
    if (!file) {
#ifdef _DEBUG_
        Serial.println(F("Failed to open data file"));
#endif
        return;
    }

    // Allocate a temporary JsonDocument
    JsonDocument doc;
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
#ifdef _DEBUG_
        Serial.println(F("Failed to read file, using default configuration"));
#endif
    }
    // Copy values from the JsonDocument to the Config
    // strlcpy(Destination_Variable, doc["Source_Variable"] /*| "Default_Value"*/, sizeof(Destination_Name));
    strlcpy(mqttBroker, doc["mqttBroker"], sizeof(mqttBroker));
    strlcpy(mqttPort, doc["mqttPort"], sizeof(mqttPort));
    strlcpy(mqttUser, doc["mqttUser"], sizeof(mqttUser));
    strlcpy(mqttPass, doc["mqttPass"], sizeof(mqttPass));
    mqttParameter = doc["mqttParameter"];

    file.close();
}

void handleMqttMessage(char* topic, byte* payload, unsigned int length) {
    String message;
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    if (String(topic) == commandTopicAirPumpSw) {
        if (message == "ON") {
            testLed.turnON();
        } else if (message == "OFF") {
            testLed.turnOFF();
        }
    }
}

void mqttInit() {
#ifdef _DEBUG_
    Serial.print(F("MQTT parameters are "));
#endif
    if (mqttParameter) {
#ifdef _DEBUG_
        Serial.println(F(" available"));
#endif
        mqtt.setBufferSize(512);
        mqtt.setCallback(handleMqttMessage);
        mqtt.setServer(mqttBroker, atoi(mqttPort));
        tConnectMqtt.start();
    } else {
#ifdef _DEBUG_
        Serial.println(F(" not available."));
#endif
    }
}

void saveParamsCallback() {
    // saveConfiguration(LittleFS, filename);
    strcpy(mqttBroker, customMqttBroker.getValue());
    strcpy(mqttPort, customMqttPort.getValue());
    strcpy(mqttUser, customMqttUser.getValue());
    strcpy(mqttPass, customMqttPass.getValue());
#ifdef _DEBUG_
    Serial.println(F("The values are updated."));
#endif

    // Delete existing file, otherwise the configuration is appended to the file
    // LittleFS.remove(filename);

    File file = LittleFS.open(filename, "w");
    if (!file) {
#ifdef _DEBUG_
        Serial.println(F("Failed to open config file for writing"));
#endif
        return;
    }

    // Allocate a temporary JsonDocument
    JsonDocument doc;
    // Set the values in the document
    doc["mqttBroker"] = mqttBroker;
    doc["mqttPort"]   = mqttPort;
    doc["mqttUser"]   = mqttUser;
    doc["mqttPass"]   = mqttPass;
#ifdef _DEBUG_
    Serial.print(F("The configuration has been saved to "));
    Serial.println(filename);
#endif

    if (doc["mqttBroker"] != "") {
        doc["mqttParameter"] = true;
        mqttParameter        = doc["mqttParameter"];
    }

    // Serialize JSON to file
    if (serializeJson(doc, file) == 0) {
#ifdef _DEBUG_
        Serial.println(F("Failed to write to file"));
#endif
    }

    file.close();  // Close the file
#ifdef _DEBUG_
    Serial.println(F("Configuration saved"));
#endif

    mqttInit();
}

// Prints the content of a file to the Serial
void printFile(fs::FS& fs, const char* filename) {
    // Open file for reading
    File file = fs.open(filename, "r");
    if (!file) {
#ifdef _DEBUG_
        Serial.println(F("Failed to open data file"));
#endif
        return;
    }

    // Extract each characters by one by one
    while (file.available()) {
        Serial.print((char)file.read());
    }
    Serial.println();

    file.close();  // Close the file
}

void deleteFile(fs::FS& fs, const char* path) {
#ifdef _DEBUG_
    Serial.print(F("Deleting file: "));
    Serial.println(String(path) + "\r\n");
#endif
    if (fs.remove(path)) {
#ifdef _DEBUG_
        Serial.println(F("- file deleted"));
#endif
    } else {
#ifdef _DEBUG_
        Serial.println(F("- delete failed"));
#endif
    }
}

//----------------- Wifi Manager --------------//
void wifiManagerSetup() {
#ifdef _DEBUG_
    Serial.println(F("Loading configuration..."));
#endif
    loadConfiguration(LittleFS, filename);

    // add all your parameters here
    wifiManager.addParameter(&customMqttBroker);
    wifiManager.addParameter(&customMqttPort);
    wifiManager.addParameter(&customMqttUser);
    wifiManager.addParameter(&customMqttPass);

    wifiManager.setDarkMode(true);
    // wifiManager.setConfigPortalTimeout(60);
    wifiManager.setConfigPortalBlocking(false);
#ifdef _DEBUG_
    Serial.println(F("Saving configuration..."));
#endif
    wifiManager.setSaveParamsCallback(saveParamsCallback);
#ifdef _DEBUG_
    Serial.println(F("Print config file..."));
#endif
    printFile(LittleFS, filename);

    if (wifiManager.autoConnect(deviceName, "password")) {
#ifdef _DEBUG_
        Serial.println(F("WiFI is connected :D"));
#endif
    } else {
#ifdef _DEBUG_
        Serial.println(F("Configportal running"));
#endif
    }
}

void subscribeMqtt() {
#ifdef _DEBUG_
    Serial.println(F("Subscribing to the MQTT topics..."));
#endif
    mqtt.subscribe(commandTopicAirPumpSw);
}
//----------------- Add MQTT Entities to HA ---//
void addMqttEntities() {
    char dht22TempBuff[512];
    char dht22HumiBuff[512];
    char airPupmSwBuff[512];

    JsonDocument doc;
#ifdef _DEBUG_
    Serial.println(F("Adding the DHT22Temp entity"));
#endif
    doc.clear();
    doc["name"]                = "Temp";
    doc["unique_id"]           = "dht22Temp";
    doc["state_topic"]         = stateTopicDht22Temp;
    doc["unit_of_measurement"] = "°C";
    doc["value_template"]      = "{{ value | float }}";
    JsonObject device          = doc["device"].to<JsonObject>();
    device["identifiers"][0]   = "dht22";
    device["name"]             = "DHT22";
    doc.shrinkToFit();  // optional
    serializeJson(doc, dht22TempBuff);
#ifndef _RemoveEntity
    mqtt.publish(configTopicDht22Humi, dht22TempBuff, true);
#else
    mqtt.publish(configTopicDht22Humi, "", true);
#endif

#ifdef _DEBUG_
    Serial.println(F("Adding the DHT22Humi entity"));
#endif
    doc.clear();
    doc["name"]                = "Humi";
    doc["unique_id"]           = "dht22Humi";
    doc["state_topic"]         = stateTopicDht22Humi;
    doc["unit_of_measurement"] = "%";
    doc["value_template"]      = "{{ value | float }}";
    JsonObject device1         = doc["device"].to<JsonObject>();
    device1["identifiers"][0]  = "dht22";
    device1["name"]            = "DHT22";
    doc.shrinkToFit();  // optional
    serializeJson(doc, dht22HumiBuff);
#ifndef _RemoveEntity
    mqtt.publish(configTopicDht22Humi, dht22HumiBuff, true);
#else
    mqtt.publish(configTopicDht22Humi, "", true);
#endif

#ifdef _DEBUG_
    Serial.println(F("Adding the airPumpSw entity"));
#endif
    doc.clear();
    doc["name"]               = "Switch";
    doc["unique_id"]          = "airPumpSw";
    doc["state_topic"]        = stateTopicAirPumpSw;
    doc["command_topic"]      = commandTopicAirPumpSw;
    doc["payload_on"]         = "ON";
    doc["payload_off"]        = "OFF";
    doc["state_on"]           = "ON";
    doc["state_off"]          = "OFF";
    doc["state_off"]          = "OFF";
    doc["optimistic"]         = true;
    JsonObject device3        = doc["device"].to<JsonObject>();
    device3["identifiers"][0] = "airPump";
    device3["name"]           = "Air Pump";
    doc.shrinkToFit();  // optional
    serializeJson(doc, airPupmSwBuff);
#ifndef _RemoveEntity
    mqtt.publish(configTopicAirPumpSw, airPupmSwBuff, true);
#else
    mqtt.publish(configTopicAirPumpSw, "", true);
#endif
}

void publishMqtt() {
    testLedState == true ? mqtt.publish(stateTopicAirPumpSw, "ON") : mqtt.publish(stateTopicAirPumpSw, "OFF");
}
//----------------- Connect MQTT --------------//
void reconnectMqtt() {
    if (WiFi.status() == WL_CONNECTED) {
#ifdef _DEBUG_
        Serial.println(F("Connecting MQTT... "));
#endif
        if (mqtt.connect(deviceName, mqttUser, mqttPass)) {
            tReconnectMqtt.stop();
#ifdef _DEBUG_
            Serial.println(F("connected"));
#endif
            tConnectMqtt.interval(0);
            tConnectMqtt.start();
            statusLed.blinkNumberOfTimes(200, 200, 3);  // 200ms ON, 200ms OFF, repeat 3 times, blink immediately
            subscribeMqtt();
            addMqttEntities();
            publishMqtt();
        } else {
#ifdef _DEBUG_
            Serial.print(F("failed state: "));
            Serial.println(mqtt.state());
            Serial.print(F("counter: "));
            Serial.println(tReconnectMqtt.counter());
#endif
            if (tReconnectMqtt.counter() >= 3) {
                tReconnectMqtt.stop();
                tConnectMqtt.interval(60 * 1000);  // Wait 1 minute before reconnecting.
                tConnectMqtt.start();
            }
        }
    } else {
        if (tReconnectMqtt.counter() <= 1) {
#ifdef _DEBUG_
            Serial.println(F("WiFi is not connected"));
#endif
        }
    }
}

void connectMqtt() {
    if (!mqtt.connected()) {
        tConnectMqtt.stop();
        tReconnectMqtt.start();
    } else {
        mqtt.loop();
    }
}

//----------------- Reset WiFi Button ---------//
void resetWifiBtPressed(Button2& btn) {
    statusLed.turnON();
#ifdef _DEBUG_
    Serial.println(F("Deleting the config file and resetting WiFi."));
#endif
    deleteFile(LittleFS, filename);
    wifiManager.resetSettings();
    Serial.print(deviceName);
#ifdef _DEBUG_
    Serial.println(F(" is restarting."));
#endif
    ESP.restart();
    delay(3000);
}

void toggleTestLed(Button2& btn) {
    testLed.toggle();
    if (testLed.getOnOff() == LED_MODE_ON) {
        testLedState = LED_MODE_ON;
        pf.putBool(kLed, true);
#ifdef _DEBUG_
        Serial.print(F("Test LED State "));
        Serial.println(pf.getBool(kLed));
#endif
        mqtt.publish(stateTopicAirPumpSw, "ON");
    } else {
        testLedState = LED_MODE_OFF;
        pf.putBool(kLed, false);
#ifdef _DEBUG_
        Serial.print(F("Test LED State "));
        Serial.println(pf.getBool(kLed));
#endif
        mqtt.publish(stateTopicAirPumpSw, "OFF");
    }
}

void updateTestLed() {
    testLedState = pf.getBool(kLed, false);
#ifdef _DEBUG_
    Serial.print(F("Test LED State "));
    Serial.println(testLedState);
#endif
    testLedState ? testLed.turnON() : testLed.turnOFF();
}

void readSendData() {
    temp = dht.readTemperature();
    humi = dht.readHumidity();
#ifdef _DEBUG_
    Serial.print(F("Temp: "));
    Serial.print(temp);
    Serial.print(F(" °C, Humi: "));
    Serial.print(humi);
    Serial.println(F(" %"));
#endif
    mqtt.publish(stateTopicDht22Temp, String(temp).c_str());
    mqtt.publish(stateTopicDht22Humi, String(humi).c_str());
}

//********************************  Setup ***********************************//
void setup() {
    Serial.begin(115200);
    pf.begin("myPrefs", false);
    statusLed.turnOFF();
    updateTestLed();
    resetWifiBt.begin(resetWifiBtPin);
    resetWifiBt.setTapHandler(toggleTestLed);
    resetWifiBt.setLongClickTime(5000);
    resetWifiBt.setLongClickDetectedHandler(resetWifiBtPressed);
    // Initialize LittleFS library
    while (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
#ifdef _DEBUG_
        Serial.println(F("Failed to initialize LittleFS library"));
#endif
        delay(1000);
    }
    dht.begin();
    wifiManagerSetup();
    mqttInit();
    tReadSendData.start();
}

//********************************  Loop ************************************//
void loop() {
    statusLed.loop();
    resetWifiBt.loop();
    wifiManager.process();
    tConnectMqtt.update();
    tReconnectMqtt.update();
    tReadSendData.update();
}
