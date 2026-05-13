#include "power.h"

#include "esp_log.h"

static const char *TAG = "power";

#define AXP2101_I2C_ADDR  0x34

#define REG_STATUS1     0x00  // bit 5 = VBUS good
#define REG_ADC_ENABLE  0x30  // bit 0 = battery fuel gauge enable
#define REG_BAT_SOC     0xA4  // 0–100%

static i2c_master_dev_handle_t dev_handle = NULL;

static esp_err_t axp_write(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev_handle, buf, 2, 100);
}

static esp_err_t axp_read(uint8_t reg, uint8_t *val) {
    return i2c_master_transmit_receive(dev_handle, &reg, 1, val, 1, 100);
}

esp_err_t power_init(i2c_master_bus_handle_t i2c_bus) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = AXP2101_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add AXP2101: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t status = 0;
    ret = axp_read(REG_STATUS1, &status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101 not responding");
        return ret;
    }

    uint8_t adc_en = 0;
    if (axp_read(REG_ADC_ENABLE, &adc_en) == ESP_OK) {
        axp_write(REG_ADC_ENABLE, adc_en | 0x01);
    }

    ESP_LOGI(TAG, "AXP2101 initialized (status=0x%02X)", status);
    return ESP_OK;
}

int power_get_battery_pct(void) {
    if (!dev_handle) return -1;
    uint8_t soc = 0;
    if (axp_read(REG_BAT_SOC, &soc) != ESP_OK) return -1;
    return (soc > 100) ? -1 : (int)soc;
}

bool power_is_vbus_present(void) {
    if (!dev_handle) return false;
    uint8_t status = 0;
    if (axp_read(REG_STATUS1, &status) != ESP_OK) return false;
    return (status & (1 << 5)) != 0;
}
