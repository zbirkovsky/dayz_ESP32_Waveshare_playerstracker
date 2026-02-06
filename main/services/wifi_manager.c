/**
 * DayZ Server Tracker - WiFi Manager Implementation
 */

#include "wifi_manager.h"
#include "config.h"
#include "app_state.h"
#include "events.h"
#include "services/settings_store.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_log.h"

static const char *TAG = "wifi_manager";

// WiFi event group
static EventGroupHandle_t s_wifi_event_group = NULL;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static bool sntp_initialized = false;
static bool wifi_initialized = false;
static int s_reconnect_attempts = 0;
static bool s_auto_connect_in_progress = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        app_state_set_wifi_connected(false);

        if (s_wifi_event_group) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }

        // Don't auto-reconnect during auto_connect sequence (we manage that ourselves)
        if (s_auto_connect_in_progress) {
            return;
        }

        s_reconnect_attempts++;
        ESP_LOGI(TAG, "Disconnected (attempt %d/%d)", s_reconnect_attempts, WIFI_RECONNECT_SCAN_THRESHOLD);

        if (s_reconnect_attempts < WIFI_RECONNECT_SCAN_THRESHOLD) {
            // Fast retry same SSID for transient drops
            esp_wifi_connect();
        } else {
            // Too many failures - try auto-connect to other known networks
            ESP_LOGI(TAG, "Reconnect threshold reached, starting auto-connect...");
            wifi_manager_auto_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        // WiFi scan completed - process results
        app_state_t *state = app_state_get();
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);

        if (ap_count > WIFI_SCAN_MAX_RESULTS) {
            ap_count = WIFI_SCAN_MAX_RESULTS;
        }

        wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
        if (ap_records) {
            esp_wifi_scan_get_ap_records(&ap_count, ap_records);

            if (app_state_lock(100)) {
                state->wifi_multi.scan_count = 0;

                for (int i = 0; i < ap_count && state->wifi_multi.scan_count < WIFI_SCAN_MAX_RESULTS; i++) {
                    // Skip hidden/empty SSIDs
                    if (ap_records[i].ssid[0] == '\0') continue;

                    wifi_scan_result_t *r = &state->wifi_multi.scan_results[state->wifi_multi.scan_count];
                    strncpy(r->ssid, (char *)ap_records[i].ssid, sizeof(r->ssid) - 1);
                    r->ssid[sizeof(r->ssid) - 1] = '\0';
                    r->rssi = ap_records[i].rssi;
                    r->authmode = ap_records[i].authmode;

                    // Check if this is a known network
                    r->known = false;
                    r->cred_idx = 0;
                    for (int j = 0; j < state->wifi_multi.count; j++) {
                        if (strcmp(state->wifi_multi.credentials[j].ssid, r->ssid) == 0) {
                            r->known = true;
                            r->cred_idx = j;
                            break;
                        }
                    }

                    state->wifi_multi.scan_count++;
                }

                state->wifi_multi.scan_in_progress = false;
                app_state_unlock();
            }

            free(ap_records);
        }

        ESP_LOGI(TAG, "Scan complete: %d networks found", state->wifi_multi.scan_count);
        events_post_simple(EVT_WIFI_SCAN_COMPLETE);

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        app_state_set_wifi_connected(true);
        s_reconnect_attempts = 0;  // Reset on successful connect
        s_auto_connect_in_progress = false;

        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }

        // Update active_idx based on current SSID
        app_state_t *state = app_state_get();
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            int idx = settings_find_wifi_credential((char *)ap_info.ssid);
            if (idx >= 0 && app_state_lock(100)) {
                state->wifi_multi.active_idx = idx;
                app_state_unlock();
            }
        }

        // Initialize SNTP for time sync
        wifi_manager_init_sntp();
    }
}

