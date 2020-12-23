#ifndef LEGO_BLE_H
#define LEGO_BLE_H

#include <Arduino.h>

#include "nimconfig.h"
#include "esp_nimble_cfg.h"
#if CONFIG_BT_NIMBLE_MAX_CONNECTIONS < 9
#error "NimBLE-Arduino\src\nimconfig.h: CONFIG_BT_NIMBLE_MAX_CONNECTIONS should be 9"
#endif

void ble_setup(void);
void ble_loop(void);

void ble_set_motor_speed(uint8_t index, int8_t speed);
int8_t ble_get_motor_speed(uint8_t index);
void ble_start_scan(void);

#endif