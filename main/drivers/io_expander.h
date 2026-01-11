/**
 * DayZ Server Tracker - CH422G IO Expander Driver
 * Centralized control for all CH422G IO expander operations
 *
 * The CH422G controls: SD_CS (EXIO4), GT911 Reset (EXIO1), Backlight (EXIO0)
 * This driver maintains the SINGLE source of truth for CH422G state.
 */

#ifndef IO_EXPANDER_H
#define IO_EXPANDER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Initialize CH422G IO expander
 * Must be called AFTER I2C init but BEFORE display_init and sd_card_init
 * @return ESP_OK on success
 */
esp_err_t io_expander_init(void);

/**
 * Control SD card chip select via EXIO4
 * @param active true = CS LOW (active), false = CS HIGH (inactive)
 */
void io_expander_set_sd_cs(bool active);

/**
 * Reset GT911 touch controller via EXIO1
 * Pulses EXIO1 low for 20ms, then high, waits 100ms for boot
 * @return ESP_OK on success
 */
esp_err_t io_expander_reset_gt911(void);

/**
 * Control LCD backlight via EXIO0 with retry logic
 * @param on true = backlight on, false = backlight off
 * @return ESP_OK on success, error code on failure after retries
 */
esp_err_t io_expander_set_backlight(bool on);

/**
 * Get current backlight state
 * @return true if backlight is on, false if off
 */
bool io_expander_get_backlight_state(void);

/**
 * Test backlight control by toggling EXIO0
 * Call this at startup to verify backlight can be controlled
 */
void io_expander_test_backlight(void);

/**
 * Control USB/CAN mode select via EXIO5
 * @param usb_mode true = USB mode (EXIO5 LOW), false = CAN mode (EXIO5 HIGH)
 */
void io_expander_set_usb_mode(bool usb_mode);

/**
 * Get current CH422G output state (for debugging)
 * @return Current state byte
 */
uint8_t io_expander_get_state(void);

/**
 * Reset io_expander state (call when I2C driver is deleted)
 * Forces reinitialization on next use
 */
void io_expander_reset_state(void);

#endif // IO_EXPANDER_H
