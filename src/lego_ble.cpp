#include <Arduino.h>
#include "lego_ble.h"
#include "lego_debug.h"
#include "Lpf2Hub.h"

char knownDevices[][18] = {
    "90:84:2b:14:xx:xx", // Red Train Hub
    "e0:7d:ea:6d:xx:xx", // Red Train Remote
    "04:ee:03:cc:xx:xx", // Yellow Remote
    "90:84:2b:1c:xx:xx", // Yellow Train Hub
    "90:84:2b:1c:xx:xx", // Green Train Hub
    "90:84:2b:0f:xx:xx3"  // Santa Fe Hub
};
byte knownDeviceChannel[] = {2, 2, 4, 4, 0, 6}; // Links default hubs and remotes together
Color channelColor[]      = {GREEN, BLUE, RED, PURPLE, YELLOW, CYAN, PINK, WHITE, ORANGE};

#define MAX_BLE_DEVICES CONFIG_BT_NIMBLE_MAX_CONNECTIONS

struct hubData_t
{
    // char name[15] = "Unknown Hub";
    Lpf2Hub * hub    = NULL;
    char address[18] = "";
    uint8_t channel  = 0;
    SemaphoreHandle_t updateMutex;
    int8_t motorSpeed = 0;
    byte batteryLevel = 0;
    byte batteryType  = 0;
    bool isPressed    = false;
    // bool isReady = false;
};
hubData_t device[MAX_BLE_DEVICES];

// The global channel speed for each channel, replicated by the local hubs connected to that channel
uint8_t channelSpeed[MAX_BLE_DEVICES];

SemaphoreHandle_t bleScanMutex;      // Single ScanToken to allow a task to scan for new devices
unsigned long scan_end_time = 30000; // The millis until which to be scanning for new devices at startup

int8_t findHubIndex(const char * address)
{
    // Check Known slots
    for(uint8_t i = 0; i < MAX_BLE_DEVICES; i++) {
        if(strcmp(device[i].address, address) == 0) return i;
    }
    return -1;
}

bool isValidAddress(const char * address)
{
    // Check Known slots
    for(uint8_t i = 0; i < MAX_BLE_DEVICES; i++) {
        if(strncmp(device[i].address, address, sizeof(device[i].address)) == 0) return true;
    }

    // Fill up empty slots?
    for(uint8_t i = 0; i < MAX_BLE_DEVICES; i++) {
        if(strlen(device[i].address) == 0) {
            memset(device[i].address, 0, sizeof(device[i].address));
            strncpy(device[i].address, address, sizeof(device[i].address));
            device[i].channel = i;
            return true;
        }
    }

    return false;
}

void ble_start_scan(void)
{
    scan_end_time = millis() + 30000; // Scan for 30 seconds after a device disconnected
}

// callback function to handle updates of remote buttons
void remoteCallback(void * hub, byte portNumber, DeviceType deviceType, uint8_t * pData)
{
    Lpf2Hub * myRemote = (Lpf2Hub *)hub;
    // Serial.print("HubAddress: ");
    // Serial.println(myRemote->getHubAddress().toString().c_str());
    // Serial.print("HubName: ");
    // Serial.println(myRemote->getHubName().c_str());

    // Serial.print("sensorMessage callback for port: ");
    // Serial.println(portNumber, DEC);
    if(deviceType == DeviceType::REMOTE_CONTROL_BUTTON) {
        int8_t index = findHubIndex(myRemote->getHubAddress().toString().c_str());
        // Serial.print("HubIndex: ");
        // Serial.println(index, HEX);
        // Serial.print("HubChannel: ");
        // Serial.println(device[index].channel, HEX);
        uint8_t channel    = device[index].channel;
        int8_t local_speed = ble_get_motor_speed(channel);
        int8_t new_speed   = local_speed;

        ButtonState buttonState = myRemote->parseRemoteButton(pData);
        // Serial.print("Buttonstate: ");
        // Serial.println((byte)buttonState, HEX);

        // Blink on key press
        myRemote->setLedColor(buttonState == ButtonState::RELEASED ? channelColor[channel] : Color::BLACK);

        if(buttonState == ButtonState::UP) {
            // Serial.println("Up");
            new_speed = min(100, local_speed + 10);
        } else if(buttonState == ButtonState::DOWN) {
            // Serial.println("Down");
            new_speed = max(-100, local_speed - 10);
        } else if(buttonState == ButtonState::STOP) {
            // Serial.println("Stop");
            new_speed = 0;
        }

        if(local_speed != new_speed) {
            ble_set_motor_speed(channel, new_speed);
            local_speed = new_speed;
        }

        // Serial.print("Current speed:");
        // Serial.println(local_speed, DEC);
    }
}

