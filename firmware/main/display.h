#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include <stdbool.h>
#include <stdint.h>

// Waveshare ESP32-C6-Touch-AMOLED-1.47 — SH8601 QSPI display
#define DISP_WIDTH   368
#define DISP_HEIGHT  448

// QSPI pins
#define BSP_LCD_CS      GPIO_NUM_5
#define BSP_LCD_PCLK    GPIO_NUM_0
#define BSP_LCD_DATA0   GPIO_NUM_1
#define BSP_LCD_DATA1   GPIO_NUM_2
#define BSP_LCD_DATA2   GPIO_NUM_3
#define BSP_LCD_DATA3   GPIO_NUM_4
#define BSP_LCD_RST     GPIO_NUM_11
#define BSP_LCD_SPI_NUM SPI2_HOST

// Touch (FT3168 via FT5x06 driver)
#define BSP_TOUCH_INT   GPIO_NUM_15
#define BSP_TOUCH_RST   GPIO_NUM_10

// I2C bus (shared with AXP2101 PMU)
#define BSP_I2C_SCL     GPIO_NUM_7
#define BSP_I2C_SDA     GPIO_NUM_8

// Button
#define BSP_BTN_GPIO    GPIO_NUM_9

/**
 * Initialize SH8601 AMOLED display, LVGL, and FT3168 touch.
 * Must be called before any other display functions.
 * Returns the shared I2C bus handle via display_get_i2c_handle().
 */
esp_err_t display_init(void);

/**
 * Get the shared I2C bus handle for AXP2101 and other I2C devices.
 * Valid after display_init() succeeds.
 */
i2c_master_bus_handle_t display_get_i2c_handle(void);

/**
 * Set display brightness (0x00 = off, 0xFF = max).
 * Controls SH8601 backlight register 0x51.
 */
void display_set_brightness(uint8_t level);
