#include "display.h"
#include "power.h"
#include "power_idle.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "lvgl.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static i2c_master_bus_handle_t i2c_handle = NULL;
static lv_display_t *lvgl_disp = NULL;
static i2c_master_dev_handle_t touch_dev = NULL;

// SH8601 init commands for 368x448 — from Waveshare C6-1.47 BSP
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},                       // SLPOUT
    {0xC4, (uint8_t[]){0x80}, 1, 0},                          // TE scan line enable
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},                    // TE scan line position
    {0x35, (uint8_t[]){0x00}, 1, 0},                           // TEON
    {0x53, (uint8_t[]){0x20}, 1, 10},                          // WRCTRLD
    {0x63, (uint8_t[]){0xFF}, 1, 10},                          // Brightness for HBM
    {0x51, (uint8_t[]){0x00}, 1, 10},                          // Set brightness (start dim)
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},        // CASET: 0-367
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},        // RASET: 0-447
    {0x29, (uint8_t[]){0x00}, 0, 10},                          // DISPON
    {0x51, (uint8_t[]){0x80}, 1, 0},                           // Set brightness ~50% (power)
};

// LVGL v9 rounder callback — SH8601 requires 2-pixel aligned draw areas
static void rounder_event_cb(lv_event_t *e) {
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

static esp_err_t init_i2c(void) {
    if (i2c_handle) return ESP_OK;
    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .sda_io_num        = BSP_I2C_SDA,
        .scl_io_num        = BSP_I2C_SCL,
        .i2c_port          = I2C_NUM_0,
        .glitch_ignore_cnt = 7,
    };
    return i2c_new_master_bus(&i2c_bus_conf, &i2c_handle);
}

static esp_err_t init_display_hw(void) {
    ESP_LOGI(TAG, "Initialize QSPI bus for SH8601");

    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        BSP_LCD_PCLK, BSP_LCD_DATA0, BSP_LCD_DATA1,
        BSP_LCD_DATA2, BSP_LCD_DATA3,
        DISP_WIDTH * DISP_HEIGHT * 2
    );
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "SPI bus init failed");

    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(
        BSP_LCD_CS, NULL, NULL
    );
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM,
                        &io_config, &io_handle),
                        TAG, "Panel IO init failed");

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle),
                        TAG, "SH8601 panel create failed");

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_set_gap(panel_handle, 0, 0);
    esp_lcd_panel_disp_on_off(panel_handle, true);

    ESP_LOGI(TAG, "SH8601 AMOLED panel initialized");
    return ESP_OK;
}

static lv_display_t *init_lvgl_display(void) {
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_FALSE(lvgl_port_init(&lvgl_cfg) == ESP_OK, NULL, TAG, "LVGL port init failed");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = DISP_WIDTH * 20,  // 20-row strip buffer (~14.7 KB)
        .monochrome = false,
        .hres = DISP_WIDTH,
        .vres = DISP_HEIGHT,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .sw_rotate = true,
            .buff_dma = true,
            .swap_bytes = true,  // SH8601 expects big-endian RGB565
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return NULL;
    }

    // SH8601 requires 2-pixel aligned draw areas
    lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    return disp;
}

// FT3168/FT5x06 protocol: reg 0x02 = touch count, reg 0x03 = first touch (6 bytes).
// Raw I2C reads bypass esp_lcd_panel_io_i2c which fails on this chip for unknown reasons
// (writes work, reads don't — see init_touch where the driver init succeeds but reads fail).
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;
    if (!touch_dev) return;

    static int err_count = 0;

    uint8_t reg = 0x02;
    uint8_t count = 0;
    esp_err_t r = i2c_master_transmit_receive(touch_dev, &reg, 1, &count, 1, 50);
    if (r != ESP_OK) {
        if (err_count++ < 3) ESP_LOGW(TAG, "touch read count failed: %s", esp_err_to_name(r));
        return;
    }
    if (count == 0 || count > 5) return;

    uint8_t buf[6];
    reg = 0x03;
    r = i2c_master_transmit_receive(touch_dev, &reg, 1, buf, 6, 50);
    if (r != ESP_OK) {
        if (err_count++ < 3) ESP_LOGW(TAG, "touch read coords failed: %s", esp_err_to_name(r));
        return;
    }

    data->point.x = (((uint16_t)(buf[0] & 0x0F)) << 8) | buf[1];
    data->point.y = (((uint16_t)(buf[2] & 0x0F)) << 8) | buf[3];
    data->state = LV_INDEV_STATE_PRESSED;
    power_idle_kick();
}

