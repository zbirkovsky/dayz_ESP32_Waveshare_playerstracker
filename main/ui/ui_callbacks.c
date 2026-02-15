/**
 * DayZ Server Tracker - UI Callbacks Implementation
 * Simple LVGL event callbacks that don't depend on widget state
 */

#include "ui_callbacks.h"
#include "ui_context.h"
#include "app_state.h"
#include "events.h"
#include "services/settings_store.h"
#include "services/server_query.h"
#include "esp_lvgl_port.h"
#include <string.h>

// Navigation callbacks

void cb_refresh_clicked(lv_event_t *e) {
    (void)e;
    server_query_request_refresh();
}

void cb_card_clicked(lv_event_t *e) {
    (void)e;
    server_query_request_refresh();
}

void cb_settings_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_SETTINGS);
}

void cb_history_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_HISTORY);
}

void cb_heatmap_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_HEATMAP);
}

void cb_back_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_MAIN);
}

void cb_wifi_settings_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_WIFI_SETTINGS);
}

void cb_server_settings_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_SERVER_SETTINGS);
}

void cb_add_server_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_ADD_SERVER);
}

// Server navigation callbacks

void cb_prev_server_clicked(lv_event_t *e) {
    (void)e;
    events_post_simple(EVT_SERVER_PREV);
}

void cb_next_server_clicked(lv_event_t *e) {
    (void)e;
    events_post_simple(EVT_SERVER_NEXT);
}

// Secondary server callbacks

void cb_secondary_box_clicked(lv_event_t *e) {
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    events_post_secondary_click(slot);
}

void cb_add_server_from_main_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_ADD_SERVER);
}

// Server management callbacks

void cb_delete_server_clicked(lv_event_t *e) {
    (void)e;
    app_state_t *state = app_state_get();
    app_event_t evt = {
        .type = EVT_SERVER_DELETE,
        .data.server_index = state->settings.active_server_index
    };
    events_post(&evt);
}

// Settings callbacks that read from event target

void cb_alerts_switch_changed(lv_event_t *e) {
    server_config_t *srv = app_state_get_active_server();
    if (!srv) return;

    lv_obj_t *sw = lv_event_get_target(e);
    srv->alerts_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    settings_save();
}

void cb_restart_hour_changed(lv_event_t *e) {
    server_config_t *srv = app_state_get_active_server();
    if (!srv) return;

    lv_obj_t *roller = lv_event_get_target(e);
    srv->restart_hour = lv_roller_get_selected(roller);
    settings_save();
}

void cb_restart_min_changed(lv_event_t *e) {
    server_config_t *srv = app_state_get_active_server();
    if (!srv) return;

    lv_obj_t *roller = lv_event_get_target(e);
    srv->restart_minute = lv_roller_get_selected(roller) * 5;
    settings_save();
}

void cb_restart_interval_changed(lv_event_t *e) {
    server_config_t *srv = app_state_get_active_server();
    if (!srv) return;

    lv_obj_t *dd = lv_event_get_target(e);
    int sel = lv_dropdown_get_selected(dd);
    const uint8_t intervals[] = {4, 6, 8, 12};
    srv->restart_interval_hours = intervals[sel];
    settings_save();
}

void cb_restart_manual_switch_changed(lv_event_t *e) {
    server_config_t *srv = app_state_get_active_server();
    if (!srv) return;

    lv_obj_t *sw = lv_event_get_target(e);
    srv->manual_restart_set = lv_obj_has_state(sw, LV_STATE_CHECKED);
    settings_save();
}

// WiFi multi callbacks

void cb_wifi_scan_clicked(lv_event_t *e) {
    (void)e;
    events_post_simple(EVT_WIFI_SCAN_START);
}

void cb_wifi_delete_credential(lv_event_t *e) {
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    events_post_wifi_delete_credential(index);
}

void cb_wifi_connect_saved(lv_event_t *e) {
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    events_post_wifi_connect_credential(index);
}

void cb_wifi_scan_result_clicked(lv_event_t *e) {
    int scan_idx = (int)(intptr_t)lv_event_get_user_data(e);
    app_state_t *state = app_state_get();

    if (app_state_lock(100)) {
        if (scan_idx >= 0 && scan_idx < state->wifi_multi.scan_count) {
            wifi_scan_result_t *r = &state->wifi_multi.scan_results[scan_idx];
            if (r->known) {
                // Known network - just connect
                app_state_unlock();
                events_post_wifi_connect_credential(r->cred_idx);
                return;
            }
        }
        app_state_unlock();
    }

    // Unknown network - show password entry
    // Store selected index in ui_context for the password save callback
    ui_context_t *ctx = ui_context_get();
    ctx->wifi_selected_scan_idx = scan_idx;

    // Show password area if it exists
    if (ctx->wifi_password_area) {
        lv_obj_clear_flag(ctx->wifi_password_area, LV_OBJ_FLAG_HIDDEN);
        if (ctx->wifi_ta_scan_pass) {
            lv_textarea_set_text(ctx->wifi_ta_scan_pass, "");
        }
    }
}

void cb_wifi_scan_connect_clicked(lv_event_t *e) {
    (void)e;
    ui_context_t *ctx = ui_context_get();
    app_state_t *state = app_state_get();
    int scan_idx = ctx->wifi_selected_scan_idx;

    if (scan_idx < 0 || scan_idx >= state->wifi_multi.scan_count) return;

    const char *password = "";
    if (ctx->wifi_ta_scan_pass) {
        password = lv_textarea_get_text(ctx->wifi_ta_scan_pass);
    }

    // Get the SSID from scan results
    char ssid[33] = {0};
    if (app_state_lock(100)) {
        strncpy(ssid, state->wifi_multi.scan_results[scan_idx].ssid, sizeof(ssid) - 1);
        app_state_unlock();
    }

    if (ssid[0] != '\0') {
        events_post_wifi_save(ssid, password);
    }
}

void cb_wifi_add_manual_clicked(lv_event_t *e) {
    (void)e;
    // Show the manual SSID/password entry (handled by screen builder toggling visibility)
    ui_context_t *ctx = ui_context_get();
    if (ctx->kb) {
        lv_obj_clear_flag(ctx->kb, LV_OBJ_FLAG_HIDDEN);
    }
}
