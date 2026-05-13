#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Tracks the time of the last user/host activity (touch press, button press,
// BLE data RX) and walks the display through ACTIVE → DIM → OFF as idle time
// grows. Wake is automatic — any kick() resets the timer and the next tick()
// restores the panel.
void power_idle_init(void);

// Mark "something happened now". Safe to call from any task; uses an atomic
// timestamp internally. Cheap — call liberally.
void power_idle_kick(void);

// Run the state machine. Call once per main-loop tick (~50 ms is fine).
void power_idle_tick(void);

// True while the panel is in DISPOFF+SLPIN. Callers can use this to skip
// LVGL-invalidating animation work that would just redraw to a black panel.
bool power_idle_is_off(void);

#ifdef __cplusplus
}
#endif
