/**
 * DayZ Server Tracker - Application State Implementation
 */

#include "app_state.h"
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

static const char *TAG = "app_state";

// Global application state
static app_state_t g_state;

void app_state_init(void) {
    memset(&g_state, 0, sizeof(app_state_t));

    // Create mutex for thread-safe access
    g_state.mutex = xSemaphoreCreateMutex();
    if (!g_state.mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
    }

    // Initialize runtime defaults
    g_state.runtime.current_players = -1;
    g_state.runtime.max_players = DEFAULT_MAX_PLAYERS;
    strncpy(g_state.runtime.last_update, "Never", sizeof(g_state.runtime.last_update) - 1);
    g_state.runtime.last_update[sizeof(g_state.runtime.last_update) - 1] = '\0';
    g_state.runtime.server_time[0] = '\0';
    g_state.runtime.is_daytime = true;
    g_state.runtime.wifi_connected = false;
    g_state.runtime.refresh_requested = false;

    // Initialize secondary server data
    memset(g_state.runtime.secondary, 0, sizeof(g_state.runtime.secondary));
    for (int i = 0; i < MAX_SECONDARY_SERVERS; i++) {
        g_state.runtime.secondary[i].player_count = -1;
        g_state.runtime.secondary[i].valid = false;
    }
    g_state.runtime.secondary_count = 0;

    // Initialize UI defaults
    g_state.ui.current_screen = SCREEN_MAIN;
    g_state.ui.current_history_range = HISTORY_RANGE_1H;
    g_state.ui.alert_active = false;
    g_state.ui.alert_start_time = 0;
    g_state.ui.screensaver_active = false;
    g_state.ui.last_activity_time = 0;  // Will be set when LVGL starts

    // Initialize history - allocate in PSRAM
    g_state.history.entries = heap_caps_calloc(MAX_HISTORY_ENTRIES,
                                                sizeof(history_entry_t),
                                                MALLOC_CAP_SPIRAM);
    if (!g_state.history.entries) {
        ESP_LOGW(TAG, "Failed to allocate history in PSRAM, trying regular RAM");
        g_state.history.entries = calloc(MAX_HISTORY_ENTRIES, sizeof(history_entry_t));
    }

    if (!g_state.history.entries) {
        ESP_LOGE(TAG, "Failed to allocate history buffer!");
    } else {
        ESP_LOGI(TAG, "History buffer allocated (%d entries)", MAX_HISTORY_ENTRIES);
    }

    g_state.history.head = 0;
    g_state.history.count = 0;
    g_state.history.unsaved_count = 0;

    // Initialize multi-WiFi defaults
    memset(&g_state.wifi_multi, 0, sizeof(wifi_multi_state_t));
    g_state.wifi_multi.active_idx = -1;

    // Initialize settings defaults
    g_state.settings.refresh_interval_sec = DEFAULT_REFRESH_INTERVAL_SEC;
    g_state.settings.screensaver_timeout_sec = DEFAULT_SCREENSAVER_TIMEOUT_SEC;
    g_state.settings.first_boot = true;
    g_state.settings.server_count = 0;
    g_state.settings.active_server_index = 0;

    ESP_LOGI(TAG, "Application state initialized");
}

app_state_t* app_state_get(void) {
    return &g_state;
}

