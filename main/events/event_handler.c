/**
 * DayZ Server Tracker - Event Handler Implementation
 * Processes events from the event queue and dispatches UI/state changes
 */

#include "event_handler.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "app_state.h"
#include "events.h"
#include "services/settings_store.h"
#include "services/wifi_manager.h"
#include "services/history_store.h"
#include "services/secondary_fetch.h"
#include "services/server_query.h"
#include "ui/ui_main.h"
#include "ui/ui_update.h"

static const char *TAG = "event_handler";

// Deferred heavy I/O work (runs after LVGL has rendered the frame)
static struct {
    bool pending;
    int old_server_idx;
    int new_server_idx;
    bool trigger_main_fetch;
} s_deferred_switch = {0};

bool event_handler_has_deferred(void) {
    return s_deferred_switch.pending;
}

void event_handler_process_deferred(void) {
    if (!s_deferred_switch.pending) return;
    s_deferred_switch.pending = false;

    int old_idx = s_deferred_switch.old_server_idx;
    int new_idx = s_deferred_switch.new_server_idx;

    // Heavy I/O: NVS save + SD read/write + JSON parsing
    history_switch_server(old_idx, new_idx);
    settings_save();

    // Trigger background fetches
    if (s_deferred_switch.trigger_main_fetch) {
        server_query_request_refresh();
    }
    secondary_fetch_refresh_now();
}

