/**
 * DayZ Server Tracker - UI Update Functions
 * Handles updating UI elements with current state
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_lvgl_port.h"

#include "ui_update.h"
#include "ui_context.h"
#include "ui_styles.h"
#include "ui_widgets.h"
#include "screen_builder.h"
#include "config.h"
#include "app_state.h"
#include "services/wifi_manager.h"
#include "services/restart_manager.h"
#include "drivers/sd_card.h"

// ============== UI WIDGET ACCESS MACROS ==============
#define UI_CTX (ui_context_get())

// Screen objects
#define screen_main         (UI_CTX->screen_main)
#define screen_settings     (UI_CTX->screen_settings)
#define screen_wifi         (UI_CTX->screen_wifi)
#define screen_server       (UI_CTX->screen_server)
#define screen_add_server   (UI_CTX->screen_add_server)
#define screen_history      (UI_CTX->screen_history)

// Main screen widgets
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

// Settings widgets
#define kb                      (UI_CTX->kb)
#define kb_add                  (UI_CTX->kb_add)

// History widgets
#define chart_history       (UI_CTX->chart_history)
#define chart_series        (UI_CTX->chart_series)
#define lbl_history_legend  (UI_CTX->lbl_history_legend)
#define lbl_y_axis          (UI_CTX->lbl_y_axis)
#define lbl_x_axis          (UI_CTX->lbl_x_axis)

// Multi-server watch widgets
#define secondary_container (UI_CTX->secondary_container)
#define secondary_boxes     (UI_CTX->secondary_boxes)
#define add_server_boxes    (UI_CTX->add_server_boxes)

// ============== HELPER FUNCTIONS ==============

const char* map_format_name(const char *raw_map) {
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

// ============== UI UPDATE FUNCTIONS ==============

void ui_update_secondary(void) {
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

void ui_switch_screen(screen_id_t screen) {
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
            if (!screen_main) screen_builder_create_main();
            lv_screen_load(screen_main);
            ui_update_main();
            break;
        case SCREEN_SETTINGS:
            screen_builder_create_settings();
            lv_screen_load(screen_settings);
            break;
        case SCREEN_WIFI_SETTINGS:
            screen_builder_create_wifi_settings();
            lv_screen_load(screen_wifi);
            break;
        case SCREEN_SERVER_SETTINGS:
            screen_builder_create_server_settings();
            lv_screen_load(screen_server);
            break;
        case SCREEN_ADD_SERVER:
            screen_builder_create_add_server();
            lv_screen_load(screen_add_server);
            break;
        case SCREEN_HISTORY:
            screen_builder_create_history();
            lv_screen_load(screen_history);
            break;
        default:
            break;
    }

    lvgl_port_unlock();
}

// ============== UPDATE UI ==============

void ui_update_main(void) {
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

void ui_update_sd_status(void) {
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
