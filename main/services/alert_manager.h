/**
 * DayZ Server Tracker - Alert Manager
 * Handles player threshold alerts and visual notifications
 *
 * This is the SERVICE layer - contains alert LOGIC only.
 * No LVGL dependencies - UI rendering is handled by ui/ui_alerts.c
 */

#ifndef ALERT_MANAGER_H
#define ALERT_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Check current player count against thresholds and show alerts if needed
 * Should be called after each server query
 */
void alert_check(void);

/**
 * Show an alert banner at the top of the screen
 * @param message Alert text to display
 * @param color_hex Background color as hex value (e.g., 0xFF4444 for red)
 */
void alert_show(const char *message, uint32_t color_hex);

/**
 * Hide the current alert if one is showing
 */
void alert_hide(void);

/**
 * Check if an alert should auto-hide (called periodically from main loop)
 */
void alert_check_auto_hide(void);

/**
 * Check if an alert is currently active
 * @return true if alert is showing
 */
bool alert_is_active(void);

#endif // ALERT_MANAGER_H
