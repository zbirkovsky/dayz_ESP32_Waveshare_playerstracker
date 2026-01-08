/**
 * DayZ Server Tracker - WiFi Manager Implementation
 */

#include "wifi_manager.h"
#include "config.h"
#include "app_state.h"
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

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        app_state_set_wifi_connected(false);
        ESP_LOGI(TAG, "Disconnected, retrying...");
        esp_wifi_connect();
        if (s_wifi_event_group) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        app_state_set_wifi_connected(true);
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
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
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_initialized = true;
    ESP_LOGI(TAG, "WiFi init complete, connecting to %s", ssid);

    return ESP_OK;
}

esp_err_t wifi_manager_reconnect(const char *ssid, const char *password) {
    ESP_LOGI(TAG, "Reconnecting WiFi with new credentials...");

    esp_wifi_stop();

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

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