esp_err_t wifi_manager_init(const char *ssid, const char *password) {
    if (wifi_initialized) {
        return wifi_manager_reconnect(ssid, password);
    }

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    // Use WPA/WPA2 mixed threshold for broader compatibility
    wifi_config.sta.threshold.authmode = (password && strlen(password) > 0)
        ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_initialized = true;
    ESP_LOGI(TAG, "WiFi init complete, connecting to %s", ssid);

    return ESP_OK;
}

esp_err_t wifi_manager_reconnect(const char *ssid, const char *password) {
    ESP_LOGI(TAG, "Reconnecting WiFi with new credentials...");

    s_reconnect_attempts = 0;
    s_auto_connect_in_progress = false;

    esp_wifi_disconnect();
    // Small delay to allow disconnect to complete
    vTaskDelay(pdMS_TO_TICKS(100));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = (password && strlen(password) > 0)
        ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect();

    return ESP_OK;
}

bool wifi_manager_is_connected(void) {
    return app_state_is_wifi_connected();
}

bool wifi_manager_wait_connected(uint32_t timeout_ms) {
    if (!s_wifi_event_group) return false;

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

void wifi_manager_stop(void) {
    if (wifi_initialized) {
        esp_wifi_stop();
        app_state_set_wifi_connected(false);
    }
}

void wifi_manager_init_sntp(void) {
    if (sntp_initialized) return;

    ESP_LOGI(TAG, "Initializing SNTP for CET timezone...");

    // Set timezone to CET (Central European Time) with DST
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    sntp_initialized = true;
    ESP_LOGI(TAG, "SNTP initialized");
}

bool wifi_manager_is_time_synced(void) {
    if (!sntp_initialized) return false;

    time_t now = 0;
    time(&now);
    // If time is > year 2020, we have synced
    return now > 1577836800;  // 2020-01-01 00:00:00 UTC
}

int wifi_manager_get_rssi(void) {
    if (!wifi_manager_is_connected()) return 0;

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

void wifi_manager_get_ip_str(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;

    if (!wifi_manager_is_connected()) {
        snprintf(buf, buf_size, "Not connected");
        return;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            snprintf(buf, buf_size, IPSTR, IP2STR(&ip_info.ip));
            return;
        }
    }
    snprintf(buf, buf_size, "Unknown");
}

void wifi_manager_get_mac_str(char *buf, size_t buf_size) {
    if (!buf || buf_size < 18) return;

    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(buf, buf_size, "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        snprintf(buf, buf_size, "Unknown");
    }
}

void wifi_manager_get_ssid(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;

    if (!wifi_manager_is_connected()) {
        snprintf(buf, buf_size, "Not connected");
        return;
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        snprintf(buf, buf_size, "%s", ap_info.ssid);
    } else {
        snprintf(buf, buf_size, "Unknown");
    }
}

// ============== MULTI-WIFI API ==============

esp_err_t wifi_manager_start_scan(void) {
    if (!wifi_initialized) return ESP_ERR_INVALID_STATE;

    app_state_t *state = app_state_get();
    if (app_state_lock(100)) {
        state->wifi_multi.scan_in_progress = true;
        app_state_unlock();
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, false);  // Non-blocking
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
        if (app_state_lock(100)) {
            state->wifi_multi.scan_in_progress = false;
            app_state_unlock();
        }
    } else {
        ESP_LOGI(TAG, "WiFi scan started");
    }

    return err;
}

int wifi_manager_get_scan_results(wifi_scan_result_t *out, int max) {
    if (!out || max <= 0) return 0;

    app_state_t *state = app_state_get();
    int count = 0;

    if (app_state_lock(100)) {
        count = state->wifi_multi.scan_count;
        if (count > max) count = max;
        memcpy(out, state->wifi_multi.scan_results, count * sizeof(wifi_scan_result_t));
        app_state_unlock();
    }

    return count;
}

