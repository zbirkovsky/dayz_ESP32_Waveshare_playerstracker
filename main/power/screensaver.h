/**
 * DayZ Server Tracker - Screensaver & Power Management
 * Handles screen timeout, touch wake, and long-press screen-off
 */

#ifndef SCREENSAVER_H
#define SCREENSAVER_H

#include <stdbool.h>
#include "lvgl.h"

/**
 * Initialize screensaver module
 * Call once during startup after display init
 */
void screensaver_init(void);

/**
 * Process screensaver tick - call from main loop
 * Handles timeout checks and touch state machine
 */
void screensaver_tick(void);

/**
 * Check if screensaver (screen off) is currently active
 * @return true if screen is off
 */
bool screensaver_is_active(void);

/**
 * Manually set screensaver state
 * @param active true to turn screen off, false to turn on
 * @return true if state changed successfully
 */
bool screensaver_set_active(bool active);

/**
 * Reset activity timer (call on user interaction)
 */
void screensaver_reset_activity(void);

/**
 * Get LVGL touch pressed callback for screen wake
 * Use this when setting up screen touch handlers
 */
lv_event_cb_t screensaver_get_touch_pressed_cb(void);

/**
 * Get LVGL touch released callback
 * Use this when setting up screen touch handlers
 */
lv_event_cb_t screensaver_get_touch_released_cb(void);

#endif // SCREENSAVER_H
