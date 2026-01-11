/**
 * DayZ Server Tracker - Screensaver & Power Management Implementation
 */

#include "screensaver.h"
#include "config.h"
#include "app_state.h"
#include "drivers/display.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "screensaver";

// Touch state machine variables
static bool s_wait_for_release = false;       // Wait for finger lift after long-press off
static int64_t s_touch_press_start = 0;       // When touch first detected (for debounce)
static bool s_touch_activity_counted = false; // Only count activity once per touch

// Debug logging throttle
static int64_t s_last_elapsed_log = 0;
static int64_t s_last_touch_debug = 0;

void screensaver_init(void) {
    s_wait_for_release = false;
    s_touch_press_start = 0;
    s_touch_activity_counted = false;
    s_last_elapsed_log = 0;
    s_last_touch_debug = 0;
    ESP_LOGI(TAG, "Screensaver module initialized");
}

bool screensaver_set_active(bool active) {
    if (app_state_lock(50)) {
        app_state_t *state = app_state_get();
        if (state->ui.screensaver_active != active) {
            esp_err_t ret = display_set_backlight(!active);
            if (ret == ESP_OK) {
                state->ui.screensaver_active = active;
                state->ui.last_activity_time = esp_timer_get_time() / 1000;
                ESP_LOGI(TAG, "Screensaver %s", active ? "ON (screen off)" : "OFF (screen on)");
                app_state_unlock();
                return true;
            } else {
                ESP_LOGE(TAG, "Failed to change backlight, screensaver state unchanged");
            }
        }
        app_state_unlock();
    } else {
        ESP_LOGW(TAG, "Could not lock state for screensaver change");
    }
    return false;
}

bool screensaver_is_active(void) {
    app_state_t *state = app_state_get();
    return state->ui.screensaver_active;
}

void screensaver_reset_activity(void) {
    app_state_t *state = app_state_get();
    state->ui.last_activity_time = esp_timer_get_time() / 1000;
}

// LVGL touch pressed callback
static void on_screen_touch_pressed(lv_event_t *e) {
    (void)e;
    app_state_t *state = app_state_get();

    ESP_LOGW(TAG, "LVGL touch event fired (ss_active=%d)", state->ui.screensaver_active);

    // Wake from screensaver if active
    if (state->ui.screensaver_active) {
        ESP_LOGI(TAG, "LVGL callback waking from screensaver");
        screensaver_set_active(false);
        state->ui.long_press_tracking = false;
    } else {
        // Start tracking for potential long-press screen-off
        state->ui.long_press_start_time = esp_timer_get_time() / 1000;
        state->ui.long_press_tracking = true;
    }
}

// LVGL touch released callback
static void on_screen_touch_released(lv_event_t *e) {
    (void)e;
    app_state_t *state = app_state_get();
    state->ui.long_press_tracking = false;
}

lv_event_cb_t screensaver_get_touch_pressed_cb(void) {
    return on_screen_touch_pressed;
}

lv_event_cb_t screensaver_get_touch_released_cb(void) {
    return on_screen_touch_released;
}

void screensaver_tick(void) {
    app_state_t *state = app_state_get();

    // Screensaver timeout check
    if (!state->ui.screensaver_active && state->settings.screensaver_timeout_sec > 0) {
        int64_t now = esp_timer_get_time() / 1000;
        int64_t elapsed_ms = now - state->ui.last_activity_time;

        // Debug: log elapsed time every 30 seconds
        if (now - s_last_elapsed_log > 30000) {
            ESP_LOGI(TAG, "SS elapsed: %lld sec / %d sec timeout",
                     elapsed_ms / 1000, state->settings.screensaver_timeout_sec);
            s_last_elapsed_log = now;
        }

        if (elapsed_ms > ((int64_t)state->settings.screensaver_timeout_sec * 1000)) {
            ESP_LOGI(TAG, "Screensaver timeout after %lld sec of inactivity", elapsed_ms / 1000);
            screensaver_set_active(true);
        }
    }

    // Touch handling with debouncing
    lv_indev_t *touch_indev = display_get_touch_indev();
    if (touch_indev) {
        if (lvgl_port_lock(10)) {
            lv_indev_state_t touch_state = lv_indev_get_state(touch_indev);
            lvgl_port_unlock();

            int64_t now = esp_timer_get_time() / 1000;

            // Debug: log raw touch state every 5 seconds
            if (now - s_last_touch_debug > 5000) {
                ESP_LOGI(TAG, "Touch debug: raw=%s, ss_active=%d, timeout=%d",
                         touch_state == LV_INDEV_STATE_PRESSED ? "PRESSED" : "released",
                         state->ui.screensaver_active,
                         state->settings.screensaver_timeout_sec);
                s_last_touch_debug = now;
            }

            if (touch_state == LV_INDEV_STATE_PRESSED) {
                // Track when touch started (for debouncing)
                if (s_touch_press_start == 0) {
                    s_touch_press_start = now;
                    ESP_LOGW(TAG, "Touch PRESS detected (raw)");
                }

                // Debounce: require 50ms of continuous press to count as real touch
                int64_t press_duration = now - s_touch_press_start;
                bool is_real_touch = (press_duration >= 50);

                // Wake from screensaver on any real touch (but not if waiting for release)
                if (state->ui.screensaver_active && !s_wait_for_release && is_real_touch) {
                    ESP_LOGI(TAG, "Touch wake from screensaver");
                    screensaver_set_active(false);
                    state->ui.long_press_tracking = false;
                    s_touch_activity_counted = true;
                } else if (!state->ui.screensaver_active && is_real_touch) {
                    // Reset activity timer ONCE per touch (not continuously)
                    if (!s_touch_activity_counted) {
                        state->ui.last_activity_time = now;
                        s_touch_activity_counted = true;
                    }

                    // Track long-press for screen-off
                    if (!state->ui.long_press_tracking) {
                        state->ui.long_press_start_time = now;
                        state->ui.long_press_tracking = true;
                    } else {
                        int64_t long_press_duration = now - state->ui.long_press_start_time;
                        if (long_press_duration >= SCREEN_OFF_LONG_PRESS_MS) {
                            ESP_LOGI(TAG, "Long-press screen-off triggered");
                            screensaver_set_active(true);
                            state->ui.long_press_tracking = false;
                            s_wait_for_release = true;
                        }
                    }
                }
            } else {
                // Finger released - reset all tracking
                state->ui.long_press_tracking = false;
                s_wait_for_release = false;
                s_touch_press_start = 0;
                s_touch_activity_counted = false;
            }
        }
    }
}
