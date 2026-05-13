#pragma once

#include "esp_err.h"

esp_err_t button_init(void);

// Call from main loop (~50ms cadence). Detects short/long press on GPIO 9
// and fires the corresponding HID keyboard event via ble.h.
void button_tick(void);
