/**
 * DayZ Server Tracker - Screensaver Screen Implementation
 * Very dim display showing player count and time on black background
 */

#include "screen_screensaver.h"
#include "app_state.h"
#include "power/screensaver.h"
#include "esp_log.h"
#include <stdio.h>
#include <time.h>

static const char *TAG = "screen_ss";

// Very dim text color (barely visible)
#define COLOR_SCREENSAVER_TEXT  lv_color_hex(0x222222)

// Screen and widget references
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_lbl_time = NULL;
static lv_obj_t *s_lbl_players = NULL;

// Touch callback to wake from screensaver
static void on_screensaver_touch(lv_event_t *e) {
    (void)e;
    ESP_LOGI(TAG, "Screensaver touched - waking up");
    screensaver_set_active(false);
}

lv_obj_t* screen_screensaver_create(void) {
    // Create screen with black background
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    // Make entire screen clickable to wake
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_screen, on_screensaver_touch, LV_EVENT_CLICKED, NULL);

    // Container for centered content
    lv_obj_t *container = lv_obj_create(s_screen);
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(container);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 20, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Time label (large, very dim)
    s_lbl_time = lv_label_create(container);
    lv_obj_set_style_text_color(s_lbl_time, COLOR_SCREENSAVER_TEXT, 0);
    lv_obj_set_style_text_font(s_lbl_time, &lv_font_montserrat_48, 0);
    lv_label_set_text(s_lbl_time, "--:--");

    // Player count label (large, very dim)
    s_lbl_players = lv_label_create(container);
    lv_obj_set_style_text_color(s_lbl_players, COLOR_SCREENSAVER_TEXT, 0);
    lv_obj_set_style_text_font(s_lbl_players, &lv_font_montserrat_48, 0);
    lv_label_set_text(s_lbl_players, "--/--");

    ESP_LOGI(TAG, "Screensaver screen created");
    return s_screen;
}

void screen_screensaver_update(void) {
    if (!s_screen || !s_lbl_time || !s_lbl_players) return;

    // Update time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char time_buf[8];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    lv_label_set_text(s_lbl_time, time_buf);

    // Update player count from main server
    app_state_t *state = app_state_get();
    char player_buf[16];

    if (state->runtime.current_players >= 0) {
        snprintf(player_buf, sizeof(player_buf), "%d/%d",
                 state->runtime.current_players,
                 state->runtime.max_players);
    } else {
        snprintf(player_buf, sizeof(player_buf), "--/--");
    }
    lv_label_set_text(s_lbl_players, player_buf);
}

lv_obj_t* screen_screensaver_get(void) {
    return s_screen;
}
