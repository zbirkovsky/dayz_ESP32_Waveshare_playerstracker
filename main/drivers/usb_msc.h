/**
 * DayZ Server Tracker - USB Mass Storage Driver
 * Exposes SD card as USB storage when connected to USB OTG port
 */

#ifndef USB_MSC_H
#define USB_MSC_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * Check if screen is being touched during boot
 * Touch and hold screen during power-on to enter USB storage mode
 * @return true if touch detected, false otherwise
 */
bool usb_msc_touch_detected(void);

/**
 * Initialize USB Mass Storage mode
 * Must be called early in boot, before SD card is mounted normally
 * @return ESP_OK on success
 */
esp_err_t usb_msc_init(void);

/**
 * Main loop for USB MSC mode
 * This function blocks and handles USB events
 * Display shows "USB Storage Mode" screen
 */
void usb_msc_task(void);

/**
 * Check if currently in USB MSC mode
 * @return true if USB MSC is active
 */
bool usb_msc_is_active(void);

#endif // USB_MSC_H