// Change Channel+Color on HubButton Presses
void bleSwitchHubChannel(Lpf2Hub * hub)
{
    byte index = findHubIndex(hub->getHubAddress().toString().c_str());
    if(index >= 0) {
        device[index].channel++;
        if(device[index].channel >= MAX_BLE_DEVICES) {
            device[index].channel = 0;
        }
        hub->setLedColor(channelColor[device[index].channel]);
    }
}

// callback function to handle updates of hub properties
void hubPropertyChangeCallback(void * hub, HubPropertyReference hubProperty, uint8_t * pData)
{
    // return; // quiet logs

    Lpf2Hub * myHub = (Lpf2Hub *)hub;
    // Serial.print("HubAddress: ");
    // Serial.println(myHub->getHubAddress().toString().c_str());
    // Serial.print("HubName: ");
    // Serial.println(myHub->getHubName().c_str());
    int8_t index = findHubIndex(myHub->getHubAddress().toString().c_str());
    // Serial.print("HubIndex: ");
    // Serial.println(index, HEX);
    // Serial.print("HubChannel: ");
    // Serial.println(device[index].channel, HEX);

    if(hubProperty == HubPropertyReference::BATTERY_VOLTAGE) {
        device[index].batteryLevel = myHub->parseBatteryLevel(pData);
    }
    // Serial.print("BatteryLevel: ");
    // Serial.println(device[index].batteryLevel, DEC);

    // Serial.print("HubProperty: ");
    // Serial.println((byte)hubProperty, HEX);

    if(hubProperty == HubPropertyReference::ADVERTISING_NAME) {
        // Serial.print("HubName: ");
        // Serial.println(myHub->parseHubAdvertisingName(pData).c_str());
        return;
    }
    if(hubProperty == HubPropertyReference::FW_VERSION || hubProperty == HubPropertyReference::HW_VERSION ||
       hubProperty == HubPropertyReference::RADIO_FIRMWARE_VERSION) {
        if(hubProperty == HubPropertyReference::FW_VERSION) {
            // Serial.print("Hub Firmware: ");
        } else if(hubProperty == HubPropertyReference::HW_VERSION) {
            // Serial.print("Hub Hardware: ");
        } else {
            // Serial.print("Hub RadioFW: ");
        }
        Version version = myHub->parseVersion(pData);
        // Serial.print(version.Major);
        // Serial.print(".");
        // Serial.print(version.Minor);
        // Serial.print(".");
        // Serial.print(version.Bugfix);
        // Serial.print(", ");
        // Serial.println(version.Build);
        return;
    }

    /*    BATTERY_TYPE = 0x07,
    MANUFACTURER_NAME = 0x08,
    LEGO_WIRELESS_PROTOCOL_VERSION = 0x0A,
    SYSTEM_TYPE_ID = 0x0B,
    HW_NETWORK_ID = 0x0C,*/

    if(hubProperty == HubPropertyReference::RSSI) {
        // Serial.print("RSSI: ");
        // Serial.println(myHub->parseRssi(pData), DEC);
        return;
    }

    if(hubProperty == HubPropertyReference::BUTTON) {
        ButtonState state = myHub->parseHubButton(pData);
        // Serial.print("Button: ");
        // Serial.println((byte)state, HEX);
        if(state == ButtonState::RELEASED && myHub->getHubType() == HubType::POWERED_UP_HUB) {
            // myHub->setBasicMotorSpeed((byte)PoweredUpHubPort::A, 0);
        }
        if(state == ButtonState::PRESSED) {
            bleSwitchHubChannel(myHub);
        }
        // device[index].isReady = true;
        return;
    }
}

