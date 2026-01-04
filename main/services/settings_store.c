/**
 * DayZ Server Tracker - Settings Store Implementation
 */

#include "settings_store.h"
#include "../config.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

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

    // Load server count and active index
    uint8_t count = 0;
    nvs_get_u8(nvs, "server_count", &count);
    state->settings.server_count = count > MAX_SERVERS ? MAX_SERVERS : count;
    nvs_get_u8(nvs, "active_srv", &state->settings.active_server_index);

    // Load each server
    for (int i = 0; i < state->settings.server_count; i++) {
        char key[16];
        server_config_t *srv = &state->settings.servers[i];

        snprintf(key, sizeof(key), "srv%d_id", i);
        len = sizeof(srv->server_id);
        nvs_get_str(nvs, key, srv->server_id, &len);

        snprintf(key, sizeof(key), "srv%d_name", i);
        len = sizeof(srv->display_name);
        nvs_get_str(nvs, key, srv->display_name, &len);

        snprintf(key, sizeof(key), "srv%d_ip", i);
        len = sizeof(srv->ip_address);
        nvs_get_str(nvs, key, srv->ip_address, &len);

        snprintf(key, sizeof(key), "srv%d_port", i);
        nvs_get_u16(nvs, key, &srv->port);

        snprintf(key, sizeof(key), "srv%d_max", i);
        nvs_get_u16(nvs, key, &srv->max_players);
        if (srv->max_players == 0) srv->max_players = DEFAULT_MAX_PLAYERS;

        snprintf(key, sizeof(key), "srv%d_alert", i);
        nvs_get_u16(nvs, key, &srv->alert_threshold);

        uint8_t alerts_en = 0;
        snprintf(key, sizeof(key), "srv%d_alen", i);
        nvs_get_u8(nvs, key, &alerts_en);
        srv->alerts_enabled = alerts_en > 0;

        // Load restart history
        snprintf(key, sizeof(key), "srv%d_rcnt", i);
        nvs_get_u8(nvs, key, &srv->restart_history.restart_count);

        snprintf(key, sizeof(key), "srv%d_ravg", i);
        nvs_get_u32(nvs, key, &srv->restart_history.avg_interval_sec);

        snprintf(key, sizeof(key), "srv%d_rlast", i);
        nvs_get_u32(nvs, key, &srv->restart_history.last_restart_time);

        snprintf(key, sizeof(key), "srv%d_rtimes", i);
        len = sizeof(srv->restart_history.restart_times);
        nvs_get_blob(nvs, key, srv->restart_history.restart_times, &len);

        srv->restart_history.last_known_players = -1;

        // Load manual restart schedule
        snprintf(key, sizeof(key), "srv%d_rhr", i);
        nvs_get_u8(nvs, key, &srv->restart_hour);

        snprintf(key, sizeof(key), "srv%d_rmin", i);
        nvs_get_u8(nvs, key, &srv->restart_minute);

        snprintf(key, sizeof(key), "srv%d_rint", i);
        nvs_get_u8(nvs, key, &srv->restart_interval_hours);

        uint8_t manual_set = 0;
        snprintf(key, sizeof(key), "srv%d_rman", i);
        nvs_get_u8(nvs, key, &manual_set);
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

    // Save server count and active index
    nvs_set_u8(nvs, "server_count", state->settings.server_count);
    nvs_set_u8(nvs, "active_srv", state->settings.active_server_index);

    // Save each server
    for (int i = 0; i < state->settings.server_count; i++) {
        char key[16];
        server_config_t *srv = &state->settings.servers[i];

        snprintf(key, sizeof(key), "srv%d_id", i);
        nvs_set_str(nvs, key, srv->server_id);

        snprintf(key, sizeof(key), "srv%d_name", i);
        nvs_set_str(nvs, key, srv->display_name);

        snprintf(key, sizeof(key), "srv%d_ip", i);
        nvs_set_str(nvs, key, srv->ip_address);

        snprintf(key, sizeof(key), "srv%d_port", i);
        nvs_set_u16(nvs, key, srv->port);

        snprintf(key, sizeof(key), "srv%d_max", i);
        nvs_set_u16(nvs, key, srv->max_players);

        snprintf(key, sizeof(key), "srv%d_alert", i);
        nvs_set_u16(nvs, key, srv->alert_threshold);

        snprintf(key, sizeof(key), "srv%d_alen", i);
        nvs_set_u8(nvs, key, srv->alerts_enabled ? 1 : 0);

        // Save restart history
        snprintf(key, sizeof(key), "srv%d_rcnt", i);
        nvs_set_u8(nvs, key, srv->restart_history.restart_count);

        snprintf(key, sizeof(key), "srv%d_ravg", i);
        nvs_set_u32(nvs, key, srv->restart_history.avg_interval_sec);

        snprintf(key, sizeof(key), "srv%d_rlast", i);
        nvs_set_u32(nvs, key, srv->restart_history.last_restart_time);

        snprintf(key, sizeof(key), "srv%d_rtimes", i);
        nvs_set_blob(nvs, key, srv->restart_history.restart_times,
                     sizeof(srv->restart_history.restart_times));

        // Save manual restart schedule
        snprintf(key, sizeof(key), "srv%d_rhr", i);
        nvs_set_u8(nvs, key, srv->restart_hour);

        snprintf(key, sizeof(key), "srv%d_rmin", i);
        nvs_set_u8(nvs, key, srv->restart_minute);

        snprintf(key, sizeof(key), "srv%d_rint", i);
        nvs_set_u8(nvs, key, srv->restart_interval_hours);

        snprintf(key, sizeof(key), "srv%d_rman", i);
        nvs_set_u8(nvs, key, srv->manual_restart_set ? 1 : 0);
    }

    app_state_unlock();

    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Settings saved");
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
