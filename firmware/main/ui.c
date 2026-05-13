#include "ui.h"
#include "splash.h"
#include "theme.h"
#include "icons.h"
#include "ble.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui";

LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_mono_32);

// --- Layout constants for 368x448 ---
#define SCR_W           368
#define SCR_H           448
#define MARGIN          16
#define TITLE_Y         24
#define CONTENT_Y       84
#define CONTENT_W       (SCR_W - 2 * MARGIN)  // 336
#define PANEL_H         130
#define PANEL_GAP       12
#define PANEL_BAR_Y     52   // y offset inside usage panel: below 48px pct label + 4px gap
#define PANEL_RESET_Y   88   // y offset inside usage panel: below bar (22px) + 14px gap
#define BLE_INFO_PANEL_H 140 // BLE info panel height (usage panels are PANEL_H=130)

// Color aliases
#define COL_BG        THEME_BG
#define COL_PANEL     THEME_PANEL
#define COL_TEXT      THEME_TEXT
#define COL_DIM       THEME_DIM
#define COL_ACCENT    THEME_ACCENT
#define COL_GREEN     THEME_GREEN
#define COL_AMBER     THEME_AMBER
#define COL_RED       THEME_RED
#define COL_BAR_BG    THEME_BAR_BG

// --- Usage screen widgets ---
static lv_obj_t *usage_container;
static lv_obj_t *bar_session;
static lv_obj_t *lbl_session_pct;
static lv_obj_t *lbl_session_reset;
static lv_obj_t *bar_weekly;
static lv_obj_t *lbl_weekly_pct;
static lv_obj_t *lbl_weekly_reset;
static lv_obj_t *lbl_anim;

// --- Bluetooth screen widgets ---
static lv_obj_t *ble_container;
static lv_obj_t *lbl_ble_status;
static lv_obj_t *lbl_ble_device;
static lv_obj_t *lbl_ble_mac;

// --- Splash placeholder ---
static lv_obj_t *splash_container;

// --- Battery indicator ---
static lv_obj_t *battery_img;
static lv_image_dsc_t battery_dscs[5];  // empty, low, medium, full, charging

// --- Shared state ---
static screen_t current_screen = SCREEN_USAGE;
static screen_t prev_non_splash = SCREEN_USAGE;

// --- Spinner animation ---
static uint32_t anim_last_ms = 0;
static uint8_t anim_phase = 0;
static uint8_t anim_msg_idx = 0;
static uint32_t anim_msg_start = 0;
#define ANIM_MSG_MS 4000

static const char * const spinner_frames[] = {
    "\xC2\xB7", "\xE2\x9C\xBB", "\xE2\x9C\xBD",
    "\xE2\x9C\xB6", "\xE2\x9C\xB3", "\xE2\x9C\xA2",
};
#define SPINNER_COUNT 6
#define SPINNER_PHASES (2 * (SPINNER_COUNT - 1))

static const uint16_t spinner_ms[SPINNER_COUNT] = {
    260, 130, 130, 130, 130, 260,
};

static const char * const anim_messages[] = {
    "Accomplishing", "Elucidating", "Perusing",
    "Actioning", "Enchanting", "Philosophising",
    "Actualizing", "Envisioning", "Pondering",
    "Baking", "Finagling", "Pontificating",
    "Booping", "Flibbertigibbeting", "Processing",
    "Brewing", "Forging", "Puttering",
    "Calculating", "Forming", "Puzzling",
    "Cerebrating", "Frolicking", "Reticulating",
    "Channelling", "Generating", "Ruminating",
    "Churning", "Germinating", "Scheming",
    "Clauding", "Hatching", "Schlepping",
    "Coalescing", "Herding", "Shimmying",
    "Cogitating", "Honking", "Shucking",
    "Combobulating", "Hustling", "Simmering",
    "Computing", "Ideating", "Smooshing",
    "Concocting", "Imagining", "Spelunking",
    "Conjuring", "Incubating", "Spinning",
    "Considering", "Inferring", "Stewing",
    "Contemplating", "Jiving", "Sussing",
    "Cooking", "Manifesting", "Synthesizing",
    "Crafting", "Marinating", "Thinking",
    "Creating", "Meandering", "Tinkering",
    "Crunching", "Moseying", "Transmuting",
    "Deciphering", "Mulling", "Unfurling",
    "Deliberating", "Mustering", "Unravelling",
    "Determining", "Musing", "Vibing",
    "Discombobulating", "Noodling", "Wandering",
    "Divining", "Percolating", "Whirring",
    "Doing", "Wibbling",
    "Effecting", "Wizarding",
    "Working", "Wrangling",
};
#define ANIM_MSG_COUNT (sizeof(anim_messages) / sizeof(anim_messages[0]))

