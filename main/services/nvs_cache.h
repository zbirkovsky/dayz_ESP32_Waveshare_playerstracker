/**
 * DayZ Server Tracker - NVS Handle Cache
 * Caches NVS handles to avoid repeated open/close overhead
 */

#ifndef NVS_CACHE_H
#define NVS_CACHE_H

#include "esp_err.h"
#include "nvs_flash.h"
#include <stdbool.h>

// Default NVS namespace for this application
#define NVS_NAMESPACE "dayz_tracker"

/**
 * Initialize the NVS cache
 * Opens handles for read and write access
 *
 * @return ESP_OK on success
 */
esp_err_t nvs_cache_init(void);

/**
 * Get the cached NVS handle for read operations
 * Returns a handle opened with NVS_READONLY
 *
 * @param handle Output: pointer to receive the handle
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t nvs_cache_get_read_handle(nvs_handle_t *handle);

/**
 * Get the cached NVS handle for write operations
 * Returns a handle opened with NVS_READWRITE
 *
 * @param handle Output: pointer to receive the handle
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t nvs_cache_get_write_handle(nvs_handle_t *handle);

/**
 * Commit any pending NVS changes
 * Should be called after a batch of writes
 *
 * @return ESP_OK on success
 */
esp_err_t nvs_cache_commit(void);

/**
 * Check if the NVS cache is initialized
 *
 * @return true if initialized and ready
 */
bool nvs_cache_is_ready(void);

/**
 * Close all cached handles and release resources
 * Call before deep sleep or shutdown
 */
void nvs_cache_close(void);

/**
 * Reinitialize after close (e.g., after wake from deep sleep)
 *
 * @return ESP_OK on success
 */
esp_err_t nvs_cache_reinit(void);

#endif // NVS_CACHE_H
