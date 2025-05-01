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
#define testLedPin 12

ezLED statusLed(ledPin);
ezLED testLed(testLedPin);

bool testLedState = false;

//----------------- Preferences ---------------//
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
WiFiClient   espClient;
PubSubClient mqtt(espClient);

//******************************** Tasks ************************************//
void    connectMqtt();
void    reconnectMqtt();
TickTwo tConnectMqtt(connectMqtt, 0, 0, MILLIS);  // (function, interval, iteration, interval unit)
TickTwo tReconnectMqtt(reconnectMqtt, 3000, 0, MILLIS);

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

    if (String(topic) == "oxy/switch/set") {
        if (message == "ON") {
            statusLed.turnON();
        } else if (message == "OFF") {
            statusLed.turnOFF();
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
    mqtt.subscribe("oxy/switch/set");
}
//----------------- Add MQTT Entities to HA ---//
void addMqttEntities() {
    char myTempBuff[512];
    char oxySwitchBuff[512];

    JsonDocument doc;
#ifdef _DEBUG_
    Serial.println(F("Adding the myTemp entity"));
#endif
    doc.clear();
    doc["name"]                = "My Temp";
    doc["unique_id"]           = "myTemp";
    doc["state_topic"]         = "sensor/myTemp";
    doc["unit_of_measurement"] = "°C";
    doc["value_template"]      = "{{ value | float }}";
    doc.shrinkToFit();  // optional
    serializeJson(doc, myTempBuff);
#ifndef _RemoveEntity
    mqtt.publish("homeassistant/sensor/myTemp/config", myTempBuff, true);
#else
    mqtt.publish("homeassistant/sensor/myTemp/config", "", true);
#endif

#ifdef _DEBUG_
    Serial.println(F("Adding the oxySwitch entity"));
#endif
    doc.clear();
    doc["name"]          = "Oxy Switch";
    doc["unique_id"]     = "oxySwitch";
    doc["state_topic"]   = "oxy/switch";
    doc["command_topic"] = "oxy/switch/set";
    doc["payload_on"]    = "ON";
    doc["payload_off"]   = "OFF";
    doc["state_on"]      = "ON";
    doc["state_off"]     = "OFF";
    doc["state_off"]     = "OFF";
    doc["optimistic"]    = true;
    doc.shrinkToFit();  // optional
    serializeJson(doc, oxySwitchBuff);
#ifndef _RemoveEntity
    mqtt.publish("homeassistant/switch/oxySwtich/config", oxySwitchBuff, true);
#else
    mqtt.publish("homeassistant/switch/oxySwtich/config", "", true);
#endif
}

void publishMqtt() {
    testLedState == true ? mqtt.publish("oxy/swtich", "ON") : mqtt.publish("oxy/switch", "OFF");
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
        pf.putBool("testledState", LED_MODE_ON);
        mqtt.publish("oxy/switch", "ON");
    } else {
        testLedState = LED_MODE_OFF;
        pf.putBool("testledState", LED_MODE_OFF);
        mqtt.publish("oxy/switch", "OFF");
    }
}

void updateTestLed() {
    testLedState = pf.getBool("testLedState", false);
    testLedState == LED_MODE_ON ? testLed.turnON() : testLed.turnOFF();
}

//********************************  Setup ***********************************//
void setup() {
    statusLed.turnOFF();
    updateTestLed();
    resetWifiBt.begin(resetWifiBtPin);
    resetWifiBt.setTapHandler(toggleTestLed);
    resetWifiBt.setLongClickTime(5000);
    resetWifiBt.setLongClickDetectedHandler(resetWifiBtPressed);
    Serial.begin(115200);

    wifiManagerSetup();
    mqttInit();
}

//********************************  Loop ************************************//
void loop() {
    statusLed.loop();
    resetWifiBt.loop();
    wifiManager.process();
    tConnectMqtt.update();
    tReconnectMqtt.update();
}
