#include "button.h"
#include "ble.h"
#include "board.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "button";

// HID keycodes
#define HID_SPACE     0x2C
#define HID_TAB       0x2B
#define HID_MOD_SHIFT 0x02

#define LONG_PRESS_US 500000  // 500ms

static bool     btn_pressed    = false;
static int64_t  press_start_us = 0;

esp_err_t button_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BSP_BTN_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Button initialized on GPIO %d", BSP_BTN_GPIO);
    }
    return ret;
}

void button_tick(void) {
    bool cur = (gpio_get_level(BSP_BTN_GPIO) == 0);  // active LOW

    if (cur && !btn_pressed) {
        btn_pressed    = true;
        press_start_us = esp_timer_get_time();
    } else if (!cur && btn_pressed) {
        int64_t held_us = esp_timer_get_time() - press_start_us;
        btn_pressed = false;

        if (held_us >= LONG_PRESS_US) {
            // Long press — Shift+Tab (Claude Code mode toggle)
            ble_keyboard_press(HID_TAB, HID_MOD_SHIFT);
            ble_keyboard_release();
        } else {
            // Short press — Space (Claude Code voice mode push-to-talk)
            ble_keyboard_press(HID_SPACE, 0);
            ble_keyboard_release();
        }
    }
}
