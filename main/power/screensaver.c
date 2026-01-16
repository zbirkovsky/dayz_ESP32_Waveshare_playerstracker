/**
 * DayZ Server Tracker - Screensaver & Power Management Implementation
 */

#include "screensaver.h"
#include "config.h"
#include "app_state.h"
#include "drivers/display.h"
#include "ui/screen_screensaver.h"
#include "ui/ui_context.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_pm.h"

static const char *TAG = "screensaver";

// Deep sleep disabled - was preventing background data refresh

// Track previous screen to return to
static screen_id_t s_previous_screen = SCREEN_MAIN;

// Power management lock for CPU frequency
#ifdef CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_pm_lock = NULL;
#endif

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

    // Initialize activity timer to NOW (fixes timeout bug where timer started at 0)
    screensaver_reset_activity();

#ifdef CONFIG_PM_ENABLE
    // Create PM lock to keep CPU at high frequency when screen is on
    esp_err_t ret = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "screensaver", &s_pm_lock);
    if (ret == ESP_OK && s_pm_lock) {
        // Acquire lock by default (screen starts on)
        esp_pm_lock_acquire(s_pm_lock);
        ESP_LOGI(TAG, "Power management lock created and acquired");
    } else {
        ESP_LOGW(TAG, "Failed to create PM lock: %s", esp_err_to_name(ret));
    }
#endif

    ESP_LOGI(TAG, "Screensaver module initialized");
}

bool screensaver_set_active(bool active) {
    if (app_state_lock(50)) {
        app_state_t *state = app_state_get();
        if (state->ui.screensaver_active != active) {
            if (lvgl_port_lock(50)) {
                ui_context_t *ui = ui_context_get();

                if (active) {
                    // Save current screen before entering screensaver
                    s_previous_screen = app_state_get_current_screen();

                    // Create screensaver screen if not exists
                    if (!ui->screen_screensaver) {
                        ui->screen_screensaver = screen_screensaver_create();
                    }

                    // Update screensaver with current data and switch to it
                    screen_screensaver_update();
                    lv_screen_load(ui->screen_screensaver);
                    app_state_set_current_screen(SCREEN_SCREENSAVER);

#ifdef CONFIG_PM_ENABLE
                    // Release PM lock to allow CPU to scale down (power saving)
                    if (s_pm_lock) {
                        esp_pm_lock_release(s_pm_lock);
                        ESP_LOGI(TAG, "PM lock released - CPU can scale down");
                    }
#endif
                    ESP_LOGI(TAG, "Screensaver ON (dim display)");
                } else {
#ifdef CONFIG_PM_ENABLE
                    // Acquire PM lock to keep CPU at max frequency
                    if (s_pm_lock) {
                        esp_pm_lock_acquire(s_pm_lock);
                        ESP_LOGI(TAG, "PM lock acquired - CPU at max frequency");
                    }
#endif
                    // Return to previous screen (usually main)
                    if (ui->screen_main) {
                        lv_screen_load(ui->screen_main);
                        app_state_set_current_screen(SCREEN_MAIN);

                        // Force full screen refresh to prevent display artifacts
                        // Direct mode can cause left-side glitches without explicit invalidation
                        lv_obj_invalidate(ui->screen_main);
                        lv_refr_now(NULL);
                    }
                    ESP_LOGI(TAG, "Screensaver OFF (returning to main)");
                }

                lvgl_port_unlock();

                state->ui.screensaver_active = active;
                state->ui.last_activity_time = esp_timer_get_time() / 1000;
                app_state_unlock();
                return true;
            } else {
                ESP_LOGW(TAG, "Could not lock LVGL for screensaver change");
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
    if (app_state_lock(10)) {
        app_state_t *state = app_state_get();
        state->ui.last_activity_time = esp_timer_get_time() / 1000;
        app_state_unlock();
    }
}

// LVGL touch pressed callback
static void on_screen_touch_pressed(lv_event_t *e) {
    (void)e;
    app_state_t *state = app_state_get();

    ESP_LOGW(TAG, "LVGL touch event fired (ss_active=%d)", state->ui.screensaver_active);

    // Reset activity timer on any touch
    screensaver_reset_activity();

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
    // Track last screensaver update time
    static int64_t s_last_ss_update = 0;
    app_state_t *state = app_state_get();

    // Update screensaver display periodically when active (every 1 second)
    if (state->ui.screensaver_active) {
        int64_t now = esp_timer_get_time() / 1000;
        if (now - s_last_ss_update > 1000) {
            if (lvgl_port_lock(10)) {
                screen_screensaver_update();
                lvgl_port_unlock();
            }
            s_last_ss_update = now;
        }
    }

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
