/**
 * DayZ Server Tracker - UI Update Functions
 * Handles updating UI elements with current state
 */

#ifndef UI_UPDATE_H
#define UI_UPDATE_H

#include "app_state.h"

/**
 * Update secondary server watch boxes
 */
void ui_update_secondary(void);

/**
 * Switch to a different screen
 */
void ui_switch_screen(screen_id_t screen);

/**
 * Update main screen with current player/server data
 */
void ui_update_main(void);

/**
 * Update SD card status indicator
 */
void ui_update_sd_status(void);

/**
 * Format raw map name to display name
 */
const char* map_format_name(const char *raw_map);

#endif // UI_UPDATE_H
