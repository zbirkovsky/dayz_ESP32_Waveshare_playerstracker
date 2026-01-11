/**
 * DayZ Server Tracker - Alert UI Rendering
 * Handles LVGL rendering of alert overlays
 *
 * This module is the UI layer - it renders alerts based on state from alert_manager.
 * The alert_manager service handles logic, this module handles display.
 */

#ifndef UI_ALERTS_H
#define UI_ALERTS_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize alert UI system
 * Call once during UI initialization
 */
void ui_alerts_init(void);

/**
 * Show an alert banner at the top of the screen
 * @param message Alert text to display
 * @param color_hex Background color as hex (e.g., 0xFF4444 for red)
 */
void ui_alerts_show(const char *message, uint32_t color_hex);

/**
 * Hide the current alert overlay
 */
void ui_alerts_hide(void);

/**
 * Check if alert overlay exists
 * @return true if overlay is currently displayed
 */
bool ui_alerts_is_visible(void);

#endif // UI_ALERTS_H
