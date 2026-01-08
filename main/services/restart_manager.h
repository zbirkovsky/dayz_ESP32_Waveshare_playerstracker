/**
 * DayZ Server Tracker - Restart Manager
 * Tracks server restarts and predicts next restart time
 */

#ifndef RESTART_MANAGER_H
#define RESTART_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include "app_state.h"

/**
 * Record a detected server restart
 * @param srv Server configuration with restart history
 * @param timestamp Unix timestamp of restart
 */
void restart_record(server_config_t *srv, uint32_t timestamp);

/**
 * Check if a restart just happened (player count dropped to 0)
 * @param srv Server configuration
 * @param current_count Current player count
 */
void restart_check_for_restart(server_config_t *srv, int current_count);

/**
 * Get countdown to next predicted restart
 * @param srv Server configuration
 * @return Seconds until restart, 0 if imminent, -1 if unknown
 */
int restart_get_countdown(server_config_t *srv);

/**
 * Format countdown seconds into human-readable string
 * @param seconds Countdown value (negative = unknown)
 * @param buf Output buffer
 * @param buf_size Buffer size
 */
void restart_format_countdown(int seconds, char *buf, size_t buf_size);

#endif // RESTART_MANAGER_H