esp_err_t wifi_manager_connect_index(int credential_idx) {
    app_state_t *state = app_state_get();

    if (!app_state_lock(100)) return ESP_ERR_TIMEOUT;

    if (credential_idx < 0 || credential_idx >= state->wifi_multi.count) {
        app_state_unlock();
        return ESP_ERR_INVALID_ARG;
    }

    char ssid[33];
    char password[65];
    strncpy(ssid, state->wifi_multi.credentials[credential_idx].ssid, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    strncpy(password, state->wifi_multi.credentials[credential_idx].password, sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';

    app_state_unlock();

    ESP_LOGI(TAG, "Connecting to credential #%d: %s", credential_idx, ssid);
    return wifi_manager_reconnect(ssid, password);
}

esp_err_t wifi_manager_auto_connect(void) {
    app_state_t *state = app_state_get();

    if (state->wifi_multi.count == 0) {
        ESP_LOGW(TAG, "No saved WiFi credentials for auto-connect");
        return ESP_ERR_NOT_FOUND;
    }

    s_auto_connect_in_progress = true;
    ESP_LOGI(TAG, "Auto-connect: scanning for known networks...");

    // Start a blocking scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    // Disconnect first to allow scanning
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);  // Blocking scan
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Auto-connect scan failed: %s", esp_err_to_name(err));
        s_auto_connect_in_progress = false;
        // Fall back to reconnecting with first credential
        if (state->wifi_multi.count > 0) {
            wifi_manager_connect_index(0);
        }
        return err;
    }

    // Get scan results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        ESP_LOGW(TAG, "Auto-connect: no networks found");
        s_auto_connect_in_progress = false;
        return ESP_ERR_NOT_FOUND;
    }

    if (ap_count > WIFI_SCAN_MAX_RESULTS) {
        ap_count = WIFI_SCAN_MAX_RESULTS;
    }

    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_records) {
        s_auto_connect_in_progress = false;
        return ESP_ERR_NO_MEM;
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    // Build list of known networks sorted by RSSI (strongest first, already sorted by esp_wifi)
    typedef struct { int cred_idx; int8_t rssi; } known_ap_t;
    known_ap_t known_aps[MAX_WIFI_CREDENTIALS];
    int known_count = 0;

    if (app_state_lock(100)) {
        for (int i = 0; i < ap_count; i++) {
            if (ap_records[i].ssid[0] == '\0') continue;

            for (int j = 0; j < state->wifi_multi.count; j++) {
                if (strcmp(state->wifi_multi.credentials[j].ssid, (char *)ap_records[i].ssid) == 0) {
                    known_aps[known_count].cred_idx = j;
                    known_aps[known_count].rssi = ap_records[i].rssi;
                    known_count++;
                    break;
                }
            }
            if (known_count >= MAX_WIFI_CREDENTIALS) break;
        }
        app_state_unlock();
    }

    free(ap_records);

    if (known_count == 0) {
        ESP_LOGW(TAG, "Auto-connect: no known networks in range");
        s_auto_connect_in_progress = false;
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Auto-connect: found %d known networks, trying strongest first", known_count);

    // Try each known network in RSSI order (already sorted by scan)
    for (int i = 0; i < known_count; i++) {
        int idx = known_aps[i].cred_idx;
        ESP_LOGI(TAG, "Auto-connect: trying credential #%d (RSSI: %d)", idx, known_aps[i].rssi);

        wifi_manager_connect_index(idx);

        // Wait for connection
        if (wifi_manager_wait_connected(WIFI_CONNECT_TIMEOUT_MS)) {
            ESP_LOGI(TAG, "Auto-connect: connected to credential #%d", idx);
            s_auto_connect_in_progress = false;
            s_reconnect_attempts = 0;
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "Auto-connect: all known networks failed");
    s_auto_connect_in_progress = false;
    s_reconnect_attempts = 0;

    // Retry with first credential as fallback
    if (state->wifi_multi.count > 0) {
        wifi_manager_connect_index(0);
    }

    return ESP_FAIL;
}
