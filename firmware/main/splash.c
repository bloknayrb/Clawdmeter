// splash.c — 20x20 pixel-art creature splash for the ESP32-C6 / 368x448 AMOLED.
//
// frame_buf is a 800-byte BSS buffer; its address is fixed in frame_dsc for
// the lifetime of the program. Each tick we overwrite the bytes and call
// lv_obj_invalidate — the image data pointer never moves.

#include "splash.h"
#include "splash_animations.h"

#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "splash";

// 20x20 grid; LVGL upscales 18x to 360x360 centered on a 368x448 display.
#define SPLASH_GRID     20
#define SPLASH_SCALE_X  18

// RGB565 fallback when a palette index is somehow out of range.
#define SPLASH_COL_EMPTY 0x0000

// 20*20*2 = 800 bytes in BSS — zero runtime cost.
static uint16_t frame_buf[SPLASH_GRID * SPLASH_GRID];

static lv_image_dsc_t frame_dsc;
static lv_obj_t      *img_obj = NULL;

static uint16_t cur_anim       = 0;
static uint16_t cur_frame      = 0;
static uint32_t frame_start_ms = 0;
static bool     active         = false;

// Expand a single 20x20 frame (palette indices) into frame_buf as RGB565.
// Pure CPU work on an SRAM-resident buffer — no LVGL calls, no lock needed.
static void expand_frame(const uint8_t *cells, const uint16_t *palette) {
    for (int i = 0; i < SPLASH_GRID * SPLASH_GRID; i++) {
        uint8_t code = cells[i];
        frame_buf[i] = (palette && code < SPLASH_PALETTE_SIZE)
                           ? palette[code]
                           : SPLASH_COL_EMPTY;
    }
}

// Push the current frame's pixels into frame_buf and ask LVGL to redraw.
// Acquires the LVGL port lock around the invalidate call so it's safe to
// invoke from the main task.
static void render_current_frame(void) {
    if (cur_anim >= SPLASH_ANIM_COUNT) return;
    const splash_anim_def_t *a = &splash_anims[cur_anim];
    if (a->frame_count == 0 || cur_frame >= a->frame_count) return;

    expand_frame(a->frames[cur_frame], a->palette);

    if (img_obj && lvgl_port_lock(0)) {
        lv_obj_invalidate(img_obj);
        lvgl_port_unlock();
    }
}

void splash_init(lv_obj_t *parent) {
    // Configure the image descriptor once — data pointer stays put for the
    // lifetime of the program; we mutate the bytes it points at.
    memset(&frame_dsc, 0, sizeof(frame_dsc));
    frame_dsc.header.cf       = LV_COLOR_FORMAT_RGB565;
    frame_dsc.header.w        = SPLASH_GRID;
    frame_dsc.header.h        = SPLASH_GRID;
    frame_dsc.header.stride   = SPLASH_GRID * 2;  // bytes per row
    frame_dsc.data_size       = sizeof(frame_buf);
    frame_dsc.data            = (const uint8_t *)frame_buf;

    if (lvgl_port_lock(0)) {
        img_obj = lv_image_create(parent);
        lv_image_set_src(img_obj, &frame_dsc);
        lv_image_set_antialias(img_obj, false);
        lv_image_set_scale(img_obj, 256 * SPLASH_SCALE_X);  // 256 = 1.0
        lv_obj_center(img_obj);
        lv_obj_add_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "splash_init: failed to acquire LVGL lock");
        return;
    }

    cur_anim       = 0;
    cur_frame      = 0;
    frame_start_ms = lv_tick_get();
    active         = false;

    if (SPLASH_ANIM_COUNT > 0) {
        // Pre-fill frame_buf so the first show() has something to draw.
        expand_frame(splash_anims[0].frames[0], splash_anims[0].palette);
    }

    ESP_LOGI(TAG, "splash ready: %d animations, source 20x20 RGB565 (%u B), scale %dx",
             (int)SPLASH_ANIM_COUNT, (unsigned)sizeof(frame_buf), SPLASH_SCALE_X);
}

void splash_tick(void) {
    if (!active || SPLASH_ANIM_COUNT == 0 || img_obj == NULL) return;

    const splash_anim_def_t *a = &splash_anims[cur_anim];
    if (a->frame_count == 0) return;

    uint32_t now  = lv_tick_get();
    uint16_t hold = a->holds[cur_frame];
    if ((now - frame_start_ms) < hold) return;

    cur_frame      = (uint16_t)((cur_frame + 1) % a->frame_count);
    frame_start_ms = now;
    render_current_frame();
}

void splash_next(void) {
    if (SPLASH_ANIM_COUNT == 0) return;
    cur_anim       = (uint16_t)((cur_anim + 1) % SPLASH_ANIM_COUNT);
    cur_frame      = 0;
    frame_start_ms = lv_tick_get();
    render_current_frame();
    ESP_LOGI(TAG, "splash: -> %s", splash_anims[cur_anim].name);
}

void splash_show(void) {
    if (img_obj == NULL) return;
    cur_frame      = 0;
    frame_start_ms = lv_tick_get();
    render_current_frame();
    if (lvgl_port_lock(0)) {
        lv_obj_clear_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
    }
    active = true;
}

void splash_hide(void) {
    if (img_obj == NULL) return;
    if (lvgl_port_lock(0)) {
        lv_obj_add_flag(img_obj, LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
    }
    active = false;
}

bool splash_is_active(void) {
    return active;
}
