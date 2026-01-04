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

// History file header structure
typedef struct {
    uint32_t magic;                 // HISTORY_FILE_MAGIC
    uint16_t head;                  // history_head
    uint16_t count;                 // history_count
} history_file_header_t;

// Application settings (persisted to NVS)
typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    uint16_t refresh_interval_sec;  // 10-300 seconds
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
    SCREEN_ALERTS
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
    bool is_daytime;                // true = day, false = night
    bool wifi_connected;
    volatile bool refresh_requested;
    connection_health_t connection;
} runtime_state_t;

// UI state
typedef struct {
    screen_id_t current_screen;
    history_range_t current_history_range;
    volatile bool alert_active;
    int64_t alert_start_time;
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
 * Request a data refresh (thread-safe)
 */
void app_state_request_refresh(void);

/**
 * Check and clear refresh request (thread-safe)
 * @return true if refresh was requested
 */
bool app_state_consume_refresh_request(void);

/**
 * Update player count and related data (thread-safe)
 */
void app_state_update_player_data(int players, int max_players,
                                   const char *server_time, bool is_daytime);

/**
 * Get current screen ID (thread-safe)
 */
screen_id_t app_state_get_current_screen(void);

/**
 * Set current screen ID (thread-safe)
 */
void app_state_set_current_screen(screen_id_t screen);

#endif // APP_STATE_H
