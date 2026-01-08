/**
 * DayZ Server Tracker - History Screen Module
 * Player history chart display and management
 */

#ifndef SCREEN_HISTORY_H
#define SCREEN_HISTORY_H

#include "lvgl.h"
#include "ui_widgets.h"

/**
 * Initialize the history screen module with widget references
 * Must be called after creating history screen widgets
 * @param widgets Pointer to history screen widgets structure
 */
void screen_history_init(history_screen_widgets_t *widgets);

/**
 * Refresh the history chart with current data
 * Updates chart points, axis labels, and legend
 */
void screen_history_refresh(void);

#endif // SCREEN_HISTORY_H
