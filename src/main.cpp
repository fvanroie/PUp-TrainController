#include <Arduino.h>
#include "lego_conf.h" // load first

#include "lego_debug.h"
#include "lego_ble.h"

bool isConnected;
uint8_t mainLoopCounter        = 0;
unsigned long mainLastLoopTime = 0;

void setup()
{
    Serial.begin(115200); /* prepare for possible serial debug */
    delay(1000);
    Serial.println();

    /****************************
     * Constant initialzations
     ***************************/

    /* Init Storage */
#if LEGO_USE_EEPROM > 0
    eepromSetup(); // Don't start at boot, only at write
#endif

    /****************************
     * Apply User Configuration
     ***************************/
    debugSetup();
    ble_setup();

#if LEGO_USE_WIFI > 0
    wifiSetup();
#endif

#if LEGO_USE_MDNS > 0
    mdnsSetup();
#endif

#if LEGO_USE_OTA > 0
    // otaSetup();
#endif

#if LEGO_USE_ETHERNET > 0
    ethernetSetup();
#endif

#if LEGO_USE_MQTT > 0
    mqttSetup();
#endif

#if LEGO_USE_HTTP > 0
    httpSetup();
#endif

#if LEGO_USE_TELNET > 0
    telnetSetup();
#endif

    mainLastLoopTime = millis() - 1000; // reset loop counter

    Serial.println("ESP32 Init Done");
}

void loop()
{
    debugLoop();

    /* Network Services Loops */
#if LEGO_USE_ETHERNET > 0
    ethernetLoop();
#endif

#if LEGO_USE_MQTT > 0
    mqttLoop();
#endif // MQTT

#if LEGO_USE_HTTP > 0
    httpLoop();
#endif // HTTP

#if LEGO_USE_MDNS > 0
    mdnsLoop();
#endif // MDNS

#if LEGO_USE_OTA > 0
    otaLoop();
#endif // OTA

#if LEGO_USE_TELNET > 0
    telnetLoop();
#endif // TELNET

    // digitalWrite(LEGO_OUTPUT_PIN, digitalRead(LEGO_INPUT_PIN)); // sets the LED to the button's value

    /* Timer Loop */
    if(millis() - mainLastLoopTime >= 1000) {

        /* Run Every Second */
#if LEGO_USE_OTA > 0
        otaEverySecond();
#endif
        debugEverySecond();

        /* Run Every 5 Seconds */
        if(mainLoopCounter == 0 || mainLoopCounter == 5) {
#if LEGO_USE_WIFI > 0
            isConnected = wifiEvery5Seconds();
#endif

#if LEGO_USE_ETHERNET > 0
            isConnected = ethernetEvery5Seconds();
#endif

#if LEGO_USE_HTTP > 0
            httpEvery5Seconds();
#endif

#if LEGO_USE_MQTT > 0
            mqttEvery5Seconds(isConnected);
#endif
        }

        /* Reset loop counter every 10 seconds */
        if(mainLoopCounter >= 9) {
            // Serial.print(".");
            mainLoopCounter = 0;
        } else {
            mainLoopCounter++;
        }
        mainLastLoopTime += 1000;
    }

    delay(5);
}