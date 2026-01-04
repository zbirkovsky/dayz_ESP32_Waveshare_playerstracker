/**
 * DayZ Server Tracker - Display Driver
 * LCD and touch initialization using LVGL
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

/**
 * Initialize the RGB LCD panel
 * @return ESP_OK on success
 */
esp_err_t display_init_lcd(void);

/**
 * Initialize the GT911 touch controller
 * @return ESP_OK on success
 */
esp_err_t display_init_touch(void);

/**
 * Initialize LVGL port and display
 * Must be called after display_init_lcd() and display_init_touch()
 * @return LVGL display handle
 */
lv_display_t* display_init_lvgl(void);

/**
 * Full display initialization (LCD + Touch + LVGL)
 * Convenience function that calls all init functions
 * @return LVGL display handle, or NULL on failure
 */
lv_display_t* display_init(void);

/**
 * Get the touch input device
 */
lv_indev_t* display_get_touch_indev(void);

/**
 * Get the LCD panel handle
 */
esp_lcd_panel_handle_t display_get_panel(void);

/**
 * Control the LCD backlight via CH422G EXIO2
 * @param on true = backlight on, false = backlight off
 */
void display_set_backlight(bool on);

#endif // DISPLAY_H
