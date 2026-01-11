/**
 * DayZ Server Tracker - Crash Log Service
 * Saves crash dumps to SD card for debugging
 */

#ifndef CRASH_LOG_H
#define CRASH_LOG_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * Check for crash dump and save to SD card if present
 * Should be called early in boot, after SD card is initialized
 * @return ESP_OK if no crash or crash saved successfully
 */
esp_err_t crash_log_check_and_save(void);

/**
 * Check if a crash dump exists in flash
 * @return true if crash dump exists
 */
bool crash_log_exists(void);

/**
 * Get the path to the most recent crash log on SD card
 * @param buf Output buffer for path
 * @param size Buffer size
 * @return true if a crash log exists
 */
bool crash_log_get_latest_path(char *buf, size_t size);

#endif // CRASH_LOG_H
