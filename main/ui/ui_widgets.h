/**
 * DayZ Server Tracker - UI Widget Factories
 * Reusable UI component creation functions
 */

#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "lvgl.h"

// ============== LAYOUT HELPERS ==============

/**
 * Create a transparent row container (for settings rows)
 * @param parent Parent object
 * @param width Width in pixels
 * @param height Height in pixels
 */
lv_obj_t* ui_create_row(lv_obj_t *parent, int width, int height);

/**
 * Create a card container with standard styling
 * @param parent Parent object
 * @param width Width in pixels
 * @param height Height in pixels
 */
lv_obj_t* ui_create_card(lv_obj_t *parent, int width, int height);

/**
 * Create a scrollable container
 * @param parent Parent object
 * @param width Width in pixels
 * @param height Height in pixels
 */
lv_obj_t* ui_create_scroll_container(lv_obj_t *parent, int width, int height);

// ============== BUTTONS ==============

/**
 * Create a back button (top-left)
 * @param parent Parent object (screen)
 * @param callback Click callback
 */
lv_obj_t* ui_create_back_button(lv_obj_t *parent, lv_event_cb_t callback);

/**
 * Create a circular icon button
 * @param parent Parent object
 * @param icon Icon symbol (e.g., LV_SYMBOL_SETTINGS)
 * @param x X position
 * @param y Y position
 * @param callback Click callback
 */
lv_obj_t* ui_create_icon_button(lv_obj_t *parent, const char *icon,
                                 lv_coord_t x, lv_coord_t y,
                                 lv_event_cb_t callback);

/**
 * Create a text button with icon
 * @param parent Parent object
 * @param text Button text (e.g., "Save")
 * @param icon Icon symbol or NULL
 * @param width Width in pixels
 * @param height Height in pixels
 * @param color Background color
 * @param callback Click callback
 */
lv_obj_t* ui_create_button(lv_obj_t *parent, const char *text, const char *icon,
                           int width, int height, lv_color_t color,
                           lv_event_cb_t callback);

/**
 * Create a menu button (full width, for settings menu)
 * @param parent Parent container
 * @param text Button text
 * @param icon Icon symbol
 * @param color Background color
 * @param callback Click callback
 */
lv_obj_t* ui_create_menu_button(lv_obj_t *parent, const char *text,
                                 const char *icon, lv_color_t color,
                                 lv_event_cb_t callback);

// ============== INPUTS ==============

/**
 * Create a labeled text input field
 * @param parent Parent object
 * @param label Label text
 * @param placeholder Placeholder text
 * @param initial_value Initial value (can be NULL)
 * @param x X position
 * @param y Y position
 * @param width Width in pixels
 * @param is_password true for password field
 * @param click_callback Callback when textarea clicked (for keyboard)
 * @return The textarea object
 */
lv_obj_t* ui_create_text_input(lv_obj_t *parent, const char *label,
                                const char *placeholder, const char *initial_value,
                                lv_coord_t x, lv_coord_t y, int width,
                                bool is_password, lv_event_cb_t click_callback);

/**
 * Create a slider with label and value display
 * @param parent Parent container (should be a row)
 * @param label Label text
 * @param min Minimum value
 * @param max Maximum value
 * @param initial Initial value
 * @param value_label_out Output: pointer to value label (for updates)
 * @param callback Value changed callback
 */
lv_obj_t* ui_create_slider(lv_obj_t *parent, const char *label,
                           int min, int max, int initial,
                           lv_obj_t **value_label_out,
                           lv_event_cb_t callback);

/**
 * Create a switch with label
 * @param parent Parent container (should be a row)
 * @param label Label text
 * @param initial_state Initial on/off state
 * @param callback State changed callback
 */
lv_obj_t* ui_create_switch(lv_obj_t *parent, const char *label,
                           bool initial_state, lv_event_cb_t callback);

// ============== DISPLAY ELEMENTS ==============

/**
 * Create a screen title label
 * @param parent Parent object (screen)
 * @param text Title text
 */
lv_obj_t* ui_create_title(lv_obj_t *parent, const char *text);

/**
 * Create a section header label
 * @param parent Parent container
 * @param text Header text
 * @param color Text color
 */
lv_obj_t* ui_create_section_header(lv_obj_t *parent, const char *text,
                                    lv_color_t color);

/**
 * Create a large value display (for player count)
 * @param parent Parent object
 * @param initial_text Initial text
 * @param font Font to use
 * @param color Text color
 */
lv_obj_t* ui_create_value_label(lv_obj_t *parent, const char *initial_text,
                                 const lv_font_t *font, lv_color_t color);

// ============== SCREENS ==============

/**
 * Create a base screen with dark background
 */
lv_obj_t* ui_create_screen(void);

/**
 * Create a standard keyboard for a screen
 * @param parent Parent screen
 * @param initial_textarea Initial textarea to attach
 */
lv_obj_t* ui_create_keyboard(lv_obj_t *parent, lv_obj_t *initial_textarea);

// ============== SCREEN WIDGET CONTAINERS ==============

/**
 * Widget pointers for main screen
 */
