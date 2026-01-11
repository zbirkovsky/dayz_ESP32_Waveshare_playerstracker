/**
 * DayZ Server Tracker - Application Initialization
 * Handles hardware and system initialization before UI creation
 */

#ifndef APP_INIT_H
#define APP_INIT_H

#include "esp_err.h"
#include "lvgl.h"

/**
 * Initialize core system components (NVS, state, events, settings, buzzer, history)
 * Call this first in app_main before any UI setup.
 *
 * @return true if USB mass storage mode was entered (app should exit)
 */
bool app_init_system(void);

/**
 * Initialize display hardware and LVGL
 * Call after app_init_system, returns display handle for UI creation.
 *
 * @return Display handle or NULL on failure
 */
lv_display_t* app_init_display(void);

/**
 * Initialize networking (WiFi, time sync) and reload history with correct timestamps
 * Call after UI is created and displayed.
 */
void app_init_network(void);

#endif // APP_INIT_H
