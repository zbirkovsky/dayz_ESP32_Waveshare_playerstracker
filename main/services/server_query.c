/**
 * DayZ Server Tracker - Server Query Service Implementation
 * Handles querying the BattleMetrics API and updating state
 */

#include "server_query.h"
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "app_state.h"
#include "services/wifi_manager.h"
#include "services/battlemetrics.h"
#include "services/history_store.h"
#include "services/alert_manager.h"
#include "services/restart_manager.h"

static const char *TAG = "server_query";

void server_query_execute(void) {
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
