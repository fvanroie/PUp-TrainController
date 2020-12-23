#include "lego_conf.h"
#if LEGO_USE_MQTT > 0

#include <Arduino.h>
#include "ArduinoLog.h"
#include "PubSubClient.h"
#include "tinyxml2.h" // tiny xml 2 library

using namespace tinyxml2;

#include "lego_mqtt.h"
#include "lego_ble.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <Wifi.h>
WiFiClient mqttNetworkClient;
#elif defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP.h>
WiFiClient mqttNetworkClient;
#else

#if defined(W5500_MOSI) && defined(W5500_MISO) && defined(W5500_SCLK)
#define W5500_LAN
#include <Ethernet.h>
#else
#include <STM32Ethernet.h>
#endif

EthernetClient mqttNetworkClient;
#endif

#include "lego_hal.h"
#include "lego_debug.h"
#include "lego_mqtt.h"
#include "lego_wifi.h"

#ifdef USE_CONFIG_OVERRIDE
#include "user_config_override.h"
#endif

extern unsigned long debugLastMillis; // UpdateStatus timer

char mqttNodeTopic[24];
char mqttGroupTopic[24];
bool mqttEnabled;

////////////////////////////////////////////////////////////////////////////////////////////////////
// These defaults may be overwritten with values saved by the web interface
#ifdef MQTT_HOST
char mqttServer[16] = MQTT_HOST;
#else
char mqttServer[16]    = "";
#endif
#ifdef MQTT_PORT
uint16_t mqttPort = MQTT_PORT;
#else
uint16_t mqttPort      = 1883;
#endif
#ifdef MQTT_USER
char mqttUser[23] = MQTT_USER;
#else
char mqttUser[23]      = "";
#endif
#ifdef MQTT_PASSW
char mqttPassword[32] = MQTT_PASSW;
#else
char mqttPassword[32]  = "";
#endif
#ifdef MQTT_NODENAME
char mqttNodeName[16] = MQTT_NODENAME;
#else
char mqttNodeName[16]  = "";
#endif
#ifdef MQTT_GROUPNAME
char mqttGroupName[16] = MQTT_GROUPNAME;
#else
char mqttGroupName[16] = "";
#endif
#ifndef MQTT_PREFIX
#define MQTT_PREFIX "lego"
#endif

PubSubClient mqttClient(mqttNetworkClient);

////////////////////////////////////////////////////////////////////////////////////////////////////
// Send changed values OUT

void mqtt_log_no_connection()
{
    Log.error(F("MQTT: Not connected"));
}

bool IRAM_ATTR mqttIsConnected()
{
    return mqttEnabled && mqttClient.connected();
}

void IRAM_ATTR mqtt_send_state(const __FlashStringHelper * subtopic, const char * payload)
{
    if(mqttIsConnected()) {
        char topic[64];
        snprintf_P(topic, sizeof(topic), PSTR("%sstate/%s"), mqttNodeTopic, subtopic);
        mqttClient.publish(topic, payload);
    } else {
        return mqtt_log_no_connection();
    }

    // Log after char buffers are cleared
    Log.notice(F("MQTT PUB: %sstate/%S = %s"), mqttNodeTopic, subtopic, payload);
}

void mqtt_send_statusupdate()
{ // Periodically publish a JSON string indicating system status
    char data[3 * 128];
    memset(data, 0, sizeof(data));
    {
        char buffer[128];
        memset(buffer, 0, sizeof(buffer));

#if defined(ARDUINO_ARCH_ESP8266)
        snprintf_P(buffer, sizeof(buffer), PSTR("\"espVcc\":%.2f,"), (float)ESP.getVcc() / 1000);
        strcat(data, buffer);
#endif
    }
    mqtt_send_state(F("statusupdate"), data);
    debugLastMillis = millis();
}

