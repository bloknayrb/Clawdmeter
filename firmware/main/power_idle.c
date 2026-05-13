#include "power_idle.h"
#include "display.h"

#include <stdatomic.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "idle";

#define IDLE_DIM_US     (30LL * 1000 * 1000)    // 30 s → dim
#define IDLE_OFF_US     (120LL * 1000 * 1000)   // 2 min → display off

#define BRIGHT_ACTIVE   0x80
#define BRIGHT_DIM      0x20

typedef enum { S_ACTIVE, S_DIM, S_OFF } state_t;

static _Atomic int64_t last_activity_us;
static state_t state = S_ACTIVE;

void power_idle_init(void) {
    atomic_store(&last_activity_us, esp_timer_get_time());
}

void power_idle_kick(void) {
    atomic_store(&last_activity_us, esp_timer_get_time());
}

bool power_idle_is_off(void) {
    return state == S_OFF;
}

static void enter_active(void) {
    if (state == S_OFF) {
        display_panel_on();
    }
    display_set_brightness(BRIGHT_ACTIVE);
    state = S_ACTIVE;
    ESP_LOGI(TAG, "wake → ACTIVE");
}

static void enter_dim(void) {
    display_set_brightness(BRIGHT_DIM);
    state = S_DIM;
    ESP_LOGI(TAG, "→ DIM");
}

static void enter_off(void) {
    display_set_brightness(0);
    display_panel_off();
    state = S_OFF;
    ESP_LOGI(TAG, "→ OFF");
}

void power_idle_tick(void) {
    int64_t idle = esp_timer_get_time() - atomic_load(&last_activity_us);

    switch (state) {
        case S_ACTIVE:
            if (idle > IDLE_DIM_US) enter_dim();
            break;
        case S_DIM:
            if (idle < IDLE_DIM_US)      enter_active();
            else if (idle > IDLE_OFF_US) enter_off();
            break;
        case S_OFF:
            if (idle < IDLE_OFF_US) enter_active();
            break;
    }
}
