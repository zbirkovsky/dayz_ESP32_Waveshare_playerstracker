/**
 * DayZ Server Tracker - Screen Builder
 * Creates all application screens and populates ui_context
 */

#include "screen_builder.h"
#include <string.h>
#include <strings.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "config.h"
#include "app_state.h"
#include "events.h"
#include "ui/ui_context.h"
#include "ui/ui_styles.h"
#include "ui/ui_widgets.h"
#include "ui/ui_callbacks.h"
#include "ui/screen_history.h"
#include "power/screensaver.h"
#include "services/wifi_manager.h"
#include "services/settings_store.h"

static const char *TAG = "screen_builder";

// ============== UI WIDGET ACCESS MACROS ==============
#define UI_CTX (ui_context_get())

#define screen_main         (UI_CTX->screen_main)
#define screen_settings     (UI_CTX->screen_settings)
#define screen_wifi         (UI_CTX->screen_wifi)
#define screen_server       (UI_CTX->screen_server)
#define screen_add_server   (UI_CTX->screen_add_server)
#define screen_history      (UI_CTX->screen_history)

#define main_card           (UI_CTX->main_card)
#define lbl_wifi_icon       (UI_CTX->lbl_wifi_icon)
#define lbl_server          (UI_CTX->lbl_server)
#define lbl_server_time     (UI_CTX->lbl_server_time)
#define lbl_map_name        (UI_CTX->lbl_map_name)
#define day_night_indicator (UI_CTX->day_night_indicator)
#define lbl_players         (UI_CTX->lbl_players)
#define lbl_max             (UI_CTX->lbl_max)
#define bar_players         (UI_CTX->bar_players)
#define lbl_status          (UI_CTX->lbl_status)
#define lbl_update          (UI_CTX->lbl_update)
#define lbl_ip              (UI_CTX->lbl_ip)
#define lbl_restart         (UI_CTX->lbl_restart)
#define lbl_main_trend      (UI_CTX->lbl_main_trend)
#define lbl_rank            (UI_CTX->lbl_rank)
#define lbl_sd_status       (UI_CTX->lbl_sd_status)
#define btn_prev_server     (UI_CTX->btn_prev_server)
#define btn_next_server     (UI_CTX->btn_next_server)

#define kb                      (UI_CTX->kb)
#define kb_add                  (UI_CTX->kb_add)
#define ta_ssid                 (UI_CTX->ta_ssid)
#define ta_password             (UI_CTX->ta_password)
#define ta_server_id            (UI_CTX->ta_server_id)
#define ta_server_name          (UI_CTX->ta_server_name)
#define slider_refresh          (UI_CTX->slider_refresh)
#define lbl_refresh_val         (UI_CTX->lbl_refresh_val)
#define dropdown_screen_off     (UI_CTX->dropdown_screen_off)
#define slider_alert            (UI_CTX->slider_alert)
#define lbl_alert_val           (UI_CTX->lbl_alert_val)
#define sw_alerts               (UI_CTX->sw_alerts)
#define sw_restart_manual       (UI_CTX->sw_restart_manual)
#define roller_restart_hour     (UI_CTX->roller_restart_hour)
#define roller_restart_min      (UI_CTX->roller_restart_min)
#define dropdown_restart_interval (UI_CTX->dropdown_restart_interval)
#define dropdown_map            (UI_CTX->dropdown_map)
#define dropdown_map_settings   (UI_CTX->dropdown_map_settings)

#define chart_history       (UI_CTX->chart_history)
#define chart_series        (UI_CTX->chart_series)
#define lbl_history_legend  (UI_CTX->lbl_history_legend)
#define lbl_y_axis          (UI_CTX->lbl_y_axis)
#define lbl_x_axis          (UI_CTX->lbl_x_axis)

#define secondary_container (UI_CTX->secondary_container)
#define secondary_boxes     (UI_CTX->secondary_boxes)
#define add_server_boxes    (UI_CTX->add_server_boxes)

// ============== MAP DATA ==============
static const char *map_options_display = "Chernarus\nLivonia\nSakhal\nDeer Isle\nNamalsk\nEsseker\nTakistan\nBanov\nExclusion Zone\nOther";
static const char *map_internal_names[] = {
    "chernarusplus", "enoch", "sakhal", "deerisle", "namalsk",
    "esseker", "takistan", "banov", "exclusionzone", ""
};
#define MAP_OPTIONS_COUNT (sizeof(map_internal_names) / sizeof(map_internal_names[0]))

static int get_map_index(const char *internal_name) {
    if (!internal_name || internal_name[0] == '\0') return MAP_OPTIONS_COUNT - 1;
    for (int i = 0; i < (int)(MAP_OPTIONS_COUNT - 1); i++) {
        if (strcasecmp(internal_name, map_internal_names[i]) == 0) {
            return i;
        }
    }
    return MAP_OPTIONS_COUNT - 1;
}

// Screen off timeout values
static const uint16_t screen_off_values[] = {0, 300, 600, 900, 1800, 3600, 5400, 7200, 14400};
static const int screen_off_count = sizeof(screen_off_values) / sizeof(screen_off_values[0]);

// ============== LOCAL CALLBACKS ==============

