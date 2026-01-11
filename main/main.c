/**
 * DayZ Server Tracker v2.0
 * For Waveshare ESP32-S3-Touch-LCD-7 (800x480)
 *
 * Refactored architecture with modular components:
 * - config.h: All constants and pin definitions
 * - app_state: Centralized state management
 * - events: Event queue for UI/logic decoupling
 * - drivers: Hardware abstraction (buzzer, sd_card, display)
 * - services: Business logic (battlemetrics, wifi, settings, history)
 * - ui: Styles and widget factories
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"

// Application modules
#include "config.h"
#include "app_state.h"
#include "events.h"
#include "drivers/buzzer.h"
#include "drivers/sd_card.h"
#include "drivers/display.h"
#include "drivers/io_expander.h"
#include "services/wifi_manager.h"
#include "services/battlemetrics.h"
#include "services/settings_store.h"
#include "services/history_store.h"
#include "services/secondary_fetch.h"
#include "services/restart_manager.h"
#include "services/alert_manager.h"
#include "ui/ui_styles.h"
#include "ui/ui_widgets.h"
#include "ui/screen_history.h"
#include "drivers/usb_msc.h"

static const char *TAG = "main";

// ============== HELPER FUNCTIONS ==============

static const char* map_format_name(const char *raw_map) {
    if (!raw_map || raw_map[0] == '\0') return "";

    // Common DayZ maps
    if (strcasecmp(raw_map, "chernarusplus") == 0) return "Chernarus";
    if (strcasecmp(raw_map, "enoch") == 0) return "Livonia";
    if (strcasecmp(raw_map, "sakhal") == 0) return "Sakhal";
    if (strcasecmp(raw_map, "deerisle") == 0) return "Deer Isle";
    if (strcasecmp(raw_map, "namalsk") == 0) return "Namalsk";
    if (strcasecmp(raw_map, "esseker") == 0) return "Esseker";
    if (strcasecmp(raw_map, "takistan") == 0) return "Takistan";
    if (strcasecmp(raw_map, "chiemsee") == 0) return "Chiemsee";
    if (strcasecmp(raw_map, "rostow") == 0) return "Rostow";
    if (strcasecmp(raw_map, "valning") == 0) return "Valning";
    if (strcasecmp(raw_map, "banov") == 0) return "Banov";
    if (strcasecmp(raw_map, "iztek") == 0) return "Iztek";
    if (strcasecmp(raw_map, "melkart") == 0) return "Melkart";
    if (strcasecmp(raw_map, "exclusionzone") == 0) return "Exclusion Zone";
    if (strcasecmp(raw_map, "pripyat") == 0) return "Pripyat";

    // Return raw name if not recognized (capitalize first letter)
    static char formatted[32];
    strncpy(formatted, raw_map, sizeof(formatted) - 1);
    formatted[sizeof(formatted) - 1] = '\0';
    if (formatted[0] >= 'a' && formatted[0] <= 'z') {
        formatted[0] -= 32;  // Capitalize
    }
    return formatted;
}

// ============== UI WIDGET POINTERS ==============
// Screen objects
static lv_obj_t *screen_main = NULL;
static lv_obj_t *screen_settings = NULL;
static lv_obj_t *screen_wifi = NULL;
static lv_obj_t *screen_server = NULL;
static lv_obj_t *screen_add_server = NULL;
static lv_obj_t *screen_history = NULL;

// Main screen widgets
static lv_obj_t *main_card = NULL;
static lv_obj_t *lbl_wifi_icon = NULL;
static lv_obj_t *lbl_server = NULL;
static lv_obj_t *lbl_server_time = NULL;
static lv_obj_t *lbl_map_name = NULL;
static lv_obj_t *day_night_indicator = NULL;
static lv_obj_t *lbl_players = NULL;
static lv_obj_t *lbl_max = NULL;
static lv_obj_t *bar_players = NULL;
static lv_obj_t *lbl_status = NULL;
static lv_obj_t *lbl_update = NULL;
static lv_obj_t *lbl_ip = NULL;
static lv_obj_t *lbl_restart = NULL;
static lv_obj_t *lbl_main_trend = NULL;
static lv_obj_t *lbl_rank = NULL;
static lv_obj_t *lbl_sd_status = NULL;
static lv_obj_t *btn_prev_server = NULL;
static lv_obj_t *btn_next_server = NULL;

// Settings widgets
static lv_obj_t *kb = NULL;
static lv_obj_t *kb_add = NULL;
static lv_obj_t *ta_ssid = NULL;
static lv_obj_t *ta_password = NULL;
static lv_obj_t *ta_server_id = NULL;
static lv_obj_t *ta_server_name = NULL;
static lv_obj_t *slider_refresh = NULL;
static lv_obj_t *lbl_refresh_val = NULL;
static lv_obj_t *dropdown_screen_off = NULL;
static lv_obj_t *slider_alert = NULL;
static lv_obj_t *lbl_alert_val = NULL;
static lv_obj_t *sw_alerts = NULL;
static lv_obj_t *sw_restart_manual = NULL;
static lv_obj_t *roller_restart_hour = NULL;
static lv_obj_t *roller_restart_min = NULL;
static lv_obj_t *dropdown_restart_interval = NULL;
static lv_obj_t *dropdown_map = NULL;  // Map selection dropdown for Add Server
static lv_obj_t *dropdown_map_settings = NULL;  // Map selection dropdown for Server Settings

// Map options for dropdown (internal names used for storage)
static const char *map_options_display = "Chernarus\nLivonia\nSakhal\nDeer Isle\nNamalsk\nEsseker\nTakistan\nBanov\nExclusion Zone\nOther";
static const char *map_internal_names[] = {
    "chernarusplus", "enoch", "sakhal", "deerisle", "namalsk",
    "esseker", "takistan", "banov", "exclusionzone", ""
};
#define MAP_OPTIONS_COUNT (sizeof(map_internal_names) / sizeof(map_internal_names[0]))

// Get dropdown index from internal map name
static int get_map_index(const char *internal_name) {
    if (!internal_name || internal_name[0] == '\0') return MAP_OPTIONS_COUNT - 1;  // "Other"
    for (int i = 0; i < (int)(MAP_OPTIONS_COUNT - 1); i++) {
        if (strcasecmp(internal_name, map_internal_names[i]) == 0) {
            return i;
        }
    }
    return MAP_OPTIONS_COUNT - 1;  // "Other"
}

// History widgets
static lv_obj_t *chart_history = NULL;
static lv_chart_series_t *chart_series = NULL;
static lv_obj_t *lbl_history_legend = NULL;
static lv_obj_t *lbl_y_axis[5] = {NULL};  // Y-axis labels: 0, 15, 30, 45, 60
static lv_obj_t *lbl_x_axis[5] = {NULL};  // X-axis time labels

// Multi-server watch widgets
static lv_obj_t *secondary_container = NULL;
static secondary_box_widgets_t secondary_boxes[MAX_SECONDARY_SERVERS];
static lv_obj_t *add_server_boxes[MAX_SECONDARY_SERVERS];

// ============== FORWARD DECLARATIONS ==============
static void update_ui(void);
static void update_sd_status(void);
static void create_main_screen(void);
static void create_settings_screen(void);
static void create_wifi_settings_screen(void);
static void create_server_settings_screen(void);
static void create_add_server_screen(void);
static void create_history_screen(void);
static void switch_to_screen(screen_id_t screen);
static void process_events(void);
static void update_secondary_boxes(void);
static void create_secondary_boxes(void);

// ============== SERVER QUERY ==============

static void query_server_status(void) {
    app_state_t *state = app_state_get();

    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, skipping query");
        return;
    }

    if (state->settings.server_count == 0) return;

    server_config_t *srv = app_state_get_active_server();
    if (!srv) return;

    server_status_t status;
    esp_err_t err = battlemetrics_query(srv->server_id, &status);

    if (err == ESP_OK && status.players >= 0) {
        // Update runtime state
        app_state_update_player_data(status.players, status.max_players,
                                      status.server_time, status.is_daytime,
                                      status.map_name);

        // Update server config if needed
        if (status.max_players > 0) {
            srv->max_players = status.max_players;
        }
        if (strlen(status.ip_address) > 0) {
            strncpy(srv->ip_address, status.ip_address, sizeof(srv->ip_address) - 1);
            srv->port = status.port;
        }
        // Store server rank
        state->runtime.server_rank = status.rank;

        // Update timestamp
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        snprintf(state->runtime.last_update, sizeof(state->runtime.last_update),
                 "%02d:%02d:%02d CET", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        ESP_LOGI(TAG, "Players: %d/%d", status.players, status.max_players);

        // Add to history
        history_add_entry(status.players);

        // Track trend for main server
        app_state_add_main_trend_point(status.players);

        // Check for alerts
        alert_check();

        // Check for server restart
        restart_check_for_restart(srv, status.players);
    } else {
        ESP_LOGE(TAG, "Query failed: %s", battlemetrics_get_last_error());
    }
}


// ============== UI CALLBACKS ==============

static void on_refresh_clicked(lv_event_t *e) {
    (void)e;
    app_state_request_refresh();
}

static void on_card_clicked(lv_event_t *e) {
    (void)e;
    app_state_request_refresh();
}

static void on_settings_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_SETTINGS);
}

static void on_history_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_HISTORY);
}

/**
 * Safely set screensaver state with mutex protection
 * This ensures hardware and software state stay synchronized
 * @param active true to turn screen off, false to turn on
 * @return true if state changed successfully, false on failure
 */
