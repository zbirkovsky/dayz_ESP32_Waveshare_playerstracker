/**
 * DayZ Server Tracker - History Store
 * Player count history storage (RAM + SD card + NVS backup)
 */

#ifndef HISTORY_STORE_H
#define HISTORY_STORE_H

#include "../app_state.h"
#include "esp_err.h"

/**
 * Initialize history storage (called after app_state_init)
 */
void history_init(void);

/**
 * Add a new history entry
 * @param player_count Current player count
 */
void history_add_entry(int player_count);

/**
 * Get a history entry by index (0 = oldest)
 * @param index Entry index
 * @param entry Output entry
 * @return 0 on success, -1 if index out of range
 */
int history_get_entry(int index, history_entry_t *entry);

/**
 * Get total number of history entries
 */
int history_get_count(void);

/**
 * Get entries within a time range
 * @param range_seconds Seconds to look back from now
 * @return Number of entries in range
 */
int history_count_in_range(uint32_t range_seconds);

/**
 * Save history to SD card for a specific server
 * @param server_index Server index to save history for
 */
void history_save_to_sd(int server_index);

/**
 * Load history from SD card for a specific server
 * @param server_index Server index to load history for
 */
void history_load_from_sd(int server_index);

/**
 * Save recent history to NVS (backup) for a specific server
 * @param server_index Server index to save history for
 */
void history_save_to_nvs(int server_index);

/**
 * Load history from NVS backup for a specific server
 * @param server_index Server index to load history for
 */
void history_load_from_nvs(int server_index);

/**
 * Switch history to a different server
 * Saves current server's history and loads the new server's history
 * @param old_server_index Previous server index (-1 if none)
 * @param new_server_index New server index to switch to
 */
void history_switch_server(int old_server_index, int new_server_index);

/**
 * Clear all history (in-memory only, does not delete files)
 */
void history_clear(void);

/**
 * Get seconds for a given history range
 */
uint32_t history_range_to_seconds(history_range_t range);

/**
 * Get label text for a history range
 */
const char* history_range_to_label(history_range_t range);

// ============== JSON HISTORY STORAGE ==============

/**
 * Append a history entry to JSON file (daily files)
 * @param server_index Server index
 * @param ts Unix timestamp
 * @param players Player count
 * @return ESP_OK on success
 */
esp_err_t history_append_entry_json(int server_index, uint32_t ts, int16_t players);

/**
 * Load history entries from JSON files within a time range
 * Loads into provided buffer, sorted by timestamp (oldest first)
 * @param server_index Server index
 * @param start_time Start timestamp (inclusive)
 * @param end_time End timestamp (inclusive)
 * @param entries Output buffer for entries
 * @param max_entries Maximum entries to load
 * @return Number of entries loaded, or -1 on error
 */
int history_load_range_json(int server_index, uint32_t start_time, uint32_t end_time,
                            history_entry_t *entries, int max_entries);

/**
 * Cleanup old history files beyond retention period
 * @param server_index Server index
 * @param days_to_keep Number of days to retain
 * @return Number of files deleted
 */
int history_cleanup_old_files(int server_index, int days_to_keep);

/**
 * Initialize JSON history directory structure
 * @param server_index Server index
 * @return ESP_OK if directory exists/created
 */
esp_err_t history_init_json_dir(int server_index);

/**
 * Get JSON history file count for a server
 * @param server_index Server index
 * @return Number of .jsonl files, or -1 on error
 */
int history_get_json_file_count(int server_index);

#endif // HISTORY_STORE_H
