/**
 * DayZ Server Tracker - Screen Builder
 * Creates all application screens and populates ui_context
 */

#ifndef SCREEN_BUILDER_H
#define SCREEN_BUILDER_H

/**
 * Create the main screen with player display and controls
 */
void screen_builder_create_main(void);

/**
 * Create the settings menu screen
 */
void screen_builder_create_settings(void);

/**
 * Create the WiFi settings screen
 */
void screen_builder_create_wifi_settings(void);

/**
 * Create the server settings screen
 */
void screen_builder_create_server_settings(void);

/**
 * Create the add server screen
 */
void screen_builder_create_add_server(void);

/**
 * Create the history graph screen
 */
void screen_builder_create_history(void);

/**
 * Create the peak hours heatmap screen
 */
void screen_builder_create_heatmap(void);

/**
 * Create secondary server watch boxes on main screen
 */
void screen_builder_create_secondary_boxes(void);

#endif // SCREEN_BUILDER_H
