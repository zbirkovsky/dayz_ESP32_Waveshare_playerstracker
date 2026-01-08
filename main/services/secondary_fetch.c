/**
 * DayZ Server Tracker - Secondary Server Fetch Service Implementation
 */

#include "secondary_fetch.h"
#include "battlemetrics.h"
#include "history_store.h"
#include "app_state.h"
#include "config.h"
#include "events.h"
#include "drivers/sd_card.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>

static const char *TAG = "secondary_fetch";

static TaskHandle_t fetch_task_handle = NULL;
static volatile bool fetch_running = false;
static volatile bool refresh_requested = false;

static void secondary_fetch_task(void *arg) {
    ESP_LOGI(TAG, "Secondary fetch task started");

    TickType_t last_fetch_time = 0;
    const TickType_t fetch_interval = pdMS_TO_TICKS(SECONDARY_REFRESH_SEC * 1000);

    while (fetch_running) {
        TickType_t now = xTaskGetTickCount();

        // Check if it's time to fetch or refresh was requested
        bool should_fetch = refresh_requested ||
                           (now - last_fetch_time >= fetch_interval) ||
                           (last_fetch_time == 0);

        if (should_fetch && fetch_running) {
            refresh_requested = false;

            // Update secondary indices in case server list changed
            app_state_update_secondary_indices();

            app_state_t *state = app_state_get();
            uint8_t count = state->runtime.secondary_count;

            ESP_LOGI(TAG, "Fetching %d secondary servers", count);

            // Fetch each secondary server
            for (int slot = 0; slot < count && fetch_running; slot++) {
                uint8_t server_idx = state->runtime.secondary_server_indices[slot];

                if (server_idx >= state->settings.server_count) continue;

                server_config_t *server = &state->settings.servers[server_idx];
                if (!server->active || server->server_id[0] == '\0') continue;

                // Mark as fetching
                if (app_state_lock(100)) {
                    state->runtime.secondary[slot].fetch_pending = true;
                    app_state_unlock();
                }

                // Query BattleMetrics
                server_status_t status;
                esp_err_t err = battlemetrics_query(server->server_id, &status);

                if (err == ESP_OK) {
                    // Update state
                    app_state_update_secondary_status(slot, status.players, status.max_players,
                                                       status.server_time, status.is_daytime,
                                                       status.map_name);
                    app_state_add_trend_point(slot, status.players);

                    // Record history for secondary server (to SD card JSON)
                    if (sd_card_is_mounted() && status.players >= 0) {
                        time_t now;
                        time(&now);
                        history_append_entry_json(server_idx, (uint32_t)now, (int16_t)status.players);
                    }

                    ESP_LOGI(TAG, "Slot %d (%s): %d/%d players, time=%s",
                             slot, server->display_name, status.players,
                             status.max_players, status.server_time);
                } else {
                    // Mark as invalid but don't clear existing data
                    if (app_state_lock(100)) {
                        state->runtime.secondary[slot].fetch_pending = false;
                        app_state_unlock();
                    }
                    ESP_LOGW(TAG, "Failed to fetch slot %d (%s): %s",
                             slot, server->display_name, battlemetrics_get_last_error());
                }

                // Small delay between requests to avoid rate limiting
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            // Post event to update UI
            events_post_simple(EVT_SECONDARY_DATA_UPDATED);

            last_fetch_time = xTaskGetTickCount();
        }

        // Sleep for a bit before checking again
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Secondary fetch task stopped");
    fetch_task_handle = NULL;
    vTaskDelete(NULL);
}

void secondary_fetch_init(void) {
    ESP_LOGI(TAG, "Secondary fetch service initialized");
}

void secondary_fetch_start(void) {
    if (fetch_task_handle != NULL) {
        ESP_LOGW(TAG, "Secondary fetch already running");
        return;
    }

    fetch_running = true;
    refresh_requested = true;  // Fetch immediately on start

    BaseType_t ret = xTaskCreate(
        secondary_fetch_task,
        "secondary_fetch",
        8192,  // Increased from 4096 - needs more for HTTP + JSON
        NULL,
        3,  // Lower priority than main UI task
        &fetch_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create secondary fetch task");
        fetch_running = false;
    } else {
        ESP_LOGI(TAG, "Secondary fetch task started");
    }
}

void secondary_fetch_stop(void) {
    if (!fetch_running) return;

    ESP_LOGI(TAG, "Stopping secondary fetch task");
    fetch_running = false;

    // Wait for task to finish
    int timeout = 50;  // 5 seconds max
    while (fetch_task_handle != NULL && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void secondary_fetch_refresh_now(void) {
    refresh_requested = true;
}
