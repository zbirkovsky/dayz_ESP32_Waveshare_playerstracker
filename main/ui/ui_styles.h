/**
 * DayZ Server Tracker - UI Styles
 * Shared colors, fonts, and style definitions
 */

#ifndef UI_STYLES_H
#define UI_STYLES_H

#include "lvgl.h"

// ============== COLORS ==============

// Brand colors
#define COLOR_DAYZ_GREEN        lv_color_hex(0x7BC144)
#define COLOR_DAYZ_GREEN_DARK   lv_color_hex(0x5A9A2F)

// Background colors
#define COLOR_BG_DARK           lv_color_hex(0x1A1A2E)
#define COLOR_CARD_BG           lv_color_hex(0x16213E)

// Status colors
#define COLOR_SUCCESS           lv_color_hex(0x4CAF50)
#define COLOR_WARNING           lv_color_hex(0xFFEB3B)
#define COLOR_DANGER            lv_color_hex(0xF44336)
#define COLOR_INFO              lv_color_hex(0x00D4FF)

// Alert colors
#define COLOR_ALERT_RED         lv_color_hex(0xFF4444)
#define COLOR_ALERT_ORANGE      lv_color_hex(0xFF8800)

// UI element colors
#define COLOR_BUTTON_PRIMARY    lv_color_hex(0x2196F3)
#define COLOR_BUTTON_SECONDARY  lv_color_hex(0x444444)
#define COLOR_BUTTON_SUCCESS    lv_color_hex(0x4CAF50)
#define COLOR_BUTTON_DANGER     lv_color_hex(0xFF4444)

// Text colors
#define COLOR_TEXT_PRIMARY      lv_color_white()
#define COLOR_TEXT_SECONDARY    lv_color_hex(0xAAAAAA)
#define COLOR_TEXT_MUTED        lv_color_hex(0x888888)
#define COLOR_TEXT_DISABLED     lv_color_hex(0x666666)

// Day/Night indicator colors
#define COLOR_DAY_SUN           lv_color_hex(0xFFD700)
#define COLOR_NIGHT_MOON        lv_color_hex(0x4169E1)
#define COLOR_DAY_TEXT          lv_color_hex(0xFFD700)
#define COLOR_NIGHT_TEXT        lv_color_hex(0x6495ED)

// Progress bar colors based on capacity
#define COLOR_CAPACITY_LOW      lv_color_hex(0x4CAF50)   // < 40%
#define COLOR_CAPACITY_MEDIUM   lv_color_hex(0xFFEB3B)   // 40-70%
#define COLOR_CAPACITY_HIGH     lv_color_hex(0xFF9800)   // 70-90%
#define COLOR_CAPACITY_FULL     lv_color_hex(0xF44336)   // 90-100%
#define COLOR_CAPACITY_OVERFLOW lv_color_hex(0xFF4444)   // 100%

// Restart countdown colors
#define COLOR_RESTART_OK        lv_color_hex(0x66BB6A)   // > 30 min
#define COLOR_RESTART_WARN      lv_color_hex(0xFFA500)   // < 30 min
#define COLOR_RESTART_URGENT    lv_color_hex(0xFF4444)   // imminent

// ============== STYLE DEFINITIONS ==============

/**
 * Initialize all shared styles
 * Must be called after LVGL is initialized
 */
void ui_styles_init(void);

/**
 * Get the card container style
 */
lv_style_t* ui_style_card(void);

/**
 * Get transparent row style (for settings rows)
 */
lv_style_t* ui_style_transparent_row(void);

/**
 * Get primary button style
 */
lv_style_t* ui_style_btn_primary(void);

/**
 * Get secondary button style
 */
lv_style_t* ui_style_btn_secondary(void);

/**
 * Get progress bar color based on capacity ratio
 * @param ratio Current/Max ratio (0.0 - 1.0+)
 */
lv_color_t ui_get_capacity_color(float ratio);

/**
 * Get progress bar color based on absolute player count
 * Red from 50+, Orange 40-49, Yellow 20-39, Green <20
 * @param players Current player count
 */
lv_color_t ui_get_player_color(int players);

/**
 * Get restart countdown color based on seconds remaining
 * @param seconds Seconds until restart (-1 for unknown)
 */
lv_color_t ui_get_restart_color(int seconds);

#endif // UI_STYLES_H
