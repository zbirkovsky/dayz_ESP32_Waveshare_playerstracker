/**
 * DayZ Server Tracker - Alert Manager Implementation
 */

#include "alert_manager.h"
#include "config.h"
#include "app_state.h"
#include "drivers/buzzer.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"

// Static alert overlay - managed by this module
static lv_obj_t *alert_overlay = NULL;

void alert_check(void) {
    app_state_t *state = app_state_get();
    server_config_t *srv = app_state_get_active_server();

    if (!srv || !srv->alerts_enabled) return;

    int players = state->runtime.current_players;
    int max_p = state->runtime.max_players;

    if (players >= max_p) {
        if (!state->ui.alert_active) {
            buzzer_alert_threshold();
        }
        alert_show("SERVER FULL!", lv_color_hex(0xFF4444));
        return;
    }

    if (srv->alert_threshold > 0 && players >= srv->alert_threshold) {
        if (!state->ui.alert_active) {
            buzzer_alert_threshold();
        }
        char msg[64];
        snprintf(msg, sizeof(msg), "ALERT: %d+ players!", srv->alert_threshold);
        alert_show(msg, lv_color_hex(0xFF8800));
        return;
    }

    if (state->ui.alert_active) {
        alert_hide();
    }
}

void alert_show(const char *message, lv_color_t color) {
    app_state_t *state = app_state_get();

    if (!lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) return;

    state->ui.alert_active = true;
    state->ui.alert_start_time = esp_timer_get_time() / 1000;

    if (alert_overlay) {
        lv_obj_delete(alert_overlay);
    }

    alert_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(alert_overlay, LCD_WIDTH - 160, 60);
    lv_obj_align(alert_overlay, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(alert_overlay, color, 0);
    lv_obj_set_style_bg_opa(alert_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(alert_overlay, 0, 0);
    lv_obj_set_style_radius(alert_overlay, 0, 0);
    lv_obj_clear_flag(alert_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(alert_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(alert_overlay);
    lv_label_set_text(lbl, message);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);

    lvgl_port_unlock();
}

void alert_hide(void) {
    app_state_t *state = app_state_get();

    if (!state->ui.alert_active) return;

    if (lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) {
        if (alert_overlay) {
            lv_obj_delete(alert_overlay);
            alert_overlay = NULL;
        }
        state->ui.alert_active = false;
        lvgl_port_unlock();
    }
}

void alert_check_auto_hide(void) {
    app_state_t *state = app_state_get();

    if (state->ui.alert_active) {
        uint32_t now = esp_timer_get_time() / 1000;
        if (now - state->ui.alert_start_time > ALERT_AUTO_HIDE_MS) {
            alert_hide();
        }
    }
}

bool alert_is_active(void) {
    app_state_t *state = app_state_get();
    return state->ui.alert_active;
}
