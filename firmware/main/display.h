#pragma once

#include "board.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdbool.h>
#include <stdint.h>

// Waveshare ESP32-C6-Touch-AMOLED-1.47 — SH8601 QSPI display
#define DISP_WIDTH   368
#define DISP_HEIGHT  448

esp_err_t display_init(void);
i2c_master_bus_handle_t display_get_i2c_handle(void);
void display_set_brightness(uint8_t level);