void handleXml(char * topic_p, byte * payload, unsigned int length)
{
    XMLDocument xmlDocument;
    if(xmlDocument.Parse((const char *)payload) != XML_SUCCESS) {
        Serial.println("Error parsing");
        return;
    }

    Serial.println("Parsing XML successful");

    const char * rr_id = "-unknown--unknown--unknown--unknown--unknown--unknown--unknown-";
    int rr_addr        = 0;

    // check for lc message
    XMLElement * element = xmlDocument.FirstChildElement("lc");
    if(element != NULL) {
        Serial.println("<lc> node found. Processing loco message...");

        // -> process lc (loco) message

        // query id attribute. This is the loco id.
        // The id is a mandatory field. If not found, the message is discarded.
        // Nevertheless, the id has no effect on the controller behaviour. Only the "addr" attribute is relevant for
        // checking if the message is for this controller - see below.
        if(element->QueryStringAttribute("id", &rr_id) != XML_SUCCESS) {
            Serial.println("id attribute not found or wrong type.");
            return;
        }
        Serial.println("loco id: " + String(rr_id));

        // query addr attribute. This is the MattzoController id.
        // If this does not equal the ControllerNo of this controller, the message is disregarded.
        if(element->QueryIntAttribute("addr", &rr_addr) != XML_SUCCESS) {
            Serial.println("addr attribute not found or wrong type. Message disregarded.");
            return;
        }
        Serial.println("addr: " + String(rr_addr));
        // if (rr_addr != controllerNo) {
        //   Serial.println("Message disgarded, as it is not for me, but for MattzoController No. " + String(rr_addr));
        //   return;
        // }

        // query dir attribute. This is direction information for the loco (forward, backward)
        const char * rr_dir = "xxxxxx"; // expected values are "true" or "false"
        int dir;
        if(element->QueryStringAttribute("dir", &rr_dir) != XML_SUCCESS) {
            Serial.println("dir attribute not found or wrong type.");
            return;
        }
        Serial.println("dir (raw): " + String(rr_dir));
        if(strcmp(rr_dir, "true") == 0) {
            Serial.println("direction: forward");
            dir = 1;
        } else if(strcmp(rr_dir, "false") == 0) {
            Serial.println("direction: backward");
            dir = -1;
        } else {
            Serial.println("unknown dir value - disregarding message.");
            return;
        }

        // query V attribute. This is speed information for the loco and ranges from 0 to V_max (see below).
        int rr_v = 0;
        if(element->QueryIntAttribute("V", &rr_v) != XML_SUCCESS) {
            Serial.println("V attribute not found or wrong type. Message disregarded.");
            return;
        }
        Serial.println("speed: " + String(rr_v));

        // query V_max attribute. This is maximum speed of the loco. It must be set in the loco settings in Rocrail as
        // percentage value. The V_max attribute is required to map to loco speed from rocrail to a power setting in the
        // MattzoController.
        int rr_vmax = 0;
        if(element->QueryIntAttribute("V_max", &rr_vmax) != XML_SUCCESS) {
            Serial.println("V_max attribute not found or wrong type. Message disregarded.");
            return;
        }
        Serial.println("V_max: " + String(rr_vmax));

        // set target train speed
        int targetTrainSpeed = rr_v * dir;
        int maxTrainSpeed    = rr_vmax;
        Serial.println("Message parsing complete, target speed set to " + String(targetTrainSpeed) +
                       ", max: " + String(maxTrainSpeed) + ")");

        ble_set_motor_speed(4, targetTrainSpeed);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Receive incoming messages
static void mqtt_message_cb(char * topic_p, byte * payload, unsigned int length)
{ // Handle incoming commands from MQTT
    if(length >= MQTT_MAX_PACKET_SIZE) return;
    payload[length] = '\0';

    // String strTopic((char *)0);
    // strTopic.reserve(MQTT_MAX_PACKET_SIZE);

    char * topic = (char *)topic_p;
    Log.notice(F("MQTT RCV: %s = %s"), topic, (char *)payload);

    if(topic == strstr(topic, mqttNodeTopic)) { // startsWith mqttNodeTopic
        topic += strlen(mqttNodeTopic);
    } else if(topic == strstr(topic, mqttGroupTopic)) { // startsWith mqttGroupTopic
        topic += strlen(mqttGroupTopic);
    } else {
        // Log.error(F("MQTT: Message received with invalid topic"));
        handleXml(topic_p, payload, length);
        return;
    }
    // Log.trace(F("MQTT IN: short topic: %s"), topic);

    if(!strcmp_P(topic, PSTR("command"))) {
        // dispatchCommand((char *)payload);
        return;
    }

    if(!strcmp_P(topic, PSTR("command/red"))) {
        ble_set_motor_speed(0, atoi((const char *)payload));
        return;
    }

    if(!strcmp_P(topic, PSTR("command/yellow"))) {
        ble_set_motor_speed(3, atoi((const char *)payload));
        return;
    }

    if(!strcmp_P(topic, PSTR("command/green"))) {
        ble_set_motor_speed(4, atoi((const char *)payload));
        return;
    }

    if(!strcmp_P(topic, PSTR("command/purple"))) {
        ble_set_motor_speed(5, atoi((const char *)payload));
        return;
    }

    if(!strcmp_P(topic, PSTR("command/scan"))) {
        ble_start_scan();
        return;
    }

    if(topic == strstr_P(topic, PSTR("command/"))) { // startsWith command/
        topic += 8u;
        // Log.trace(F("MQTT IN: command subtopic: %s"), topic);

        if(!strcmp_P(topic, PSTR("json"))) { // '[...]/device/command/json' -m '["dim=5", "page 1"]' =
                                             // nextionSendCmd("dim=50"), nextionSendCmd("page 1")
                                             //  dispatchJson((char *)payload); // Send to nextionParseJson()
        } else if(!strcmp_P(topic, PSTR("jsonl"))) {
            //  dispatchJsonl((char *)payload);
        } else if(length == 0) {
            // dispatchCommand(topic);
        } else { // '[...]/device/command/p[1].b[4].txt' -m '"Lights On"' ==
                 // nextionSetAttr("p[1].b[4].txt", "\"Lights On\"")
                 //  dispatchAttribute(topic, (char *)payload);
        }
        return;
    }

    if(topic == strstr_P(topic, PSTR("config/"))) { // startsWith command/
        topic += 7u;
        // dispatchConfig(topic, (char *)payload);
        return;
    }

    // catch a dangling LWT from a previous connection if it appears
    if(!strcmp_P(topic, PSTR("status")) && !strcmp_P((char *)payload, PSTR("OFF"))) {
        char topicBuffer[128];
        snprintf_P(topicBuffer, sizeof(topicBuffer), PSTR("%sstatus"), mqttNodeTopic);
        mqttClient.publish(topicBuffer, "ON", true);
        Log.notice(F("MQTT: binary_sensor state: [status] : ON"));
        return;
    }
}

void mqttSubscribeTo(const char * format, const char * data)
{
    char topic[64];
    snprintf_P(topic, sizeof(topic), format, data);
    if(mqttClient.subscribe(topic)) {
        Log.verbose(F("MQTT:    * Subscribed to %s"), topic);
    } else {
        Log.error(F("MQTT: Failed to subscribe to %s"), topic);
    }
}

void mqttReconnect()
{
    char buffer[128];
    char mqttClientId[64];
    static uint8_t mqttReconnectCount = 0;
    bool mqttFirstConnect             = true;

    {
        String mac = halGetMacAddress(3, "");
        mac.toLowerCase();
        memset(mqttClientId, 0, sizeof(mqttClientId));
        snprintf_P(mqttClientId, sizeof(mqttClientId), PSTR("plate_%s"), mac.c_str());
        Log.verbose(mqttClientId);
    }

    // Attempt to connect and set LWT and Clean Session
    snprintf_P(buffer, sizeof(buffer), PSTR("%sstatus"), mqttNodeTopic);
    if(!mqttClient.connect(mqttClientId, mqttUser, mqttPassword, buffer, 0, false, "OFF", true)) {
        // Retry until we give up and restart after connectTimeout seconds
        mqttReconnectCount++;
        snprintf_P(buffer, sizeof(buffer), PSTR("MQTT: %%s"));
        switch(mqttClient.state()) {
            case MQTT_CONNECTION_TIMEOUT:
                strcat_P(buffer, PSTR("Server didn't respond within the keepalive time"));
                break;
            case MQTT_CONNECTION_LOST:
                strcat_P(buffer, PSTR("Network connection was broken"));
                break;
            case MQTT_CONNECT_FAILED:
                strcat_P(buffer, PSTR("Network connection failed"));
                break;
            case MQTT_DISCONNECTED:
                strcat_P(buffer, PSTR("Client is disconnected cleanly"));
                break;
            case MQTT_CONNECTED:
                strcat_P(buffer, PSTR("(Client is connected"));
                break;
            case MQTT_CONNECT_BAD_PROTOCOL:
                strcat_P(buffer, PSTR("Server doesn't support the requested version of MQTT"));
                break;
            case MQTT_CONNECT_BAD_CLIENT_ID:
                strcat_P(buffer, PSTR("Server rejected the client identifier"));
                break;
            case MQTT_CONNECT_UNAVAILABLE:
                strcat_P(buffer, PSTR("Server was unable to accept the connection"));
                break;
            case MQTT_CONNECT_BAD_CREDENTIALS:
                strcat_P(buffer, PSTR("Username or Password rejected"));
                break;
            case MQTT_CONNECT_UNAUTHORIZED:
                strcat_P(buffer, PSTR("Client was not authorized to connect"));
                break;
            default:
                strcat_P(buffer, PSTR("Unknown failure"));
        }
        Log.warning(buffer);

        if(mqttReconnectCount > 50) {
            Log.error(F("MQTT: %sRetry count exceeded, rebooting..."));
            //  dispatchReboot(false);
        }
        return;
    }

    Log.notice(F("MQTT: [SUCCESS] Connected to broker %s as clientID %s"), mqttServer, mqttClientId);

    // Attempt to connect to broker, setting last will and testament
    // Subscribe to our incoming topics
    mqttSubscribeTo(PSTR("%scommand/#"), mqttGroupTopic);
    mqttSubscribeTo(PSTR("%scommand/#"), mqttNodeTopic);
    mqttSubscribeTo(PSTR("%sstatus"), mqttNodeTopic);
    mqttSubscribeTo(PSTR("%s/service/command"), "rocrail");

    // Force any subscribed clients to toggle OFF/ON when we first connect to
    // make sure we get a full panel refresh at power on.  Sending OFF,
    // "ON" will be sent by the mqttStatusTopic subscription action.
    snprintf_P(buffer, sizeof(buffer), PSTR("%sstatus"), mqttNodeTopic);
    mqttClient.publish(buffer, mqttFirstConnect ? "OFF" : "ON", true); //, 1);

    Log.notice(F("MQTT: binary_sensor state: [%sstatus] : %s"), mqttNodeTopic,
               mqttFirstConnect ? PSTR("OFF") : PSTR("ON"));

    mqttFirstConnect   = false;
    mqttReconnectCount = 0;

    mqtt_send_statusupdate();
}

void mqttSetup()
{
    mqttEnabled = strlen(mqttServer) > 0 && mqttPort > 0;
    if(mqttEnabled) {
        mqttClient.setServer(mqttServer, 1883);
        mqttClient.setCallback(mqtt_message_cb);
        Log.notice(F("MQTT: Setup Complete"));
    } else {
        Log.notice(F("MQTT: Broker not configured"));
    }

    snprintf_P(mqttNodeTopic, sizeof(mqttNodeTopic), PSTR(MQTT_PREFIX "/%s/"), mqttNodeName);
    snprintf_P(mqttGroupTopic, sizeof(mqttGroupTopic), PSTR(MQTT_PREFIX "/%s/"), mqttGroupName);
}

void mqttLoop()
{
    if(mqttEnabled) mqttClient.loop();
}

void mqttEvery5Seconds(bool wifiIsConnected)
{
    if(mqttEnabled && wifiIsConnected && !mqttClient.connected()) mqttReconnect();
}

String mqttGetNodename()
{
    return mqttNodeName;
}

void mqttStop()
{
    if(mqttEnabled && mqttClient.connected()) {
        char topicBuffer[128];

        snprintf_P(topicBuffer, sizeof(topicBuffer), PSTR("%sstatus"), mqttNodeTopic);
        mqttClient.publish(topicBuffer, "OFF");

        snprintf_P(topicBuffer, sizeof(topicBuffer), PSTR("%ssensor"), mqttNodeTopic);
        mqttClient.publish(topicBuffer, "{\"status\": \"unavailable\"}");

        mqttClient.disconnect();
        Log.notice(F("MQTT: Disconnected from broker"));
    }
}

#endif // LEGO_USE_MQTT