// --- Helpers ---

static lv_color_t pct_color(float pct) {
    if (pct >= 80.0f) return COL_RED;
    if (pct >= 50.0f) return COL_AMBER;
    return COL_GREEN;
}

static void format_reset_time(int mins, char *buf, size_t len) {
    if (mins < 0) {
        snprintf(buf, len, "---");
    } else if (mins < 60) {
        snprintf(buf, len, "Resets in %dm", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    } else {
        snprintf(buf, len, "Resets in %dd %dh", mins / 1440, (mins % 1440) / 60);
    }
}

static lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, 16, 0);
    lv_obj_set_style_pad_right(panel, 16, 0);
    lv_obj_set_style_pad_top(panel, 12, 0);
    lv_obj_set_style_pad_bottom(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static lv_obj_t *make_bar(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    return bar;
}

static lv_obj_t *make_pill(lv_obj_t *parent, const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_bg_color(lbl, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(lbl, 18, 0);
    lv_obj_set_style_pad_right(lbl, 18, 0);
    lv_obj_set_style_pad_top(lbl, 6, 0);
    lv_obj_set_style_pad_bottom(lbl, 6, 0);
    return lbl;
}

static void init_icon_dsc(lv_image_dsc_t *dsc, int w, int h,
                          lv_color_format_t cf, const uint8_t *data) {
    dsc->header.w      = w;
    dsc->header.h      = h;
    dsc->header.cf     = cf;
    dsc->header.stride = w * 2;
    dsc->data          = data;
    dsc->data_size     = (cf == LV_COLOR_FORMAT_RGB565A8) ? (size_t)(w * h * 3)
                                                           : (size_t)(w * h * 2);
}

static lv_obj_t *make_screen_container(lv_obj_t *parent, bool opaque) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, SCR_W, SCR_H);
    lv_obj_set_pos(c, 0, 0);
    lv_obj_set_style_bg_color(c, COL_BG, 0);
    lv_obj_set_style_bg_opa(c, opaque ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static void apply_battery_visibility(void) {
    if (!battery_img) return;
    if (current_screen == SCREEN_SPLASH) lv_obj_add_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
    else                                  lv_obj_clear_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
}

// --- Event callbacks ---

static void swipe_event_cb(lv_event_t *e) {
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_TOP || dir == LV_DIR_BOTTOM) {
        if (current_screen == SCREEN_SPLASH) {
            // Swipe on splash → toggle off, then cycle
            ui_show_screen(prev_non_splash);
        } else {
            ui_cycle_screen();
        }
    }
}

static void tap_event_cb(lv_event_t *e) {
    (void)e;
    if (current_screen != SCREEN_BLUETOOTH) {
        ui_toggle_splash();
    }
}

static void ble_reset_click_cb(lv_event_t *e) {
    (void)e;
    ble_clear_bonds();
}

// --- Screen builders ---

static void make_usage_panel(lv_obj_t *parent, int y, const char *pill_text,
                             lv_obj_t **out_pct, lv_obj_t **out_bar, lv_obj_t **out_reset) {
    lv_obj_t *panel = make_panel(parent, MARGIN, y, CONTENT_W, PANEL_H);

    *out_pct = lv_label_create(panel);
    lv_label_set_text(*out_pct, "---%");
    lv_obj_set_style_text_font(*out_pct, &font_styrene_48, 0);
    lv_obj_set_style_text_color(*out_pct, COL_TEXT, 0);
    lv_obj_set_pos(*out_pct, 0, 0);

    lv_obj_t *pill = make_pill(panel, pill_text);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, 0, 1);

    *out_bar = make_bar(panel, 0, PANEL_BAR_Y, CONTENT_W - 32, 22);

    *out_reset = lv_label_create(panel);
    lv_label_set_text(*out_reset, "---");
    lv_obj_set_style_text_font(*out_reset, &font_styrene_28, 0);
    lv_obj_set_style_text_color(*out_reset, COL_DIM, 0);
    lv_obj_set_pos(*out_reset, 0, PANEL_RESET_Y);
}

