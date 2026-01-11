/**
 * DayZ Server Tracker - UI Context
 * Centralized storage for all UI widget pointers
 */

#ifndef UI_CONTEXT_H
#define UI_CONTEXT_H

#include "lvgl.h"
#include "ui_widgets.h"
#include "config.h"

/**
 * UI Context - holds all widget pointers for the application
 * This enables passing widgets between modules without global statics
 */
typedef struct {
    // Screen objects
    lv_obj_t *screen_main;
    lv_obj_t *screen_settings;
    lv_obj_t *screen_wifi;
    lv_obj_t *screen_server;
    lv_obj_t *screen_add_server;
    lv_obj_t *screen_history;

    // Main screen widgets
    lv_obj_t *main_card;
    lv_obj_t *lbl_wifi_icon;
    lv_obj_t *lbl_server;
    lv_obj_t *lbl_server_time;
    lv_obj_t *lbl_map_name;
    lv_obj_t *day_night_indicator;
    lv_obj_t *lbl_players;
    lv_obj_t *lbl_max;
    lv_obj_t *bar_players;
    lv_obj_t *lbl_status;
    lv_obj_t *lbl_update;
    lv_obj_t *lbl_ip;
    lv_obj_t *lbl_restart;
    lv_obj_t *lbl_main_trend;
    lv_obj_t *lbl_rank;
    lv_obj_t *lbl_sd_status;
    lv_obj_t *btn_prev_server;
    lv_obj_t *btn_next_server;

    // Settings widgets
    lv_obj_t *kb;           // Keyboard for WiFi settings
    lv_obj_t *kb_add;       // Keyboard for Add Server screen
    lv_obj_t *ta_ssid;
    lv_obj_t *ta_password;
    lv_obj_t *ta_server_id;
    lv_obj_t *ta_server_name;
    lv_obj_t *slider_refresh;
    lv_obj_t *lbl_refresh_val;
    lv_obj_t *dropdown_screen_off;
    lv_obj_t *slider_alert;
    lv_obj_t *lbl_alert_val;
    lv_obj_t *sw_alerts;
    lv_obj_t *sw_restart_manual;
    lv_obj_t *roller_restart_hour;
    lv_obj_t *roller_restart_min;
    lv_obj_t *dropdown_restart_interval;
    lv_obj_t *dropdown_map;          // Map dropdown for Add Server
    lv_obj_t *dropdown_map_settings; // Map dropdown for Server Settings

    // History widgets
    lv_obj_t *chart_history;
    lv_chart_series_t *chart_series;
    lv_obj_t *lbl_history_legend;
    lv_obj_t *lbl_y_axis[5];
    lv_obj_t *lbl_x_axis[5];

    // Multi-server watch widgets
    lv_obj_t *secondary_container;
    secondary_box_widgets_t secondary_boxes[MAX_SECONDARY_SERVERS];
    lv_obj_t *add_server_boxes[MAX_SECONDARY_SERVERS];
} ui_context_t;

/**
 * Get the global UI context
 * @return Pointer to the UI context (singleton)
 */
ui_context_t* ui_context_get(void);

/**
 * Initialize the UI context (call once at startup)
 */
void ui_context_init(void);

#endif // UI_CONTEXT_H
