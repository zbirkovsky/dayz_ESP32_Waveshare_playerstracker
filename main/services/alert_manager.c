/**
 * DayZ Server Tracker - Alert Manager Implementation
 *
 * This is the SERVICE layer - handles alert LOGIC only.
 * UI rendering is delegated to ui/ui_alerts.c
 */

#include "alert_manager.h"
#include "config.h"
#include "app_state.h"
#include "drivers/buzzer.h"
#include "ui/ui_alerts.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "alert_manager";

// Alert colors (hex values, no LVGL dependency)
#define ALERT_COLOR_RED    0xFF4444
#define ALERT_COLOR_ORANGE 0xFF8800

void alert_check(void) {
    app_state_t *state = app_state_get();
    server_config_t *srv = app_state_get_active_server();

    if (!srv || !srv->alerts_enabled) return;

    int players = state->runtime.current_players;
    int max_p = state->runtime.max_players;

    // Check server full condition
    if (players >= max_p) {
        if (!state->ui.alert_active) {
            buzzer_alert_threshold();
            ESP_LOGI(TAG, "Server full alert triggered: %d/%d", players, max_p);
        }
        alert_show("SERVER FULL!", ALERT_COLOR_RED);
        return;
    }

    // Check threshold condition
    if (srv->alert_threshold > 0 && players >= srv->alert_threshold) {
        if (!state->ui.alert_active) {
            buzzer_alert_threshold();
            ESP_LOGI(TAG, "Threshold alert triggered: %d >= %d", players, srv->alert_threshold);
        }
        char msg[64];
        snprintf(msg, sizeof(msg), "ALERT: %d+ players!", srv->alert_threshold);
        alert_show(msg, ALERT_COLOR_ORANGE);
        return;
    }

    // No alert condition - hide if currently showing
    if (state->ui.alert_active) {
        alert_hide();
    }
}

void alert_show(const char *message, uint32_t color_hex) {
    app_state_t *state = app_state_get();

    // Update state
    state->ui.alert_active = true;
    state->ui.alert_start_time = esp_timer_get_time() / 1000;

    // Delegate UI rendering to ui_alerts module
    ui_alerts_show(message, color_hex);
}

void alert_hide(void) {
    app_state_t *state = app_state_get();

    if (!state->ui.alert_active) return;

    state->ui.alert_active = false;

    // Delegate UI hiding to ui_alerts module
    ui_alerts_hide();
}

void alert_check_auto_hide(void) {
    app_state_t *state = app_state_get();

    if (state->ui.alert_active) {
        uint32_t now = esp_timer_get_time() / 1000;
        if (now - state->ui.alert_start_time > ALERT_AUTO_HIDE_MS) {
            ESP_LOGI(TAG, "Auto-hiding alert after timeout");
            alert_hide();
        }
    }
}

bool alert_is_active(void) {
    app_state_t *state = app_state_get();
    return state->ui.alert_active;
}
