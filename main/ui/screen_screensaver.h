/**
 * DayZ Server Tracker - Screensaver Screen
 * Displays dim player count and time on black background
 */

#ifndef SCREEN_SCREENSAVER_H
#define SCREEN_SCREENSAVER_H

#include "lvgl.h"

/**
 * Create the screensaver screen
 * @return The created screen object
 */
lv_obj_t* screen_screensaver_create(void);

/**
 * Update screensaver display with current data
 * Call periodically to update time and player count
 */
void screen_screensaver_update(void);

/**
 * Get the screensaver screen object
 * @return The screensaver screen, or NULL if not created
 */
lv_obj_t* screen_screensaver_get(void);

#endif // SCREEN_SCREENSAVER_H
