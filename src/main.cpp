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
#define _DEBUG_  // Comment this line if you don't want to debug
#include "Debug.h"

// #define CUSTOM_IP // Uncomment this line if you want to use DHCP

// #define _RemoveEntity  // Uncomment this line if you want to remove the entity from Home Assistant

//******************************** Variables & Objects **********************//
#define FORMAT_LITTLEFS_IF_FAILED true

#define deviceName "MyESP32"

//----------------- esLED ---------------------//
#define ledPin     LED_BUILTIN
#define airPumpPin 4

ezLED statusLed(ledPin);
ezLED airPump(airPumpPin);

//----------------- Preferences ---------------//
#define kAirPumpSt "airPumpSt"
bool        airPumpState = false;
Preferences pf;

//----------------- Reset WiFi Button ---------//
#define resetWifiBtPin 0
Button2 resetWifiBt;

//----------------- WiFi Manager --------------//
const char* filename = "/config.txt";  // Config file name

#ifdef CUSTOM_IP
// default custom static IP
char static_ip[16]  = "192.168.0.191";
char static_gw[16]  = "192.168.0.1";
char static_sn[16]  = "255.255.255.0";
char static_dns[16] = "1.1.1.1";
#endif
char mqttBroker[16] = "192.168.0.10";
char mqttPort[6]    = "1883";
char mqttUser[10];
char mqttPass[10];

bool mqttParameter;

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
    _delnF("Loading configuration");
    // Open file for reading
    File file = fs.open(filename, "r");
    if (!file) {

        Serial.println(F("Failed to open data file"));

        return;
    }

    // Allocate a temporary JsonDocument
    JsonDocument doc;
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, file);
    if (error) {

        Serial.println(F("Failed to read file, using default configuration"));

    }
    // Copy values from the JsonDocument to the Config
    // strlcpy(Destination_Variable, doc["Source_Variable"] /*| "Default_Value"*/, sizeof(Destination_Name));
    strlcpy(mqttBroker, doc["mqttBroker"], sizeof(mqttBroker));
    strlcpy(mqttPort, doc["mqttPort"], sizeof(mqttPort));
    strlcpy(mqttUser, doc["mqttUser"], sizeof(mqttUser));
    strlcpy(mqttPass, doc["mqttPass"], sizeof(mqttPass));
    mqttParameter = doc["mqttParameter"];

#ifdef CUSTOM_IP
    if (doc["ip"]) {
        strlcpy(static_ip, doc["ip"], sizeof(static_ip));
        strlcpy(static_gw, doc["gateway"], sizeof(static_gw));
        strlcpy(static_sn, doc["subnet"], sizeof(static_sn));
        strlcpy(static_dns, doc["dns"], sizeof(static_dns));
    } else {
        _delnF("No custom IP in config file");
    }
#endif

    file.close();
}

void saveState(bool var, const char* key, bool state) {
    var = state;
    pf.putBool(key, state);

    Serial.print(key);
    Serial.print(F(": "));
    Serial.println(pf.getBool(kAirPumpSt));

}

void handleMqttMessage(char* topic, byte* payload, unsigned int length) {
    String message;
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    if (!strcmp(topic, commandTopicAirPumpSw)) {
        if (message == "ON") {
            saveState(airPumpState, kAirPumpSt, true);
            airPump.turnON();
        } else if (message == "OFF") {
            saveState(airPumpState, kAirPumpSt, false);
            airPump.turnOFF();
        }
    }
}

void mqttInit() {

    Serial.print(F("MQTT parameters are "));

    if (mqttParameter) {

        Serial.println(F(" available"));

        mqtt.setBufferSize(512);
        mqtt.setCallback(handleMqttMessage);
        mqtt.setServer(mqttBroker, atoi(mqttPort));
        tConnectMqtt.start();
    } else {

        Serial.println(F(" not available."));

    }
}

