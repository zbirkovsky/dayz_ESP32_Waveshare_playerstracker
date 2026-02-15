/**
 * DayZ Server Tracker - Server Query Service Implementation
 * Handles querying the BattleMetrics API and updating state
 */

#include "server_query.h"
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_state.h"
#include "events.h"
#include "services/wifi_manager.h"
#include "services/battlemetrics.h"
#include "services/history_store.h"
#include "services/alert_manager.h"
#include "services/restart_manager.h"

static const char *TAG = "server_query";

static TaskHandle_t query_task_handle = NULL;
static volatile bool task_running = false;

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

static void server_query_task(void *arg) {
    ESP_LOGI(TAG, "Server query background task started");

    // Initial fetch immediately
    server_query_execute();
    events_post_simple(EVT_DATA_UPDATED);

    while (task_running) {
        app_state_t *state = app_state_get();
        int interval_sec = state->settings.refresh_interval_sec;

        // Block until notified OR timeout expires (replaces busy-polling loop)
        uint32_t notify_value = 0;
        xTaskNotifyWait(0, ULONG_MAX, &notify_value,
                        pdMS_TO_TICKS(interval_sec * 1000));

        if (!task_running) break;

        server_query_execute();
        events_post_simple(EVT_DATA_UPDATED);
    }

    ESP_LOGI(TAG, "Server query background task stopped");
    query_task_handle = NULL;
    vTaskDelete(NULL);
}

void server_query_task_start(void) {
    if (query_task_handle != NULL) {
        ESP_LOGW(TAG, "Server query task already running");
        return;
    }

    task_running = true;

    BaseType_t ret = xTaskCreate(
        server_query_task,
        "server_query",
        8192,
        NULL,
        3,
        &query_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create server query task");
        task_running = false;
    } else {
        ESP_LOGI(TAG, "Server query background task created");
    }
}

void server_query_request_refresh(void) {
    if (query_task_handle) {
        xTaskNotify(query_task_handle, 1, eSetValueWithOverwrite);
    }
}
