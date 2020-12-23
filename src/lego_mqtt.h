#ifndef LEGO_MQTT_H
#define LEGO_MQTT_H

void mqttSetup();
void mqttLoop();
void mqttEvery5Seconds(bool wifiIsConnected);
void mqttStop();
void mqttReconnect();

void IRAM_ATTR mqtt_send_state(const __FlashStringHelper * subtopic, const char * payload);

void mqtt_send_statusupdate(void);
bool IRAM_ATTR mqttIsConnected();

String mqttGetNodename(void);

#endif