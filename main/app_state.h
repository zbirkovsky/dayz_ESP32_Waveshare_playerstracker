/**
 * DayZ Server Tracker - Application State
 * Centralized state management with thread-safe access
 */

#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ============== DATA STRUCTURES ==============

// Restart tracking per server
typedef struct {
    uint32_t restart_times[MAX_RESTART_HISTORY];    // Unix timestamps of detected restarts
    uint8_t restart_count;                          // Number of recorded restarts
    uint32_t avg_interval_sec;                      // Calculated average interval between restarts
    uint32_t last_restart_time;                     // Most recent restart timestamp
    int16_t last_known_players;                     // Player count before restart (for detection)
} restart_history_t;

// Server configuration
typedef struct {
    char server_id[32];             // BattleMetrics server ID
    char display_name[64];          // User-friendly name
    char map_name[32];              // Map name (user-selected)
    char ip_address[32];            // Server IP
    uint16_t port;                  // Server port
    uint16_t max_players;           // Max player capacity
    uint16_t alert_threshold;       // Alert when players >= this
    bool alerts_enabled;
    bool active;                    // Is this slot in use?
    restart_history_t restart_history;  // Restart pattern tracking

    // Manual restart schedule (CET timezone)
    uint8_t restart_hour;           // Known restart hour (0-23 CET)
    uint8_t restart_minute;         // Known restart minute (0-59)
    uint8_t restart_interval_hours; // Interval: 4, 6, 8, or 12 hours
    bool manual_restart_set;        // True if user manually set restart time
} server_config_t;

// Player history entry
typedef struct {
    uint32_t timestamp;             // Unix timestamp
    int16_t player_count;           // Player count (-1 if unknown)
} history_entry_t;

// Trend tracking data (ring buffer for ~2 hour trend)
typedef struct {
    int16_t player_counts[TREND_HISTORY_SIZE];
    uint32_t timestamps[TREND_HISTORY_SIZE];
    uint8_t head;
    uint8_t count;
    int16_t cached_delta;   // Pre-calculated trend direction
} trend_data_t;

// Secondary server runtime status
typedef struct {
    int player_count;               // Current players (-1 = unknown)
    int max_players;                // Max capacity
    char server_time[16];           // In-game time
    char map_name[32];              // Map name (e.g., "chernarusplus")
    bool is_daytime;                // Day/night indicator
    bool valid;                     // True if data is valid
    bool fetch_pending;             // True if fetch in progress
    int64_t last_update_time;       // Timestamp of last fetch (ms)
    trend_data_t trend;             // Trend tracking data
} secondary_server_status_t;

// History file header structure
typedef struct {
    uint32_t magic;                 // HISTORY_FILE_MAGIC
    uint16_t head;                  // history_head
    uint16_t count;                 // history_count
} history_file_header_t;

// WiFi credential pair
typedef struct {
    char ssid[33];
    char password[65];
} wifi_credential_t;

// WiFi scan result entry
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;    // wifi_auth_mode_t
    bool known;          // has saved credentials
    uint8_t cred_idx;    // index if known
} wifi_scan_result_t;

// Multi-WiFi state
typedef struct {
    wifi_credential_t credentials[MAX_WIFI_CREDENTIALS];
    uint8_t count;
    int8_t active_idx;              // currently connected credential (-1=none)
    wifi_scan_result_t scan_results[WIFI_SCAN_MAX_RESULTS];
    uint8_t scan_count;
    bool scan_in_progress;
} wifi_multi_state_t;

// Application settings (persisted to NVS)
typedef struct {
    char wifi_ssid[33];             // Legacy single credential (for migration)
    char wifi_password[65];         // Legacy single credential (for migration)
    uint16_t refresh_interval_sec;      // 10-300 seconds
    uint16_t screensaver_timeout_sec;   // 0=disabled, or 300/600/900/1800/3600/5400/7200/14400
    uint8_t active_server_index;
    uint8_t server_count;
    server_config_t servers[MAX_SERVERS];
    bool first_boot;
} app_settings_t;

// Screen IDs
typedef enum {
    SCREEN_MAIN = 0,
    SCREEN_SETTINGS,
    SCREEN_WIFI_SETTINGS,
    SCREEN_SERVER_SETTINGS,
    SCREEN_ADD_SERVER,
    SCREEN_HISTORY,
    SCREEN_HEATMAP,
    SCREEN_ALERTS,
    SCREEN_SCREENSAVER
} screen_id_t;

// History time range selection
typedef enum {
    HISTORY_RANGE_1H = 0,       // 1 hour
    HISTORY_RANGE_8H,           // 8 hours
    HISTORY_RANGE_24H,          // 24 hours
    HISTORY_RANGE_WEEK          // 7 days
} history_range_t;

// Connection health tracking
typedef struct {
    int consecutive_failures;
    int64_t last_success_time;
    char last_error[64];
} connection_health_t;