typedef struct {
    lv_obj_t *screen;               // Screen object
    lv_obj_t *main_card;            // Main info card
    lv_obj_t *img_map_bg;           // Background image
    lv_obj_t *bg_overlay;           // Dark overlay
    lv_obj_t *lbl_server;           // Server name
    lv_obj_t *lbl_server_time;      // In-game time
    lv_obj_t *lbl_map_name;         // Map name
    lv_obj_t *day_night_indicator;  // Day/night icon
    lv_obj_t *lbl_players;          // Player count
    lv_obj_t *lbl_max;              // Max players
    lv_obj_t *bar_players;          // Player bar
    lv_obj_t *lbl_status;           // Status text
    lv_obj_t *lbl_update;           // Last update time
    lv_obj_t *lbl_ip;               // Server IP
    lv_obj_t *lbl_restart;          // Restart countdown
    lv_obj_t *lbl_main_trend;       // Trend indicator
    lv_obj_t *lbl_rank;             // Server rank
    lv_obj_t *lbl_sd_status;        // SD card status
    lv_obj_t *btn_prev_server;      // Previous server button
    lv_obj_t *btn_next_server;      // Next server button
    lv_obj_t *secondary_container;  // Secondary servers container
} main_screen_widgets_t;

/**
 * Widget pointers for history screen
 */
typedef struct {
    lv_obj_t *screen;               // Screen object
    lv_obj_t *chart;                // History chart
    lv_chart_series_t *series;      // Chart data series
    lv_obj_t *lbl_legend;           // Legend label
    lv_obj_t *lbl_y_axis[5];        // Y-axis labels
    lv_obj_t *lbl_x_axis[5];        // X-axis labels
} history_screen_widgets_t;

/**
 * Widget pointers for settings screens
 */
typedef struct {
    lv_obj_t *screen;               // Screen object
    lv_obj_t *keyboard;             // On-screen keyboard
} settings_screen_widgets_t;

typedef struct {
    lv_obj_t *screen;               // Screen object
    lv_obj_t *keyboard;             // On-screen keyboard
    lv_obj_t *ta_ssid;              // SSID text area
    lv_obj_t *ta_password;          // Password text area
} wifi_settings_widgets_t;

typedef struct {
    lv_obj_t *screen;               // Screen object
    lv_obj_t *slider_alert;         // Alert threshold slider
    lv_obj_t *lbl_alert_val;        // Alert threshold value label
    lv_obj_t *sw_alerts;            // Alerts enabled switch
    lv_obj_t *sw_restart_manual;    // Manual restart switch
    lv_obj_t *roller_restart_hour;  // Restart hour roller
    lv_obj_t *roller_restart_min;   // Restart minute roller
    lv_obj_t *dropdown_restart_interval; // Restart interval dropdown
    lv_obj_t *dropdown_map;         // Map selection dropdown
} server_settings_widgets_t;

typedef struct {
    lv_obj_t *screen;               // Screen object
    lv_obj_t *keyboard;             // On-screen keyboard
    lv_obj_t *ta_server_id;         // Server ID text area
    lv_obj_t *ta_server_name;       // Server name text area
    lv_obj_t *dropdown_map;         // Map selection dropdown
} add_server_widgets_t;

// ============== MULTI-SERVER WATCH ==============

/**
 * Widget pointers for secondary server box
 */
typedef struct {
    lv_obj_t *container;            // Main box container
    lv_obj_t *lbl_name;             // Server name label
    lv_obj_t *lbl_players;          // Player count label (e.g., "45/60")
    lv_obj_t *lbl_map;              // Map name label
    lv_obj_t *lbl_time;             // Server time label
    lv_obj_t *lbl_trend;            // Trend label (e.g., "+12" or "-5")
    lv_obj_t *day_night_indicator;  // Day/night icon
} secondary_box_widgets_t;

/**
 * Create a secondary server watch box
 * @param parent Parent container
 * @param width Box width
 * @param height Box height
 * @param click_callback Click callback (for swapping with main)
 * @param user_data User data (slot index)
 * @return Widgets structure
 */
secondary_box_widgets_t ui_create_secondary_box(lv_obj_t *parent, int width, int height,
                                                  lv_event_cb_t click_callback, void *user_data);

/**
 * Create an "Add Server" placeholder box
 * @param parent Parent container
 * @param width Box width
 * @param height Box height
 * @param click_callback Click callback
 * @return The box container
 */
lv_obj_t* ui_create_add_server_box(lv_obj_t *parent, int width, int height,
                                    lv_event_cb_t click_callback);

/**
 * Update secondary box display values
 * @param widgets Widgets to update
 * @param name Server name
 * @param players Current players
 * @param max_players Maximum players
 * @param map_name Map name (will be formatted)
 * @param server_time Server time string
 * @param is_daytime true if daytime
 * @param trend_delta Trend delta (+ for joining, - for leaving)
 * @param valid true if data is valid
 */
void ui_update_secondary_box(secondary_box_widgets_t *widgets, const char *name,
                              int players, int max_players, const char *map_name,
                              const char *server_time, bool is_daytime,
                              int trend_delta, bool valid);

#endif // UI_WIDGETS_H
