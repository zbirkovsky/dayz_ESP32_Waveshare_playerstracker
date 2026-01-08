/**
 * DayZ Server Tracker - Alert Manager
 * Handles player threshold alerts and visual notifications
 */

#ifndef ALERT_MANAGER_H
#define ALERT_MANAGER_H

#include "lvgl.h"

/**
 * Check current player count against thresholds and show alerts if needed
 * Should be called after each server query
 */
void alert_check(void);

/**
 * Show an alert banner at the top of the screen
 * @param message Alert text to display
 * @param color Background color of the alert
 */
void alert_show(const char *message, lv_color_t color);

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
