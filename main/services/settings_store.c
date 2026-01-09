/**
 * DayZ Server Tracker - Settings Store Implementation
 */

#include "settings_store.h"
#include "history_store.h"
#include "config.h"
#include "drivers/sd_card.h"
#include "nvs_keys.h"
#include "storage_config.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "settings_store";

void settings_init(void) {
    // NVS is initialized in main, this is just a placeholder for any
    // future initialization needs
    ESP_LOGI(TAG, "Settings store initialized");
}

esp_err_t settings_load(void) {
    app_state_t *state = app_state_get();
    nvs_handle_t nvs;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);

    // Set defaults first
    if (app_state_lock(100)) {
        memset(&state->settings, 0, sizeof(app_settings_t));
        state->settings.refresh_interval_sec = DEFAULT_REFRESH_INTERVAL_SEC;
        state->settings.first_boot = true;
        state->settings.server_count = 0;
        state->settings.active_server_index = 0;
        app_state_unlock();
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No settings found in NVS, using defaults");
        // Add default server
        settings_add_server(DEFAULT_SERVER_ID, DEFAULT_SERVER_NAME);
        return ESP_OK;
    }

    if (!app_state_lock(100)) {
        nvs_close(nvs);
        return ESP_ERR_TIMEOUT;
    }

    size_t len;

    // Load WiFi credentials
    len = sizeof(state->settings.wifi_ssid);
    if (nvs_get_str(nvs, "wifi_ssid", state->settings.wifi_ssid, &len) == ESP_OK) {
        state->settings.first_boot = false;
    }

    len = sizeof(state->settings.wifi_password);
    nvs_get_str(nvs, "wifi_pass", state->settings.wifi_password, &len);

    // Load refresh interval
    nvs_get_u16(nvs, "refresh_int", &state->settings.refresh_interval_sec);
    if (state->settings.refresh_interval_sec < MIN_REFRESH_INTERVAL_SEC) {
        state->settings.refresh_interval_sec = MIN_REFRESH_INTERVAL_SEC;
    }
    if (state->settings.refresh_interval_sec > MAX_REFRESH_INTERVAL_SEC) {
        state->settings.refresh_interval_sec = MAX_REFRESH_INTERVAL_SEC;
    }

    // Load screensaver timeout
    nvs_get_u16(nvs, "screen_off", &state->settings.screensaver_timeout_sec);

    // Load server count and active index
    uint8_t count = 0;
    nvs_get_u8(nvs, "server_count", &count);
    state->settings.server_count = count > MAX_SERVERS ? MAX_SERVERS : count;
    nvs_get_u8(nvs, "active_srv", &state->settings.active_server_index);

    // Load each server using NVS key macros
    for (int i = 0; i < state->settings.server_count; i++) {
        server_config_t *srv = &state->settings.servers[i];

        NVS_KEY_SERVER(key_id, i, NVS_SUFFIX_ID);
        len = sizeof(srv->server_id);
        nvs_get_str(nvs, key_id, srv->server_id, &len);

        NVS_KEY_SERVER(key_name, i, NVS_SUFFIX_NAME);
        len = sizeof(srv->display_name);
        nvs_get_str(nvs, key_name, srv->display_name, &len);

        NVS_KEY_SERVER(key_map, i, NVS_SUFFIX_MAP);
        len = sizeof(srv->map_name);
        nvs_get_str(nvs, key_map, srv->map_name, &len);

        NVS_KEY_SERVER(key_ip, i, NVS_SUFFIX_IP);
        len = sizeof(srv->ip_address);
        nvs_get_str(nvs, key_ip, srv->ip_address, &len);

        NVS_KEY_SERVER(key_port, i, NVS_SUFFIX_PORT);
        nvs_get_u16(nvs, key_port, &srv->port);

        NVS_KEY_SERVER(key_max, i, NVS_SUFFIX_MAX);
        nvs_get_u16(nvs, key_max, &srv->max_players);
        if (srv->max_players == 0) srv->max_players = DEFAULT_MAX_PLAYERS;

        NVS_KEY_SERVER(key_alert, i, NVS_SUFFIX_ALERT);
        nvs_get_u16(nvs, key_alert, &srv->alert_threshold);

        uint8_t alerts_en = 0;
        NVS_KEY_SERVER(key_alen, i, NVS_SUFFIX_ALEN);
        nvs_get_u8(nvs, key_alen, &alerts_en);
        srv->alerts_enabled = alerts_en > 0;

        // Load restart history
        NVS_KEY_SERVER(key_rcnt, i, NVS_SUFFIX_RCNT);
        nvs_get_u8(nvs, key_rcnt, &srv->restart_history.restart_count);

        NVS_KEY_SERVER(key_ravg, i, NVS_SUFFIX_RAVG);
        nvs_get_u32(nvs, key_ravg, &srv->restart_history.avg_interval_sec);

        NVS_KEY_SERVER(key_rlast, i, NVS_SUFFIX_RLAST);
        nvs_get_u32(nvs, key_rlast, &srv->restart_history.last_restart_time);

        NVS_KEY_SERVER(key_rtimes, i, NVS_SUFFIX_RTIMES);
        len = sizeof(srv->restart_history.restart_times);
        nvs_get_blob(nvs, key_rtimes, srv->restart_history.restart_times, &len);

        srv->restart_history.last_known_players = -1;

        // Load manual restart schedule
        NVS_KEY_SERVER(key_rhr, i, NVS_SUFFIX_RHR);
        nvs_get_u8(nvs, key_rhr, &srv->restart_hour);

        NVS_KEY_SERVER(key_rmin, i, NVS_SUFFIX_RMIN);
        nvs_get_u8(nvs, key_rmin, &srv->restart_minute);

        NVS_KEY_SERVER(key_rint, i, NVS_SUFFIX_RINT);
        nvs_get_u8(nvs, key_rint, &srv->restart_interval_hours);

        uint8_t manual_set = 0;
        NVS_KEY_SERVER(key_rman, i, NVS_SUFFIX_RMAN);
        nvs_get_u8(nvs, key_rman, &manual_set);
        srv->manual_restart_set = manual_set > 0;

        srv->active = true;
    }

    app_state_unlock();
    nvs_close(nvs);

    // If no servers loaded, add default
    if (state->settings.server_count == 0) {
        settings_add_server(DEFAULT_SERVER_ID, DEFAULT_SERVER_NAME);
    }

    ESP_LOGI(TAG, "Settings loaded: %d servers, refresh=%ds, first_boot=%d",
             state->settings.server_count, state->settings.refresh_interval_sec,
             state->settings.first_boot);

    return ESP_OK;
}