// Runtime state (volatile, not persisted)
typedef struct {
    int current_players;
    int max_players;
    char last_update[32];
    char server_time[16];           // In-game server time (e.g., "10:12")
    char map_name[32];              // Map name (e.g., "chernarusplus")
    bool is_daytime;                // true = day, false = night
    bool wifi_connected;
    connection_health_t connection;
    int server_rank;                    // Server rank from BattleMetrics (0 = unranked)

    // Main server trend tracking
    trend_data_t main_trend;

    // Multi-server watch data
    secondary_server_status_t secondary[MAX_SECONDARY_SERVERS];
    uint8_t secondary_server_indices[MAX_SECONDARY_SERVERS];  // Which servers are shown
    uint8_t secondary_count;                                   // How many secondary slots filled
} runtime_state_t;

// UI state
typedef struct {
    screen_id_t current_screen;
    history_range_t current_history_range;
    volatile bool alert_active;
    int64_t alert_start_time;
    bool screensaver_active;            // true = backlight off (screen saving)
    int64_t last_activity_time;         // Timestamp of last touch event
    int64_t long_press_start_time;      // When touch press started (for 5s screen-off)
    bool long_press_tracking;           // true = touch is currently held down
} ui_state_t;

// History state
typedef struct {
    history_entry_t *entries;       // PSRAM allocated buffer
    uint16_t head;
    uint16_t count;
    int unsaved_count;              // Track new entries since last save
} history_state_t;

// Centralized application state
typedef struct {
    app_settings_t settings;        // Persisted settings
    runtime_state_t runtime;        // Runtime volatile state
    ui_state_t ui;                  // UI state
    history_state_t history;        // Player history
    wifi_multi_state_t wifi_multi;  // Multi-WiFi credentials & scan state
    SemaphoreHandle_t mutex;        // Thread safety mutex
} app_state_t;

// ============== PUBLIC API ==============

/**
 * Initialize the application state
 * Must be called before any other app_state functions
 */
void app_state_init(void);

/**
 * Get pointer to the global application state
 * WARNING: Use app_state_lock/unlock for thread-safe access
 */
app_state_t* app_state_get(void);

/**
 * Lock the state mutex for thread-safe access
 * @param timeout_ms Maximum time to wait for lock
 * @return true if lock acquired, false on timeout
 */
bool app_state_lock(uint32_t timeout_ms);

/**
 * Unlock the state mutex
 */
void app_state_unlock(void);

/**
 * Get the currently active server config
 * @return Pointer to active server, or NULL if no servers
 */
server_config_t* app_state_get_active_server(void);

/**
 * Check if WiFi is connected (thread-safe read)
 */
bool app_state_is_wifi_connected(void);

/**
 * Set WiFi connection status (thread-safe write)
 */
void app_state_set_wifi_connected(bool connected);

/**
 * Update player count and related data (thread-safe)
 */
void app_state_update_player_data(int players, int max_players,
                                   const char *server_time, bool is_daytime,
                                   const char *map_name);

/**
 * Get current screen ID (thread-safe)
 */
screen_id_t app_state_get_current_screen(void);

/**
 * Set current screen ID (thread-safe)
 */
void app_state_set_current_screen(screen_id_t screen);

// ============== MULTI-SERVER WATCH API ==============

/**
 * Recalculate which servers are shown as secondary
 * Call after changing active server or server list
 */
void app_state_update_secondary_indices(void);

/**
 * Update secondary server status from API response
 * @param slot Secondary slot index (0-2)
 * @param players Player count
 * @param max_players Max capacity
 * @param server_time In-game time string
 * @param is_daytime Day/night indicator
 * @param map_name Map name (e.g., "chernarusplus")
 */
void app_state_update_secondary_status(int slot, int players, int max_players,
                                        const char *server_time, bool is_daytime,
                                        const char *map_name);

/**
 * Add a trend data point for a secondary server
 * @param slot Secondary slot index (0-2)
 * @param player_count Current player count
 */
void app_state_add_trend_point(int slot, int player_count);

/**
 * Calculate trend delta over ~2 hours for a secondary server
 * @param slot Secondary slot index (0-2)
 * @return Player count change (positive=joining, negative=leaving)
 */
int app_state_calculate_trend(int slot);

/**
 * Add a trend data point for the main (active) server
 * @param player_count Current player count
 */
void app_state_add_main_trend_point(int player_count);

/**
 * Calculate trend delta over ~2 hours for the main server
 * @return Player count change (positive=joining, negative=leaving)
 */
int app_state_calculate_main_trend(void);

/**
 * Get the number of trend data points collected for main server
 * @return Number of data points (0 if no data)
 */
int app_state_get_main_trend_count(void);

/**
 * Get cached trend delta for a secondary server (O(1), no mutex)
 * @param slot Secondary slot index (0-2)
 * @return Cached player count change
 */
int app_state_get_cached_trend(int slot);

/**
 * Get cached trend delta for the main server (O(1), no mutex)
 * @return Cached player count change
 */
int app_state_get_cached_main_trend(void);

/**
 * Clear main server trend data (call when switching active server)
 */
void app_state_clear_main_trend(void);

/**
 * Clear secondary server data (call when switching active server)
 */
void app_state_clear_secondary_data(void);

#endif // APP_STATE_H
