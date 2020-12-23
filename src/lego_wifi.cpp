#include <Arduino.h>
#include "ArduinoLog.h"

#include "lego_conf.h"

#if LEGO_USE_WIFI > 0

#include "lego_debug.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <Wifi.h>
#elif defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>

static WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;

#endif
//#include "DNSserver.h"

#ifdef USE_CONFIG_OVERRIDE
#include "user_config_override.h"
#endif

#ifdef WIFI_SSID
char wifiSsid[32] = WIFI_SSID;
#else
char wifiSsid[32]     = "";
#endif
#ifdef WIFI_PASSW
char wifiPassword[32] = WIFI_PASSW;
#else
char wifiPassword[32] = "";
#endif
uint8_t wifiReconnectCounter = 0;

// const byte DNS_PORT = 53;
// DNSServer dnsServer;

void wifiConnected(IPAddress ipaddress)
{
    Log.notice(F("WIFI: Received IP address %s"), ipaddress.toString().c_str());
    Log.verbose(F("WIFI: Connected = %s"), WiFi.status() == WL_CONNECTED ? PSTR("yes") : PSTR("no"));

    // if(isConnected) {
    // mqttReconnect();
    // httpReconnect();
    // mdnsStart();
    //}
}

void wifiDisconnected(const char * ssid, uint8_t reason)
{
    wifiReconnectCounter++;
    if(wifiReconnectCounter > 45) {
        Log.error(F("WIFI: Retries exceed %u: Rebooting..."), wifiReconnectCounter);
        // dispatchReboot(false);
    }
    Log.warning(F("WIFI: Disconnected from %s (Reason: %d)"), ssid, reason);
}

void wifiSsidConnected(const char * ssid)
{
    Log.notice(F("WIFI: Connected to SSID %s. Requesting IP..."), ssid);
    wifiReconnectCounter = 0;
}

#if defined(ARDUINO_ARCH_ESP32)
void wifi_callback(system_event_id_t event, system_event_info_t info)
{
    switch(event) {
        case SYSTEM_EVENT_STA_CONNECTED:
            wifiSsidConnected((const char *)info.connected.ssid);
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            wifiConnected(IPAddress(info.got_ip.ip_info.ip.addr));
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            wifiDisconnected((const char *)info.disconnected.ssid, info.disconnected.reason);
            // NTP.stop(); // NTP sync can be disabled to avoid sync errors
            break;
        default:
            break;
    }
}
#endif

#if defined(ARDUINO_ARCH_ESP8266)
void wifiSTAConnected(WiFiEventStationModeConnected info)
{
    wifiSsidConnected(info.ssid.c_str());
}

// Start NTP only after IP network is connected
void wifiSTAGotIP(WiFiEventStationModeGotIP info)
{
    wifiConnected(IPAddress(info.ip));
}

// Manage network disconnection
void wifiSTADisconnected(WiFiEventStationModeDisconnected info)
{
    wifiDisconnected(info.ssid.c_str(), info.reason);
}
#endif

void wifiSetup()
{
    WiFi.mode(WIFI_STA);

#if defined(ARDUINO_ARCH_ESP8266)
    // wifiEventHandler[0]      = WiFi.onStationModeConnected(wifiSTAConnected);
    gotIpEventHandler        = WiFi.onStationModeGotIP(wifiSTAGotIP); // As soon WiFi is connected, start NTP Client
    disconnectedEventHandler = WiFi.onStationModeDisconnected(wifiSTADisconnected);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
#if defined(ARDUINO_ARCH_ESP32)
    WiFi.onEvent(wifi_callback);
    WiFi.setSleep(false);
#endif
    WiFi.begin(wifiSsid, wifiPassword);
    Log.notice(F("WIFI: Connecting to : %s"), wifiSsid);
}

bool wifiEvery5Seconds()
{
    if(WiFi.getMode() != WIFI_STA) {
        return false;
    } else if(WiFi.status() == WL_CONNECTED) {
        return true;
    } else {
        wifiReconnectCounter++;
        if(wifiReconnectCounter > 45) {
            Log.error(F("WIFI: Retries exceed %u: Rebooting..."), wifiReconnectCounter);
            // dispatchReboot(false);
        }
        Log.warning(F("WIFI: No Connection... retry %u"), wifiReconnectCounter);
        if(wifiReconnectCounter % 6 == 0) WiFi.begin(wifiSsid, wifiPassword);
        return false;
    }
}

void wifiStop()
{
    wifiReconnectCounter = 0; // Prevent endless loop in wifiDisconnected
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    Log.warning(F("WIFI: Stopped"));
}

#endif