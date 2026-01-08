/**
 * DayZ Server Tracker - Map Background Manager
 * Handles map name formatting and background image loading
 */

#ifndef MAP_BACKGROUND_H
#define MAP_BACKGROUND_H

#include "lvgl.h"

/**
 * Format raw map name to display name
 * @param raw_map Raw map identifier (e.g., "chernarusplus")
 * @return Display name (e.g., "Chernarus")
 */
const char* map_format_name(const char *raw_map);

/**
 * Initialize the map background system
 * Must be called before load_map_background()
 * @param img_widget The LVGL image widget for the background
 * @param overlay_widget The overlay widget for text readability
 */
void map_background_init(lv_obj_t *img_widget, lv_obj_t *overlay_widget);

/**
 * Load map background image from SD card
 * Images should be stored as /sdcard/maps/<mapname>.png
 * Recommended size: 720x180 pixels (main card inner size)
 * @param map_name Internal map name (e.g., "chernarusplus")
 */
void map_background_load(const char *map_name);

/**
 * Clear current map background (e.g., when SD card becomes unavailable)
 */
void map_background_clear(void);

#endif // MAP_BACKGROUND_H
