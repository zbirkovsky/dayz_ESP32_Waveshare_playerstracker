/**
 * DayZ Server Tracker - BattleMetrics API Client Implementation
 */

#include "battlemetrics.h"
#include "config.h"
#include <string.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "battlemetrics";

static char *http_response = NULL;
static int http_response_len = 0;
static char last_error[64] = "";
static bool last_query_success = false;
static SemaphoreHandle_t bm_mutex = NULL;
static esp_http_client_handle_t s_client = NULL;

void battlemetrics_init(void) {
    if (bm_mutex == NULL) {
        bm_mutex = xSemaphoreCreateMutex();
        if (bm_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create BattleMetrics mutex");
        } else {
            ESP_LOGI(TAG, "BattleMetrics client initialized");
        }
    }
    if (!http_response) {
        http_response = heap_caps_malloc(HTTP_RESPONSE_BUFFER_SIZE,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!http_response) {
            ESP_LOGE(TAG, "Failed to allocate HTTP response buffer in PSRAM");
        }
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (http_response && http_response_len + evt->data_len < HTTP_RESPONSE_BUFFER_SIZE - 1) {
                memcpy(http_response + http_response_len, evt->data, evt->data_len);
                http_response_len += evt->data_len;
                http_response[http_response_len] = 0;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

static esp_http_client_handle_t get_or_create_client(void) {
    if (s_client) return s_client;
    esp_http_client_config_t config = {
        .url = BATTLEMETRICS_API_BASE,
        .event_handler = http_event_handler,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };
    s_client = esp_http_client_init(&config);
    return s_client;
}

// Parse server time and map from details
static void parse_server_details(cJSON *details, server_status_t *status) {
    status->server_time[0] = '\0';
    status->map_name[0] = '\0';
    status->is_daytime = true;

    if (!details) return;

    // Parse time
    cJSON *time_field = cJSON_GetObjectItem(details, "time");
    if (time_field && cJSON_IsString(time_field)) {
        strncpy(status->server_time, time_field->valuestring,
                sizeof(status->server_time) - 1);

        // Parse hour to determine day/night
        // Format is "HH:MM" so atoi will get the hour
        int hour = atoi(status->server_time);

        // DayZ day/night cycle:
        // Sunrise ~05:00, Sunset ~21:00 (varies by server settings)
        status->is_daytime = (hour >= 5 && hour < 21);

        ESP_LOGD(TAG, "Server time: %s, hour=%d (%s)",
                 status->server_time, hour,
                 status->is_daytime ? "Day" : "Night");
    }

    // Parse map name
    cJSON *map_field = cJSON_GetObjectItem(details, "map");
    if (map_field && cJSON_IsString(map_field)) {
        strncpy(status->map_name, map_field->valuestring,
                sizeof(status->map_name) - 1);
        ESP_LOGD(TAG, "Map: %s", status->map_name);
    }
}

// Parse IP and port from address field
static void parse_address(cJSON *attributes, server_status_t *status) {
    status->ip_address[0] = '\0';
    status->port = 0;

    cJSON *ip_field = cJSON_GetObjectItem(attributes, "ip");
    if (ip_field && cJSON_IsString(ip_field)) {
        strncpy(status->ip_address, ip_field->valuestring,
                sizeof(status->ip_address) - 1);
    }

    cJSON *port_field = cJSON_GetObjectItem(attributes, "port");
    if (port_field && cJSON_IsNumber(port_field)) {
        status->port = (uint16_t)port_field->valueint;
    }
}

esp_err_t battlemetrics_query(const char *server_id, server_status_t *status) {
    if (!server_id || !status) {
        strncpy(last_error, "Invalid parameters", sizeof(last_error) - 1);
        last_query_success = false;
        return ESP_ERR_INVALID_ARG;
    }

    // Acquire mutex for thread-safe HTTP client access
    if (bm_mutex && xSemaphoreTake(bm_mutex, pdMS_TO_TICKS(30000)) != pdTRUE) {
        strncpy(last_error, "Mutex timeout", sizeof(last_error) - 1);
        last_query_success = false;
        return ESP_ERR_TIMEOUT;
    }

    // Initialize status with defaults
    memset(status, 0, sizeof(server_status_t));
    status->players = -1;
    status->max_players = DEFAULT_MAX_PLAYERS;
    status->is_daytime = true;

    // Prepare request
    http_response_len = 0;
    if (http_response) http_response[0] = 0;

    char url[256];
    snprintf(url, sizeof(url), "%s%s", BATTLEMETRICS_API_BASE, server_id);

    esp_http_client_handle_t client = get_or_create_client();
    if (!client) {
        strncpy(last_error, "Failed to create HTTP client", sizeof(last_error) - 1);
        last_query_success = false;
        if (bm_mutex) xSemaphoreGive(bm_mutex);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_url(client, url);
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        // Connection broken - destroy and retry once
        ESP_LOGW(TAG, "HTTP failed (%s), retrying with fresh connection", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        s_client = NULL;
        http_response_len = 0;
        if (http_response) http_response[0] = 0;

        client = get_or_create_client();
        if (client) {
            esp_http_client_set_url(client, url);
            err = esp_http_client_perform(client);
        }
        if (err != ESP_OK) {
            snprintf(last_error, sizeof(last_error), "HTTP failed: %s", esp_err_to_name(err));
            ESP_LOGE(TAG, "%s", last_error);
            if (client) {
                esp_http_client_cleanup(client);
                s_client = NULL;
            }
            last_query_success = false;
            if (bm_mutex) xSemaphoreGive(bm_mutex);
            return err;
        }
    }

    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
             status_code, esp_http_client_get_content_length(client));

    if (status_code != 200) {
        snprintf(last_error, sizeof(last_error), "HTTP status %d", status_code);
        last_query_success = false;
        if (bm_mutex) xSemaphoreGive(bm_mutex);
        return ESP_ERR_HTTP_BASE + status_code;
    }

    // Parse JSON response using cJSON
    cJSON *root = cJSON_Parse(http_response);
    if (!root) {
        strncpy(last_error, "JSON parse failed", sizeof(last_error) - 1);
        ESP_LOGE(TAG, "Failed to parse JSON response");
        last_query_success = false;
        if (bm_mutex) xSemaphoreGive(bm_mutex);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Navigate to data.attributes
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) {
        cJSON_Delete(root);
        strncpy(last_error, "Missing 'data' in response", sizeof(last_error) - 1);
        last_query_success = false;
        if (bm_mutex) xSemaphoreGive(bm_mutex);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *attributes = cJSON_GetObjectItem(data, "attributes");
    if (!attributes) {
        cJSON_Delete(root);
        strncpy(last_error, "Missing 'attributes' in response", sizeof(last_error) - 1);
        last_query_success = false;
        if (bm_mutex) xSemaphoreGive(bm_mutex);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Parse player count
    cJSON *players = cJSON_GetObjectItem(attributes, "players");
    if (players && cJSON_IsNumber(players)) {
        status->players = players->valueint;
    }

    // Parse max players
    cJSON *max_players = cJSON_GetObjectItem(attributes, "maxPlayers");
    if (max_players && cJSON_IsNumber(max_players)) {
        status->max_players = max_players->valueint;
    }

    // Parse server name
    cJSON *name = cJSON_GetObjectItem(attributes, "name");
    if (name && cJSON_IsString(name)) {
        strncpy(status->server_name, name->valuestring,
                sizeof(status->server_name) - 1);
    }

    // Parse online status
    cJSON *online = cJSON_GetObjectItem(attributes, "status");
    if (online && cJSON_IsString(online)) {
        status->online = (strcmp(online->valuestring, "online") == 0);
    }

    // Parse server rank (lower = more popular, null = unranked)
    cJSON *rank = cJSON_GetObjectItem(attributes, "rank");
    if (rank && cJSON_IsNumber(rank)) {
        status->rank = rank->valueint;
    } else {
        status->rank = 0;  // 0 = unranked
    }

    // Parse IP and port
    parse_address(attributes, status);

    // Parse details (contains server time and map)
    cJSON *details = cJSON_GetObjectItem(attributes, "details");
    parse_server_details(details, status);

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Parsed: players=%d/%d, time=%s, online=%d, rank=%d",
             status->players, status->max_players,
             status->server_time, status->online, status->rank);

    last_error[0] = '\0';
    last_query_success = true;
    if (bm_mutex) xSemaphoreGive(bm_mutex);
    return ESP_OK;
}

const char* battlemetrics_get_last_error(void) {
    return last_error;
}

bool battlemetrics_last_query_ok(void) {
    return last_query_success;
}
