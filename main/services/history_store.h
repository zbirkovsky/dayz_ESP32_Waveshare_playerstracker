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
 * Save history to SD card
 */
void history_save_to_sd(void);

/**
 * Load history from SD card
 */
void history_load_from_sd(void);

/**
 * Save recent history to NVS (backup)
 */
void history_save_to_nvs(void);

/**
 * Load history from NVS backup
 */
void history_load_from_nvs(void);

/**
 * Clear all history
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

#endif // HISTORY_STORE_H
