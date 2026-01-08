/**
 * DayZ Server Tracker - Storage Paths
 * Centralized path building for all storage operations
 */

#ifndef STORAGE_PATHS_H
#define STORAGE_PATHS_H

#include <stddef.h>
#include <stdint.h>

/**
 * Get path for binary history file (NVS backup on SD)
 * @param server_idx Server index
 * @param buf Output buffer
 * @param buf_size Buffer size
 */
void storage_path_history_bin(int server_idx, char *buf, size_t buf_size);

/**
 * Get path for server JSON history directory
 * @param server_idx Server index
 * @param buf Output buffer
 * @param buf_size Buffer size
 */
void storage_path_history_dir(int server_idx, char *buf, size_t buf_size);

/**
 * Get path for daily JSON history file
 * @param server_idx Server index
 * @param date_str Date string (YYYY-MM-DD)
 * @param buf Output buffer
 * @param buf_size Buffer size
 */
void storage_path_history_json(int server_idx, const char *date_str, char *buf, size_t buf_size);

/**
 * Get path for server configuration JSON file
 * @param buf Output buffer
 * @param buf_size Buffer size
 */
void storage_path_config(char *buf, size_t buf_size);

/**
 * Get root history directory path
 * @param buf Output buffer
 * @param buf_size Buffer size
 */
void storage_path_history_root(char *buf, size_t buf_size);

/**
 * Build NVS key for server-specific data
 * @param server_idx Server index
 * @param suffix Key suffix (e.g., "meta", "data")
 * @param key Output buffer
 * @param key_size Buffer size
 */
void storage_nvs_key(int server_idx, const char *suffix, char *key, size_t key_size);

/**
 * Convert timestamp to date string (YYYY-MM-DD)
 * @param timestamp Unix timestamp
 * @param buf Output buffer (at least 11 bytes)
 * @param buf_size Buffer size
 */
void storage_timestamp_to_date(uint32_t timestamp, char *buf, size_t buf_size);

#endif // STORAGE_PATHS_H
