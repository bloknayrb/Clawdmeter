#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdbool.h>

// Initialize AXP2101 on the shared I2C bus. Enables the fuel gauge.
esp_err_t power_init(i2c_master_bus_handle_t i2c_bus);

// Battery state-of-charge (0–100). Returns -1 if no battery or read fails.
int power_get_battery_pct(void);

// True when USB power (VBUS) is present — also indicates charging.
bool power_is_vbus_present(void);