void saveParamsCallback() {
    // saveConfiguration(LittleFS, filename);
    strcpy(mqttBroker, customMqttBroker.getValue());
    strcpy(mqttPort, customMqttPort.getValue());
    strcpy(mqttUser, customMqttUser.getValue());
    strcpy(mqttPass, customMqttPass.getValue());

    Serial.println(F("The values are updated."));


    // Delete existing file, otherwise the configuration is appended to the file
    // LittleFS.remove(filename);
    File file = LittleFS.open(filename, "w");
    if (!file) {

        Serial.println(F("Failed to open config file for writing"));

        return;
    }

    // Allocate a temporary JsonDocument
    JsonDocument doc;
    // Set the values in the document
    doc["mqttBroker"] = mqttBroker;
    doc["mqttPort"]   = mqttPort;
    doc["mqttUser"]   = mqttUser;
    doc["mqttPass"]   = mqttPass;

    Serial.print(F("The configuration has been saved to "));
    Serial.println(filename);


    if (doc["mqttBroker"] != "") {
        doc["mqttParameter"] = true;
        mqttParameter        = doc["mqttParameter"];
    }
#ifdef CUSTOM_IP
    doc["ip"]      = WiFi.localIP().toString();
    doc["gateway"] = WiFi.gatewayIP().toString();
    doc["subnet"]  = WiFi.subnetMask().toString();
    doc["dns"]     = WiFi.dnsIP().toString();
#endif
    // Serialize JSON to file
    if (serializeJson(doc, file) == 0) {
        _delnF("Failed to write to file");
    } else {
        _delnF("Configuration saved successfully");
    }

    file.close();  // Close the file

    Serial.println(F("Configuration saved"));


    mqttInit();
}

void printFile(fs::FS& fs, const char* filename) {
    _delnF("Print config file...");
    File file = fs.open(filename, "r");
    if (!file) {
        _delnF("Failed to open data file");
        return;
    }

    JsonDocument         doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        _delnF("Failed to read file");
    }

    char buffer[512];
    serializeJsonPretty(doc, buffer);
    _delnF(buffer);

    file.close();
}

void deleteFile(fs::FS& fs, const char* path) {
    _deVarln("Delete file: ", path);
    if (fs.remove(path)) {
        _delnF("- file deleted");
    } else {
        _delnF("- delete failed");
    }
}

//----------------- Wifi Manager --------------//
void wifiManagerSetup() {
    loadConfiguration(LittleFS, filename);
#ifdef _DEBUG_
    printFile(LittleFS, filename);
#endif

    // reset settings - wipe credentials for testing
    // wifiManager.resetSettings();

#ifdef CUSTOM_IP
    // set static ip
    IPAddress _ip, _gw, _sn, _dns;
    _ip.fromString(static_ip);
    _gw.fromString(static_gw);
    _sn.fromString(static_sn);
    _dns.fromString(static_dns);
    wifiManager.setSTAStaticIPConfig(_ip, _gw, _sn, _dns);
#endif

    wifiManager.addParameter(&customMqttBroker);
    wifiManager.addParameter(&customMqttPort);
    wifiManager.addParameter(&customMqttUser);
    wifiManager.addParameter(&customMqttPass);

    wifiManager.setDarkMode(true);
#ifndef _DEBUG_
    wifiManager.setDebugOutput(true, WM_DEBUG_SILENT);
#endif
    // wifiManager.setDebugOutput(true, WM_DEBUG_DEV);
    // wifiManager.setMinimumSignalQuality(20); // Default: 8%
    // wifiManager.setConfigPortalTimeout(60);
    wifiManager.setConfigPortalBlocking(false);
    wifiManager.setSaveParamsCallback(saveParamsCallback);

    // automatically connect using saved credentials if they exist
    // If connection fails it starts an access point with the specified name
    if (wifiManager.autoConnect(deviceName, "password")) {
        _delnF("WiFI is connected :D");
    } else {
        _delnF("Configportal running");
    }
}

