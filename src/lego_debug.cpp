#include "ArduinoLog.h"
//#include "time.h"

#include "lego_conf.h"

#if defined(ARDUINO_ARCH_ESP8266)
#include <sntp.h> // sntp_servermode_dhcp()
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <Wifi.h>
#include <WiFiUdp.h>
#endif

#include "lego_hal.h"
#if LEGO_USE_MQTT > 0
#include "lego_mqtt.h"
#endif

#include "lego_debug.h"

#ifdef USE_CONFIG_OVERRIDE
#include "user_config_override.h"
#endif

#ifndef SERIAL_SPEED
#define SERIAL_SPEED 115200
#endif

#if LEGO_USE_TELNET > 0
#include "lego_telnet.h"
#endif

#if LEGO_USE_SYSLOG > 0
#include "Syslog.h"

#ifndef SYSLOG_SERVER
#define SYSLOG_SERVER ""
#endif

#ifndef SYSLOG_PORT
#define SYSLOG_PORT 514
#endif

#ifndef APP_NAME
#define APP_NAME "PUPT"
#endif

// variables for debug stream writer
// static String debugOutput((char *)0);
// static StringStream debugStream((String &)debugOutput);

extern char mqttNodeName[16];
const char * syslogAppName  = APP_NAME;
char debugSyslogHost[32]    = SYSLOG_SERVER;
uint16_t debugSyslogPort    = SYSLOG_PORT;
uint8_t debugSyslogFacility = 0;
uint8_t debugSyslogProtocol = 0;

// A UDP instance to let us send and receive packets over UDP
WiFiUDP syslogClient;

// Create a new syslog instance with LOG_KERN facility
// Syslog syslog(syslogClient, SYSLOG_SERVER, SYSLOG_PORT, MQTT_CLIENT, APP_NAME, LOG_KERN);
// Create a new empty syslog instance
Syslog * syslog;
#endif // USE_SYSLOG

// Serial Settings
uint8_t serialInputIndex = 0; // Empty buffer
char serialInputBuffer[1024];
uint16_t debugSerialBaud = SERIAL_SPEED / 10; // Multiplied by 10
bool debugSerialStarted  = false;
bool debugAnsiCodes      = true;

unsigned long debugLastMillis = 0;
uint16_t debugTelePeriod      = 300;

String debugHeader()
{
    String header((char *)0);
    header.reserve(256);
    if(debugAnsiCodes) header += TERM_COLOR_YELLOW;
    header += F("  ____                _____          _        \r\n"
                "|  _ \\ _   _ _ __   |_   __ __ __ _(_)_ __  ___ \r\n"
                "| |_) | | | | '_ \\    | || '__/ _` | | '_ \\/ __|\r\n"
                "|  __/| |_| | |_) |   | || | | (_| | | | | \\__ \\r\n"
                "|_|    \\__,_| .__/    |_||_|  \\__,_|_|_| |_|___/\r\n"
                "            |_|                                 \r\n"
                "        Lego(R) PoweredUp(R) Train Controller\r\n");
    char buffer[128];
    snprintf(buffer, sizeof(buffer), PSTR("        Open Hardware edition v%u.%u.%u\r\n"), LEGO_VERSION_MAJOR,
             LEGO_VERSION_MINOR, LEGO_VERSION_REVISION);
    header += buffer;
    return header;
}

void debugStart()
{
    if(debugSerialStarted) {
        Serial.flush();
        Serial.println();
        Serial.println(debugHeader());
        Serial.flush();
    }

    // prepare syslog configuration here (can be anywhere before first call of
    // log/logf method)
}

#if LEGO_USE_SYSLOG > 0
void syslogSend(uint8_t priority, const char * debugText)
{
    if(strlen(debugSyslogHost) != 0 && WiFi.isConnected()) {
        syslog->log(priority, debugText);
    }
}
#endif

void debugSetup()
{
#if LEGO_USE_SYSLOG > 0
    syslog = new Syslog(syslogClient, debugSyslogProtocol == 0 ? SYSLOG_PROTO_IETF : SYSLOG_PROTO_BSD);
    syslog->server(debugSyslogHost, debugSyslogPort);
    syslog->deviceHostname(mqttNodeName);
    syslog->appName(syslogAppName);
    uint16_t priority = (uint16_t)(debugSyslogFacility + 16) << 3; // localx facility, x = 0-7
    syslog->defaultPriority(priority);
#endif
}

void debugStop()
{
    if(debugSerialStarted) Serial.flush();
}

inline void debugSendAnsiCode(const __FlashStringHelper * code, Print * _logOutput)
{
    if(debugAnsiCodes) _logOutput->print(code);
}

