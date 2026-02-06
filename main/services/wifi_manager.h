/**
 * DayZ Server Tracker - WiFi Manager
 * Handles WiFi connection and SNTP time synchronization
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"
#include "app_state.h"

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

// ============== MULTI-WIFI API ==============

/**
 * Start a WiFi scan for nearby access points
 * Results will be posted as EVT_WIFI_SCAN_COMPLETE event
 */
esp_err_t wifi_manager_start_scan(void);

/**
 * Get scan results (call after EVT_WIFI_SCAN_COMPLETE)
 * @param out Output array of scan results
 * @param max Maximum number of results to return
 * @return Number of results written
 */
int wifi_manager_get_scan_results(wifi_scan_result_t *out, int max);

/**
 * Connect to a specific saved credential by index
 * @param credential_idx Index in wifi_multi.credentials
 */
esp_err_t wifi_manager_connect_index(int credential_idx);

/**
 * Auto-connect: scan for networks and connect to strongest known one
 */
esp_err_t wifi_manager_auto_connect(void);

#endif // WIFI_MANAGER_H