void bleRequestHubDetails(Lpf2Hub * hub)
{
    hub->requestHubPropertyUpdate(HubPropertyReference::RSSI, hubPropertyChangeCallback);
    hub->requestHubPropertyUpdate(HubPropertyReference::FW_VERSION, hubPropertyChangeCallback);
    hub->requestHubPropertyUpdate(HubPropertyReference::HW_VERSION, hubPropertyChangeCallback);
}

void ble_set_motor_speed(uint8_t channel, int8_t speed)
{
    if(channel < MAX_BLE_DEVICES) {
        channelSpeed[channel] = speed;
    }
}

int8_t ble_get_motor_speed(uint8_t channel)
{
    if(channel < MAX_BLE_DEVICES) {
        return channelSpeed[channel];
    } else {
        return 0;
    }
}

void ble_ready_wait()
{
    do {
        // Wait for active connect or disconnect events to finnish up
    } while(ble_gap_conn_active() || ble_gap_disc_active());
}

void ble_Serial_output(void * parameter)
{
    char buffer[256];
    while(1) {
        Serial.print(TERM_COLOR_GRAY
                     "\e[?25l\e[0;0fTsk#  Speed  Name                Address             Battery\e[0K\n");
        Serial.print(TERM_COLOR_GRAY "----  -----  ------------------  ------------------  -------\e[0K\n");
        for(uint8_t i = 0; i < MAX_BLE_DEVICES; i++) {

            switch(device[i].channel) {
                case 0:
                    Serial.print(TERM_COLOR_GREEN);
                    break;
                case 1:
                    Serial.print(TERM_COLOR_BLUE);
                    break;
                case 2:
                    Serial.print(TERM_COLOR_RED);
                    break;
                case 3:
                    Serial.print(TERM_COLOR_PURPLE);
                    break;
                case 4:
                    Serial.print(TERM_COLOR_YELLOW);
                    break;
                case 5:
                    Serial.print(TERM_COLOR_CYAN);
                    break;
                case 6:
                    Serial.print(TERM_COLOR_MAGENTA);
                    break;
                case 7:
                    Serial.print(TERM_COLOR_WHITE);
                    break;
                case 8:
                    Serial.print(TERM_COLOR_ORANGE);
                    break;
                default:
                    Serial.print(TERM_COLOR_GRAY);
            }

            if(device[i].hub != NULL) {
                snprintf(buffer, sizeof(buffer), "%2d. %6d    %-19s %-19s %3d %%\e[0K\n", i,
                         ble_get_motor_speed(device[i].channel), device[i].hub->getHubName().c_str(), device[i].address,
                         device[i].batteryLevel);
            } else {
                snprintf(buffer, sizeof(buffer), TERM_COLOR_GRAY "%2d.\e[0K\n", i);
            }
            Serial.print(buffer);
        }
        Serial.print(TERM_COLOR_RESET "\e[0K\n\e[?25l");
        delay(1000);
    }
}