static void on_keyboard_event(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    (void)lv_keyboard_get_textarea(obj);
    if (lv_event_get_code(e) == LV_EVENT_READY ||
        lv_event_get_code(e) == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_textarea_clicked(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    if (kb) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view(ta, LV_ANIM_ON);
    }
}

static void on_add_server_textarea_clicked(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    if (kb_add) {
        lv_keyboard_set_textarea(kb_add, ta);
        lv_obj_clear_flag(kb_add, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view(ta, LV_ANIM_ON);
    }
}

static void on_wifi_save_clicked(lv_event_t *e) {
    (void)e;
    if (lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) {
        events_post_wifi_save(lv_textarea_get_text(ta_ssid),
                              lv_textarea_get_text(ta_password));
        lvgl_port_unlock();
    }
}

static void on_refresh_slider_changed(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    app_state_t *state = app_state_get();
    state->settings.refresh_interval_sec = lv_slider_get_value(slider);
    if (lbl_refresh_val) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d sec", state->settings.refresh_interval_sec);
        lv_label_set_text(lbl_refresh_val, buf);
    }
    settings_save();
}

static void on_screen_off_changed(lv_event_t *e) {
    lv_obj_t *dropdown = lv_event_get_target(e);
    app_state_t *state = app_state_get();
    int idx = lv_dropdown_get_selected(dropdown);
    if (idx >= 0 && idx < screen_off_count) {
        state->settings.screensaver_timeout_sec = screen_off_values[idx];
        state->ui.last_activity_time = esp_timer_get_time() / 1000;
        ESP_LOGI(TAG, "Screen off timeout set to %d sec", state->settings.screensaver_timeout_sec);
        settings_save();
    }
}

static void on_alert_slider_changed(lv_event_t *e) {
    server_config_t *srv = app_state_get_active_server();
    if (!srv) return;
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    srv->alert_threshold = val;
    settings_save();
    if (lbl_alert_val) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d players", val);
        lv_label_set_text(lbl_alert_val, buf);
    }
}

static void on_server_save_clicked(lv_event_t *e) {
    (void)e;
    if (lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) {
        const char *map_name = "";
        if (dropdown_map) {
            uint16_t map_idx = lv_dropdown_get_selected(dropdown_map);
            if (map_idx < sizeof(map_internal_names) / sizeof(map_internal_names[0])) {
                map_name = map_internal_names[map_idx];
            }
        }
        events_post_server_add(lv_textarea_get_text(ta_server_id),
                               lv_textarea_get_text(ta_server_name),
                               map_name);
        lvgl_port_unlock();
    }
}

static void on_map_settings_changed(lv_event_t *e) {
    (void)e;
    if (!dropdown_map_settings) return;
    uint16_t map_idx = lv_dropdown_get_selected(dropdown_map_settings);
    if (map_idx < sizeof(map_internal_names) / sizeof(map_internal_names[0])) {
        if (app_state_lock(100)) {
            server_config_t *srv = app_state_get_active_server();
            if (srv) {
                strncpy(srv->map_name, map_internal_names[map_idx], sizeof(srv->map_name) - 1);
                srv->map_name[sizeof(srv->map_name) - 1] = '\0';
            }
            app_state_unlock();
            settings_save();
        }
    }
}

static void on_history_range_clicked(lv_event_t *e) {
    app_state_t *state = app_state_get();
    history_range_t range = (history_range_t)(intptr_t)lv_event_get_user_data(e);
    state->ui.current_history_range = range;

    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *parent = lv_obj_get_parent(btn);
    uint32_t child_cnt = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        lv_obj_set_style_bg_color(child, COLOR_CARD_BG, 0);
    }
    lv_obj_set_style_bg_color(btn, COLOR_DAYZ_GREEN, 0);
    screen_history_refresh();
}

// ============== SCREEN CREATION ==============

