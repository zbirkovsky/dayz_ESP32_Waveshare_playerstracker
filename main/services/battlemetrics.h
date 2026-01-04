/**
 * DayZ Server Tracker - BattleMetrics API Client
 * Fetches server status from BattleMetrics API
 */

#ifndef BATTLEMETRICS_H
#define BATTLEMETRICS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Server status data structure
typedef struct {
    int players;                // Current player count (-1 if unknown)
    int max_players;            // Maximum players
    char server_time[16];       // In-game time (e.g., "10:12")
    bool is_daytime;            // true if daytime in game
    char ip_address[32];        // Server IP
    uint16_t port;              // Server port
    char server_name[64];       // Server name from API
    bool online;                // Server online status
} server_status_t;

/**
 * Query server status from BattleMetrics API
 * @param server_id BattleMetrics server ID
 * @param status Output structure to fill
 * @return ESP_OK on success, error code on failure
 */
esp_err_t battlemetrics_query(const char *server_id, server_status_t *status);

/**
 * Get the last error message (if any)
 */
const char* battlemetrics_get_last_error(void);

/**
 * Check if the last query was successful
 */
bool battlemetrics_last_query_ok(void);

#endif // BATTLEMETRICS_H