void subscribeMqtt() {

    Serial.println(F("Subscribing to the MQTT topics..."));

    mqtt.subscribe(commandTopicAirPumpSw);
}
//----------------- Add MQTT Entities to HA ---//
void addMqttEntities() {
    char dht22TempBuff[512];
    char dht22HumiBuff[512];
    char airPupmSwBuff[512];

    JsonDocument doc;

    Serial.println(F("Adding the DHT22Temp entity"));

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


    Serial.println(F("Adding the DHT22Humi entity"));

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


    Serial.println(F("Adding the airPumpSw entity"));

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
    airPumpState == true ? mqtt.publish(stateTopicAirPumpSw, "ON") : mqtt.publish(stateTopicAirPumpSw, "OFF");
}

//----------------- Connect MQTT --------------//
void reconnectMqtt() {
    if (WiFi.status() == WL_CONNECTED) {
        _deVar("MQTT Broker: ", mqttBroker);
        _deVar(" | Port: ", mqttPort);
        _deVar(" | User: ", mqttUser);
        _deVarln(" | Pass: ", mqttPass);
        _deF("Connecting MQTT... ");
        if (mqtt.connect(deviceName, mqttUser, mqttPass)) {
            tReconnectMqtt.stop();
            _delnF("Connected");
            tConnectMqtt.interval(0);
            tConnectMqtt.start();
            statusLed.blinkNumberOfTimes(200, 200, 3);  // 200ms ON, 200ms OFF, repeat 3 times, blink immediately
            subscribeMqtt();
            addMqttEntities();
            publishMqtt();
        } else {

            Serial.print(F("failed state: "));
            Serial.println(mqtt.state());
            Serial.print(F("counter: "));
            Serial.println(tReconnectMqtt.counter());

            if (tReconnectMqtt.counter() >= 3) {
                tReconnectMqtt.stop();
                tConnectMqtt.interval(60 * 1000);  // Wait 1 minute before reconnecting.
                tConnectMqtt.start();
            }
        }
    } else {
        if (tReconnectMqtt.counter() <= 1) {

            Serial.println(F("WiFi is not connected"));

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
    _delnF("Deleting the config file and resetting WiFi.");
    deleteFile(LittleFS, filename);
    wifiManager.resetSettings();
    _deF(deviceName);
    _delnF(" is restarting.");
    delay(3000);
    ESP.restart();
}

void toggleTestLed(Button2& btn) {
    airPump.toggle();
    if (airPump.getOnOff() == LED_MODE_ON) {
        saveState(airPumpState, kAirPumpSt, true);
        mqtt.publish(stateTopicAirPumpSw, "ON");
    } else {
        saveState(airPumpState, kAirPumpSt, false);
        mqtt.publish(stateTopicAirPumpSw, "OFF");
    }
}

void updateAirPumpState() {
    airPumpState = pf.getBool(kAirPumpSt, false);

    Serial.print(kAirPumpSt);
    Serial.print(F(": "));
    Serial.println(airPumpState);

    airPumpState ? airPump.turnON() : airPump.turnOFF();
}

void readSendData() {
    temp = dht.readTemperature();
    humi = dht.readHumidity();

    Serial.print(F("Temp: "));
    Serial.print(temp);
    Serial.print(F(" °C, Humi: "));
    Serial.print(humi);
    Serial.println(F(" %"));

    mqtt.publish(stateTopicDht22Temp, String(temp).c_str());
    mqtt.publish(stateTopicDht22Humi, String(humi).c_str());
}

//********************************  Setup ***********************************//
void setup() {
    _serialBegin(115200);
    pf.begin("myPrefs", false);

    while (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
        _delnF("Failed to initialize LittleFS library");
        delay(1000);
    }

    statusLed.turnOFF();
    updateAirPumpState();
    resetWifiBt.begin(resetWifiBtPin);
    resetWifiBt.setTapHandler(toggleTestLed);
    resetWifiBt.setLongClickTime(5000);
    resetWifiBt.setLongClickDetectedHandler(resetWifiBtPressed);
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
