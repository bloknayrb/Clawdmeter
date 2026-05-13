#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLE_STATE_INIT,
    BLE_STATE_ADVERTISING,
    BLE_STATE_CONNECTED,
    BLE_STATE_DISCONNECTED,
} ble_state_t;

void ble_init(void);
void ble_tick(void);
ble_state_t ble_get_state(void);
const char* ble_get_device_name(void);
const char* ble_get_mac_address(void);
void ble_clear_bonds(void);
bool ble_has_data(void);
const char* ble_get_data(void);
void ble_send_ack(void);
void ble_send_nack(void);
void ble_request_refresh(void);
void ble_keyboard_press(uint8_t key, uint8_t modifier);
void ble_keyboard_release(void);

bool ble_parse_usage(const char *json, UsageData *out);

#ifdef __cplusplus
}
#endif