esp_err_t settings_save(void) {
    app_state_t *state = app_state_get();
    nvs_handle_t nvs;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return err;
    }

    if (!app_state_lock(100)) {
        nvs_close(nvs);
        return ESP_ERR_TIMEOUT;
    }

    // Save WiFi credentials
    nvs_set_str(nvs, "wifi_ssid", state->settings.wifi_ssid);
    nvs_set_str(nvs, "wifi_pass", state->settings.wifi_password);

    // Save refresh interval
    nvs_set_u16(nvs, "refresh_int", state->settings.refresh_interval_sec);

    // Save screensaver timeout
    nvs_set_u16(nvs, "screen_off", state->settings.screensaver_timeout_sec);

    // Save server count and active index
    nvs_set_u8(nvs, "server_count", state->settings.server_count);
    nvs_set_u8(nvs, "active_srv", state->settings.active_server_index);

    // Save each server using NVS key macros
    for (int i = 0; i < state->settings.server_count; i++) {
        server_config_t *srv = &state->settings.servers[i];

        NVS_KEY_SERVER(key_id, i, NVS_SUFFIX_ID);
        nvs_set_str(nvs, key_id, srv->server_id);

        NVS_KEY_SERVER(key_name, i, NVS_SUFFIX_NAME);
        nvs_set_str(nvs, key_name, srv->display_name);

        NVS_KEY_SERVER(key_map, i, NVS_SUFFIX_MAP);
        nvs_set_str(nvs, key_map, srv->map_name);

        NVS_KEY_SERVER(key_ip, i, NVS_SUFFIX_IP);
        nvs_set_str(nvs, key_ip, srv->ip_address);

        NVS_KEY_SERVER(key_port, i, NVS_SUFFIX_PORT);
        nvs_set_u16(nvs, key_port, srv->port);

        NVS_KEY_SERVER(key_max, i, NVS_SUFFIX_MAX);
        nvs_set_u16(nvs, key_max, srv->max_players);

        NVS_KEY_SERVER(key_alert, i, NVS_SUFFIX_ALERT);
        nvs_set_u16(nvs, key_alert, srv->alert_threshold);

        NVS_KEY_SERVER(key_alen, i, NVS_SUFFIX_ALEN);
        nvs_set_u8(nvs, key_alen, srv->alerts_enabled ? 1 : 0);

        // Save restart history
        NVS_KEY_SERVER(key_rcnt, i, NVS_SUFFIX_RCNT);
        nvs_set_u8(nvs, key_rcnt, srv->restart_history.restart_count);

        NVS_KEY_SERVER(key_ravg, i, NVS_SUFFIX_RAVG);
        nvs_set_u32(nvs, key_ravg, srv->restart_history.avg_interval_sec);

        NVS_KEY_SERVER(key_rlast, i, NVS_SUFFIX_RLAST);
        nvs_set_u32(nvs, key_rlast, srv->restart_history.last_restart_time);

        NVS_KEY_SERVER(key_rtimes, i, NVS_SUFFIX_RTIMES);
        nvs_set_blob(nvs, key_rtimes, srv->restart_history.restart_times,
                     sizeof(srv->restart_history.restart_times));

        // Save manual restart schedule
        NVS_KEY_SERVER(key_rhr, i, NVS_SUFFIX_RHR);
        nvs_set_u8(nvs, key_rhr, srv->restart_hour);

        NVS_KEY_SERVER(key_rmin, i, NVS_SUFFIX_RMIN);
        nvs_set_u8(nvs, key_rmin, srv->restart_minute);

        NVS_KEY_SERVER(key_rint, i, NVS_SUFFIX_RINT);
        nvs_set_u8(nvs, key_rint, srv->restart_interval_hours);

        NVS_KEY_SERVER(key_rman, i, NVS_SUFFIX_RMAN);
        nvs_set_u8(nvs, key_rman, srv->manual_restart_set ? 1 : 0);
    }

    app_state_unlock();

    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Settings saved to NVS");

    // Also export to JSON on SD card (async, ignore errors)
    settings_export_to_json();

    return ESP_OK;
}

