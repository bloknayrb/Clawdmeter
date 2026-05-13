#include "display.h"
#include "ble.h"
#include "ui.h"
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
    ESP_LOGI(TAG, "Free heap at boot: %lu bytes, largest internal block: %lu bytes",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    ESP_ERROR_CHECK(display_init());

    lvgl_port_lock(0);
    ui_init();
    lvgl_port_unlock();

    ble_init();
    ESP_LOGI(TAG, "BLE initialized — advertising as '%s' MAC=%s",
             ble_get_device_name(), ble_get_mac_address());

    // Show initial BLE state on Bluetooth screen
    ui_update_ble_status(ble_get_state(), ble_get_device_name(), ble_get_mac_address());

    ESP_LOGI(TAG, "Free heap after full init: %lu bytes, largest internal: %lu bytes",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    ESP_LOGI(TAG, "=== Clawdmeter ready ===");

    UsageData usage = {0};
    ble_state_t last_ble_state = BLE_STATE_INIT;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));

        ble_tick();
        ui_tick_anim();

        ble_state_t cur_state = ble_get_state();
        if (cur_state != last_ble_state) {
            last_ble_state = cur_state;
            ui_update_ble_status(cur_state, ble_get_device_name(), ble_get_mac_address());
        }

        if (ble_has_data()) {
            const char *json = ble_get_data();
            if (ble_parse_usage(json, &usage)) {
                ESP_LOGI(TAG, "Usage: session=%.1f%% (reset %dm) weekly=%.1f%% (reset %dm) status=%s",
                         usage.session_pct, usage.session_reset_mins,
                         usage.weekly_pct, usage.weekly_reset_mins,
                         usage.status);
                ui_update(&usage);
                ble_send_ack();
            } else {
                ESP_LOGW(TAG, "Failed to parse: %s", json);
                ble_send_nack();
            }
        }
    }
}
