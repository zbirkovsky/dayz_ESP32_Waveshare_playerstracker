/**
 * DayZ Server Tracker - Application Initialization
 * Handles hardware and system initialization before UI creation
 */

#include "app_init.h"
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "config.h"
#include "app_state.h"
#include "events.h"
#include "drivers/buzzer.h"
#include "drivers/sd_card.h"
#include "drivers/display.h"
#include "drivers/io_expander.h"
#include "drivers/usb_msc.h"
#include "services/wifi_manager.h"
#include "services/battlemetrics.h"
#include "services/settings_store.h"
#include "services/history_store.h"
#include "services/secondary_fetch.h"
#include "services/restart_manager.h"
#include "ui/ui_styles.h"

static const char *TAG = "app_init";

bool app_init_system(void) {
    ESP_LOGI(TAG, "%s v%s Starting...", APP_NAME, APP_VERSION);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Check if screen is being touched during boot for USB storage mode
    if (usb_msc_touch_detected()) {
        ESP_LOGI(TAG, "Touch detected on boot - entering USB Mass Storage mode");
        if (usb_msc_init() == ESP_OK) {
            usb_msc_task();  // This function never returns
        } else {
            ESP_LOGE(TAG, "USB MSC init failed, continuing with normal boot");
        }
        return true;  // USB mode was attempted
    }

    // Initialize application state
    app_state_init();

    // Initialize event system
    events_init();

    // Load settings
    settings_load();

    // Initialize buzzer and test
    buzzer_init();
    buzzer_test();

    // Initialize history
    history_init();

    return false;  // Normal boot continues
}

lv_display_t* app_init_display(void) {
    // Initialize display with LVGL and touch (includes I2C, CH422G, GT911 reset)
    lv_display_t *disp = display_init();
    if (!disp) {
        ESP_LOGE(TAG, "Display initialization failed!");
        return NULL;
    }

    // Verify backlight control works
    ESP_LOGW(TAG, "Running backlight test - watch for screen flicker over next 4 seconds...");
    io_expander_test_backlight();

    // Initialize UI styles
    ui_styles_init();

    // Initialize SD card and load history for active server
    app_state_t *state = app_state_get();
    int active_srv = state->settings.active_server_index;
    if (sd_card_init() == ESP_OK) {
        // Load JSON history (primary source with full 7-day data)
        history_load_json_for_server(active_srv);
    }
    // Fallback to NVS if no JSON data loaded
    if (history_get_count() == 0) {
        history_load_from_nvs(active_srv);
    }

    return disp;
}

void app_init_network(void) {
    app_state_t *state = app_state_get();

    // Handle first boot defaults
    if (state->settings.first_boot || (strlen(state->settings.wifi_ssid) == 0 && state->wifi_multi.count == 0)) {
        ESP_LOGI(TAG, "First boot detected");
        // Default credentials for backward compatibility
        strncpy(state->settings.wifi_ssid, "meshnetwork2131", sizeof(state->settings.wifi_ssid) - 1);
        state->settings.wifi_ssid[sizeof(state->settings.wifi_ssid) - 1] = '\0';
        strncpy(state->settings.wifi_password, "9696Polikut.", sizeof(state->settings.wifi_password) - 1);
        state->settings.wifi_password[sizeof(state->settings.wifi_password) - 1] = '\0';
        state->settings.first_boot = false;
        // Add to multi-WiFi
        settings_add_wifi_credential(state->settings.wifi_ssid, state->settings.wifi_password);
        settings_add_wifi_credential("WiFimodem-B4200", "xtMP9NQPBCcmBpdB");
        settings_save_wifi_credentials();
        settings_save();
    }

    // Ensure second WiFi credential exists (added post-deployment)
    if (settings_find_wifi_credential("WiFimodem-B4200") < 0) {
        settings_add_wifi_credential("WiFimodem-B4200", "xtMP9NQPBCcmBpdB");
        settings_save_wifi_credentials();
    }

    // Initialize BattleMetrics API client
    battlemetrics_init();

    // Determine initial WiFi credentials to connect with
    const char *init_ssid = state->settings.wifi_ssid;
    const char *init_pass = state->settings.wifi_password;

    // Prefer first multi-WiFi credential if available
    if (state->wifi_multi.count > 0) {
        init_ssid = state->wifi_multi.credentials[0].ssid;
        init_pass = state->wifi_multi.credentials[0].password;
    }

    // Initialize WiFi
    wifi_manager_init(init_ssid, init_pass);

    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi...");
    if (wifi_manager_wait_connected(WIFI_CONNECT_TIMEOUT_MS)) {
        ESP_LOGI(TAG, "WiFi connected!");

        // Wait for SNTP time sync (important for history timestamps)
        ESP_LOGI(TAG, "Waiting for time sync...");
        int sntp_retries = 0;
        while (!wifi_manager_is_time_synced() && sntp_retries < 50) {  // Max 5 seconds
            vTaskDelay(pdMS_TO_TICKS(100));
            sntp_retries++;
        }
        if (wifi_manager_is_time_synced()) {
            time_t now;
            time(&now);
            ESP_LOGI(TAG, "Time synced: %lu", (unsigned long)now);

            // Reload JSON history with correct time
            if (sd_card_is_mounted()) {
                ESP_LOGI(TAG, "Reloading JSON history with correct time...");
                history_load_json_for_server(state->settings.active_server_index);
                ESP_LOGI(TAG, "History reloaded: %d entries", history_get_count());
            }

            // Check if restart data is stale and reset if needed
            server_config_t *srv = app_state_get_active_server();
            if (srv) {
                restart_check_stale_and_reset(srv);
            }
        } else {
            ESP_LOGW(TAG, "Time sync timeout, timestamps may be wrong");
        }
    } else {
        ESP_LOGW(TAG, "WiFi connection timeout, trying auto-connect...");
        if (state->wifi_multi.count > 1) {
            wifi_manager_auto_connect();
        }
    }

    // Start secondary server fetch background task
    secondary_fetch_init();
    secondary_fetch_start();
}