static bool screensaver_set_active(bool active) {
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

// Global touch handler for screensaver wake (LVGL events)
// NOTE: Activity time is tracked in main loop polling for consistency
static void on_screen_touch_pressed(lv_event_t *e) {
    (void)e;
    app_state_t *state = app_state_get();

    ESP_LOGW(TAG, "LVGL touch event fired (ss_active=%d)", state->ui.screensaver_active);

    // Wake from screensaver if active (use helper for safe state change)
    if (state->ui.screensaver_active) {
        ESP_LOGI(TAG, "LVGL callback waking from screensaver");
        screensaver_set_active(false);
        state->ui.long_press_tracking = false;  // Don't start long-press tracking when waking
    } else {
        // Start tracking for potential long-press screen-off
        state->ui.long_press_start_time = esp_timer_get_time() / 1000;
        state->ui.long_press_tracking = true;
    }
}

// Touch release handler - stop long-press tracking
static void on_screen_touch_released(lv_event_t *e) {
    (void)e;
    app_state_t *state = app_state_get();
    state->ui.long_press_tracking = false;
}

static void on_back_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_MAIN);
}

static void on_prev_server_clicked(lv_event_t *e) {
    (void)e;
    events_post_simple(EVT_SERVER_PREV);
}

static void on_next_server_clicked(lv_event_t *e) {
    (void)e;
    events_post_simple(EVT_SERVER_NEXT);
}

static void on_wifi_settings_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_WIFI_SETTINGS);
}

static void on_server_settings_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_SERVER_SETTINGS);
}

static void on_add_server_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_ADD_SERVER);
}

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

// Screen off timeout values in seconds: Off, 5m, 10m, 15m, 30m, 60m, 90m, 120m, 240m
static const uint16_t screen_off_values[] = {0, 300, 600, 900, 1800, 3600, 5400, 7200, 14400};
static const int screen_off_count = sizeof(screen_off_values) / sizeof(screen_off_values[0]);

