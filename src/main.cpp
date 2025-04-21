/*
0.1 - Update ArduinoJson to 7.2.1
    - Update ezLED to 2.3.2
0.0 - Base on ESP32_WiFiManager_MQTT_SPIFFS_NonBlock
    - Change Serial.print to DebugMode
    - You can use wifi without MQTT Broker by removing the MQTT Broker field on web UI.
*/
#include <Arduino.h>
#include <FS.h>  //this needs to be first, or it all crashes and burns...
#include <SPIFFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>  //https://github.com/bblanchon/ArduinoJson
#include <WiFiManager.h>  //https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <TickTwo.h>
#include <ezLED.h>
#include <Button2.h>

//******************************** Configulation ****************************//
#define DebugMode  // Uncomment this line if you want to debug

//******************************** Variables & Objects **********************//
#define deviceName "MyESP32"

bool storedValues;
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
//----------------- SPIFFS --------------------//
void loadConfigration() {
    // clean FS, for testing
    // SPIFFS.format();

    // read configuration from FS json
    Serial.println(F("mounting FS..."));

    if (SPIFFS.begin()) {
        Serial.println(F("mounted file system"));
        if (SPIFFS.exists("/config.json")) {
            // file exists, reading and loading
            Serial.println(F("reading config file"));
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile) {
                Serial.println(F("opened config file"));
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);
                JsonDocument json;
                auto         deserializeError = deserializeJson(json, buf.get());
                serializeJson(json, Serial);
                if (!deserializeError) {
                    Serial.println(F("\nparsed json"));
                    strcpy(mqttBroker, json["mqttBroker"]);
                    strcpy(mqttPort, json["mqttPort"]);
                    strcpy(mqttUser, json["mqttUser"]);
                    strcpy(mqttPass, json["mqttPass"]);
                    storedValues = json["storedValues"];
                } else {
                    Serial.println(F("failed to load json config"));
                }
            }
        }
    } else {
        Serial.println(F("failed to mount FS"));
    }
}

void saveConfigCallback() {
    // read updated parameters
    strcpy(mqttBroker, customMqttBroker.getValue());
    strcpy(mqttPort, customMqttPort.getValue());
    strcpy(mqttUser, customMqttUser.getValue());
    strcpy(mqttPass, customMqttPass.getValue());
    // Serial.println("The values in the file are: ");
    // Serial.println("\tmqtt_broker : " + String(mqttBroker));
    // Serial.println("\tmqtt_port : " + String(mqttPort));
    // Serial.println("\tmqtt_user : " + String(mqttUser));
    // Serial.println("\tmqtt_pass : " + String(mqttPass));

    // save the custom parameters to FS
    Serial.println(F("saving config"));
    JsonDocument json;
    json["mqttBroker"] = mqttBroker;
    json["mqttPort"]   = mqttPort;
    json["mqttUser"]   = mqttUser;
    json["mqttPass"]   = mqttPass;

    if (json["mqttBroker"] != "") {
        json["storedValues"] = true;
        storedValues         = json["storedValues"];
    }

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
        Serial.println(F("failed to open config file for writing"));
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);

    configFile.close();
    // end save

    Serial.println(F("\nlocal ip"));
    Serial.println(WiFi.localIP());
    Serial.println(WiFi.gatewayIP());
    Serial.println(WiFi.subnetMask());
    Serial.println(WiFi.dnsIP());

    if (storedValues) {
        Serial.print(F("Setting MQTT Broker: "));
        Serial.println(mqttBroker);
        mqtt.setServer(mqttBroker, atoi(mqttPort));
        tConnectMqtt.start();
    }

    // Serial.println("set MQTT Broker: " + String(mqttBroker));
    // mqtt.setServer(mqttBroker, atoi(mqttPort));
    // tConnectMqtt.start();
}

//----------------- Wifi Manager --------------//
void wifiManagerSetup() {
    Serial.println(F("Loading configuration..."));
    loadConfigration();

    // add all your parameters here
    wifiManager.addParameter(&customMqttBroker);
    wifiManager.addParameter(&customMqttPort);
    wifiManager.addParameter(&customMqttUser);
    wifiManager.addParameter(&customMqttPass);

    wifiManager.setDarkMode(true);
    // wifiManager.setConfigPortalTimeout(60);  // auto close configportal after 30 seconds
    wifiManager.setConfigPortalBlocking(false);

    Serial.println(F("Saving configuration..."));
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    if (wifiManager.autoConnect(deviceName, "password")) {
        Serial.println(F("connected...yeey :D"));
    } else {
        Serial.println(F("Configportal running"));
    }
}

void subscribeMqtt() {
    Serial.println(F("Subscribing to the MQTT topics..."));
    mqtt.subscribe("oxy/switch/set");
}
//----------------- Add MQTT Entities to HA ---//
void addMqttEntities() {
    char myTempBuff[512];
    char oxySwitchBuff[512];

    JsonDocument doc;
    Serial.println(F("Adding the myTemp entity"));
    doc.clear();
    doc["name"]                = "My Temp";
    doc["unique_id"]           = "myTemp";
    doc["state_topic"]         = "sensor/myTemp";
    doc["unit_of_measurement"] = "°C";
    doc["value_template"]      = "{{ value | float }}";
    doc.shrinkToFit();  // optional
    serializeJson(doc, myTempBuff);
    mqtt.publish("homeassistant/sensor/myTemp/config", myTempBuff, true);

    Serial.println(F("Adding the oxySwitch entity"));
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
    mqtt.publish("homeassistant/switch/oxySwtich/config", oxySwitchBuff, true);
}

void publishMqtt() {
    testLedState == true ? mqtt.publish("oxy/swtich", "ON") : mqtt.publish("oxy/switch", "OFF");
}
//----------------- Connect MQTT --------------//
void reconnectMqtt() {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("Connecting MQTT... "));
        if (mqtt.connect(deviceName, mqttUser, mqttPass)) {
            tReconnectMqtt.stop();
            Serial.println(F("connected"));
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
                // ESP.restart();
                tReconnectMqtt.stop();
                // tConnectMqtt.interval(3600 * 1000);
                tConnectMqtt.interval(60 * 1000);  // Wait 1 minute before reconnecting.
                tConnectMqtt.resume();
            }
        }
    } else {
        if (tReconnectMqtt.counter() <= 1) Serial.println(F("WiFi is not connected"));
    }
}

void connectMqtt() {
    if (!mqtt.connected()) {
        tConnectMqtt.pause();
        tReconnectMqtt.start();
    } else {
        mqtt.loop();
    }
}

//----------------- Reset WiFi Button ---------//
void resetWifiBtPressed(Button2& btn) {
    statusLed.turnON();
    Serial.println(F("Deleting the config file and resetting WiFi."));
    SPIFFS.format();
    wifiManager.resetSettings();
    Serial.print(deviceName);
    Serial.println(F(" is restarting."));
    ESP.restart();
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
    mqtt.setCallback(handleMqttMessage);

    if (storedValues) {
        mqtt.setServer(mqttBroker, atoi(mqttPort));
        tConnectMqtt.start();
    }
}

//********************************  Loop ************************************//
void loop() {
    statusLed.loop();
    resetWifiBt.loop();
    wifiManager.process();
    tConnectMqtt.update();
    tReconnectMqtt.update();
}
