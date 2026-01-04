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

#endif // UI_WIDGETS_H
