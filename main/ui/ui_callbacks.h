/**
 * DayZ Server Tracker - UI Callbacks
 * Simple LVGL event callbacks that don't depend on widget state
 */

#ifndef UI_CALLBACKS_H
#define UI_CALLBACKS_H

#include "lvgl.h"

// Navigation callbacks - trigger screen changes via events
void cb_refresh_clicked(lv_event_t *e);
void cb_card_clicked(lv_event_t *e);
void cb_settings_clicked(lv_event_t *e);
void cb_history_clicked(lv_event_t *e);
void cb_back_clicked(lv_event_t *e);
void cb_wifi_settings_clicked(lv_event_t *e);
void cb_server_settings_clicked(lv_event_t *e);
void cb_add_server_clicked(lv_event_t *e);

// Server navigation callbacks
void cb_prev_server_clicked(lv_event_t *e);
void cb_next_server_clicked(lv_event_t *e);

// Secondary server callbacks
void cb_secondary_box_clicked(lv_event_t *e);
void cb_add_server_from_main_clicked(lv_event_t *e);

// Server management callbacks
void cb_delete_server_clicked(lv_event_t *e);

// Settings callbacks that read from event target (no static widget dependency)
void cb_alerts_switch_changed(lv_event_t *e);
void cb_restart_hour_changed(lv_event_t *e);
void cb_restart_min_changed(lv_event_t *e);
void cb_restart_interval_changed(lv_event_t *e);
void cb_restart_manual_switch_changed(lv_event_t *e);

#endif // UI_CALLBACKS_H