void screen_builder_create_main(void) {
    screen_main = ui_create_screen();

    lv_obj_add_event_cb(screen_main, screensaver_get_touch_pressed_cb(), LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen_main, screensaver_get_touch_released_cb(), LV_EVENT_RELEASED, NULL);

    main_card = ui_create_card(screen_main, 760, MAIN_CARD_HEIGHT_COMPACT);
    lv_obj_align(main_card, LV_ALIGN_TOP_MID, 0, 65);
    lv_obj_add_flag(main_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(main_card, cb_card_clicked, LV_EVENT_CLICKED, NULL);

    ui_create_icon_button(screen_main, LV_SYMBOL_SETTINGS, 20, 10, cb_settings_clicked);
    ui_create_icon_button(screen_main, LV_SYMBOL_IMAGE, 80, 10, cb_history_clicked);

    lv_obj_t *wifi_btn = lv_btn_create(screen_main);
    lv_obj_set_size(wifi_btn, 50, 50);
    lv_obj_align(wifi_btn, LV_ALIGN_TOP_LEFT, 140, 10);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(wifi_btn, 0, 0);
    lv_obj_set_style_border_width(wifi_btn, 0, 0);
    lv_obj_add_event_cb(wifi_btn, cb_wifi_settings_clicked, LV_EVENT_CLICKED, NULL);

    lbl_wifi_icon = lv_label_create(wifi_btn);
    lv_label_set_text(lbl_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(lbl_wifi_icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_wifi_icon, COLOR_TEXT_MUTED, 0);
    lv_obj_center(lbl_wifi_icon);

    btn_prev_server = ui_create_icon_button(screen_main, LV_SYMBOL_LEFT, LCD_WIDTH/2 - 125, 10, cb_prev_server_clicked);
    btn_next_server = ui_create_icon_button(screen_main, LV_SYMBOL_RIGHT, LCD_WIDTH/2 + 75, 10, cb_next_server_clicked);

    lbl_sd_status = lv_label_create(screen_main);
    lv_label_set_text(lbl_sd_status, "SD: --");
    lv_obj_set_style_text_font(lbl_sd_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_sd_status, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_sd_status, LV_ALIGN_TOP_RIGHT, -140, 25);

    lv_obj_t *btn_refresh = lv_btn_create(screen_main);
    lv_obj_set_size(btn_refresh, 110, 50);
    lv_obj_align(btn_refresh, LV_ALIGN_TOP_RIGHT, -20, 10);
    lv_obj_set_style_bg_color(btn_refresh, COLOR_BUTTON_PRIMARY, 0);
    lv_obj_set_style_radius(btn_refresh, UI_BUTTON_RADIUS, 0);
    lv_obj_add_event_cb(btn_refresh, cb_refresh_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_refresh = lv_label_create(btn_refresh);
    lv_label_set_text(lbl_refresh, LV_SYMBOL_REFRESH " Refresh");
    lv_obj_set_style_text_font(lbl_refresh, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_refresh);

    lv_obj_t *server_row = ui_create_row(main_card, 700, 30);
    lv_obj_align(server_row, LV_ALIGN_TOP_MID, 0, 0);

    lbl_server = lv_label_create(server_row);
    lv_label_set_text(lbl_server, "Loading...");
    lv_obj_set_style_text_font(lbl_server, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_server, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_width(lbl_server, 450);
    lv_label_set_long_mode(lbl_server, LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_server, LV_ALIGN_LEFT_MID, 0, -8);

    lbl_rank = lv_label_create(server_row);
    lv_label_set_text(lbl_rank, "");
    lv_obj_set_style_text_font(lbl_rank, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_rank, COLOR_INFO, 0);
    lv_obj_align(lbl_rank, LV_ALIGN_LEFT_MID, 0, 10);

    day_night_indicator = lv_obj_create(server_row);
    lv_obj_set_size(day_night_indicator, 16, 16);
    lv_obj_align(day_night_indicator, LV_ALIGN_RIGHT_MID, -80, 0);
    lv_obj_set_style_radius(day_night_indicator, 8, 0);
    lv_obj_set_style_bg_color(day_night_indicator, COLOR_DAY_SUN, 0);
    lv_obj_set_style_border_width(day_night_indicator, 0, 0);
    lv_obj_add_flag(day_night_indicator, LV_OBJ_FLAG_HIDDEN);

    lbl_server_time = lv_label_create(server_row);
    lv_label_set_text(lbl_server_time, "");
    lv_obj_set_style_text_font(lbl_server_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_server_time, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_server_time, LV_ALIGN_RIGHT_MID, 0, -6);

    lv_obj_t *players_cont = ui_create_row(main_card, 550, 60);
    lv_obj_align(players_cont, LV_ALIGN_TOP_MID, 0, 45);

    lv_obj_t *lbl_players_title = lv_label_create(players_cont);
    lv_label_set_text(lbl_players_title, "PLAYERS");
    lv_obj_set_style_text_font(lbl_players_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_players_title, COLOR_INFO, 0);
    lv_obj_align(lbl_players_title, LV_ALIGN_LEFT_MID, 0, 0);

    lbl_players = lv_label_create(players_cont);
    lv_label_set_text(lbl_players, "---");
    lv_obj_set_style_text_font(lbl_players, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_players, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(lbl_players, LV_ALIGN_LEFT_MID, 120, 0);

    lbl_max = lv_label_create(players_cont);
    lv_label_set_text(lbl_max, "/60");
    lv_obj_set_style_text_font(lbl_max, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_max, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_max, LV_ALIGN_LEFT_MID, 200, 3);

    lbl_main_trend = lv_label_create(players_cont);
    lv_label_set_text(lbl_main_trend, "");
    lv_obj_set_style_text_font(lbl_main_trend, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_main_trend, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_main_trend, LV_ALIGN_RIGHT_MID, 0, 0);

    bar_players = lv_bar_create(main_card);
    lv_obj_set_size(bar_players, 680, 25);
    lv_obj_align(bar_players, LV_ALIGN_TOP_MID, 0, 120);
    lv_bar_set_range(bar_players, 0, 60);
    lv_bar_set_value(bar_players, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_players, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(bar_players, COLOR_SUCCESS, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_players, 8, 0);
    lv_obj_set_style_radius(bar_players, 8, LV_PART_INDICATOR);

    lv_obj_t *info_row = ui_create_row(main_card, 680, 35);
    lv_obj_align(info_row, LV_ALIGN_TOP_MID, 0, 160);

    lbl_restart = lv_label_create(info_row);
    lv_label_set_text(lbl_restart, "");
    lv_obj_set_style_text_font(lbl_restart, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_restart, lv_color_hex(0xFF6B6B), 0);
    lv_obj_align(lbl_restart, LV_ALIGN_LEFT_MID, 0, 0);

    lbl_status = lv_label_create(info_row);
    lv_label_set_text(lbl_status, "CONNECTING...");
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xFFA500), 0);
    lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, 0);

    lbl_update = lv_label_create(info_row);
    lv_label_set_text(lbl_update, "");
    lv_obj_set_style_text_font(lbl_update, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_update, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_update, LV_ALIGN_RIGHT_MID, 0, 0);

    lbl_ip = lv_label_create(main_card);
    lv_label_set_text(lbl_ip, "");
    lv_obj_add_flag(lbl_ip, LV_OBJ_FLAG_HIDDEN);

    lbl_map_name = lv_label_create(main_card);
    lv_label_set_text(lbl_map_name, "");
    lv_obj_set_style_text_font(lbl_map_name, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_map_name, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_map_name, LV_ALIGN_TOP_RIGHT, -10, 28);
    lv_obj_move_foreground(lbl_map_name);

    screen_builder_create_secondary_boxes();
}

void screen_builder_create_settings(void) {
    app_state_t *state = app_state_get();

    screen_settings = ui_create_screen();
    lv_obj_add_event_cb(screen_settings, screensaver_get_touch_pressed_cb(), LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen_settings, screensaver_get_touch_released_cb(), LV_EVENT_RELEASED, NULL);
    ui_create_back_button(screen_settings, cb_back_clicked);
    ui_create_title(screen_settings, "Settings");

    lv_obj_t *cont = ui_create_scroll_container(screen_settings, 700, 400);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 20);

    ui_create_menu_button(cont, "WiFi Settings", LV_SYMBOL_WIFI, COLOR_BUTTON_PRIMARY, cb_wifi_settings_clicked);
    ui_create_menu_button(cont, "Server Settings", LV_SYMBOL_LIST, COLOR_SUCCESS, cb_server_settings_clicked);

    lv_obj_t *refresh_row = ui_create_row(cont, 660, 55);
    slider_refresh = ui_create_slider(refresh_row, "Refresh:", MIN_REFRESH_INTERVAL_SEC, MAX_REFRESH_INTERVAL_SEC,
                                       state->settings.refresh_interval_sec, &lbl_refresh_val, on_refresh_slider_changed);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d sec", state->settings.refresh_interval_sec);
    lv_label_set_text(lbl_refresh_val, buf);

    lv_obj_t *screenoff_row = ui_create_row(cont, 660, 50);
    lv_obj_t *lbl_screenoff = lv_label_create(screenoff_row);
    lv_label_set_text(lbl_screenoff, "Screen Off:");
    lv_obj_set_style_text_font(lbl_screenoff, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_screenoff, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(lbl_screenoff, LV_ALIGN_LEFT_MID, 0, 0);

    dropdown_screen_off = lv_dropdown_create(screenoff_row);
    lv_dropdown_set_options(dropdown_screen_off, "Off\n5 min\n10 min\n15 min\n30 min\n60 min\n90 min\n120 min\n240 min");
    lv_obj_set_size(dropdown_screen_off, 120, 40);
    lv_obj_align(dropdown_screen_off, LV_ALIGN_RIGHT_MID, 0, 0);

    int screen_off_idx = 0;
    for (int i = 0; i < screen_off_count; i++) {
        if (screen_off_values[i] == state->settings.screensaver_timeout_sec) {
            screen_off_idx = i;
            break;
        }
    }
    lv_dropdown_set_selected(dropdown_screen_off, screen_off_idx);
    lv_obj_add_event_cb(dropdown_screen_off, on_screen_off_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *alerts_row = ui_create_row(cont, 660, 45);
    server_config_t *srv = app_state_get_active_server();
    bool alerts_on = srv ? srv->alerts_enabled : false;
    sw_alerts = ui_create_switch(alerts_row, "Alerts Enabled:", alerts_on, cb_alerts_switch_changed);

    lv_obj_t *threshold_row = ui_create_row(cont, 660, 55);
    int slider_max = (srv && srv->max_players > 0) ? srv->max_players : DEFAULT_MAX_PLAYERS;
    int threshold = srv ? srv->alert_threshold : (slider_max / 2);
    if (threshold > slider_max) threshold = slider_max;

    slider_alert = ui_create_slider(threshold_row, "Threshold:", 1, slider_max, threshold,
                                     &lbl_alert_val, on_alert_slider_changed);
    snprintf(buf, sizeof(buf), "%d players", threshold);
    lv_label_set_text(lbl_alert_val, buf);

    ui_create_section_header(cont, "Restart Schedule (CET)", lv_color_hex(0xFF9800));

    lv_obj_t *restart_enable_row = ui_create_row(cont, 660, 45);
    bool manual_on = srv ? srv->manual_restart_set : false;
    sw_restart_manual = ui_create_switch(restart_enable_row, "Manual Schedule:", manual_on, cb_restart_manual_switch_changed);

    lv_obj_t *restart_time_row = ui_create_row(cont, 660, 60);

    lv_obj_t *lbl_time = lv_label_create(restart_time_row);
    lv_label_set_text(lbl_time, "Known restart:");
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_time, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 0, 0);

    roller_restart_hour = lv_roller_create(restart_time_row);
    lv_roller_set_options(roller_restart_hour,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23",
        LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_restart_hour, 2);
    lv_obj_set_size(roller_restart_hour, 60, 50);
    lv_obj_align(roller_restart_hour, LV_ALIGN_CENTER, -20, 0);
    uint8_t srv_hour = srv ? srv->restart_hour : 0;
    lv_roller_set_selected(roller_restart_hour, srv_hour, LV_ANIM_OFF);
    lv_obj_add_event_cb(roller_restart_hour, cb_restart_hour_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_colon = lv_label_create(restart_time_row);
    lv_label_set_text(lbl_colon, ":");
    lv_obj_set_style_text_font(lbl_colon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_colon, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(lbl_colon, LV_ALIGN_CENTER, 25, 0);

    roller_restart_min = lv_roller_create(restart_time_row);
    lv_roller_set_options(roller_restart_min, "00\n05\n10\n15\n20\n25\n30\n35\n40\n45\n50\n55", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_restart_min, 2);
    lv_obj_set_size(roller_restart_min, 60, 50);
    lv_obj_align(roller_restart_min, LV_ALIGN_CENTER, 70, 0);
    uint8_t srv_min = srv ? srv->restart_minute : 0;
    lv_roller_set_selected(roller_restart_min, srv_min / 5, LV_ANIM_OFF);
    lv_obj_add_event_cb(roller_restart_min, cb_restart_min_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_interval = lv_label_create(restart_time_row);
    lv_label_set_text(lbl_interval, "every");
    lv_obj_set_style_text_font(lbl_interval, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_interval, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(lbl_interval, LV_ALIGN_CENTER, 130, 0);

    dropdown_restart_interval = lv_dropdown_create(restart_time_row);
    lv_dropdown_set_options(dropdown_restart_interval, "4h\n6h\n8h\n12h");
    lv_obj_set_size(dropdown_restart_interval, 70, 40);
    lv_obj_align(dropdown_restart_interval, LV_ALIGN_RIGHT_MID, 0, 0);
    uint8_t srv_interval = srv ? srv->restart_interval_hours : 4;
    int interval_idx = 0;
    if (srv_interval == 6) interval_idx = 1;
    else if (srv_interval == 8) interval_idx = 2;
    else if (srv_interval == 12) interval_idx = 3;
    lv_dropdown_set_selected(dropdown_restart_interval, interval_idx);
    lv_obj_add_event_cb(dropdown_restart_interval, cb_restart_interval_changed, LV_EVENT_VALUE_CHANGED, NULL);
}

void screen_builder_create_wifi_settings(void) {
    app_state_t *state = app_state_get();

    screen_wifi = ui_create_screen();
    lv_obj_add_event_cb(screen_wifi, screensaver_get_touch_pressed_cb(), LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen_wifi, screensaver_get_touch_released_cb(), LV_EVENT_RELEASED, NULL);
    ui_create_back_button(screen_wifi, cb_back_clicked);
    ui_create_title(screen_wifi, "WiFi Settings");

    // Diagnostics panel
    lv_obj_t *diag_panel = lv_obj_create(screen_wifi);
    lv_obj_set_size(diag_panel, 280, 180);
    lv_obj_align(diag_panel, LV_ALIGN_TOP_RIGHT, -20, 60);
    lv_obj_set_style_bg_color(diag_panel, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_color(diag_panel, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(diag_panel, 1, 0);
    lv_obj_set_style_radius(diag_panel, 10, 0);
    lv_obj_set_style_pad_all(diag_panel, 15, 0);
    lv_obj_clear_flag(diag_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *diag_title = lv_label_create(diag_panel);
    lv_label_set_text(diag_title, "Connection Status");
    lv_obj_set_style_text_font(diag_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(diag_title, COLOR_INFO, 0);
    lv_obj_align(diag_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_conn_status = lv_label_create(diag_panel);
    if (wifi_manager_is_connected()) {
        lv_label_set_text(lbl_conn_status, LV_SYMBOL_OK " Connected");
        lv_obj_set_style_text_color(lbl_conn_status, COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text(lbl_conn_status, LV_SYMBOL_CLOSE " Disconnected");
        lv_obj_set_style_text_color(lbl_conn_status, COLOR_DANGER, 0);
    }
    lv_obj_set_style_text_font(lbl_conn_status, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_conn_status, LV_ALIGN_TOP_LEFT, 0, 22);

    char ssid_buf[64];
    wifi_manager_get_ssid(ssid_buf, sizeof(ssid_buf));
    lv_obj_t *lbl_ssid_info = lv_label_create(diag_panel);
    char ssid_line[80];
    snprintf(ssid_line, sizeof(ssid_line), "SSID: %s", ssid_buf);
    lv_label_set_text(lbl_ssid_info, ssid_line);
    lv_obj_set_style_text_font(lbl_ssid_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_ssid_info, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_ssid_info, LV_ALIGN_TOP_LEFT, 0, 44);

    int rssi = wifi_manager_get_rssi();
    lv_obj_t *lbl_rssi = lv_label_create(diag_panel);
    char rssi_line[32];
    if (rssi != 0) {
        const char *strength = rssi >= -50 ? "Excellent" : rssi >= -60 ? "Good" : rssi >= -70 ? "Fair" : "Weak";
        snprintf(rssi_line, sizeof(rssi_line), "Signal: %d dBm (%s)", rssi, strength);
    } else {
        snprintf(rssi_line, sizeof(rssi_line), "Signal: --");
    }
    lv_label_set_text(lbl_rssi, rssi_line);
    lv_obj_set_style_text_font(lbl_rssi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_rssi, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_rssi, LV_ALIGN_TOP_LEFT, 0, 62);

    char ip_buf[32];
    wifi_manager_get_ip_str(ip_buf, sizeof(ip_buf));
    lv_obj_t *lbl_ip_info = lv_label_create(diag_panel);
    char ip_line[48];
    snprintf(ip_line, sizeof(ip_line), "IP: %s", ip_buf);
    lv_label_set_text(lbl_ip_info, ip_line);
    lv_obj_set_style_text_font(lbl_ip_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_ip_info, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_ip_info, LV_ALIGN_TOP_LEFT, 0, 80);

    char mac_buf[20];
    wifi_manager_get_mac_str(mac_buf, sizeof(mac_buf));
    lv_obj_t *lbl_mac = lv_label_create(diag_panel);
    char mac_line[40];
    snprintf(mac_line, sizeof(mac_line), "MAC: %s", mac_buf);
    lv_label_set_text(lbl_mac, mac_line);
    lv_obj_set_style_text_font(lbl_mac, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_mac, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_mac, LV_ALIGN_TOP_LEFT, 0, 98);

    lv_obj_t *lbl_time_sync = lv_label_create(diag_panel);
    if (wifi_manager_is_time_synced()) {
        lv_label_set_text(lbl_time_sync, "Time: Synced " LV_SYMBOL_OK);
        lv_obj_set_style_text_color(lbl_time_sync, COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text(lbl_time_sync, "Time: Not synced");
        lv_obj_set_style_text_color(lbl_time_sync, COLOR_WARNING, 0);
    }
    lv_obj_set_style_text_font(lbl_time_sync, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_time_sync, LV_ALIGN_TOP_LEFT, 0, 116);

    ta_ssid = ui_create_text_input(screen_wifi, "WiFi Network (SSID):", "Enter SSID",
                                    state->settings.wifi_ssid, 30, 70, 350, false, on_textarea_clicked);
    ta_password = ui_create_text_input(screen_wifi, "Password:", "Enter password",
                                        state->settings.wifi_password, 30, 150, 350, true, on_textarea_clicked);

    lv_obj_t *btn_save = ui_create_button(screen_wifi, "Save", LV_SYMBOL_OK, 100, 40, COLOR_DAYZ_GREEN, on_wifi_save_clicked);
    lv_obj_align(btn_save, LV_ALIGN_TOP_LEFT, 120, 10);

    kb = ui_create_keyboard(screen_wifi, ta_ssid);
    lv_obj_add_event_cb(kb, on_keyboard_event, LV_EVENT_ALL, NULL);
}

void screen_builder_create_server_settings(void) {
    app_state_t *state = app_state_get();

    screen_server = ui_create_screen();
    lv_obj_add_event_cb(screen_server, screensaver_get_touch_pressed_cb(), LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen_server, screensaver_get_touch_released_cb(), LV_EVENT_RELEASED, NULL);
    ui_create_back_button(screen_server, cb_back_clicked);
    ui_create_title(screen_server, "Server Settings");

    lv_obj_t *list = ui_create_scroll_container(screen_server, 700, 250);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 50);

    for (int i = 0; i < state->settings.server_count; i++) {
        lv_obj_t *item = lv_obj_create(list);
        lv_obj_set_size(item, 660, 50);
        lv_obj_set_style_bg_color(item, (i == state->settings.active_server_index) ? COLOR_BUTTON_PRIMARY : lv_color_hex(0x333333), 0);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, state->settings.servers[i].display_name);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, COLOR_TEXT_PRIMARY, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);

        lv_obj_t *lbl_id = lv_label_create(item);
        char id_buf[48];
        snprintf(id_buf, sizeof(id_buf), "ID: %s", state->settings.servers[i].server_id);
        lv_label_set_text(lbl_id, id_buf);
        lv_obj_set_style_text_font(lbl_id, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_id, COLOR_TEXT_SECONDARY, 0);
        lv_obj_align(lbl_id, LV_ALIGN_RIGHT_MID, -10, 0);
    }

    lv_obj_t *map_row = ui_create_row(screen_server, 400, 50);
    lv_obj_align(map_row, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_set_flex_flow(map_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(map_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_map = lv_label_create(map_row);
    lv_label_set_text(lbl_map, "Map:");
    lv_obj_set_style_text_font(lbl_map, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_map, COLOR_TEXT_PRIMARY, 0);

    dropdown_map_settings = lv_dropdown_create(map_row);
    lv_dropdown_set_options(dropdown_map_settings, map_options_display);
    server_config_t *active_srv = app_state_get_active_server();
    int map_idx = active_srv ? get_map_index(active_srv->map_name) : 0;
    lv_dropdown_set_selected(dropdown_map_settings, map_idx);
    lv_obj_set_size(dropdown_map_settings, 200, 40);
    lv_obj_set_style_bg_color(dropdown_map_settings, COLOR_CARD_BG, 0);
    lv_obj_set_style_text_color(dropdown_map_settings, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(dropdown_map_settings, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(dropdown_map_settings, 1, 0);
    lv_obj_set_style_radius(dropdown_map_settings, 8, 0);
    lv_obj_set_style_bg_color(dropdown_map_settings, COLOR_CARD_BG, LV_PART_ITEMS);
    lv_obj_set_style_text_color(dropdown_map_settings, COLOR_TEXT_PRIMARY, LV_PART_ITEMS);
    lv_obj_add_event_cb(dropdown_map_settings, on_map_settings_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *btn_add = ui_create_button(screen_server, "Add Server", LV_SYMBOL_PLUS, 200, 50, COLOR_DAYZ_GREEN, cb_add_server_clicked);
    lv_obj_align(btn_add, LV_ALIGN_BOTTOM_LEFT, 50, -20);

    if (state->settings.server_count > 1) {
        lv_obj_t *btn_del = ui_create_button(screen_server, "Delete Active", LV_SYMBOL_TRASH, 200, 50, COLOR_ALERT_RED, cb_delete_server_clicked);
        lv_obj_align(btn_del, LV_ALIGN_BOTTOM_RIGHT, -50, -20);
    }
}

void screen_builder_create_add_server(void) {
    screen_add_server = ui_create_screen();
    lv_obj_add_event_cb(screen_add_server, screensaver_get_touch_pressed_cb(), LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen_add_server, screensaver_get_touch_released_cb(), LV_EVENT_RELEASED, NULL);
    ui_create_back_button(screen_add_server, cb_back_clicked);
    ui_create_title(screen_add_server, "Add Server");

    ta_server_id = ui_create_text_input(screen_add_server, "BattleMetrics Server ID:", "e.g., 29986583", "",
                                         50, 70, 400, false, on_add_server_textarea_clicked);
    ta_server_name = ui_create_text_input(screen_add_server, "Display Name:", "e.g., My DayZ Server", "",
                                           50, 150, 400, false, on_add_server_textarea_clicked);

    lv_obj_t *lbl_map = lv_label_create(screen_add_server);
    lv_label_set_text(lbl_map, "Map:");
    lv_obj_set_style_text_font(lbl_map, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_map, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_pos(lbl_map, 50, 230);

    dropdown_map = lv_dropdown_create(screen_add_server);
    lv_dropdown_set_options(dropdown_map, map_options_display);
    lv_dropdown_set_selected(dropdown_map, 0);
    lv_obj_set_size(dropdown_map, 200, 40);
    lv_obj_set_pos(dropdown_map, 100, 225);
    lv_obj_set_style_bg_color(dropdown_map, COLOR_CARD_BG, 0);
    lv_obj_set_style_text_color(dropdown_map, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(dropdown_map, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(dropdown_map, 1, 0);
    lv_obj_set_style_radius(dropdown_map, 8, 0);
    lv_obj_set_style_bg_color(dropdown_map, COLOR_CARD_BG, LV_PART_ITEMS);
    lv_obj_set_style_text_color(dropdown_map, COLOR_TEXT_PRIMARY, LV_PART_ITEMS);

    lv_obj_t *btn_save = ui_create_button(screen_add_server, "Add", LV_SYMBOL_OK, 150, 50, COLOR_DAYZ_GREEN, on_server_save_clicked);
    lv_obj_align(btn_save, LV_ALIGN_TOP_RIGHT, -50, 225);

    kb_add = ui_create_keyboard(screen_add_server, ta_server_id);
}

void screen_builder_create_history(void) {
    app_state_t *state = app_state_get();

    screen_history = ui_create_screen();
    lv_obj_add_event_cb(screen_history, screensaver_get_touch_pressed_cb(), LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen_history, screensaver_get_touch_released_cb(), LV_EVENT_RELEASED, NULL);
    ui_create_back_button(screen_history, cb_back_clicked);
    ui_create_title(screen_history, "Player History");

    lv_obj_t *btn_cont = ui_create_row(screen_history, 500, 45);
    lv_obj_align(btn_cont, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const char *btn_labels[] = {"1 Hour", "8 Hours", "24 Hours", "1 Week"};
    history_range_t ranges[] = {HISTORY_RANGE_1H, HISTORY_RANGE_8H, HISTORY_RANGE_24H, HISTORY_RANGE_WEEK};

    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(btn_cont);
        lv_obj_set_size(btn, 110, 35);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_bg_color(btn, (i == state->ui.current_history_range) ? COLOR_DAYZ_GREEN : COLOR_CARD_BG, 0);
        lv_obj_add_event_cb(btn, on_history_range_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)ranges[i]);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, btn_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);
    }

    chart_history = lv_chart_create(screen_history);
    lv_obj_set_size(chart_history, 680, 280);
    lv_obj_align(chart_history, LV_ALIGN_CENTER, 20, 20);
    lv_obj_set_style_bg_color(chart_history, COLOR_CARD_BG, 0);
    lv_obj_set_style_radius(chart_history, 15, 0);
    lv_obj_set_style_border_width(chart_history, 0, 0);
    lv_obj_set_style_line_color(chart_history, lv_color_hex(UI_CHART_GRID_COLOR), LV_PART_MAIN);
    lv_obj_set_style_pad_left(chart_history, 10, 0);
    lv_obj_set_style_pad_right(chart_history, 10, 0);
    lv_obj_set_style_pad_bottom(chart_history, 5, 0);

    lv_chart_set_type(chart_history, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart_history, LV_CHART_AXIS_PRIMARY_Y, 0, 60);
    lv_chart_set_div_line_count(chart_history, 4, 4);

    chart_series = lv_chart_add_series(chart_history, COLOR_DAYZ_GREEN, LV_CHART_AXIS_PRIMARY_Y);

    lv_coord_t chart_top = 120;
    lv_coord_t chart_plot_height = 270;
    for (int i = 0; i < 5; i++) {
        lbl_y_axis[i] = lv_label_create(screen_history);
        lv_label_set_text(lbl_y_axis[i], "--");
        lv_obj_set_style_text_font(lbl_y_axis[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_y_axis[i], COLOR_TEXT_MUTED, 0);
        lv_obj_set_pos(lbl_y_axis[i], 50, chart_top + (i * chart_plot_height / 4) - 7);
    }

    lv_coord_t chart_left = 90;
    lv_coord_t chart_width = 660;
    lv_coord_t x_label_y = 410;
    for (int i = 0; i < 5; i++) {
        lbl_x_axis[i] = lv_label_create(screen_history);
        lv_label_set_text(lbl_x_axis[i], "--:--");
        lv_obj_set_style_text_font(lbl_x_axis[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_x_axis[i], COLOR_TEXT_MUTED, 0);
        lv_coord_t x_pos = chart_left + (i * chart_width / 4) - 18;
        lv_obj_set_pos(lbl_x_axis[i], x_pos, x_label_y);
    }

    lbl_history_legend = lv_label_create(screen_history);
    lv_obj_set_style_text_font(lbl_history_legend, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_history_legend, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_history_legend, LV_ALIGN_BOTTOM_MID, 0, -5);

    static history_screen_widgets_t history_widgets;
    history_widgets.screen = screen_history;
    history_widgets.chart = chart_history;
    history_widgets.series = chart_series;
    history_widgets.lbl_legend = lbl_history_legend;
    for (int i = 0; i < 5; i++) {
        history_widgets.lbl_y_axis[i] = lbl_y_axis[i];
        history_widgets.lbl_x_axis[i] = lbl_x_axis[i];
    }
    screen_history_init(&history_widgets);
    screen_history_refresh();
}

void screen_builder_create_secondary_boxes(void) {
    app_state_t *state = app_state_get();

    secondary_container = lv_obj_create(screen_main);
    lv_obj_set_size(secondary_container, 760, SECONDARY_CONTAINER_HEIGHT);
    lv_obj_align(secondary_container, LV_ALIGN_TOP_MID, 0, 65 + MAIN_CARD_HEIGHT_COMPACT + SECONDARY_BOX_GAP);
    lv_obj_set_style_bg_opa(secondary_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(secondary_container, 0, 0);
    lv_obj_set_style_pad_all(secondary_container, 0, 0);
    lv_obj_clear_flag(secondary_container, LV_OBJ_FLAG_SCROLLABLE);

    int total_width = (SECONDARY_BOX_WIDTH * 3) + (SECONDARY_BOX_GAP * 2);
    int start_x = (760 - total_width) / 2;

    app_state_update_secondary_indices();

    for (int i = 0; i < MAX_SECONDARY_SERVERS; i++) {
        int x_pos = start_x + (i * (SECONDARY_BOX_WIDTH + SECONDARY_BOX_GAP));

        if (i < state->runtime.secondary_count) {
            secondary_boxes[i] = ui_create_secondary_box(secondary_container, SECONDARY_BOX_WIDTH, SECONDARY_BOX_HEIGHT,
                                                          cb_secondary_box_clicked, (void*)(intptr_t)i);
            lv_obj_set_pos(secondary_boxes[i].container, x_pos, 2);
            add_server_boxes[i] = NULL;
        } else if (state->settings.server_count < MAX_SERVERS) {
            add_server_boxes[i] = ui_create_add_server_box(secondary_container, SECONDARY_BOX_WIDTH, SECONDARY_BOX_HEIGHT,
                                                            cb_add_server_from_main_clicked);
            lv_obj_set_pos(add_server_boxes[i], x_pos, 2);
            memset(&secondary_boxes[i], 0, sizeof(secondary_box_widgets_t));
        } else {
            add_server_boxes[i] = NULL;
            memset(&secondary_boxes[i], 0, sizeof(secondary_box_widgets_t));
        }
    }
}