static void on_screen_off_changed(lv_event_t *e) {
    lv_obj_t *dropdown = lv_event_get_target(e);
    app_state_t *state = app_state_get();

    int idx = lv_dropdown_get_selected(dropdown);
    if (idx >= 0 && idx < screen_off_count) {
        state->settings.screensaver_timeout_sec = screen_off_values[idx];
        // Reset activity timer so new timeout starts from now
        state->ui.last_activity_time = esp_timer_get_time() / 1000;
        ESP_LOGI(TAG, "Screen off timeout set to %d sec, activity timer reset", state->settings.screensaver_timeout_sec);
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

static void on_alerts_switch_changed(lv_event_t *e) {
    server_config_t *srv = app_state_get_active_server();
    if (!srv) return;

    lv_obj_t *sw = lv_event_get_target(e);
    srv->alerts_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    settings_save();
}

static void on_restart_hour_changed(lv_event_t *e) {
    server_config_t *srv = app_state_get_active_server();
    if (!srv) return;

    lv_obj_t *roller = lv_event_get_target(e);
    srv->restart_hour = lv_roller_get_selected(roller);
    settings_save();
}

static void on_restart_min_changed(lv_event_t *e) {
    server_config_t *srv = app_state_get_active_server();
    if (!srv) return;

    lv_obj_t *roller = lv_event_get_target(e);
    srv->restart_minute = lv_roller_get_selected(roller) * 5;
    settings_save();
}

static void on_restart_interval_changed(lv_event_t *e) {
    server_config_t *srv = app_state_get_active_server();
    if (!srv) return;

    lv_obj_t *dd = lv_event_get_target(e);
    int sel = lv_dropdown_get_selected(dd);
    const uint8_t intervals[] = {4, 6, 8, 12};
    srv->restart_interval_hours = intervals[sel];
    settings_save();
}

static void on_restart_manual_switch_changed(lv_event_t *e) {
    server_config_t *srv = app_state_get_active_server();
    if (!srv) return;

    lv_obj_t *sw = lv_event_get_target(e);
    srv->manual_restart_set = lv_obj_has_state(sw, LV_STATE_CHECKED);
    settings_save();
}

static void on_server_save_clicked(lv_event_t *e) {
    (void)e;
    if (lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) {
        // Get selected map from dropdown
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

static void on_delete_server_clicked(lv_event_t *e) {
    (void)e;
    app_state_t *state = app_state_get();
    app_event_t evt = {
        .type = EVT_SERVER_DELETE,
        .data.server_index = state->settings.active_server_index
    };
    events_post(&evt);
}

static void on_map_settings_changed(lv_event_t *e) {
    (void)e;
    if (!dropdown_map_settings) return;

    uint16_t map_idx = lv_dropdown_get_selected(dropdown_map_settings);

    if (map_idx < sizeof(map_internal_names) / sizeof(map_internal_names[0])) {
        if (app_state_lock(100)) {
            server_config_t *srv = app_state_get_active_server();
            if (srv) {
                strncpy(srv->map_name, map_internal_names[map_idx],
                        sizeof(srv->map_name) - 1);
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

static void on_secondary_box_clicked(lv_event_t *e) {
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    events_post_secondary_click(slot);
}

static void on_add_server_from_main_clicked(lv_event_t *e) {
    (void)e;
    events_post_screen_change(SCREEN_ADD_SERVER);
}

// ============== UI SCREENS ==============

static void create_main_screen(void) {
    screen_main = ui_create_screen();

    // Add global touch handlers for screensaver and long-press screen-off
    lv_obj_add_event_cb(screen_main, on_screen_touch_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen_main, on_screen_touch_released, LV_EVENT_RELEASED, NULL);

    // Main card - compacted height for multi-server view
    main_card = ui_create_card(screen_main, 760, MAIN_CARD_HEIGHT_COMPACT);
    lv_obj_align(main_card, LV_ALIGN_TOP_MID, 0, 65);
    lv_obj_add_flag(main_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(main_card, on_card_clicked, LV_EVENT_CLICKED, NULL);

    // Settings button
    ui_create_icon_button(screen_main, LV_SYMBOL_SETTINGS, 20, 10, on_settings_clicked);

    // History button
    ui_create_icon_button(screen_main, LV_SYMBOL_IMAGE, 80, 10, on_history_clicked);

    // WiFi status icon (clickable - opens WiFi settings)
    lv_obj_t *wifi_btn = lv_btn_create(screen_main);
    lv_obj_set_size(wifi_btn, 50, 50);
    lv_obj_align(wifi_btn, LV_ALIGN_TOP_LEFT, 140, 10);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(wifi_btn, 0, 0);
    lv_obj_set_style_border_width(wifi_btn, 0, 0);
    lv_obj_add_event_cb(wifi_btn, on_wifi_settings_clicked, LV_EVENT_CLICKED, NULL);

    lbl_wifi_icon = lv_label_create(wifi_btn);
    lv_label_set_text(lbl_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(lbl_wifi_icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_wifi_icon, COLOR_TEXT_MUTED, 0);
    lv_obj_center(lbl_wifi_icon);

    // Server navigation
    btn_prev_server = ui_create_icon_button(screen_main, LV_SYMBOL_LEFT,
                                             LCD_WIDTH/2 - 125, 10, on_prev_server_clicked);
    btn_next_server = ui_create_icon_button(screen_main, LV_SYMBOL_RIGHT,
                                             LCD_WIDTH/2 + 75, 10, on_next_server_clicked);

    // SD card status indicator
    lbl_sd_status = lv_label_create(screen_main);
    lv_label_set_text(lbl_sd_status, "SD: --");
    lv_obj_set_style_text_font(lbl_sd_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_sd_status, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_sd_status, LV_ALIGN_TOP_RIGHT, -140, 25);

    // Refresh button
    lv_obj_t *btn_refresh = lv_btn_create(screen_main);
    lv_obj_set_size(btn_refresh, 110, 50);
    lv_obj_align(btn_refresh, LV_ALIGN_TOP_RIGHT, -20, 10);
    lv_obj_set_style_bg_color(btn_refresh, COLOR_BUTTON_PRIMARY, 0);
    lv_obj_set_style_radius(btn_refresh, UI_BUTTON_RADIUS, 0);
    lv_obj_add_event_cb(btn_refresh, on_refresh_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_refresh = lv_label_create(btn_refresh);
    lv_label_set_text(lbl_refresh, LV_SYMBOL_REFRESH " Refresh");
    lv_obj_set_style_text_font(lbl_refresh, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_refresh);

    // Server name + time row
    lv_obj_t *server_row = ui_create_row(main_card, 700, 30);
    lv_obj_align(server_row, LV_ALIGN_TOP_MID, 0, 0);

    lbl_server = lv_label_create(server_row);
    lv_label_set_text(lbl_server, "Loading...");
    lv_obj_set_style_text_font(lbl_server, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_server, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_width(lbl_server, 450);
    lv_label_set_long_mode(lbl_server, LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_server, LV_ALIGN_LEFT_MID, 0, -8);

    // Server rank below server name
    lbl_rank = lv_label_create(server_row);
    lv_label_set_text(lbl_rank, "");
    lv_obj_set_style_text_font(lbl_rank, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_rank, COLOR_INFO, 0);
    lv_obj_align(lbl_rank, LV_ALIGN_LEFT_MID, 0, 10);

    // Day/night + time on the right
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

    // Players container with trend
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

    // Trend indicator (2h trend)
    lbl_main_trend = lv_label_create(players_cont);
    lv_label_set_text(lbl_main_trend, "");
    lv_obj_set_style_text_font(lbl_main_trend, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_main_trend, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_main_trend, LV_ALIGN_RIGHT_MID, 0, 0);

    // Progress bar
    bar_players = lv_bar_create(main_card);
    lv_obj_set_size(bar_players, 680, 25);
    lv_obj_align(bar_players, LV_ALIGN_TOP_MID, 0, 120);
    lv_bar_set_range(bar_players, 0, 60);
    lv_bar_set_value(bar_players, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_players, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(bar_players, COLOR_SUCCESS, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_players, 8, 0);
    lv_obj_set_style_radius(bar_players, 8, LV_PART_INDICATOR);

    // Restart countdown + status row
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

    // Remove IP label from compact view (available in settings)
    lbl_ip = lv_label_create(main_card);
    lv_label_set_text(lbl_ip, "");
    lv_obj_add_flag(lbl_ip, LV_OBJ_FLAG_HIDDEN);

    // Map name (on main_card directly, below day/night indicator area)
    lbl_map_name = lv_label_create(main_card);
    lv_label_set_text(lbl_map_name, "");
    lv_obj_set_style_text_font(lbl_map_name, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_map_name, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_map_name, LV_ALIGN_TOP_RIGHT, -10, 28);
    lv_obj_move_foreground(lbl_map_name);  // Ensure it's on top

    // Create secondary server container
    create_secondary_boxes();
}

static void create_settings_screen(void) {
    app_state_t *state = app_state_get();

    screen_settings = ui_create_screen();
    lv_obj_add_event_cb(screen_settings, on_screen_touch_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen_settings, on_screen_touch_released, LV_EVENT_RELEASED, NULL);
    ui_create_back_button(screen_settings, on_back_clicked);
    ui_create_title(screen_settings, "Settings");

    lv_obj_t *cont = ui_create_scroll_container(screen_settings, 700, 400);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 20);

    // WiFi button
    ui_create_menu_button(cont, "WiFi Settings", LV_SYMBOL_WIFI,
                          COLOR_BUTTON_PRIMARY, on_wifi_settings_clicked);

    // Server button
    ui_create_menu_button(cont, "Server Settings", LV_SYMBOL_LIST,
                          COLOR_SUCCESS, on_server_settings_clicked);

    // Refresh interval
    lv_obj_t *refresh_row = ui_create_row(cont, 660, 55);
    slider_refresh = ui_create_slider(refresh_row, "Refresh:",
                                       MIN_REFRESH_INTERVAL_SEC, MAX_REFRESH_INTERVAL_SEC,
                                       state->settings.refresh_interval_sec,
                                       &lbl_refresh_val, on_refresh_slider_changed);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d sec", state->settings.refresh_interval_sec);
    lv_label_set_text(lbl_refresh_val, buf);

    // Screen off timeout
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

    // Find current screen off index
    int screen_off_idx = 0;
    for (int i = 0; i < screen_off_count; i++) {
        if (screen_off_values[i] == state->settings.screensaver_timeout_sec) {
            screen_off_idx = i;
            break;
        }
    }
    lv_dropdown_set_selected(dropdown_screen_off, screen_off_idx);
    lv_obj_add_event_cb(dropdown_screen_off, on_screen_off_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // Alerts enabled
    lv_obj_t *alerts_row = ui_create_row(cont, 660, 45);
    server_config_t *srv = app_state_get_active_server();
    bool alerts_on = srv ? srv->alerts_enabled : false;
    sw_alerts = ui_create_switch(alerts_row, "Alerts Enabled:", alerts_on,
                                  on_alerts_switch_changed);

    // Alert threshold
    lv_obj_t *threshold_row = ui_create_row(cont, 660, 55);
    int slider_max = (srv && srv->max_players > 0) ? srv->max_players : DEFAULT_MAX_PLAYERS;
    int threshold = srv ? srv->alert_threshold : (slider_max / 2);
    if (threshold > slider_max) threshold = slider_max;

    slider_alert = ui_create_slider(threshold_row, "Threshold:",
                                     1, slider_max, threshold,
                                     &lbl_alert_val, on_alert_slider_changed);
    snprintf(buf, sizeof(buf), "%d players", threshold);
    lv_label_set_text(lbl_alert_val, buf);

    // Restart schedule section
    ui_create_section_header(cont, "Restart Schedule (CET)", lv_color_hex(0xFF9800));

    // Manual schedule switch
    lv_obj_t *restart_enable_row = ui_create_row(cont, 660, 45);
    bool manual_on = srv ? srv->manual_restart_set : false;
    sw_restart_manual = ui_create_switch(restart_enable_row, "Manual Schedule:",
                                          manual_on, on_restart_manual_switch_changed);

    // Restart time row
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
    lv_obj_add_event_cb(roller_restart_hour, on_restart_hour_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_colon = lv_label_create(restart_time_row);
    lv_label_set_text(lbl_colon, ":");
    lv_obj_set_style_text_font(lbl_colon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_colon, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(lbl_colon, LV_ALIGN_CENTER, 25, 0);

    roller_restart_min = lv_roller_create(restart_time_row);
    lv_roller_set_options(roller_restart_min,
        "00\n05\n10\n15\n20\n25\n30\n35\n40\n45\n50\n55",
        LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_restart_min, 2);
    lv_obj_set_size(roller_restart_min, 60, 50);
    lv_obj_align(roller_restart_min, LV_ALIGN_CENTER, 70, 0);
    uint8_t srv_min = srv ? srv->restart_minute : 0;
    lv_roller_set_selected(roller_restart_min, srv_min / 5, LV_ANIM_OFF);
    lv_obj_add_event_cb(roller_restart_min, on_restart_min_changed, LV_EVENT_VALUE_CHANGED, NULL);

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
    lv_obj_add_event_cb(dropdown_restart_interval, on_restart_interval_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);
}

static void create_wifi_settings_screen(void) {
    app_state_t *state = app_state_get();

    screen_wifi = ui_create_screen();
    lv_obj_add_event_cb(screen_wifi, on_screen_touch_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen_wifi, on_screen_touch_released, LV_EVENT_RELEASED, NULL);
    ui_create_back_button(screen_wifi, on_back_clicked);
    ui_create_title(screen_wifi, "WiFi Settings");

    // Diagnostics panel on the right side
    lv_obj_t *diag_panel = lv_obj_create(screen_wifi);
    lv_obj_set_size(diag_panel, 280, 180);
    lv_obj_align(diag_panel, LV_ALIGN_TOP_RIGHT, -20, 60);
    lv_obj_set_style_bg_color(diag_panel, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_color(diag_panel, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(diag_panel, 1, 0);
    lv_obj_set_style_radius(diag_panel, 10, 0);
    lv_obj_set_style_pad_all(diag_panel, 15, 0);
    lv_obj_clear_flag(diag_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Diagnostics title
    lv_obj_t *diag_title = lv_label_create(diag_panel);
    lv_label_set_text(diag_title, "Connection Status");
    lv_obj_set_style_text_font(diag_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(diag_title, COLOR_INFO, 0);
    lv_obj_align(diag_title, LV_ALIGN_TOP_LEFT, 0, 0);

    // Status
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

    // Connected SSID
    char ssid_buf[64];
    wifi_manager_get_ssid(ssid_buf, sizeof(ssid_buf));
    lv_obj_t *lbl_ssid_info = lv_label_create(diag_panel);
    char ssid_line[80];
    snprintf(ssid_line, sizeof(ssid_line), "SSID: %s", ssid_buf);
    lv_label_set_text(lbl_ssid_info, ssid_line);
    lv_obj_set_style_text_font(lbl_ssid_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_ssid_info, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_ssid_info, LV_ALIGN_TOP_LEFT, 0, 44);

    // Signal strength
    int rssi = wifi_manager_get_rssi();
    lv_obj_t *lbl_rssi = lv_label_create(diag_panel);
    char rssi_line[32];
    if (rssi != 0) {
        const char *strength;
        if (rssi >= -50) strength = "Excellent";
        else if (rssi >= -60) strength = "Good";
        else if (rssi >= -70) strength = "Fair";
        else strength = "Weak";
        snprintf(rssi_line, sizeof(rssi_line), "Signal: %d dBm (%s)", rssi, strength);
    } else {
        snprintf(rssi_line, sizeof(rssi_line), "Signal: --");
    }
    lv_label_set_text(lbl_rssi, rssi_line);
    lv_obj_set_style_text_font(lbl_rssi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_rssi, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_rssi, LV_ALIGN_TOP_LEFT, 0, 62);

    // IP Address
    char ip_buf[32];
    wifi_manager_get_ip_str(ip_buf, sizeof(ip_buf));
    lv_obj_t *lbl_ip_info = lv_label_create(diag_panel);
    char ip_line[48];
    snprintf(ip_line, sizeof(ip_line), "IP: %s", ip_buf);
    lv_label_set_text(lbl_ip_info, ip_line);
    lv_obj_set_style_text_font(lbl_ip_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_ip_info, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_ip_info, LV_ALIGN_TOP_LEFT, 0, 80);

    // MAC Address
    char mac_buf[20];
    wifi_manager_get_mac_str(mac_buf, sizeof(mac_buf));
    lv_obj_t *lbl_mac = lv_label_create(diag_panel);
    char mac_line[40];
    snprintf(mac_line, sizeof(mac_line), "MAC: %s", mac_buf);
    lv_label_set_text(lbl_mac, mac_line);
    lv_obj_set_style_text_font(lbl_mac, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_mac, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_mac, LV_ALIGN_TOP_LEFT, 0, 98);

    // Time sync status
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

    // Input fields on the left
    ta_ssid = ui_create_text_input(screen_wifi, "WiFi Network (SSID):",
                                    "Enter SSID", state->settings.wifi_ssid,
                                    30, 70, 350, false, on_textarea_clicked);

    ta_password = ui_create_text_input(screen_wifi, "Password:",
                                        "Enter password", state->settings.wifi_password,
                                        30, 150, 350, true, on_textarea_clicked);

    lv_obj_t *btn_save = ui_create_button(screen_wifi, "Save", LV_SYMBOL_OK,
                                           100, 40, COLOR_DAYZ_GREEN, on_wifi_save_clicked);
    lv_obj_align(btn_save, LV_ALIGN_TOP_LEFT, 120, 10);

    kb = ui_create_keyboard(screen_wifi, ta_ssid);
    lv_obj_add_event_cb(kb, on_keyboard_event, LV_EVENT_ALL, NULL);
}

static void create_server_settings_screen(void) {
    app_state_t *state = app_state_get();

    screen_server = ui_create_screen();
    lv_obj_add_event_cb(screen_server, on_screen_touch_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen_server, on_screen_touch_released, LV_EVENT_RELEASED, NULL);
    ui_create_back_button(screen_server, on_back_clicked);
    ui_create_title(screen_server, "Server Settings");

    lv_obj_t *list = ui_create_scroll_container(screen_server, 700, 250);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 50);

    for (int i = 0; i < state->settings.server_count; i++) {
        lv_obj_t *item = lv_obj_create(list);
        lv_obj_set_size(item, 660, 50);
        lv_obj_set_style_bg_color(item,
            (i == state->settings.active_server_index) ? COLOR_BUTTON_PRIMARY : lv_color_hex(0x333333), 0);
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

    // Map selection for active server
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
    // Set current map
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

    lv_obj_t *btn_add = ui_create_button(screen_server, "Add Server", LV_SYMBOL_PLUS,
                                          200, 50, COLOR_DAYZ_GREEN, on_add_server_clicked);
    lv_obj_align(btn_add, LV_ALIGN_BOTTOM_LEFT, 50, -20);

    if (state->settings.server_count > 1) {
        lv_obj_t *btn_del = ui_create_button(screen_server, "Delete Active", LV_SYMBOL_TRASH,
                                              200, 50, COLOR_ALERT_RED, on_delete_server_clicked);
        lv_obj_align(btn_del, LV_ALIGN_BOTTOM_RIGHT, -50, -20);
    }
}

static void create_add_server_screen(void) {
    screen_add_server = ui_create_screen();
    lv_obj_add_event_cb(screen_add_server, on_screen_touch_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen_add_server, on_screen_touch_released, LV_EVENT_RELEASED, NULL);
    ui_create_back_button(screen_add_server, on_back_clicked);
    ui_create_title(screen_add_server, "Add Server");

    ta_server_id = ui_create_text_input(screen_add_server, "BattleMetrics Server ID:",
                                         "e.g., 29986583", "",
                                         50, 70, 400, false, on_add_server_textarea_clicked);

    ta_server_name = ui_create_text_input(screen_add_server, "Display Name:",
                                           "e.g., My DayZ Server", "",
                                           50, 150, 400, false, on_add_server_textarea_clicked);

    // Map selection dropdown
    lv_obj_t *lbl_map = lv_label_create(screen_add_server);
    lv_label_set_text(lbl_map, "Map:");
    lv_obj_set_style_text_font(lbl_map, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_map, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_pos(lbl_map, 50, 230);

    dropdown_map = lv_dropdown_create(screen_add_server);
    lv_dropdown_set_options(dropdown_map, map_options_display);
    lv_dropdown_set_selected(dropdown_map, 0);  // Default to Chernarus
    lv_obj_set_size(dropdown_map, 200, 40);
    lv_obj_set_pos(dropdown_map, 100, 225);
    lv_obj_set_style_bg_color(dropdown_map, COLOR_CARD_BG, 0);
    lv_obj_set_style_text_color(dropdown_map, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(dropdown_map, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(dropdown_map, 1, 0);
    lv_obj_set_style_radius(dropdown_map, 8, 0);
    // Style the dropdown list
    lv_obj_set_style_bg_color(dropdown_map, COLOR_CARD_BG, LV_PART_ITEMS);
    lv_obj_set_style_text_color(dropdown_map, COLOR_TEXT_PRIMARY, LV_PART_ITEMS);

    lv_obj_t *btn_save = ui_create_button(screen_add_server, "Add", LV_SYMBOL_OK,
                                           150, 50, COLOR_DAYZ_GREEN, on_server_save_clicked);
    lv_obj_align(btn_save, LV_ALIGN_TOP_RIGHT, -50, 225);

    kb_add = ui_create_keyboard(screen_add_server, ta_server_id);
}

static void create_history_screen(void) {
    app_state_t *state = app_state_get();

    screen_history = ui_create_screen();
    lv_obj_add_event_cb(screen_history, on_screen_touch_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen_history, on_screen_touch_released, LV_EVENT_RELEASED, NULL);
    ui_create_back_button(screen_history, on_back_clicked);
    ui_create_title(screen_history, "Player History");

    // Time range buttons
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
        lv_obj_set_style_bg_color(btn,
            (i == state->ui.current_history_range) ? COLOR_DAYZ_GREEN : COLOR_CARD_BG, 0);
        lv_obj_add_event_cb(btn, on_history_range_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)ranges[i]);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, btn_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);
    }

    // Chart - offset to make room for Y-axis labels on left
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
    lv_chart_set_range(chart_history, LV_CHART_AXIS_PRIMARY_Y, 0, 60);  // Initial range, updated dynamically
    lv_chart_set_div_line_count(chart_history, 4, 4);  // 4 divisions = 5 lines

    chart_series = lv_chart_add_series(chart_history, COLOR_DAYZ_GREEN, LV_CHART_AXIS_PRIMARY_Y);

    // Y-axis labels (will be updated dynamically by refresh_history_chart)
    // Chart is 680x280 centered at (420, 260), so top=120, bottom=400
    lv_coord_t chart_top = 120;
    lv_coord_t chart_plot_height = 270;  // Approximate plot area height
    for (int i = 0; i < 5; i++) {
        lbl_y_axis[i] = lv_label_create(screen_history);
        lv_label_set_text(lbl_y_axis[i], "--");  // Will be set by refresh_history_chart
        lv_obj_set_style_text_font(lbl_y_axis[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_y_axis[i], COLOR_TEXT_MUTED, 0);
        // Position from top to bottom along left side of chart
        lv_obj_set_pos(lbl_y_axis[i], 50, chart_top + (i * chart_plot_height / 4) - 7);
    }

    // X-axis time labels (will be updated by refresh_history_chart)
    // Chart left edge = 80, plot area with padding starts at ~90
    lv_coord_t chart_left = 90;
    lv_coord_t chart_width = 660;  // Plot area width
    lv_coord_t x_label_y = 410;  // Below chart bottom (400) with margin
    for (int i = 0; i < 5; i++) {
        lbl_x_axis[i] = lv_label_create(screen_history);
        lv_label_set_text(lbl_x_axis[i], "--:--");
        lv_obj_set_style_text_font(lbl_x_axis[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_x_axis[i], COLOR_TEXT_MUTED, 0);
        // Position from left to right along bottom
        lv_coord_t x_pos = chart_left + (i * chart_width / 4) - 18;
        lv_obj_set_pos(lbl_x_axis[i], x_pos, x_label_y);
    }

    // Legend
    lbl_history_legend = lv_label_create(screen_history);
    lv_obj_set_style_text_font(lbl_history_legend, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_history_legend, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_history_legend, LV_ALIGN_BOTTOM_MID, 0, -5);

    // Initialize history screen module with widget references
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

// ============== SECONDARY SERVER WATCH ==============

static void create_secondary_boxes(void) {
    app_state_t *state = app_state_get();

    // Container positioned with same gap as between boxes (8px below main card)
    // Main card: y=65, height=220, bottom=285. Gap=8px. Container top=293
    secondary_container = lv_obj_create(screen_main);
    lv_obj_set_size(secondary_container, 760, SECONDARY_CONTAINER_HEIGHT);
    lv_obj_align(secondary_container, LV_ALIGN_TOP_MID, 0, 65 + MAIN_CARD_HEIGHT_COMPACT + SECONDARY_BOX_GAP);
    lv_obj_set_style_bg_opa(secondary_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(secondary_container, 0, 0);
    lv_obj_set_style_pad_all(secondary_container, 0, 0);
    lv_obj_clear_flag(secondary_container, LV_OBJ_FLAG_SCROLLABLE);

    // Layout: 3 boxes centered with gaps
    int total_width = (SECONDARY_BOX_WIDTH * 3) + (SECONDARY_BOX_GAP * 2);
    int start_x = (760 - total_width) / 2;

    // Initialize secondary indices
    app_state_update_secondary_indices();

    for (int i = 0; i < MAX_SECONDARY_SERVERS; i++) {
        int x_pos = start_x + (i * (SECONDARY_BOX_WIDTH + SECONDARY_BOX_GAP));

        // Create actual server box or add server placeholder
        if (i < state->runtime.secondary_count) {
            // Real secondary server box
            secondary_boxes[i] = ui_create_secondary_box(
                secondary_container,
                SECONDARY_BOX_WIDTH,
                SECONDARY_BOX_HEIGHT,
                on_secondary_box_clicked,
                (void*)(intptr_t)i
            );
            lv_obj_set_pos(secondary_boxes[i].container, x_pos, 2);
            add_server_boxes[i] = NULL;
        } else if (state->settings.server_count < MAX_SERVERS) {
            // Add server placeholder
            add_server_boxes[i] = ui_create_add_server_box(
                secondary_container,
                SECONDARY_BOX_WIDTH,
                SECONDARY_BOX_HEIGHT,
                on_add_server_from_main_clicked
            );
            lv_obj_set_pos(add_server_boxes[i], x_pos, 2);
            memset(&secondary_boxes[i], 0, sizeof(secondary_box_widgets_t));
        } else {
            // Max servers reached, hide this slot
            add_server_boxes[i] = NULL;
            memset(&secondary_boxes[i], 0, sizeof(secondary_box_widgets_t));
        }
    }
}

static void update_secondary_boxes(void) {
    app_state_t *state = app_state_get();

    if (!secondary_container) return;
    if (!lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) return;

    // Re-check secondary indices in case server list changed
    app_state_update_secondary_indices();

    for (int slot = 0; slot < MAX_SECONDARY_SERVERS; slot++) {
        if (slot < state->runtime.secondary_count && secondary_boxes[slot].container) {
            // Get the actual server index for this slot
            uint8_t srv_idx = state->runtime.secondary_server_indices[slot];
            server_config_t *srv = &state->settings.servers[srv_idx];
            secondary_server_status_t *status = &state->runtime.secondary[slot];

            // Calculate trend
            int trend = app_state_calculate_trend(slot);

            // Determine map to display - prefer stored config, fallback to API
            const char *map_to_show = "";
            if (strlen(srv->map_name) > 0) {
                map_to_show = srv->map_name;
            } else if (strlen(status->map_name) > 0) {
                map_to_show = status->map_name;
            }

            // Update the box
            ui_update_secondary_box(
                &secondary_boxes[slot],
                srv->display_name,
                status->player_count,
                status->max_players > 0 ? status->max_players : srv->max_players,
                map_format_name(map_to_show),
                status->server_time,
                status->is_daytime,
                trend,
                status->valid
            );

            // Make sure the box is visible
            lv_obj_clear_flag(secondary_boxes[slot].container, LV_OBJ_FLAG_HIDDEN);

            // Hide add server box if it exists
            if (add_server_boxes[slot]) {
                lv_obj_add_flag(add_server_boxes[slot], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            // Hide secondary box if no server in this slot
            if (secondary_boxes[slot].container) {
                lv_obj_add_flag(secondary_boxes[slot].container, LV_OBJ_FLAG_HIDDEN);
            }

            // Show add server box if we haven't reached max
            if (add_server_boxes[slot] && state->settings.server_count < MAX_SERVERS) {
                lv_obj_clear_flag(add_server_boxes[slot], LV_OBJ_FLAG_HIDDEN);
            } else if (add_server_boxes[slot]) {
                lv_obj_add_flag(add_server_boxes[slot], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    lvgl_port_unlock();
}

// ============== SCREEN NAVIGATION ==============

static void switch_to_screen(screen_id_t screen) {
    if (!lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) return;

    // Delete old screens to free memory (except main)
    if (screen != SCREEN_SETTINGS && screen_settings) {
        lv_obj_delete(screen_settings);
        screen_settings = NULL;
    }
    if (screen != SCREEN_WIFI_SETTINGS && screen_wifi) {
        lv_obj_delete(screen_wifi);
        screen_wifi = NULL;
        kb = NULL;
    }
    if (screen != SCREEN_SERVER_SETTINGS && screen_server) {
        lv_obj_delete(screen_server);
        screen_server = NULL;
    }
    if (screen != SCREEN_ADD_SERVER && screen_add_server) {
        lv_obj_delete(screen_add_server);
        screen_add_server = NULL;
        kb_add = NULL;
    }
    if (screen != SCREEN_HISTORY && screen_history) {
        lv_obj_delete(screen_history);
        screen_history = NULL;
        chart_history = NULL;
        chart_series = NULL;
        lbl_history_legend = NULL;
        for (int i = 0; i < 5; i++) {
            lbl_y_axis[i] = NULL;
            lbl_x_axis[i] = NULL;
        }
    }

    app_state_set_current_screen(screen);

    switch (screen) {
        case SCREEN_MAIN:
            if (!screen_main) create_main_screen();
            lv_screen_load(screen_main);
            update_ui();
            break;
        case SCREEN_SETTINGS:
            create_settings_screen();
            lv_screen_load(screen_settings);
            break;
        case SCREEN_WIFI_SETTINGS:
            create_wifi_settings_screen();
            lv_screen_load(screen_wifi);
            break;
        case SCREEN_SERVER_SETTINGS:
            create_server_settings_screen();
            lv_screen_load(screen_server);
            break;
        case SCREEN_ADD_SERVER:
            create_add_server_screen();
            lv_screen_load(screen_add_server);
            break;
        case SCREEN_HISTORY:
            create_history_screen();
            lv_screen_load(screen_history);
            break;
        default:
            break;
    }

    lvgl_port_unlock();
}

// ============== UPDATE UI ==============

static void update_ui(void) {
    app_state_t *state = app_state_get();

    if (app_state_get_current_screen() != SCREEN_MAIN) return;
    if (!lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) return;

    server_config_t *srv = app_state_get_active_server();

    // Server name
    if (srv) {
        lv_label_set_text(lbl_server, srv->display_name);

        if (strlen(srv->ip_address) > 0) {
            char ip_buf[48];
            snprintf(ip_buf, sizeof(ip_buf), "%s:%d", srv->ip_address, srv->port);
            lv_label_set_text(lbl_ip, ip_buf);
        } else {
            lv_label_set_text(lbl_ip, "");
        }
    }

    // Server rank
    if (state->runtime.server_rank > 0) {
        char rank_buf[24];
        snprintf(rank_buf, sizeof(rank_buf), "Rank #%d", state->runtime.server_rank);
        lv_label_set_text(lbl_rank, rank_buf);
    } else {
        lv_label_set_text(lbl_rank, "");
    }

    // Server time
    if (strlen(state->runtime.server_time) > 0) {
        char time_buf[32];
        snprintf(time_buf, sizeof(time_buf), "%s %s",
                 state->runtime.server_time,
                 state->runtime.is_daytime ? "DAY" : "NIGHT");
        lv_label_set_text(lbl_server_time, time_buf);

        lv_obj_clear_flag(day_night_indicator, LV_OBJ_FLAG_HIDDEN);
        if (state->runtime.is_daytime) {
            lv_obj_set_style_bg_color(day_night_indicator, COLOR_DAY_SUN, 0);
            lv_obj_set_style_text_color(lbl_server_time, COLOR_DAY_TEXT, 0);
        } else {
            lv_obj_set_style_bg_color(day_night_indicator, COLOR_NIGHT_MOON, 0);
            lv_obj_set_style_text_color(lbl_server_time, COLOR_NIGHT_TEXT, 0);
        }
    } else {
        lv_label_set_text(lbl_server_time, "");
        lv_obj_add_flag(day_night_indicator, LV_OBJ_FLAG_HIDDEN);
    }

    // Map name - prefer stored map from config, fallback to API if available
    server_config_t *active_srv = app_state_get_active_server();
    const char *map_to_display = NULL;
    if (active_srv && strlen(active_srv->map_name) > 0) {
        map_to_display = active_srv->map_name;
    } else if (strlen(state->runtime.map_name) > 0) {
        map_to_display = state->runtime.map_name;
    }
    if (map_to_display) {
        lv_label_set_text(lbl_map_name, map_format_name(map_to_display));
    } else {
        lv_label_set_text(lbl_map_name, "");
    }

    // Player count
    if (state->runtime.current_players >= 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", state->runtime.current_players);
        lv_label_set_text(lbl_players, buf);

        snprintf(buf, sizeof(buf), "/%d", state->runtime.max_players);
        lv_label_set_text(lbl_max, buf);

        lv_bar_set_range(bar_players, 0, state->runtime.max_players);
        lv_bar_set_value(bar_players, state->runtime.current_players, LV_ANIM_ON);

        // Use absolute player count for color (red from 50+)
        lv_obj_set_style_bg_color(bar_players, ui_get_player_color(state->runtime.current_players), LV_PART_INDICATOR);

        // Main server trend (2h)
        int trend = app_state_calculate_main_trend();
        int trend_count = app_state_get_main_trend_count();
        if (trend > 0) {
            char trend_buf[16];
            snprintf(trend_buf, sizeof(trend_buf), LV_SYMBOL_UP "+%d", trend);
            lv_obj_set_style_text_color(lbl_main_trend, COLOR_DAYZ_GREEN, 0);
            lv_label_set_text(lbl_main_trend, trend_buf);
        } else if (trend < 0) {
            char trend_buf[16];
            snprintf(trend_buf, sizeof(trend_buf), LV_SYMBOL_DOWN "%d", trend);
            lv_obj_set_style_text_color(lbl_main_trend, COLOR_DANGER, 0);
            lv_label_set_text(lbl_main_trend, trend_buf);
        } else if (trend_count >= 2) {
            // Stable - show right arrow when we have enough data but no change
            lv_obj_set_style_text_color(lbl_main_trend, COLOR_TEXT_MUTED, 0);
            lv_label_set_text(lbl_main_trend, LV_SYMBOL_RIGHT " 0");
        } else {
            lv_label_set_text(lbl_main_trend, "");
        }
    } else {
        lv_label_set_text(lbl_players, "---");
        lv_label_set_text(lbl_main_trend, "");
    }

    // Status
    if (wifi_manager_is_connected()) {
        lv_label_set_text(lbl_status, "ONLINE");
        lv_obj_set_style_text_color(lbl_status, COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text(lbl_status, "OFFLINE");
        lv_obj_set_style_text_color(lbl_status, COLOR_DANGER, 0);
    }

    // WiFi icon color
    if (lbl_wifi_icon) {
        if (wifi_manager_is_connected()) {
            lv_obj_set_style_text_color(lbl_wifi_icon, COLOR_SUCCESS, 0);
        } else {
            lv_obj_set_style_text_color(lbl_wifi_icon, COLOR_DANGER, 0);
        }
    }

    // Update timestamp
    if (strcmp(state->runtime.last_update, "Never") != 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Updated: %s", state->runtime.last_update);
        lv_label_set_text(lbl_update, buf);
    }

    // Last restart time (show CET time)
    if (srv && lbl_restart) {
        int time_since = restart_get_time_since_last(srv);

        if (time_since >= 0 && wifi_manager_is_time_synced()) {
            char restart_buf[64];
            char time_str[16];
            char ago_str[32];
            restart_format_last_time(srv, time_str, sizeof(time_str));
            restart_format_time_since(time_since, ago_str, sizeof(ago_str));
            snprintf(restart_buf, sizeof(restart_buf), "Restart: %s (%s)", time_str, ago_str);
            lv_label_set_text(lbl_restart, restart_buf);
            // Color based on recency: green if recent, fades to muted
            if (time_since < 1800) {  // < 30 min
                lv_obj_set_style_text_color(lbl_restart, COLOR_SUCCESS, 0);
            } else if (time_since < 7200) {  // < 2 hours
                lv_obj_set_style_text_color(lbl_restart, COLOR_WARNING, 0);
            } else {
                lv_obj_set_style_text_color(lbl_restart, COLOR_TEXT_MUTED, 0);
            }
        } else if (time_since >= 0) {
            // Time not synced yet, just show the stored time
            char restart_buf[64];
            char time_str[16];
            restart_format_last_time(srv, time_str, sizeof(time_str));
            snprintf(restart_buf, sizeof(restart_buf), "Restart: %s", time_str);
            lv_label_set_text(lbl_restart, restart_buf);
            lv_obj_set_style_text_color(lbl_restart, COLOR_TEXT_MUTED, 0);
        } else {
            lv_label_set_text(lbl_restart, "");
        }
    }

    // Server navigation visibility
    if (state->settings.server_count <= 1) {
        lv_obj_add_flag(btn_prev_server, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btn_next_server, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(btn_prev_server, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btn_next_server, LV_OBJ_FLAG_HIDDEN);
    }

    lvgl_port_unlock();
}

static void update_sd_status(void) {
    if (!lbl_sd_status) return;
    if (!lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) return;

    int usage = sd_card_get_usage_percent();
    char buf[16];

    if (usage < 0) {
        // SD card not mounted or access failed
        lv_label_set_text(lbl_sd_status, "SD: FAIL");
        lv_obj_set_style_text_color(lbl_sd_status, COLOR_DANGER, 0);
    } else {
        snprintf(buf, sizeof(buf), "SD: %d%%", usage);
        lv_label_set_text(lbl_sd_status, buf);

        if (usage > 90) {
            lv_obj_set_style_text_color(lbl_sd_status, COLOR_WARNING, 0);
        } else if (usage > 80) {
            lv_obj_set_style_text_color(lbl_sd_status, COLOR_ALERT_ORANGE, 0);
        } else {
            lv_obj_set_style_text_color(lbl_sd_status, COLOR_TEXT_MUTED, 0);
        }
    }

    lvgl_port_unlock();
}

// ============== EVENT PROCESSING ==============

static void process_events(void) {
    app_event_t evt;
    app_state_t *state = app_state_get();

    while (events_receive(&evt)) {
        switch (evt.type) {
            case EVT_SCREEN_CHANGE:
                switch_to_screen(evt.data.screen);
                break;

            case EVT_WIFI_SAVE:
                settings_save_wifi(evt.data.wifi.ssid, evt.data.wifi.password);
                wifi_manager_reconnect(evt.data.wifi.ssid, evt.data.wifi.password);
                switch_to_screen(SCREEN_SETTINGS);
                break;

            case EVT_SERVER_ADD: {
                int new_idx = settings_add_server(evt.data.server.server_id, evt.data.server.display_name);
                // Set map_name on the newly added server
                if (new_idx >= 0 && evt.data.server.map_name[0] != '\0') {
                    if (app_state_lock(100)) {
                        strncpy(state->settings.servers[new_idx].map_name,
                                evt.data.server.map_name,
                                sizeof(state->settings.servers[new_idx].map_name) - 1);
                        app_state_unlock();
                        settings_save();  // Save with map name
                    }
                }
                state->runtime.current_players = -1;
                state->runtime.server_time[0] = '\0';
                app_state_clear_secondary_data();
                app_state_update_secondary_indices();
                secondary_fetch_refresh_now();
                app_state_request_refresh();
                switch_to_screen(SCREEN_MAIN);
                break;
            }

            case EVT_SERVER_DELETE: {
                int old_idx = state->settings.active_server_index;
                settings_delete_server(evt.data.server_index);
                int new_idx = state->settings.active_server_index;
                // Switch history if active server changed
                if (old_idx != new_idx || evt.data.server_index == old_idx) {
                    history_switch_server(-1, new_idx);  // -1 because deleted server data is gone
                }
                state->runtime.current_players = -1;
                state->runtime.server_time[0] = '\0';
                app_state_clear_secondary_data();
                app_state_update_secondary_indices();
                secondary_fetch_refresh_now();
                app_state_request_refresh();
                switch_to_screen(SCREEN_MAIN);
                break;
            }

            case EVT_SERVER_NEXT:
                if (state->settings.server_count > 1) {
                    int old_idx = state->settings.active_server_index;
                    int new_idx = (old_idx + 1) % state->settings.server_count;
                    state->settings.active_server_index = new_idx;
                    history_switch_server(old_idx, new_idx);
                    state->runtime.current_players = -1;
                    state->runtime.server_time[0] = '\0';
                    app_state_clear_main_trend();
                    app_state_clear_secondary_data();
                    app_state_update_secondary_indices();
                    secondary_fetch_refresh_now();
                    app_state_request_refresh();
                    settings_save();
                    update_secondary_boxes();
                }
                break;

            case EVT_SERVER_PREV:
                if (state->settings.server_count > 1) {
                    int old_idx = state->settings.active_server_index;
                    int new_idx;
                    if (old_idx == 0) {
                        new_idx = state->settings.server_count - 1;
                    } else {
                        new_idx = old_idx - 1;
                    }
                    state->settings.active_server_index = new_idx;
                    history_switch_server(old_idx, new_idx);
                    state->runtime.current_players = -1;
                    state->runtime.server_time[0] = '\0';
                    app_state_clear_main_trend();
                    app_state_clear_secondary_data();
                    app_state_update_secondary_indices();
                    secondary_fetch_refresh_now();
                    app_state_request_refresh();
                    settings_save();
                    update_secondary_boxes();
                }
                break;

            case EVT_REFRESH_DATA:
                app_state_request_refresh();
                break;

            case EVT_SECONDARY_SERVER_CLICKED: {
                // Swap clicked secondary server with main
                int slot = evt.data.secondary.slot;
                if (slot >= 0 && slot < state->runtime.secondary_count) {
                    int old_active = state->settings.active_server_index;
                    int new_active = state->runtime.secondary_server_indices[slot];

                    ESP_LOGI(TAG, "Swapping server: slot %d, index %d -> %d",
                             slot, old_active, new_active);

                    // Save current main server data before switching
                    secondary_server_status_t old_main_data = {
                        .player_count = state->runtime.current_players,
                        .max_players = state->runtime.max_players,
                        .is_daytime = state->runtime.is_daytime,
                        .valid = (state->runtime.current_players >= 0),
                        .fetch_pending = false,
                        .last_update_time = esp_timer_get_time() / 1000
                    };
                    strncpy(old_main_data.server_time, state->runtime.server_time,
                            sizeof(old_main_data.server_time) - 1);
                    strncpy(old_main_data.map_name, state->runtime.map_name,
                            sizeof(old_main_data.map_name) - 1);
                    memcpy(&old_main_data.trend, &state->runtime.main_trend, sizeof(trend_data_t));

                    // Transfer secondary server data to main BEFORE switching
                    secondary_server_status_t *sec = &state->runtime.secondary[slot];
                    if (sec->valid) {
                        state->runtime.current_players = sec->player_count;
                        state->runtime.max_players = sec->max_players;
                        strncpy(state->runtime.server_time, sec->server_time,
                                sizeof(state->runtime.server_time) - 1);
                        strncpy(state->runtime.map_name, sec->map_name,
                                sizeof(state->runtime.map_name) - 1);
                        state->runtime.is_daytime = sec->is_daytime;
                        memcpy(&state->runtime.main_trend, &sec->trend, sizeof(trend_data_t));

                        ESP_LOGI(TAG, "Transferred secondary->main: %d/%d players",
                                 sec->player_count, sec->max_players);
                    }

                    // Switch active server
                    state->settings.active_server_index = new_active;

                    // Switch history
                    history_switch_server(old_active, new_active);

                    // Clear secondary data and recalculate indices
                    app_state_clear_secondary_data();
                    app_state_update_secondary_indices();

                    // Find which slot the old main server is now in and restore its data
                    for (int i = 0; i < state->runtime.secondary_count; i++) {
                        if (state->runtime.secondary_server_indices[i] == old_active) {
                            memcpy(&state->runtime.secondary[i], &old_main_data,
                                   sizeof(secondary_server_status_t));
                            ESP_LOGI(TAG, "Transferred main->secondary[%d]: %d/%d players",
                                     i, old_main_data.player_count, old_main_data.max_players);
                            break;
                        }
                    }

                    // Trigger secondary fetch for other secondary servers
                    secondary_fetch_refresh_now();

                    settings_save();
                    update_ui();
                    update_secondary_boxes();
                }
                break;
            }

            case EVT_SECONDARY_DATA_UPDATED:
                // Refresh secondary boxes display
                update_secondary_boxes();
                break;

            default:
                break;
        }
    }
}

// ============== MAIN ==============

void app_main(void) {
    ESP_LOGI(TAG, "%s v%s Starting...", APP_NAME, APP_VERSION);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Check if screen is being touched during boot
    // Touch and hold screen during power-on to enter USB storage mode
    if (usb_msc_touch_detected()) {
        ESP_LOGI(TAG, "Touch detected on boot - entering USB Mass Storage mode");
        if (usb_msc_init() == ESP_OK) {
            usb_msc_task();  // This function never returns
        } else {
            ESP_LOGE(TAG, "USB MSC init failed, continuing with normal boot");
        }
    }

    // Initialize application state
    app_state_init();

    // Initialize event system
    events_init();

    // Load settings
    settings_load();

    // Initialize buzzer and test
    buzzer_init();
    buzzer_test();

    // Initialize history
    history_init();

    // Initialize display with LVGL and touch (includes I2C, CH422G, GT911 reset)
    lv_display_t *disp = display_init();
    if (!disp) {
        ESP_LOGE(TAG, "Display initialization failed!");
        return;
    }

    // TEST: Verify backlight control works - watch for screen flicker!
    ESP_LOGW(TAG, "Running backlight test - watch for screen flicker over next 4 seconds...");
    io_expander_test_backlight();

    // Initialize UI styles
    ui_styles_init();

    // Now I2C is ready, try to load history from SD for the active server
    app_state_t *init_state = app_state_get();
    int active_srv = init_state->settings.active_server_index;
    if (sd_card_init() == ESP_OK) {
        // Load JSON history (primary source with full 7-day data)
        history_load_json_for_server(active_srv);
    }
    // Fallback to NVS if no JSON data loaded
    if (history_get_count() == 0) {
        history_load_from_nvs(active_srv);
    }

    // Create main screen
    if (lvgl_port_lock(1000)) {
        create_main_screen();
        lv_screen_load(screen_main);
        lvgl_port_unlock();
    }

    // Get state reference
    app_state_t *state = app_state_get();

    // Initialize screensaver activity time
    state->ui.last_activity_time = esp_timer_get_time() / 1000;

    // First boot handling
    if (state->settings.first_boot || strlen(state->settings.wifi_ssid) == 0) {
        ESP_LOGI(TAG, "First boot detected");
        // Default credentials for backward compatibility
        strncpy(state->settings.wifi_ssid, "meshnetwork2131", sizeof(state->settings.wifi_ssid) - 1);
        state->settings.wifi_ssid[sizeof(state->settings.wifi_ssid) - 1] = '\0';
        strncpy(state->settings.wifi_password, "9696Polikut.", sizeof(state->settings.wifi_password) - 1);
        state->settings.wifi_password[sizeof(state->settings.wifi_password) - 1] = '\0';
        state->settings.first_boot = false;
        settings_save();
    }

    // Initialize BattleMetrics API client (creates mutex for thread safety)
    battlemetrics_init();

    // Initialize WiFi
    wifi_manager_init(state->settings.wifi_ssid, state->settings.wifi_password);

    // Wait for WiFi
    ESP_LOGI(TAG, "Waiting for WiFi...");
    if (wifi_manager_wait_connected(WIFI_CONNECT_TIMEOUT_MS)) {
        ESP_LOGI(TAG, "WiFi connected!");

        // Wait for SNTP time sync (important for history timestamps)
        ESP_LOGI(TAG, "Waiting for time sync...");
        int sntp_retries = 0;
        while (!wifi_manager_is_time_synced() && sntp_retries < 50) {  // Max 5 seconds
            vTaskDelay(pdMS_TO_TICKS(100));
            sntp_retries++;
        }
        if (wifi_manager_is_time_synced()) {
            time_t now;
            time(&now);
            ESP_LOGI(TAG, "Time synced: %lu", (unsigned long)now);

            // CRITICAL: Reload JSON history now that we have correct time!
            // Initial load at boot used wrong time (device time was ~1970)
            if (sd_card_is_mounted()) {
                ESP_LOGI(TAG, "Reloading JSON history with correct time...");
                history_load_json_for_server(state->settings.active_server_index);
                ESP_LOGI(TAG, "History reloaded: %d entries", history_get_count());
            }

            // Check if restart data is stale and reset if needed
            server_config_t *srv = app_state_get_active_server();
            if (srv) {
                restart_check_stale_and_reset(srv);
            }
        } else {
            ESP_LOGW(TAG, "Time sync timeout, timestamps may be wrong");
        }
    } else {
        ESP_LOGW(TAG, "WiFi connection timeout, will retry in background");
    }

    // Start secondary server fetch background task
    secondary_fetch_init();
    secondary_fetch_start();

    // Give LVGL task time to process before starting main loop
    vTaskDelay(pdMS_TO_TICKS(500));

    // Initial UI update
    update_ui();
    update_sd_status();
    update_secondary_boxes();

    // Main loop
    while (1) {
        // Yield to other tasks (especially LVGL) at start of each loop
        vTaskDelay(pdMS_TO_TICKS(10));

        // Process any pending events
        process_events();

        // Query server status (on main screen OR during screensaver for background data)
        if (app_state_get_current_screen() == SCREEN_MAIN || state->ui.screensaver_active) {
            query_server_status();
            // Yield after HTTP request to let LVGL process
            vTaskDelay(pdMS_TO_TICKS(50));
            // Only update UI if not in screensaver mode
            if (!state->ui.screensaver_active) {
                update_ui();
                update_secondary_boxes();  // Also update secondary servers display
                update_sd_status();
            }
        }

        // Wait for refresh interval
        int wait_cycles = (state->settings.refresh_interval_sec * 1000) / 100;
        for (int i = 0; i < wait_cycles; i++) {
            // Process events during wait
            process_events();

            // Check for manual refresh
            if (app_state_consume_refresh_request()) {
                ESP_LOGI(TAG, "Manual refresh triggered");
                break;
            }

            // Auto-hide alerts
            alert_check_auto_hide();

            // Screensaver timeout check (uses mutex-protected helper)
            if (!state->ui.screensaver_active && state->settings.screensaver_timeout_sec > 0) {
                int64_t now = esp_timer_get_time() / 1000;
                int64_t elapsed_ms = now - state->ui.last_activity_time;

                // Debug: log elapsed time every 30 seconds
                static int64_t last_elapsed_log = 0;
                if (now - last_elapsed_log > 30000) {
                    ESP_LOGI(TAG, "SS elapsed: %lld sec / %d sec timeout",
                             elapsed_ms / 1000, state->settings.screensaver_timeout_sec);
                    last_elapsed_log = now;
                }

                if (elapsed_ms > ((int64_t)state->settings.screensaver_timeout_sec * 1000)) {
                    ESP_LOGI(TAG, "Screensaver timeout after %lld sec of inactivity", elapsed_ms / 1000);
                    screensaver_set_active(true);
                }
            }

            // Touch handling with debouncing - single source of truth for activity tracking
            static bool wait_for_release = false;       // Wait for finger lift after long-press off
            static int64_t touch_press_start = 0;       // When touch first detected (for debounce)
            static bool touch_activity_counted = false; // Only count activity once per touch

            lv_indev_t *touch_indev = display_get_touch_indev();
            if (touch_indev) {
                if (lvgl_port_lock(10)) {
                    lv_indev_state_t touch_state = lv_indev_get_state(touch_indev);
                    lvgl_port_unlock();

                    int64_t now = esp_timer_get_time() / 1000;

                    // Debug: log raw touch state every 5 seconds
                    static int64_t last_touch_debug = 0;
                    if (now - last_touch_debug > 5000) {
                        ESP_LOGI(TAG, "Touch debug: raw=%s, ss_active=%d, timeout=%d",
                                 touch_state == LV_INDEV_STATE_PRESSED ? "PRESSED" : "released",
                                 state->ui.screensaver_active,
                                 state->settings.screensaver_timeout_sec);
                        last_touch_debug = now;
                    }

                    if (touch_state == LV_INDEV_STATE_PRESSED) {
                        // Track when touch started (for debouncing)
                        if (touch_press_start == 0) {
                            touch_press_start = now;
                            ESP_LOGW(TAG, "Touch PRESS detected (raw)");
                        }

                        // Debounce: require 50ms of continuous press to count as real touch
                        int64_t press_duration = now - touch_press_start;
                        bool is_real_touch = (press_duration >= 50);

                        // Wake from screensaver on any real touch (but not if waiting for release)
                        if (state->ui.screensaver_active && !wait_for_release && is_real_touch) {
                            ESP_LOGI(TAG, "Touch wake from screensaver");
                            screensaver_set_active(false);
                            state->ui.long_press_tracking = false;
                            touch_activity_counted = true;
                        } else if (!state->ui.screensaver_active && is_real_touch) {
                            // Reset activity timer ONCE per touch (not continuously)
                            if (!touch_activity_counted) {
                                state->ui.last_activity_time = now;
                                touch_activity_counted = true;
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
                                    wait_for_release = true;  // Don't wake until finger lifted
                                }
                            }
                        }
                    } else {
                        // Finger released - reset all tracking
                        state->ui.long_press_tracking = false;
                        wait_for_release = false;
                        touch_press_start = 0;
                        touch_activity_counted = false;
                    }
                }
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