esp_err_t settings_save_wifi(const char *ssid, const char *password) {
    app_state_t *state = app_state_get();

    if (app_state_lock(100)) {
        strncpy(state->settings.wifi_ssid, ssid,
                sizeof(state->settings.wifi_ssid) - 1);
        strncpy(state->settings.wifi_password, password,
                sizeof(state->settings.wifi_password) - 1);
        state->settings.first_boot = false;
        app_state_unlock();
    }

    return settings_save();
}

esp_err_t settings_save_refresh_interval(uint16_t interval_sec) {
    app_state_t *state = app_state_get();

    if (interval_sec < MIN_REFRESH_INTERVAL_SEC) {
        interval_sec = MIN_REFRESH_INTERVAL_SEC;
    }
    if (interval_sec > MAX_REFRESH_INTERVAL_SEC) {
        interval_sec = MAX_REFRESH_INTERVAL_SEC;
    }

    if (app_state_lock(100)) {
        state->settings.refresh_interval_sec = interval_sec;
        app_state_unlock();
    }

    return settings_save();
}

int settings_add_server(const char *server_id, const char *display_name) {
    app_state_t *state = app_state_get();

    if (!app_state_lock(100)) {
        return -1;
    }

    if (state->settings.server_count >= MAX_SERVERS) {
        app_state_unlock();
        ESP_LOGW(TAG, "Max servers reached");
        return -1;
    }

    int old_idx = state->settings.active_server_index;
    int idx = state->settings.server_count;
    server_config_t *srv = &state->settings.servers[idx];

    memset(srv, 0, sizeof(server_config_t));
    strncpy(srv->server_id, server_id, sizeof(srv->server_id) - 1);
    strncpy(srv->display_name, display_name, sizeof(srv->display_name) - 1);
    srv->max_players = DEFAULT_MAX_PLAYERS;
    srv->alert_threshold = DEFAULT_ALERT_THRESHOLD;
    srv->alerts_enabled = false;
    srv->active = true;
    srv->restart_history.last_known_players = -1;

    state->settings.server_count++;
    state->settings.active_server_index = idx;

    app_state_unlock();

    // Switch history: save old server's history, clear for new server (no history yet)
    history_switch_server(old_idx, idx);

    settings_save();

    ESP_LOGI(TAG, "Server added: %s (idx=%d)", display_name, idx);
    return idx;
}

esp_err_t settings_delete_server(int index) {
    app_state_t *state = app_state_get();

    if (!app_state_lock(100)) {
        return ESP_ERR_TIMEOUT;
    }

    if (state->settings.server_count <= 1) {
        app_state_unlock();
        ESP_LOGW(TAG, "Cannot delete last server");
        return ESP_ERR_INVALID_STATE;
    }

    if (index < 0 || index >= state->settings.server_count) {
        app_state_unlock();
        return ESP_ERR_INVALID_ARG;
    }

    // Shift servers down
    for (int i = index; i < state->settings.server_count - 1; i++) {
        state->settings.servers[i] = state->settings.servers[i + 1];
    }

    state->settings.server_count--;
    if (state->settings.active_server_index >= state->settings.server_count) {
        state->settings.active_server_index = state->settings.server_count - 1;
    }

    app_state_unlock();

    settings_save();

    ESP_LOGI(TAG, "Server deleted (idx=%d)", index);
    return ESP_OK;
}