// Hub task Handler
void ble_hub_task(void * parameter)
{
    uint32_t tasknr = (uint32_t)parameter;
    Lpf2Hub myHub;
    myHub._isConnected         = false;
    myHub._isConnecting        = false;
    int local_speed            = 0;
    int new_speed              = 0;
    bool isInitialized         = false;
    unsigned long lastlooptime = 0;
    int8_t index               = -1;
    bool hasToken              = false; // Is this task allowed to use the global BLE scan resource?
                                        // Only one task can be scanning at the same time
    while(true) {

        if(!myHub.isConnected()) {
            if(!myHub.isConnecting()) {
                /********** !isConnected && !isConnecting && isInitialized ***********/
                if(isInitialized) {
                    // A disconnect just happened, reset the dangling initialization state and start scanning
                    isInitialized = false;
                    if(index >= 0) device[index].hub = NULL;
                    ble_start_scan(); // Extend scan_end_time
                }                     // isInitialized

                /********** !isConnected && !isConnecting && !isInitialized **********/
                if(millis() < scan_end_time) {
                    Serial.print("\n");
                    Serial.print(tasknr);
                    Serial.print(" = wait ");

                    // xSemaphoreTake(bleScanMutex, portMAX_DELAY); // Wait for scan token
                    if(!hasToken) {
                        // We are not yet allowed to start the scanning process, another task has the token
                        do {
                            // Wait for the token to become available
                        } while(xSemaphoreTake(bleScanMutex, portMAX_DELAY) != pdPASS);
                        hasToken = true; // Now we have the scan token!
                    }

                    /*
                    Serial.print("\nScanning task");
                    Serial.print(tasknr);
                    Serial.print(" on core ");
                    Serial.println(xPortGetCoreID());
*/

                    /*** Allow other connections or scans to complete first ***/
                    // delay(200);
                    ble_ready_wait();
                    local_speed = 0; // Reset the local motorspeed

                    Serial.print(tasknr);
                    Serial.print("scan ");
                    // myHub.init(address[tasknr], 1);              // BLE scan
                    myHub.init(20); // BLE scan for any device
                    ble_ready_wait();

                    Serial.print(tasknr);
                    Serial.print("scan=done ");

                    // xSemaphoreGive(bleScanMutex); // Release scan token
                    // hasToken=false;
                } else {
                    delay(500);
                } // scan_end_time
            } else {
                /********** !isConnected && isConnecting && !isInitialized ***********/
                // A Device wants to connect, verify it first
                isInitialized   = false;
                HubType hubtype = myHub.getHubType();

                if((hubtype == HubType::POWERED_UP_HUB || hubtype == HubType::POWERED_UP_REMOTE) &&
                   isValidAddress(myHub._pServerAddress->toString().c_str())) {
                    ble_ready_wait(); /*** Allow other connections or scans to complete first ***/

                    // This is the right device, try to connect
                    if(!myHub.connectHub()) {
                        // myHub._isConnecting = false;
                        Serial.println("Unable to connect to hub + Release ScanToken");
                        xSemaphoreGive(bleScanMutex); // Release scan token
                        hasToken = false;             // allow other tasks to scan
                    } else {
                        Serial.println("Hub connected + Keep ScanToken");
                        ble_start_scan(); // Extend scan_end_time for finding more devices
                        hasToken = true;  // We KEEP the token until the connection is fully initialized!
                    }                     // connectHub
                } else {
                    // Reject this device connection
                    myHub._isConnected  = false;
                    myHub._isConnecting = false;

                    Serial.println("Incorrect ID + Release ScanToken");
                    xSemaphoreGive(bleScanMutex); // Release scan token
                    hasToken = false;
                    // delay(3500);

                } // valid device

            } // isConnecting
        } else {
            if(!isInitialized) {

                /********** isConnected && !isInitialized **************************/
                lastlooptime = millis();

                Serial.println("System is initialized");
                isInitialized = true;
                bleRequestHubDetails(&myHub);

                byte waitTime     = 100;
                index             = findHubIndex(myHub.getHubAddress().toString().c_str());
                device[index].hub = &myHub;

                myHub.setLedColor(Color::BLACK);
                delay(waitTime);

                //  myHub.activateButtonReports();
                // myHub.activateHubPropertyUpdate(HubPropertyReference::RSSI, hubPropertyChangeCallback);
                myHub.activateHubPropertyUpdate(HubPropertyReference::BUTTON, hubPropertyChangeCallback);
                delay(waitTime); // needed because otherwise the message is to fast after the connection procedure and
                                 // the message will get lost
                myHub.activateHubPropertyUpdate(HubPropertyReference::BATTERY_VOLTAGE, hubPropertyChangeCallback);
                delay(waitTime);
                myHub.setLedColor(channelColor[device[index].channel]);
                delay(waitTime);
                myHub.activateHubPropertyUpdate(HubPropertyReference::FW_VERSION, hubPropertyChangeCallback);
                delay(waitTime);
                myHub.activateHubPropertyUpdate(HubPropertyReference::HW_VERSION, hubPropertyChangeCallback);
                delay(waitTime);
                myHub.setLedColor(Color::BLACK);
                delay(waitTime);
                // myHub.activateHubPropertyUpdate(HubPropertyReference::ADVERTISING_NAME, hubPropertyChangeCallback);
                // delay(waitTime);

                if(myHub.getHubType() == HubType::POWERED_UP_REMOTE) {
                    byte _portLeft  = (byte)PoweredUpRemoteHubPort::LEFT;
                    byte _portRight = (byte)PoweredUpRemoteHubPort::RIGHT;

                    // Activate the remote buttons
                    myHub.activatePortDevice(_portLeft, remoteCallback);
                    delay(waitTime);
                    myHub.activatePortDevice(_portRight, remoteCallback);
                } else {
                    delay(waitTime);
                    myHub.setLedColor(channelColor[device[index].channel]);
                }
                delay(waitTime);
                myHub.setLedColor(channelColor[device[index].channel]);
                delay(waitTime);

                Serial.print("Port A: Device Type ");
                Serial.println(myHub.getDeviceTypeForPortNumber((byte)PoweredUpHubPort::A));

                xSemaphoreGive(bleScanMutex); // Release scan token
                hasToken = false;
            } else {

                /********** isConnected && isInitialized **************************/
                if(myHub.getHubType() == HubType::POWERED_UP_REMOTE) {
                    // Nothing yet
                } else {

                    // Check if the global channelSpeed and localSpeed match
                    new_speed = ble_get_motor_speed(device[index].channel);
                    if(local_speed != new_speed) {
                        myHub.setBasicMotorSpeed((byte)PoweredUpHubPort::A, new_speed); // Update motorSpeed
                        local_speed = new_speed;

                        Serial.print("Current speed:\t");
                        Serial.println(local_speed, DEC);
                    }
                }

                // Only update if the battery level goes down or the delta is more than 1%
                /* if (currentBattery > myHub.getBatteryLevel() || (myHub.getBatteryLevel() - currentBattery) > 1)
            {
                currentBattery = myHub.getBatteryLevel();
                Serial.print("Battery level:\t");
                Serial.println(currentBattery, DEC);
            } */

                if(millis() - lastlooptime >= 20000) {
                    lastlooptime = millis();
                    // bleRequestHubDetails(&myHub);
                    Serial.print(tasknr);
                    Serial.print("c ");
                }
            } // isInitialized

        } // isConnected

        delay(50); // let the CPU breathe

    } // while

    vTaskDelete(NULL);
}

/* Create tasks and initialize devices array */
void ble_setup()
{
    if(strcmp(CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME, "nimble") == 0) {
        Serial.println(CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME);
    } else {
        Serial.println(CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME " is not correct");
        while(1) {
        }
    }
    delay(5000);

    esp_err_t errRc = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);

    /* create Mutexes & Tasks */
    bleScanMutex = xSemaphoreCreateMutex();
    for(uint8_t i = 0; i < MAX_BLE_DEVICES; i++) {
        if(i < sizeof(knownDevices) / sizeof(*knownDevices)) // number of devices
        {
            strncpy(device[i].address, knownDevices[i], sizeof(*knownDevices)); // Copy addresses
        }

        if(i < sizeof(knownDeviceChannel) / sizeof(*knownDeviceChannel)) // number of devices
        {
            device[i].channel = knownDeviceChannel[i]; // Set channel
        } else {
            device[i].channel = 0;
        }

        channelSpeed[i]       = 0;
        device[i].updateMutex = xSemaphoreCreateMutex();
        xTaskCreate(ble_hub_task, "BleTask0", 8192, (void *)i, 1, NULL);
    }
    xTaskCreate(ble_Serial_output, "BleTask1", 8192, (void *)0, 1, NULL);
}
