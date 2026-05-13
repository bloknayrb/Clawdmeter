#include "display.h"
#include "ble.h"
#include "ui.h"
#include "splash.h"
#include "button.h"
#include "power.h"
#include "data.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "clawdmeter";

void app_main(void) {
    ESP_LOGI(TAG, "=== Clawdmeter starting ===");
    ESP_LOGI(TAG, "Free heap at boot: %lu bytes, largest internal: %lu bytes",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(display_init());

    lvgl_port_lock(0);
    ui_init();
    lvgl_port_unlock();

    ble_init();
    ESP_ERROR_CHECK(button_init());

    i2c_master_bus_handle_t i2c_bus = display_get_i2c_handle();
    if (i2c_bus) {
        esp_err_t pwr_ret = power_init(i2c_bus);
        if (pwr_ret != ESP_OK) {
            ESP_LOGW(TAG, "Power init failed — battery monitoring unavailable");
        }
    }

    ui_update_ble_status(ble_get_state(), ble_get_device_name(), ble_get_mac_address());

    ESP_LOGI(TAG, "Free heap after init: %lu bytes, largest internal: %lu bytes",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "=== Clawdmeter ready — advertising as '%s' ===", ble_get_device_name());

    UsageData usage      = {0};
    ble_state_t last_ble = BLE_STATE_INIT;
    int  batt_tick       = 0;
    int  last_pct        = -2;
    bool last_charging   = false;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));

        ble_tick();
        button_tick();
        splash_tick();
        ui_tick_anim();

        ble_state_t cur_state = ble_get_state();
        if (cur_state != last_ble) {
            last_ble = cur_state;
            ui_update_ble_status(cur_state, ble_get_device_name(), ble_get_mac_address());
        }

        if (ble_has_data()) {
            const char *json = ble_get_data();
            if (ble_parse_usage(json, &usage)) {
                ESP_LOGI(TAG, "Usage: session=%.1f%% weekly=%.1f%% status=%s",
                         usage.session_pct, usage.weekly_pct, usage.status);
                ui_update(&usage);
                ble_send_ack();
            } else {
                ESP_LOGW(TAG, "Failed to parse: %s", json);
                ble_send_nack();
            }
        }

        if (++batt_tick >= 20) {
            batt_tick = 0;
            int  pct      = power_get_battery_pct();
            bool charging = power_is_vbus_present();
            if (pct != last_pct || charging != last_charging) {
                last_pct      = pct;
                last_charging = charging;
                ui_update_battery(pct, charging);
            }
        }
    }
}