esp_err_t settings_save_restart_schedule(int server_index, uint8_t hour,
                                          uint8_t minute, uint8_t interval_hours,
                                          bool manual_enabled) {
    app_state_t *state = app_state_get();

    if (!app_state_lock(100)) {
        return ESP_ERR_TIMEOUT;
    }

    if (server_index < 0 || server_index >= state->settings.server_count) {
        app_state_unlock();
        return ESP_ERR_INVALID_ARG;
    }

    server_config_t *srv = &state->settings.servers[server_index];
    srv->restart_hour = hour;
    srv->restart_minute = minute;
    srv->restart_interval_hours = interval_hours;
    srv->manual_restart_set = manual_enabled;

    app_state_unlock();

    return settings_save();
}

// ============== JSON CONFIG EXPORT/IMPORT ==============

esp_err_t settings_export_to_json(void) {
    if (!sd_card_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted, cannot export settings");
        return ESP_ERR_INVALID_STATE;
    }

    app_state_t *state = app_state_get();

    // Create root JSON object
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    // Add metadata
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "app", APP_NAME);
    cJSON_AddStringToObject(root, "app_version", APP_VERSION);

    // Add general settings (not WiFi credentials)
    cJSON_AddNumberToObject(root, "refresh_interval_sec", state->settings.refresh_interval_sec);
    cJSON_AddNumberToObject(root, "screensaver_timeout_sec", state->settings.screensaver_timeout_sec);
    cJSON_AddNumberToObject(root, "active_server_index", state->settings.active_server_index);

    // Add servers array
    cJSON *servers = cJSON_CreateArray();
    if (!servers) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < state->settings.server_count; i++) {
        server_config_t *srv = &state->settings.servers[i];

        cJSON *server = cJSON_CreateObject();
        if (!server) continue;

        cJSON_AddStringToObject(server, "server_id", srv->server_id);
        cJSON_AddStringToObject(server, "display_name", srv->display_name);
        cJSON_AddStringToObject(server, "map_name", srv->map_name);
        cJSON_AddStringToObject(server, "ip_address", srv->ip_address);
        cJSON_AddNumberToObject(server, "port", srv->port);
        cJSON_AddNumberToObject(server, "max_players", srv->max_players);
        cJSON_AddNumberToObject(server, "alert_threshold", srv->alert_threshold);
        cJSON_AddBoolToObject(server, "alerts_enabled", srv->alerts_enabled);

        // Restart schedule
        cJSON *restart = cJSON_CreateObject();
        if (restart) {
            cJSON_AddNumberToObject(restart, "hour", srv->restart_hour);
            cJSON_AddNumberToObject(restart, "minute", srv->restart_minute);
            cJSON_AddNumberToObject(restart, "interval_hours", srv->restart_interval_hours);
            cJSON_AddBoolToObject(restart, "manual_set", srv->manual_restart_set);
            cJSON_AddItemToObject(server, "restart_schedule", restart);
        }

        cJSON_AddItemToArray(servers, server);
    }

    cJSON_AddItemToObject(root, "servers", servers);

    // Write to file
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return ESP_ERR_NO_MEM;
    }

    FILE *f = fopen(CONFIG_JSON_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", CONFIG_JSON_FILE);
        free(json_str);
        return ESP_FAIL;
    }

    fputs(json_str, f);
    fclose(f);
    free(json_str);

    ESP_LOGI(TAG, "Settings exported to %s", CONFIG_JSON_FILE);
    return ESP_OK;
}

