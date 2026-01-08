/**
 * DayZ Server Tracker - SD Card Driver
 * Handles SD card initialization and CH422G IO expander control
 */

#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * Initialize the CH422G IO expander
 * @return ESP_OK on success
 */
esp_err_t sd_card_init_io_expander(void);

/**
 * Control SD card chip select via CH422G EXIO4
 * @param active true = CS low (active), false = CS high (inactive)
 */
void sd_card_set_cs(bool active);

/**
 * Initialize and mount the SD card
 * @return ESP_OK on success
 */
esp_err_t sd_card_init(void);

/**
 * Check if SD card is mounted (with periodic access verification)
 */
bool sd_card_is_mounted(void);

/**
 * Verify SD card is actually accessible (performs write test)
 * Also updates mounted status if access fails
 * @return true if SD card is accessible, false otherwise
 */
bool sd_card_verify_access(void);

/**
 * Unmount SD card (if needed)
 */
void sd_card_deinit(void);

/**
 * Get SD card space information
 * @param total_mb Output: total space in MB
 * @param free_mb Output: free space in MB
 * @return ESP_OK on success, error if SD not mounted
 */
esp_err_t sd_card_get_space(uint32_t *total_mb, uint32_t *free_mb);

/**
 * Get SD card usage percentage
 * @return Percentage used (0-100), or -1 if SD not mounted
 */
int sd_card_get_usage_percent(void);

#endif // SD_CARD_H