// Private: process a single event (shared by blocking and non-blocking paths)
static void handle_event(app_event_t *evt, app_state_t *state) {
    switch (evt->type) {
        case EVT_SCREEN_CHANGE:
            ui_switch_screen(evt->data.screen);
            break;

        case EVT_WIFI_SAVE: {
            // Add/update credential in multi-WiFi, then connect
            int cred_idx = settings_add_wifi_credential(evt->data.wifi.ssid, evt->data.wifi.password);
            settings_save_wifi_credentials();
            // Also update legacy fields
            settings_save_wifi(evt->data.wifi.ssid, evt->data.wifi.password);
            wifi_manager_reconnect(evt->data.wifi.ssid, evt->data.wifi.password);
            if (cred_idx >= 0 && app_state_lock(100)) {
                state->wifi_multi.active_idx = cred_idx;
                app_state_unlock();
            }
            ui_switch_screen(SCREEN_WIFI_SETTINGS);
            break;
        }

        case EVT_SERVER_ADD: {
            int new_idx = settings_add_server(evt->data.server.server_id, evt->data.server.display_name);
            // Set map_name on the newly added server
            if (new_idx >= 0 && evt->data.server.map_name[0] != '\0') {
                if (app_state_lock(100)) {
                    strncpy(state->settings.servers[new_idx].map_name,
                            evt->data.server.map_name,
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
            server_query_request_refresh();
            settings_export_to_json();
            ui_switch_screen(SCREEN_MAIN);
            break;
        }

        case EVT_SERVER_DELETE: {
            int old_idx = state->settings.active_server_index;
            settings_delete_server(evt->data.server_index);
            int new_idx = state->settings.active_server_index;
            // Switch history if active server changed
            if (old_idx != new_idx || evt->data.server_index == old_idx) {
                history_switch_server(-1, new_idx);  // -1 because deleted server data is gone
            }
            state->runtime.current_players = -1;
            state->runtime.server_time[0] = '\0';
            app_state_clear_secondary_data();
            app_state_update_secondary_indices();
            secondary_fetch_refresh_now();
            server_query_request_refresh();
            settings_export_to_json();
            ui_switch_screen(SCREEN_MAIN);
            break;
        }

        case EVT_SERVER_NEXT:
            if (state->settings.server_count > 1) {
                int old_idx = state->settings.active_server_index;
                int new_idx = (old_idx + 1) % state->settings.server_count;
                state->settings.active_server_index = new_idx;
                state->runtime.current_players = -1;
                state->runtime.server_time[0] = '\0';
                app_state_clear_main_trend();
                app_state_clear_secondary_data();
                app_state_update_secondary_indices();
                ui_update_all();
                // Defer heavy I/O (history save/load, NVS, fetch)
                s_deferred_switch.old_server_idx = old_idx;
                s_deferred_switch.new_server_idx = new_idx;
                s_deferred_switch.trigger_main_fetch = true;
                s_deferred_switch.pending = true;
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
                state->runtime.current_players = -1;
                state->runtime.server_time[0] = '\0';
                app_state_clear_main_trend();
                app_state_clear_secondary_data();
                app_state_update_secondary_indices();
                ui_update_all();
                // Defer heavy I/O (history save/load, NVS, fetch)
                s_deferred_switch.old_server_idx = old_idx;
                s_deferred_switch.new_server_idx = new_idx;
                s_deferred_switch.trigger_main_fetch = true;
                s_deferred_switch.pending = true;
            }
            break;

        case EVT_REFRESH_DATA:
            server_query_request_refresh();
            break;

        case EVT_DATA_UPDATED:
            // Background query task completed - update UI if on main screen
            ui_update_all();
            break;

        case EVT_SECONDARY_SERVER_CLICKED: {
            // Swap clicked secondary server with main
            int slot = evt->data.secondary.slot;
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

                // Clear secondary data and recalculate indices
                app_state_clear_secondary_data();
                app_state_update_secondary_indices();

                // Restore old main server data into its new secondary slot
                for (int i = 0; i < state->runtime.secondary_count; i++) {
                    if (state->runtime.secondary_server_indices[i] == old_active) {
                        memcpy(&state->runtime.secondary[i], &old_main_data,
                               sizeof(secondary_server_status_t));
                        break;
                    }
                }

                // UI update immediately - user sees swap instantly
                ui_update_all();

                // Defer heavy I/O (history save/load, NVS, fetch)
                s_deferred_switch.old_server_idx = old_active;
                s_deferred_switch.new_server_idx = new_active;
                s_deferred_switch.trigger_main_fetch = false;
                s_deferred_switch.pending = true;
            }
            break;
        }

        case EVT_SECONDARY_DATA_UPDATED:
            // Refresh secondary boxes display
            ui_update_secondary();
            break;

        case EVT_WIFI_SCAN_START:
            wifi_manager_start_scan();
            break;

        case EVT_WIFI_SCAN_COMPLETE:
            // Refresh WiFi screen if visible
            if (app_state_get_current_screen() == SCREEN_WIFI_SETTINGS) {
                ui_switch_screen(SCREEN_WIFI_SETTINGS);
            }
            break;

        case EVT_WIFI_DELETE_CREDENTIAL: {
            int del_idx = evt->data.wifi_credential.index;
            bool was_active = (state->wifi_multi.active_idx == del_idx);
            settings_delete_wifi_credential(del_idx);
            if (was_active && state->wifi_multi.count > 0) {
                // Connect to first available credential
                wifi_manager_connect_index(0);
            }
            // Refresh WiFi screen
            if (app_state_get_current_screen() == SCREEN_WIFI_SETTINGS) {
                ui_switch_screen(SCREEN_WIFI_SETTINGS);
            }
            break;
        }

        case EVT_WIFI_CONNECT_CREDENTIAL:
            wifi_manager_connect_index(evt->data.wifi_credential.index);
            break;

        default:
            break;
    }
}

void event_handler_process(void) {
    app_event_t evt;
    app_state_t *state = app_state_get();

    while (events_receive(&evt)) {
        handle_event(&evt, state);
    }
}

void event_handler_process_blocking(uint32_t timeout_ms) {
    app_event_t evt;
    app_state_t *state = app_state_get();

    // Block until first event arrives or timeout
    if (!events_receive_blocking(&evt, timeout_ms)) {
        return;  // Timeout, no events
    }

    // Process the first event and drain any remaining
    do {
        handle_event(&evt, state);
    } while (events_receive(&evt));  // Drain remaining events non-blocking
}