static esp_err_t init_touch(void) {
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "I2C init failed");

    // The FT3168's INT pin is open-drain; without a host-side pull-up it floats
    // and the chip enters a stuck state where writes work but reads always fail
    // with ESP_ERR_INVALID_STATE. Pull GPIO 15 high so INT idles high cleanly.
    const gpio_config_t int_cfg = {
        .pin_bit_mask = 1ULL << BSP_TOUCH_INT,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&int_cfg);

    if (power_enable_touch_rails(i2c_handle) == ESP_OK) {
        ESP_LOGI(TAG, "Waiting for FT3168 at I2C 0x38...");
        esp_err_t probe = ESP_ERR_NOT_FOUND;
        for (int ms = 0; ms < 5000 && probe != ESP_OK; ms += 50) {
            vTaskDelay(pdMS_TO_TICKS(50));
            probe = i2c_master_probe(i2c_handle, 0x38, 20);
        }
        if (probe == ESP_OK) {
            ESP_LOGI(TAG, "FT3168 responded on I2C");
        } else {
            ESP_LOGE(TAG, "FT3168 never responded after 5s — touch may not work");
        }
    } else {
        ESP_LOGW(TAG, "ALDO enable failed — touch may not respond");
    }

    const i2c_device_config_t touch_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = 0x38,
        .scl_speed_hz    = 100000,  // slower clock, more tolerant of weak pull-ups
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_handle, &touch_cfg, &touch_dev),
                        TAG, "Touch I2C device add failed");

    if (lvgl_port_lock(0)) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        lv_indev_set_display(indev, lvgl_disp);
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "Touch initialized (raw I2C 0x38)");
    return ESP_OK;
}

esp_err_t display_init(void) {
    ESP_LOGI(TAG, "Display init — SH8601 QSPI AMOLED + FT3168 touch");

    ESP_RETURN_ON_ERROR(init_display_hw(), TAG, "Display hardware init failed");

    lvgl_disp = init_lvgl_display();
    if (!lvgl_disp) return ESP_FAIL;

    esp_err_t touch_ret = init_touch();
    if (touch_ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed (%s) — retrying in 2s", esp_err_to_name(touch_ret));
        vTaskDelay(pdMS_TO_TICKS(2000));
        touch_ret = init_touch();
        if (touch_ret != ESP_OK) {
            ESP_LOGW(TAG, "Touch init failed again — display will work without touch");
        }
    }

    // Set black background on the bottom layer so un-drawn areas don't flicker
    lvgl_port_lock(0);
    lv_obj_set_style_bg_color(lv_layer_bottom(), lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_layer_bottom(), LV_OPA_COVER, 0);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Display ready — %dx%d AMOLED", DISP_WIDTH, DISP_HEIGHT);
    ESP_LOGI(TAG, "Free heap after display init: %lu bytes, largest block: %lu bytes",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return ESP_OK;
}

i2c_master_bus_handle_t display_get_i2c_handle(void) {
    return i2c_handle;
}

void display_set_brightness(uint8_t level) {
    if (!io_handle) return;
    lvgl_port_lock(0);
    esp_lcd_panel_io_tx_param(io_handle, 0x51, &level, 1);
    lvgl_port_unlock();
}

void display_panel_off(void) {
    if (!io_handle) return;
    lvgl_port_lock(0);
    esp_lcd_panel_io_tx_param(io_handle, 0x28, NULL, 0);  // DISPOFF
    esp_lcd_panel_io_tx_param(io_handle, 0x10, NULL, 0);  // SLPIN
    lvgl_port_unlock();
}

void display_panel_on(void) {
    if (!io_handle) return;
    lvgl_port_lock(0);
    esp_lcd_panel_io_tx_param(io_handle, 0x11, NULL, 0);  // SLPOUT
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(120));                        // panel wake settle
    lvgl_port_lock(0);
    esp_lcd_panel_io_tx_param(io_handle, 0x29, NULL, 0);  // DISPON
    lvgl_port_unlock();
}
