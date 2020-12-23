#ifndef LEGO_CONF_H
#define LEGO_CONF_H

#define LEGO_VERSION_MAJOR 0
#define LEGO_VERSION_MINOR 1
#define LEGO_VERSION_REVISION 0

#define LEGO_USE_APP 1

/* Network Services */
#define LEGO_HAS_NETWORK (ARDUINO_ARCH_ESP32 > 0 || ARDUINO_ARCH_ESP8266 > 0)

#ifndef LEGO_USE_OTA
#define LEGO_USE_OTA 0 // (LEGO_HAS_NETWORK)
#endif

#ifndef LEGO_USE_WIFI
#define LEGO_USE_WIFI (LEGO_HAS_NETWORK)
#endif

#ifndef LEGO_USE_ETHERNET
#define LEGO_USE_ETHERNET 0
#endif

#ifndef LEGO_USE_MQTT
#define LEGO_USE_MQTT 1
#endif

#ifndef LEGO_USE_HTTP
#define LEGO_USE_HTTP 0
#endif

#ifndef LEGO_USE_MDNS
#define LEGO_USE_MDNS 0 // (LEGO_HAS_NETWORK)
#endif

#ifndef LEGO_USE_SYSLOG
#define LEGO_USE_SYSLOG 0
#endif

#ifndef LEGO_USE_TELNET
#define LEGO_USE_TELNET 0
#endif

/* Filesystem */
#define LEGO_HAS_FILESYSTEM (ARDUINO_ARCH_ESP32 > 0 || ARDUINO_ARCH_ESP8266 > 0)

#ifndef LEGO_USE_SPIFFS
#define LEGO_USE_SPIFFS 1 //(LEGO_HAS_FILESYSTEM)
#endif

#ifndef LEGO_USE_EEPROM
#define LEGO_USE_EEPROM 0
#endif

#ifndef LEGO_USE_SDCARD
#define LEGO_USE_SDCARD 0
#endif

#ifndef LEGO_USE_GPIO
#define LEGO_USE_GPIO 0
#endif

#ifndef LEGO_NUM_INPUTS
#define LEGO_NUM_INPUTS 3 // Buttons
#endif

#ifndef LEGO_NUM_OUTPUTS
#define LEGO_NUM_OUTPUTS 3
#endif

#ifndef LEGO_NUM_PAGES
#if defined(ARDUINO_ARCH_ESP8266)
#define LEGO_NUM_PAGES 4
#else
#define LEGO_NUM_PAGES 12
#endif
#endif

/* Includes */
#if LEGO_USE_SPIFFS > 0

#if defined(STM32F4xx)
#include "STM32Spiffs.h" // Include the SPIFFS library
#endif

#if defined(ARDUINO_ARCH_ESP32)
#include "SPIFFS.h"
#include <FS.h> // Include the SPIFFS library
#endif

#if defined(ARDUINO_ARCH_ESP8266)
#include <FS.h> // Include the SPIFFS library
#endif

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
//#include "lv_zifont.h"
#endif

#endif // SPIFFS

#if LEGO_USE_EEPROM > 0
// #include "LEGO_eeprom.h"
#endif

#if LEGO_USE_WIFI > 0
#include "lego_wifi.h"
#endif

#if LEGO_USE_ETHERNET > 0
#if USE_BUILTIN_ETHERNET > 0
#include <LwIP.h>
#include <STM32Ethernet.h>
#warning Use built-in STM32 Ethernet
#elif USE_UIP_ETHERNET
#include <UIPEthernet.h>
#include <utility/logging.h>
#warning Use ENC28J60 Ethernet shield
#else
#include "Ethernet.h"
#warning Use W5x00 Ethernet shield
#endif
#include "LEGO_ethernet.h"
#endif

#if LEGO_USE_MQTT > 0
#include "lego_mqtt.h"
#endif

#if LEGO_USE_HTTP > 0
#include "lego_http.h"
#endif

#if LEGO_USE_TELNET > 0
#include "LEGO_telnet.h"
#endif

#if LEGO_USE_MDNS > 0
#include "LEGO_mdns.h"
#endif

#if LEGO_USE_BUTTON > 0
#include "LEGO_button.h"
#endif

#if LEGO_USE_OTA > 0
#include "LEGO_ota.h"
#endif

#if LEGO_USE_TASMOTA_SLAVE > 0
#include "LEGO_slave.h"
#endif

#if LEGO_USE_ETHERNET > 0
#include "LEGO_ethernet.h"
#endif

#ifndef FPSTR
#define FPSTR(pstr_pointer) (reinterpret_cast<const __FlashStringHelper *>(pstr_pointer))
#endif

#ifndef PGM_P
#define PGM_P const char *
#endif

#endif // LEGO_CONF_H