bool app_state_lock(uint32_t timeout_ms) {
    if (!g_state.mutex) return false;
    return xSemaphoreTake(g_state.mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void app_state_unlock(void) {
    if (g_state.mutex) {
        xSemaphoreGive(g_state.mutex);
    }
}

server_config_t* app_state_get_active_server(void) {
    if (g_state.settings.server_count == 0) {
        return NULL;
    }
    if (g_state.settings.active_server_index >= g_state.settings.server_count) {
        g_state.settings.active_server_index = 0;
    }
    return &g_state.settings.servers[g_state.settings.active_server_index];
}

bool app_state_is_wifi_connected(void) {
    // Simple read doesn't need lock for bool
    return g_state.runtime.wifi_connected;
}

void app_state_set_wifi_connected(bool connected) {
    g_state.runtime.wifi_connected = connected;
}

void app_state_request_refresh(void) {
    g_state.runtime.refresh_requested = true;
}

bool app_state_consume_refresh_request(void) {
    if (g_state.runtime.refresh_requested) {
        g_state.runtime.refresh_requested = false;
        return true;
    }
    return false;
}

void app_state_update_player_data(int players, int max_players,
                                   const char *server_time, bool is_daytime,
                                   const char *map_name) {
    if (app_state_lock(100)) {
        g_state.runtime.current_players = players;
        if (max_players > 0) {
            g_state.runtime.max_players = max_players;
        }
        if (server_time) {
            strncpy(g_state.runtime.server_time, server_time,
                    sizeof(g_state.runtime.server_time) - 1);
        }
        if (map_name) {
            strncpy(g_state.runtime.map_name, map_name,
                    sizeof(g_state.runtime.map_name) - 1);
            g_state.runtime.map_name[sizeof(g_state.runtime.map_name) - 1] = '\0';
        }
        g_state.runtime.is_daytime = is_daytime;
        app_state_unlock();
    }
}

screen_id_t app_state_get_current_screen(void) {
    return g_state.ui.current_screen;
}

void app_state_set_current_screen(screen_id_t screen) {
    g_state.ui.current_screen = screen;
}

// ============== MULTI-SERVER WATCH API ==============

void app_state_update_secondary_indices(void) {
    if (app_state_lock(100)) {
        g_state.runtime.secondary_count = 0;

        // Find all servers that are not the active one
        for (int i = 0; i < g_state.settings.server_count &&
             g_state.runtime.secondary_count < MAX_SECONDARY_SERVERS; i++) {
            if (i != g_state.settings.active_server_index &&
                g_state.settings.servers[i].active) {
                g_state.runtime.secondary_server_indices[g_state.runtime.secondary_count] = i;
                g_state.runtime.secondary_count++;
            }
        }

        ESP_LOGI(TAG, "Secondary indices updated: %d servers", g_state.runtime.secondary_count);
        app_state_unlock();
    }
}

void app_state_update_secondary_status(int slot, int players, int max_players,
                                        const char *server_time, bool is_daytime,
                                        const char *map_name) {
    if (slot < 0 || slot >= MAX_SECONDARY_SERVERS) return;

    if (app_state_lock(100)) {
        secondary_server_status_t *sec = &g_state.runtime.secondary[slot];
        sec->player_count = players;
        sec->max_players = max_players;
        if (server_time) {
            strncpy(sec->server_time, server_time, sizeof(sec->server_time) - 1);
            sec->server_time[sizeof(sec->server_time) - 1] = '\0';
        }
        if (map_name) {
            strncpy(sec->map_name, map_name, sizeof(sec->map_name) - 1);
            sec->map_name[sizeof(sec->map_name) - 1] = '\0';
        }
        sec->is_daytime = is_daytime;
        sec->valid = (players >= 0);
        sec->fetch_pending = false;
        sec->last_update_time = esp_timer_get_time() / 1000;  // Convert to ms
        app_state_unlock();
    }
}

void app_state_add_trend_point(int slot, int player_count) {
    if (slot < 0 || slot >= MAX_SECONDARY_SERVERS) return;
    if (player_count < 0) return;  // Don't add invalid data

    if (app_state_lock(100)) {
        trend_data_t *trend = &g_state.runtime.secondary[slot].trend;

        // Add to ring buffer
        trend->player_counts[trend->head] = (int16_t)player_count;
        trend->timestamps[trend->head] = (uint32_t)(esp_timer_get_time() / 1000000);  // seconds

        trend->head = (trend->head + 1) % TREND_HISTORY_SIZE;
        if (trend->count < TREND_HISTORY_SIZE) {
            trend->count++;
        }

        app_state_unlock();
    }
}

int app_state_calculate_trend(int slot) {
    if (slot < 0 || slot >= MAX_SECONDARY_SERVERS) return 0;

    int delta = 0;

    if (app_state_lock(100)) {
        trend_data_t *trend = &g_state.runtime.secondary[slot].trend;

        if (trend->count >= 2) {
            // Find oldest and newest entries within the trend window
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000);
            int oldest_idx = -1;
            int newest_idx = -1;
            uint32_t oldest_time = now;
            uint32_t newest_time = 0;

            for (int i = 0; i < trend->count; i++) {
                uint32_t age = now - trend->timestamps[i];
                if (age <= TREND_WINDOW_SEC) {
                    if (trend->timestamps[i] < oldest_time) {
                        oldest_time = trend->timestamps[i];
                        oldest_idx = i;
                    }
                    if (trend->timestamps[i] > newest_time) {
                        newest_time = trend->timestamps[i];
                        newest_idx = i;
                    }
                }
            }

            if (oldest_idx >= 0 && newest_idx >= 0 && oldest_idx != newest_idx) {
                delta = trend->player_counts[newest_idx] - trend->player_counts[oldest_idx];
            }
        }

        app_state_unlock();
    }

    return delta;
}

void app_state_add_main_trend_point(int player_count) {
    if (player_count < 0) return;  // Don't add invalid data

    if (app_state_lock(100)) {
        trend_data_t *trend = &g_state.runtime.main_trend;

        // Add to ring buffer
        trend->player_counts[trend->head] = (int16_t)player_count;
        trend->timestamps[trend->head] = (uint32_t)(esp_timer_get_time() / 1000000);  // seconds

        trend->head = (trend->head + 1) % TREND_HISTORY_SIZE;
        if (trend->count < TREND_HISTORY_SIZE) {
            trend->count++;
        }

        ESP_LOGI(TAG, "Trend: added %d players, count=%d", player_count, trend->count);

        app_state_unlock();
    }
}

int app_state_calculate_main_trend(void) {
    int delta = 0;

    if (app_state_lock(100)) {
        trend_data_t *trend = &g_state.runtime.main_trend;

        if (trend->count >= 2) {
            // Find oldest and newest entries within the trend window
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000);
            int oldest_idx = -1;
            int newest_idx = -1;
            uint32_t oldest_time = now;
            uint32_t newest_time = 0;

            for (int i = 0; i < trend->count; i++) {
                uint32_t age = now - trend->timestamps[i];
                if (age <= TREND_WINDOW_SEC) {
                    if (trend->timestamps[i] < oldest_time) {
                        oldest_time = trend->timestamps[i];
                        oldest_idx = i;
                    }
                    if (trend->timestamps[i] > newest_time) {
                        newest_time = trend->timestamps[i];
                        newest_idx = i;
                    }
                }
            }

            if (oldest_idx >= 0 && newest_idx >= 0 && oldest_idx != newest_idx) {
                delta = trend->player_counts[newest_idx] - trend->player_counts[oldest_idx];
                ESP_LOGI(TAG, "Trend calc: oldest[%d]=%d, newest[%d]=%d, delta=%d",
                         oldest_idx, trend->player_counts[oldest_idx],
                         newest_idx, trend->player_counts[newest_idx], delta);
            } else {
                ESP_LOGD(TAG, "Trend calc: not enough distinct points (oldest=%d, newest=%d)",
                         oldest_idx, newest_idx);
            }
        }

        app_state_unlock();
    }

    return delta;
}

int app_state_get_main_trend_count(void) {
    int count = 0;
    if (app_state_lock(100)) {
        count = g_state.runtime.main_trend.count;
        app_state_unlock();
    }
    return count;
}

void app_state_clear_main_trend(void) {
    if (app_state_lock(100)) {
        memset(&g_state.runtime.main_trend, 0, sizeof(trend_data_t));
        ESP_LOGI(TAG, "Main server trend data cleared");
        app_state_unlock();
    }
}

void app_state_clear_secondary_data(void) {
    if (app_state_lock(100)) {
        for (int i = 0; i < MAX_SECONDARY_SERVERS; i++) {
            memset(&g_state.runtime.secondary[i], 0, sizeof(secondary_server_status_t));
            g_state.runtime.secondary[i].player_count = -1;
            g_state.runtime.secondary[i].valid = false;
        }
        g_state.runtime.secondary_count = 0;
        ESP_LOGI(TAG, "Secondary server data cleared");
        app_state_unlock();
    }
}
