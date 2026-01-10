/**
 * DayZ Server Tracker - WiFi Manager
 * Handles WiFi connection and SNTP time synchronization
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * Initialize WiFi subsystem and start connection
 * @param ssid WiFi network SSID
 * @param password WiFi password
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(const char *ssid, const char *password);

/**
 * Reconnect WiFi with new credentials
 * @param ssid New WiFi network SSID
 * @param password New WiFi password
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_reconnect(const char *ssid, const char *password);

/**
 * Check if WiFi is connected
 */
bool wifi_manager_is_connected(void);

/**
 * Wait for WiFi connection with timeout
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return true if connected, false on timeout
 */
bool wifi_manager_wait_connected(uint32_t timeout_ms);

/**
 * Stop WiFi connection
 */
void wifi_manager_stop(void);

/**
 * Initialize SNTP for CET timezone
 */
void wifi_manager_init_sntp(void);

/**
 * Check if SNTP has synchronized time
 */
bool wifi_manager_is_time_synced(void);

/**
 * Get WiFi signal strength (RSSI)
 * @return RSSI in dBm, or 0 if not connected
 */
int wifi_manager_get_rssi(void);

/**
 * Get current IP address as string
 * @param buf Output buffer
 * @param buf_size Buffer size
 */
void wifi_manager_get_ip_str(char *buf, size_t buf_size);

/**
 * Get device MAC address as string
 * @param buf Output buffer (at least 18 bytes)
 * @param buf_size Buffer size
 */
void wifi_manager_get_mac_str(char *buf, size_t buf_size);

/**
 * Get connected SSID
 * @param buf Output buffer
 * @param buf_size Buffer size
 */
void wifi_manager_get_ssid(char *buf, size_t buf_size);

#endif // WIFI_MANAGER_H
