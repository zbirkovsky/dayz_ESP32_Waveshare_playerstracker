/**
 * DayZ Server Tracker - Main UI Functions
 * Declares UI update functions implemented in main.c
 */

#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "app_state.h"

/**
 * Switch to a different screen
 * @param screen Target screen ID
 */
void ui_switch_screen(screen_id_t screen);

/**
 * Update the main screen UI with current state
 */
void ui_update_main(void);

/**
 * Update the secondary server boxes display
 */
void ui_update_secondary(void);

/**
 * Update SD card status display
 */
void ui_update_sd_status(void);

#endif // UI_MAIN_H