static void debugPrintTimestamp(int level, Print * _logOutput)
{ /* Print Current Time */
    time_t rawtime;
    struct tm * timeinfo;

    // time(&rawtime);
    // timeinfo = localtime(&rawtime);

    // strftime(buffer, sizeof(buffer), "%b %d %H:%M:%S.", timeinfo);
    // Serial.println(buffer);

    debugSendAnsiCode(F(TERM_COLOR_CYAN), _logOutput);

    /* if(timeinfo->tm_year >= 120) {
         char buffer[64];
         strftime(buffer, sizeof(buffer), "[%b %d %H:%M:%S.", timeinfo); // Literal String
         _logOutput->print(buffer);
         _logOutput->printf(PSTR("%03lu]"), millis() % 1000);
     } else */
    {
        uint32_t msecs = millis();
        _logOutput->printf(PSTR("[%16d.%03d]"), msecs / 1000, msecs % 1000);
    }
}

static void debugPrintMemory(int level, Print * _logOutput)
{
    size_t maxfree     = halGetMaxFreeBlock();
    uint32_t totalfree = halGetFreeHeap();
    uint8_t frag       = halGetHeapFragmentation();

    /* Print Memory Info */
    if(debugAnsiCodes) {
        if(maxfree > (1024u * 5) && (totalfree > 1024u * 6) && (frag <= 10))
            debugSendAnsiCode(F(TERM_COLOR_GREEN), _logOutput);
        else if(maxfree > (1024u * 3) && (totalfree > 1024u * 5) && (frag <= 20))
            debugSendAnsiCode(F(TERM_COLOR_ORANGE), _logOutput);
        else
            debugSendAnsiCode(F(TERM_COLOR_RED), _logOutput);
    }
    _logOutput->printf(PSTR("[%5u/%5u%3u]"), maxfree, totalfree, frag);
}

static void debugPrintPriority(int level, Print * _logOutput)
{
    switch(level) {
        case LOG_LEVEL_FATAL:
        case LOG_LEVEL_ERROR:
            debugSendAnsiCode(F(TERM_COLOR_RED), _logOutput);
            break;
        case LOG_LEVEL_WARNING:
            debugSendAnsiCode(F(TERM_COLOR_YELLOW), _logOutput);
            break;
        case LOG_LEVEL_NOTICE:
            debugSendAnsiCode(F(TERM_COLOR_WHITE), _logOutput);
            break;
        case LOG_LEVEL_VERBOSE:
            debugSendAnsiCode(F(TERM_COLOR_CYAN), _logOutput);
            break;
        case LOG_LEVEL_TRACE:
            debugSendAnsiCode(F(TERM_COLOR_GRAY), _logOutput);
            break;
        default:
            debugSendAnsiCode(F(TERM_COLOR_RESET), _logOutput);
    }
}

void debugPrintPrefix(int level, Print * _logOutput)
{
    debugPrintTimestamp(level, _logOutput);
    debugPrintMemory(level, _logOutput);
    debugPrintPriority(level, _logOutput);
}

void debugPrintSuffix(int level, Print * _logOutput)
{
    if(debugAnsiCodes)
        _logOutput->println(F(TERM_COLOR_RESET));
    else
        _logOutput->println();
    if(debugAnsiCodes) _logOutput->print(F(TERM_COLOR_MAGENTA));
}

void debugLoop()
{
    while(Serial.available()) {
        char ch = Serial.read();
        Serial.print(ch);
        if(ch == 13 || ch == 10) {
            serialInputBuffer[serialInputIndex] = 0;
            // if(serialInputIndex > 0) dispatchCommand(serialInputBuffer);
            serialInputIndex = 0;
        } else {
            if(serialInputIndex < sizeof(serialInputBuffer) - 1) {
                serialInputBuffer[serialInputIndex++] = ch;
            }
            serialInputBuffer[serialInputIndex] = 0;
            if(strcmp(serialInputBuffer, "jsonl=") == 0) {
                //    dispatchJsonl(Serial);
                serialInputIndex = 0;
            }
        }
    }
}

/*void printLocalTime()
{
    char buffer[128];
    time_t rawtime;
    struct tm * timeinfo;

    // if(!time(nullptr)) return;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer, sizeof(buffer), "%b %d %H:%M:%S.", timeinfo);
    Serial.println(buffer);
    // struct tm timeinfo;
    // time_t now = time(nullptr);

    // Serial-.print(ctime(&now));
    // Serial.print(&timeinfo, " %d %B %Y %H:%M:%S ");

#if LWIP_VERSION_MAJOR > 1

    // LwIP v2 is able to list more details about the currently configured SNTP servers
    for(int i = 0; i < SNTP_MAX_SERVERS; i++) {
        IPAddress sntp    = *sntp_getserver(i);
        const char * name = sntp_getservername(i);
        if(sntp.isSet()) {
            Serial.printf("sntp%d:     ", i);
            if(name) {
                Serial.printf("%s (%s) ", name, sntp.toString().c_str());
            } else {
                Serial.printf("%s ", sntp.toString().c_str());
            }
            Serial.printf("IPv6: %s Reachability: %o\n", sntp.isV6() ? "Yes" : "No", sntp_getreachability(i));
        }
    }
#endif
}*/

void debugEverySecond()
{
    if(debugTelePeriod > 0 && (millis() - debugLastMillis) >= debugTelePeriod * 1000) {
        // dispatchStatusUpdate();
        debugLastMillis = millis();
    }
    // printLocalTime();
}
