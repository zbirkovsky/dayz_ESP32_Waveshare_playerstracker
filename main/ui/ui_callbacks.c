/**
 * DayZ Server Tracker - UI Callbacks Implementation
 * Simple LVGL event callbacks that don't depend on widget state
 */

#include "ui_callbacks.h"
#include "app_state.h"
#include "events.h"
#include "services/settings_store.h"

// Navigation callbacks

void cb_refresh_clicked(lv_event_t *e) {
    (void)e;
    app_state_request_refresh();
}

void cb_card_clicked(lv_event_t *e) {
    (void)e;
    app_state_request_refresh();
}

void cb_settings_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_SETTINGS);
}

void cb_history_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_HISTORY);
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
