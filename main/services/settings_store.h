/**
 * DayZ Server Tracker - Settings Store
 * NVS-based persistent settings storage
 */

#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include "app_state.h"
#include "esp_err.h"

/**
 * Load all settings from NVS into app_state
 * Sets defaults if no settings found
 * @return ESP_OK on success
 */
esp_err_t settings_load(void);

/**
 * Save all settings from app_state to NVS
 * @return ESP_OK on success
 */
esp_err_t settings_save(void);

/**
 * Save WiFi credentials
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @return ESP_OK on success
 */
esp_err_t settings_save_wifi(const char *ssid, const char *password);

/**
 * Save refresh interval setting
 * @param interval_sec Refresh interval in seconds
 * @return ESP_OK on success
 */
esp_err_t settings_save_refresh_interval(uint16_t interval_sec);

/**
 * Add a new server configuration
 * @param server_id BattleMetrics server ID
 * @param display_name User-friendly name
 * @return Index of added server, or -1 on failure
 */
int settings_add_server(const char *server_id, const char *display_name);

/**
 * Delete a server by index
 * @param index Server index to delete
 * @return ESP_OK on success
 */
esp_err_t settings_delete_server(int index);

/**
 * Save restart schedule for a server
 * @param server_index Server index
 * @param hour Restart hour (0-23)
 * @param minute Restart minute (0-59)
 * @param interval_hours Interval in hours (4, 6, 8, or 12)
 * @param manual_enabled Whether manual schedule is enabled
 * @return ESP_OK on success
 */
esp_err_t settings_save_restart_schedule(int server_index, uint8_t hour,
                                          uint8_t minute, uint8_t interval_hours,
                                          bool manual_enabled);

/**
 * Initialize settings subsystem (call once at startup)
 */
void settings_init(void);

// ============== MULTI-WIFI API ==============

/**
 * Load all WiFi credentials from NVS into app_state.wifi_multi
 * Called automatically by settings_load()
 */
esp_err_t settings_load_wifi_credentials(void);

/**
 * Save all WiFi credentials from app_state.wifi_multi to NVS
 */
esp_err_t settings_save_wifi_credentials(void);

/**
 * Add or update a WiFi credential
 * If SSID already exists, updates the password. Otherwise adds new.
 * @return Index of credential, or -1 if full
 */
int settings_add_wifi_credential(const char *ssid, const char *password);

/**
 * Delete a WiFi credential by index
 */
esp_err_t settings_delete_wifi_credential(int index);

/**
 * Find a WiFi credential by SSID
 * @return Index of matching credential, or -1 if not found
 */
int settings_find_wifi_credential(const char *ssid);

// ============== JSON CONFIG EXPORT/IMPORT ==============

/**
 * Export server configurations to JSON file on SD card
 * Does NOT export WiFi credentials (security)
 * @return ESP_OK on success
 */
esp_err_t settings_export_to_json(void);

/**
 * Import server configurations from JSON file on SD card
 * Does NOT import WiFi credentials
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no file
 */
esp_err_t settings_import_from_json(void);

#endif // SETTINGS_STORE_H
