/**
 * DayZ Server Tracker - Application State Implementation
 */

#include "app_state.h"
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

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
    strcpy(g_state.runtime.last_update, "Never");
    g_state.runtime.server_time[0] = '\0';
    g_state.runtime.is_daytime = true;
    g_state.runtime.wifi_connected = false;
    g_state.runtime.refresh_requested = false;

    // Initialize UI defaults
    g_state.ui.current_screen = SCREEN_MAIN;
    g_state.ui.current_history_range = HISTORY_RANGE_1H;
    g_state.ui.alert_active = false;
    g_state.ui.alert_start_time = 0;

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

    // Initialize settings defaults
    g_state.settings.refresh_interval_sec = DEFAULT_REFRESH_INTERVAL_SEC;
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
                                   const char *server_time, bool is_daytime) {
    if (app_state_lock(100)) {
        g_state.runtime.current_players = players;
        if (max_players > 0) {
            g_state.runtime.max_players = max_players;
        }
        if (server_time) {
            strncpy(g_state.runtime.server_time, server_time,
                    sizeof(g_state.runtime.server_time) - 1);
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
