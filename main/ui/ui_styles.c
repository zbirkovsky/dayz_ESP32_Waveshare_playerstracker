/**
 * DayZ Server Tracker - UI Styles Implementation
 */

#include "ui_styles.h"

// Static style objects
static lv_style_t style_card;
static lv_style_t style_transparent_row;
static lv_style_t style_btn_primary;
static lv_style_t style_btn_secondary;
static bool styles_initialized = false;

void ui_styles_init(void) {
    if (styles_initialized) return;

    // Card style
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, COLOR_CARD_BG);
    lv_style_set_radius(&style_card, 20);
    lv_style_set_border_width(&style_card, 2);
    lv_style_set_border_color(&style_card, COLOR_DAYZ_GREEN);
    lv_style_set_pad_all(&style_card, 30);

    // Transparent row style (for settings)
    lv_style_init(&style_transparent_row);
    lv_style_set_bg_opa(&style_transparent_row, LV_OPA_TRANSP);
    lv_style_set_border_width(&style_transparent_row, 0);
    lv_style_set_pad_all(&style_transparent_row, 0);

    // Primary button style
    lv_style_init(&style_btn_primary);
    lv_style_set_bg_color(&style_btn_primary, COLOR_BUTTON_PRIMARY);
    lv_style_set_radius(&style_btn_primary, 10);

    // Secondary button style
    lv_style_init(&style_btn_secondary);
    lv_style_set_bg_color(&style_btn_secondary, COLOR_BUTTON_SECONDARY);
    lv_style_set_radius(&style_btn_secondary, 10);

    styles_initialized = true;
}

lv_style_t* ui_style_card(void) {
    return &style_card;
}

lv_style_t* ui_style_transparent_row(void) {
    return &style_transparent_row;
}

lv_style_t* ui_style_btn_primary(void) {
    return &style_btn_primary;
}

lv_style_t* ui_style_btn_secondary(void) {
    return &style_btn_secondary;
}

lv_color_t ui_get_capacity_color(float ratio) {
    if (ratio >= 1.0f) {
        return COLOR_CAPACITY_OVERFLOW;
    } else if (ratio > 0.9f) {
        return COLOR_CAPACITY_FULL;
    } else if (ratio > 0.7f) {
        return COLOR_CAPACITY_HIGH;
    } else if (ratio > 0.4f) {
        return COLOR_CAPACITY_MEDIUM;
    } else {
        return COLOR_CAPACITY_LOW;
    }
}

lv_color_t ui_get_player_color(int players) {
    // Red from 50 players and up (user requested)
    if (players >= 50) {
        return COLOR_CAPACITY_FULL;  // Red
    } else if (players >= 40) {
        return COLOR_CAPACITY_HIGH;  // Orange
    } else if (players >= 20) {
        return COLOR_CAPACITY_MEDIUM;  // Yellow
    } else {
        return COLOR_CAPACITY_LOW;  // Green
    }
}

lv_color_t ui_get_restart_color(int seconds) {
    if (seconds < 0) {
        return COLOR_TEXT_MUTED;  // Unknown
    } else if (seconds == 0) {
        return COLOR_RESTART_URGENT;  // Imminent
    } else if (seconds < 1800) {  // < 30 min
        return COLOR_RESTART_WARN;
    } else {
        return COLOR_RESTART_OK;
    }
}