static void init_usage_screen(lv_obj_t *scr) {
    usage_container = make_screen_container(scr, false);
    lv_obj_add_event_cb(usage_container, swipe_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(usage_container, tap_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_title = lv_label_create(usage_container);
    lv_label_set_text(lbl_title, "Usage");
    lv_obj_set_style_text_font(lbl_title, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 16, TITLE_Y);

    make_usage_panel(usage_container, CONTENT_Y, "Current",
                     &lbl_session_pct, &bar_session, &lbl_session_reset);
    make_usage_panel(usage_container, CONTENT_Y + PANEL_H + PANEL_GAP, "Weekly",
                     &lbl_weekly_pct, &bar_weekly, &lbl_weekly_reset);

    lbl_anim = lv_label_create(usage_container);
    lv_label_set_text(lbl_anim, "");
    lv_obj_set_style_text_font(lbl_anim, &font_mono_32, 0);
    lv_obj_set_style_text_color(lbl_anim, COL_ACCENT, 0);
    lv_obj_align(lbl_anim, LV_ALIGN_BOTTOM_MID, 0, -15);
}

static void init_bluetooth_screen(lv_obj_t *scr) {
    ble_container = make_screen_container(scr, false);
    lv_obj_add_event_cb(ble_container, swipe_event_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t *lbl_ble_title = lv_label_create(ble_container);
    lv_label_set_text(lbl_ble_title, "Bluetooth");
    lv_obj_set_style_text_font(lbl_ble_title, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(lbl_ble_title, COL_TEXT, 0);
    lv_obj_align(lbl_ble_title, LV_ALIGN_TOP_MID, 16, TITLE_Y);

    lv_obj_t *p_info = make_panel(ble_container, MARGIN, CONTENT_Y, CONTENT_W, BLE_INFO_PANEL_H);

    static lv_image_dsc_t icon_bt_dsc;
    init_icon_dsc(&icon_bt_dsc, ICON_BLUETOOTH_W, ICON_BLUETOOTH_H, LV_COLOR_FORMAT_RGB565, (const uint8_t *)icon_bluetooth_data);
    lv_obj_t *bt_img = lv_image_create(p_info);
    lv_image_set_src(bt_img, &icon_bt_dsc);
    lv_obj_set_pos(bt_img, 0, 0);

    lbl_ble_status = lv_label_create(p_info);
    lv_label_set_text(lbl_ble_status, "Initializing...");
    lv_obj_set_style_text_font(lbl_ble_status, &font_styrene_48, 0);
    lv_obj_set_style_text_color(lbl_ble_status, COL_DIM, 0);
    lv_obj_set_pos(lbl_ble_status, 56, 2);

    lbl_ble_device = lv_label_create(p_info);
    lv_label_set_text(lbl_ble_device, "Device: ---");
    lv_obj_set_style_text_font(lbl_ble_device, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl_ble_device, COL_DIM, 0);
    lv_obj_set_pos(lbl_ble_device, 0, 60);

    lbl_ble_mac = lv_label_create(p_info);
    lv_label_set_text(lbl_ble_mac, "Address: ---");
    lv_obj_set_style_text_font(lbl_ble_mac, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl_ble_mac, COL_DIM, 0);
    lv_obj_set_pos(lbl_ble_mac, 0, 96);

    int reset_y = CONTENT_Y + BLE_INFO_PANEL_H + PANEL_GAP;
    lv_obj_t *reset_zone = lv_obj_create(ble_container);
    lv_obj_set_pos(reset_zone, MARGIN, reset_y);
    lv_obj_set_size(reset_zone, CONTENT_W, 60);
    lv_obj_set_style_bg_color(reset_zone, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(reset_zone, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(reset_zone, 8, 0);
    lv_obj_set_style_border_width(reset_zone, 0, 0);
    lv_obj_set_style_pad_column(reset_zone, 14, 0);
    lv_obj_set_flex_flow(reset_zone, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(reset_zone, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(reset_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(reset_zone, ble_reset_click_cb, LV_EVENT_CLICKED, NULL);

    static lv_image_dsc_t icon_trash_dsc;
    init_icon_dsc(&icon_trash_dsc, ICON_TRASH2_W, ICON_TRASH2_H, LV_COLOR_FORMAT_RGB565, (const uint8_t *)icon_trash2_data);
    lv_obj_t *trash_img = lv_image_create(reset_zone);
    lv_image_set_src(trash_img, &icon_trash_dsc);

    lv_obj_t *reset_lbl = lv_label_create(reset_zone);
    lv_label_set_text(reset_lbl, "Reset Bluetooth");
    lv_obj_set_style_text_font(reset_lbl, &font_styrene_28, 0);
    lv_obj_set_style_text_color(reset_lbl, COL_DIM, 0);

    // Attribution
    lv_obj_t *lbl_credit = lv_label_create(ble_container);
    lv_label_set_text(lbl_credit, "Built by @hermannbjorgvin");
    lv_obj_set_style_text_font(lbl_credit, &font_styrene_24, 0);
    lv_obj_set_style_text_color(lbl_credit, COL_DIM, 0);
    lv_obj_align(lbl_credit, LV_ALIGN_BOTTOM_MID, 0, -40);

    lv_obj_t *lbl_credit2 = lv_label_create(ble_container);
    lv_label_set_text(lbl_credit2, "Clawd animation by @amaanbuilds");
    lv_obj_set_style_text_font(lbl_credit2, &font_styrene_20, 0);
    lv_obj_set_style_text_color(lbl_credit2, COL_DIM, 0);
    lv_obj_align(lbl_credit2, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_obj_add_flag(ble_container, LV_OBJ_FLAG_HIDDEN);
}

static void init_splash_screen(lv_obj_t *scr) {
    splash_container = make_screen_container(scr, true);
    lv_obj_add_event_cb(splash_container, tap_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(splash_container, swipe_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
    splash_init(splash_container);
}

static void init_battery_icons(void) {
    init_icon_dsc(&battery_dscs[0], ICON_BATTERY_W,          ICON_BATTERY_H,          LV_COLOR_FORMAT_RGB565A8, icon_battery_data);
    init_icon_dsc(&battery_dscs[1], ICON_BATTERY_LOW_W,      ICON_BATTERY_LOW_H,      LV_COLOR_FORMAT_RGB565A8, icon_battery_low_data);
    init_icon_dsc(&battery_dscs[2], ICON_BATTERY_MEDIUM_W,   ICON_BATTERY_MEDIUM_H,   LV_COLOR_FORMAT_RGB565A8, icon_battery_medium_data);
    init_icon_dsc(&battery_dscs[3], ICON_BATTERY_FULL_W,     ICON_BATTERY_FULL_H,     LV_COLOR_FORMAT_RGB565A8, icon_battery_full_data);
    init_icon_dsc(&battery_dscs[4], ICON_BATTERY_CHARGING_W, ICON_BATTERY_CHARGING_H, LV_COLOR_FORMAT_RGB565A8, icon_battery_charging_data);
}

// --- Public API ---

void ui_init(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    init_battery_icons();
    init_usage_screen(scr);
    init_bluetooth_screen(scr);
    init_splash_screen(scr);

    // Battery indicator — upper right, inset from corner
    battery_img = lv_image_create(scr);
    lv_image_set_src(battery_img, &battery_dscs[0]);
    lv_obj_set_pos(battery_img, SCR_W - 48 - MARGIN, TITLE_Y);

    ui_show_screen(SCREEN_USAGE);
    ESP_LOGI(TAG, "UI initialized — 3 screens created");
}

void ui_update(const UsageData *data) {
    if (!data->valid) return;

    int s_pct = (int)(data->session_pct + 0.5f);
    int w_pct = (int)(data->weekly_pct + 0.5f);
    char buf[48];

    lvgl_port_lock(0);

    lv_label_set_text_fmt(lbl_session_pct, "%d%%", s_pct);
    lv_bar_set_value(bar_session, s_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_session, pct_color(data->session_pct), LV_PART_INDICATOR);
    format_reset_time(data->session_reset_mins, buf, sizeof(buf));
    lv_label_set_text(lbl_session_reset, buf);

    lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", w_pct);
    lv_bar_set_value(bar_weekly, w_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_weekly, pct_color(data->weekly_pct), LV_PART_INDICATOR);
    format_reset_time(data->weekly_reset_mins, buf, sizeof(buf));
    lv_label_set_text(lbl_weekly_reset, buf);

    lvgl_port_unlock();
}

void ui_tick_anim(void) {
    if (current_screen != SCREEN_USAGE) return;

    uint32_t now = lv_tick_get();

    if (now - anim_msg_start >= ANIM_MSG_MS) {
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        anim_msg_start = now;
    }

    uint8_t spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                        : (SPINNER_PHASES - anim_phase);
    if (now - anim_last_ms >= spinner_ms[spinner_idx]) {
        anim_last_ms = now;
        anim_phase = (anim_phase + 1) % SPINNER_PHASES;
        spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                   : (SPINNER_PHASES - anim_phase);

        static char buf[80];
        snprintf(buf, sizeof(buf), "%s %s\xE2\x80\xA6",
                 spinner_frames[spinner_idx],
                 anim_messages[anim_msg_idx]);

        lvgl_port_lock(0);
        lv_label_set_text(lbl_anim, buf);
        lvgl_port_unlock();
    }
}

void ui_show_screen(screen_t screen) {
    lvgl_port_lock(0);

    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ble_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();

    switch (screen) {
    case SCREEN_SPLASH:
        lv_obj_clear_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
        splash_show();
        break;
    case SCREEN_USAGE:
        lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
        break;
    case SCREEN_BLUETOOTH:
        lv_obj_clear_flag(ble_container, LV_OBJ_FLAG_HIDDEN);
        break;
    default:
        break;
    }

    if (screen != SCREEN_SPLASH) prev_non_splash = screen;
    current_screen = screen;
    apply_battery_visibility();

    lvgl_port_unlock();
}

void ui_cycle_screen(void) {
    screen_t next = (current_screen == SCREEN_USAGE) ? SCREEN_BLUETOOTH : SCREEN_USAGE;
    ui_show_screen(next);
}

void ui_toggle_splash(void) {
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash);
    else                                  ui_show_screen(SCREEN_SPLASH);
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_update_ble_status(ble_state_t state, const char *name, const char *mac) {
    lvgl_port_lock(0);

    switch (state) {
    case BLE_STATE_CONNECTED:
        lv_label_set_text(lbl_ble_status, "Connected");
        lv_obj_set_style_text_color(lbl_ble_status, COL_GREEN, 0);
        break;
    case BLE_STATE_ADVERTISING:
        lv_label_set_text(lbl_ble_status, "Advertising");
        lv_obj_set_style_text_color(lbl_ble_status, COL_AMBER, 0);
        break;
    case BLE_STATE_DISCONNECTED:
        lv_label_set_text(lbl_ble_status, "Disconnected");
        lv_obj_set_style_text_color(lbl_ble_status, COL_RED, 0);
        break;
    default:
        lv_label_set_text(lbl_ble_status, "Initializing");
        lv_obj_set_style_text_color(lbl_ble_status, COL_DIM, 0);
        break;
    }

    if (name) {
        static char nbuf[48];
        snprintf(nbuf, sizeof(nbuf), "Device: %s", name);
        lv_label_set_text(lbl_ble_device, nbuf);
    }
    if (mac) {
        static char mbuf[48];
        snprintf(mbuf, sizeof(mbuf), "Address: %s", mac);
        lv_label_set_text(lbl_ble_mac, mbuf);
    }

    lvgl_port_unlock();
}

void ui_update_battery(int percent, bool charging) {
    int idx;
    if (charging)       idx = 4;
    else if (percent < 0)  idx = 0;
    else if (percent <= 10) idx = 0;
    else if (percent <= 35) idx = 1;
    else if (percent <= 75) idx = 2;
    else                    idx = 3;

    lvgl_port_lock(0);
    lv_image_set_src(battery_img, &battery_dscs[idx]);
    apply_battery_visibility();
    lvgl_port_unlock();
}