esp_err_t settings_import_from_json(void) {
    if (!sd_card_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    // Check if file exists
    struct stat st;
    if (stat(CONFIG_JSON_FILE, &st) != 0) {
        ESP_LOGI(TAG, "No config file found at %s", CONFIG_JSON_FILE);
        return ESP_ERR_NOT_FOUND;
    }

    FILE *f = fopen(CONFIG_JSON_FILE, "r");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }

    // Read file contents
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 32768) {  // Max 32KB config
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    char *json_str = malloc(fsize + 1);
    if (!json_str) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read_len = fread(json_str, 1, fsize, f);
    fclose(f);

    json_str[read_len] = '\0';

    // Parse JSON
    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (!root) {
        ESP_LOGE(TAG, "Failed to parse config JSON");
        return ESP_ERR_INVALID_ARG;
    }

    app_state_t *state = app_state_get();

    if (!app_state_lock(100)) {
        cJSON_Delete(root);
        return ESP_ERR_TIMEOUT;
    }

    // Load general settings
    cJSON *item = cJSON_GetObjectItem(root, "refresh_interval_sec");
    if (item && cJSON_IsNumber(item)) {
        state->settings.refresh_interval_sec = (uint16_t)item->valueint;
    }

    item = cJSON_GetObjectItem(root, "screensaver_timeout_sec");
    if (item && cJSON_IsNumber(item)) {
        state->settings.screensaver_timeout_sec = (uint16_t)item->valueint;
    }

    // Load servers array
    cJSON *servers = cJSON_GetObjectItem(root, "servers");
    if (servers && cJSON_IsArray(servers)) {
        int server_count = cJSON_GetArraySize(servers);
        if (server_count > MAX_SERVERS) server_count = MAX_SERVERS;

        state->settings.server_count = server_count;

        for (int i = 0; i < server_count; i++) {
            cJSON *server = cJSON_GetArrayItem(servers, i);
            if (!server) continue;

            server_config_t *srv = &state->settings.servers[i];
            memset(srv, 0, sizeof(server_config_t));

            item = cJSON_GetObjectItem(server, "server_id");
            if (item && cJSON_IsString(item)) {
                strncpy(srv->server_id, item->valuestring, sizeof(srv->server_id) - 1);
            }

            item = cJSON_GetObjectItem(server, "display_name");
            if (item && cJSON_IsString(item)) {
                strncpy(srv->display_name, item->valuestring, sizeof(srv->display_name) - 1);
            }

            item = cJSON_GetObjectItem(server, "map_name");
            if (item && cJSON_IsString(item)) {
                strncpy(srv->map_name, item->valuestring, sizeof(srv->map_name) - 1);
            }

            item = cJSON_GetObjectItem(server, "ip_address");
            if (item && cJSON_IsString(item)) {
                strncpy(srv->ip_address, item->valuestring, sizeof(srv->ip_address) - 1);
            }

            item = cJSON_GetObjectItem(server, "port");
            if (item && cJSON_IsNumber(item)) {
                srv->port = (uint16_t)item->valueint;
            }

            item = cJSON_GetObjectItem(server, "max_players");
            if (item && cJSON_IsNumber(item)) {
                srv->max_players = (uint16_t)item->valueint;
            } else {
                srv->max_players = DEFAULT_MAX_PLAYERS;
            }

            item = cJSON_GetObjectItem(server, "alert_threshold");
            if (item && cJSON_IsNumber(item)) {
                srv->alert_threshold = (uint16_t)item->valueint;
            }

            item = cJSON_GetObjectItem(server, "alerts_enabled");
            if (item && cJSON_IsBool(item)) {
                srv->alerts_enabled = cJSON_IsTrue(item);
            }

            // Load restart schedule
            cJSON *restart = cJSON_GetObjectItem(server, "restart_schedule");
            if (restart) {
                item = cJSON_GetObjectItem(restart, "hour");
                if (item && cJSON_IsNumber(item)) srv->restart_hour = (uint8_t)item->valueint;

                item = cJSON_GetObjectItem(restart, "minute");
                if (item && cJSON_IsNumber(item)) srv->restart_minute = (uint8_t)item->valueint;

                item = cJSON_GetObjectItem(restart, "interval_hours");
                if (item && cJSON_IsNumber(item)) srv->restart_interval_hours = (uint8_t)item->valueint;

                item = cJSON_GetObjectItem(restart, "manual_set");
                if (item && cJSON_IsBool(item)) srv->manual_restart_set = cJSON_IsTrue(item);
            }

            srv->active = true;
            srv->restart_history.last_known_players = -1;
        }

        // Set active server index
        item = cJSON_GetObjectItem(root, "active_server_index");
        if (item && cJSON_IsNumber(item)) {
            int idx = item->valueint;
            if (idx >= 0 && idx < server_count) {
                state->settings.active_server_index = (uint8_t)idx;
            }
        }
    }

    app_state_unlock();
    cJSON_Delete(root);

    // Save to NVS
    settings_save();

    ESP_LOGI(TAG, "Settings imported from %s (%d servers)",
             CONFIG_JSON_FILE, state->settings.server_count);
    return ESP_OK;
}
