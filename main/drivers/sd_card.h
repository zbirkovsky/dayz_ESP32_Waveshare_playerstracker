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
 * Check if SD card is mounted
 */
bool sd_card_is_mounted(void);

/**
 * Unmount SD card (if needed)
 */
void sd_card_deinit(void);

#endif // SD_CARD_H
