#pragma once

#include <stdbool.h>
#include "lvgl.h"

// Initialize the splash module. Creates an lv_image widget on `parent`,
// centered, configured for 18x nearest-neighbor upscaling of a 20x20
// RGB565 source buffer (800 bytes, BSS, no PSRAM).
//
// After init the splash is hidden — call splash_show() to make it visible.
void splash_init(lv_obj_t *parent);

// Advance animation frame if the current frame's hold time has elapsed.
// Safe to call from the main task at ~20 Hz; uses lvgl_port_lock around
// LVGL access internally. No-op when not active or no animations loaded.
void splash_tick(void);

// Cycle to the next animation in splash_anims[].
void splash_next(void);

// Show / hide the splash widget.
void splash_show(void);
void splash_hide(void);

// True when splash is currently visible.
bool splash_is_active(void);
