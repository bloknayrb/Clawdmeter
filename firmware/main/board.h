#pragma once

#include "driver/gpio.h"
#include "driver/spi_common.h"

// Waveshare ESP32-C6-Touch-AMOLED-1.47 — board-level pin assignments

// SH8601 display (QSPI)
#define BSP_LCD_CS      GPIO_NUM_5
#define BSP_LCD_PCLK    GPIO_NUM_0
#define BSP_LCD_DATA0   GPIO_NUM_1
#define BSP_LCD_DATA1   GPIO_NUM_2
#define BSP_LCD_DATA2   GPIO_NUM_3
#define BSP_LCD_DATA3   GPIO_NUM_4
#define BSP_LCD_RST     GPIO_NUM_11
#define BSP_LCD_SPI_NUM SPI2_HOST

// FT3168 touch (I2C)
#define BSP_TOUCH_INT   GPIO_NUM_15
#define BSP_TOUCH_RST   GPIO_NUM_10

// I2C bus (AXP2101 PMU + FT3168)
#define BSP_I2C_SCL     GPIO_NUM_7
#define BSP_I2C_SDA     GPIO_NUM_8

// BOOT button
#define BSP_BTN_GPIO    GPIO_NUM_9
