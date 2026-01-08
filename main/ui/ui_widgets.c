/**
 * DayZ Server Tracker - UI Widget Factories Implementation
 */

#include "ui_widgets.h"
#include "ui_styles.h"
#include "config.h"
#include <stdio.h>

// ============== LAYOUT HELPERS ==============

lv_obj_t* ui_create_row(lv_obj_t *parent, int width, int height) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, width, height);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

lv_obj_t* ui_create_card(lv_obj_t *parent, int width, int height) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_bg_color(card, COLOR_CARD_BG, 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, COLOR_DAYZ_GREEN, 0);
    lv_obj_set_style_pad_all(card, 30, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t* ui_create_scroll_container(lv_obj_t *parent, int width, int height) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, width, height);
    lv_obj_set_style_bg_color(cont, COLOR_CARD_BG, 0);
    lv_obj_set_style_radius(cont, 15, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(cont, 15, 0);
    lv_obj_set_style_pad_row(cont, 10, 0);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    return cont;
}

// ============== BUTTONS ==============

lv_obj_t* ui_create_back_button(lv_obj_t *parent, lv_event_cb_t callback) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 100, 40);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);

    return btn;
}

lv_obj_t* ui_create_icon_button(lv_obj_t *parent, const char *icon,
                                 lv_coord_t x, lv_coord_t y,
                                 lv_event_cb_t callback) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 50, 50);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, COLOR_BUTTON_SECONDARY, 0);
    lv_obj_set_style_radius(btn, UI_BUTTON_RADIUS, 0);
    if (callback) {
        lv_obj_add_event_cb(btn, callback, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, icon);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl);

    return btn;
}

lv_obj_t* ui_create_button(lv_obj_t *parent, const char *text, const char *icon,
                           int width, int height, lv_color_t color,
                           lv_event_cb_t callback) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, height);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    if (callback) {
        lv_obj_add_event_cb(btn, callback, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *lbl = lv_label_create(btn);
    if (icon) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %s", icon, text);
        lv_label_set_text(lbl, buf);
    } else {
        lv_label_set_text(lbl, text);
    }
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl);

    return btn;
}

lv_obj_t* ui_create_menu_button(lv_obj_t *parent, const char *text,
                                 const char *icon, lv_color_t color,
                                 lv_event_cb_t callback) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 660, 60);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    if (callback) {
        lv_obj_add_event_cb(btn, callback, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *lbl = lv_label_create(btn);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s  %s", icon, text);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl);

    return btn;
}

// ============== INPUTS ==============

lv_obj_t* ui_create_text_input(lv_obj_t *parent, const char *label,
                                const char *placeholder, const char *initial_value,
                                lv_coord_t x, lv_coord_t y, int width,
                                bool is_password, lv_event_cb_t click_callback) {
    // Label
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_pos(lbl, x, y);

    // Text area
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, width, 45);
    lv_obj_set_pos(ta, x, y + 25);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);

    if (initial_value) {
        lv_textarea_set_text(ta, initial_value);
    }

    if (is_password) {
        lv_textarea_set_password_mode(ta, true);
    }

    if (click_callback) {
        lv_obj_add_event_cb(ta, click_callback, LV_EVENT_CLICKED, NULL);
    }

    return ta;
}

lv_obj_t* ui_create_slider(lv_obj_t *parent, const char *label,
                           int min, int max, int initial,
                           lv_obj_t **value_label_out,
                           lv_event_cb_t callback) {
    // Label
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // Slider
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_size(slider, 350, 20);
    lv_obj_align(slider, LV_ALIGN_CENTER, 30, 0);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, initial, LV_ANIM_OFF);
    if (callback) {
        lv_obj_add_event_cb(slider, callback, LV_EVENT_VALUE_CHANGED, NULL);
    }

    // Value label
    lv_obj_t *val_lbl = lv_label_create(parent);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", initial);
    lv_label_set_text(val_lbl, buf);
    lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(val_lbl, COLOR_DAYZ_GREEN, 0);
    lv_obj_align(val_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    if (value_label_out) {
        *value_label_out = val_lbl;
    }

    return slider;
}

lv_obj_t* ui_create_switch(lv_obj_t *parent, const char *label,
                           bool initial_state, lv_event_cb_t callback) {
    // Label
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // Switch
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    if (initial_state) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    if (callback) {
        lv_obj_add_event_cb(sw, callback, LV_EVENT_VALUE_CHANGED, NULL);
    }

    return sw;
}

// ============== DISPLAY ELEMENTS ==============

lv_obj_t* ui_create_title(lv_obj_t *parent, const char *text) {
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, text);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    return title;
}

lv_obj_t* ui_create_section_header(lv_obj_t *parent, const char *text,
                                    lv_color_t color) {
    lv_obj_t *header = lv_label_create(parent);
    lv_label_set_text(header, text);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(header, color, 0);
    return header;
}

lv_obj_t* ui_create_value_label(lv_obj_t *parent, const char *initial_text,
                                 const lv_font_t *font, lv_color_t color) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, initial_text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    return lbl;
}

// ============== SCREENS ==============

lv_obj_t* ui_create_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, COLOR_BG_DARK, 0);
    return screen;
}

lv_obj_t* ui_create_keyboard(lv_obj_t *parent, lv_obj_t *initial_textarea) {
    lv_obj_t *kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb, LCD_WIDTH, 220);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    if (initial_textarea) {
        lv_keyboard_set_textarea(kb, initial_textarea);
    }
    return kb;
}

// ============== MULTI-SERVER WATCH ==============

secondary_box_widgets_t ui_create_secondary_box(lv_obj_t *parent, int width, int height,
                                                  lv_event_cb_t click_callback, void *user_data) {
    secondary_box_widgets_t widgets = {0};

    // Container box with border
    widgets.container = lv_obj_create(parent);
    lv_obj_set_size(widgets.container, width, height);
    lv_obj_set_style_bg_color(widgets.container, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(widgets.container, UI_CARD_RADIUS, 0);
    lv_obj_set_style_border_width(widgets.container, 2, 0);
    lv_obj_set_style_border_color(widgets.container, lv_color_hex(0x404040), 0);
    lv_obj_set_style_pad_all(widgets.container, 10, 0);
    lv_obj_clear_flag(widgets.container, LV_OBJ_FLAG_SCROLLABLE);

    if (click_callback) {
        lv_obj_add_flag(widgets.container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(widgets.container, click_callback, LV_EVENT_CLICKED, user_data);
    }

    // Server name (top)
    widgets.lbl_name = lv_label_create(widgets.container);
    lv_label_set_text(widgets.lbl_name, "---");
    lv_obj_set_style_text_font(widgets.lbl_name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(widgets.lbl_name, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_width(widgets.lbl_name, width - 24);
    lv_label_set_long_mode(widgets.lbl_name, LV_LABEL_LONG_DOT);
    lv_obj_align(widgets.lbl_name, LV_ALIGN_TOP_LEFT, 0, 0);

    // Player count (large, middle-left)
    widgets.lbl_players = lv_label_create(widgets.container);
    lv_label_set_text(widgets.lbl_players, "--/--");
    lv_obj_set_style_text_font(widgets.lbl_players, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(widgets.lbl_players, COLOR_DAYZ_GREEN, 0);
    lv_obj_align(widgets.lbl_players, LV_ALIGN_LEFT_MID, 0, -5);

    // Map name (below player count)
    widgets.lbl_map = lv_label_create(widgets.container);
    lv_label_set_text(widgets.lbl_map, "");
    lv_obj_set_style_text_font(widgets.lbl_map, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(widgets.lbl_map, COLOR_TEXT_MUTED, 0);
    lv_obj_align(widgets.lbl_map, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // Day/night indicator (right side of players)
    widgets.day_night_indicator = lv_label_create(widgets.container);
    lv_label_set_text(widgets.day_night_indicator, LV_SYMBOL_IMAGE);  // Sun/moon icon
    lv_obj_set_style_text_font(widgets.day_night_indicator, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(widgets.day_night_indicator, lv_color_hex(0xFFD700), 0);  // Yellow
    lv_obj_align(widgets.day_night_indicator, LV_ALIGN_RIGHT_MID, -50, -10);

    // Server time (next to day/night)
    widgets.lbl_time = lv_label_create(widgets.container);
    lv_label_set_text(widgets.lbl_time, "--:--");
    lv_obj_set_style_text_font(widgets.lbl_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(widgets.lbl_time, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(widgets.lbl_time, LV_ALIGN_RIGHT_MID, 0, -10);

    // Trend indicator (bottom right)
    widgets.lbl_trend = lv_label_create(widgets.container);
    lv_label_set_text(widgets.lbl_trend, "---");
    lv_obj_set_style_text_font(widgets.lbl_trend, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(widgets.lbl_trend, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(widgets.lbl_trend, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    return widgets;
}

lv_obj_t* ui_create_add_server_box(lv_obj_t *parent, int width, int height,
                                    lv_event_cb_t click_callback) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, width, height);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_radius(box, UI_CARD_RADIUS, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_opa(box, LV_OPA_50, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    if (click_callback) {
        lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(box, click_callback, LV_EVENT_CLICKED, NULL);
    }

    // Plus icon
    lv_obj_t *icon = lv_label_create(box);
    lv_label_set_text(icon, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x555555), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -10);

    // Text
    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, "Add Server");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x555555), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 20);

    return box;
}

void ui_update_secondary_box(secondary_box_widgets_t *widgets, const char *name,
                              int players, int max_players, const char *map_name,
                              const char *server_time, bool is_daytime,
                              int trend_delta, bool valid) {
    if (!widgets || !widgets->container) return;

    // Update name
    if (widgets->lbl_name) {
        lv_label_set_text(widgets->lbl_name, name ? name : "---");
    }

    // Update player count
    if (widgets->lbl_players) {
        if (valid && players >= 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d/%d", players, max_players);
            lv_label_set_text(widgets->lbl_players, buf);

            // Color code based on player count using existing helper
            lv_obj_set_style_text_color(widgets->lbl_players, ui_get_player_color(players), 0);
        } else {
            lv_label_set_text(widgets->lbl_players, "--/--");
            lv_obj_set_style_text_color(widgets->lbl_players, COLOR_TEXT_SECONDARY, 0);
        }
    }

    // Update map name
    if (widgets->lbl_map) {
        if (valid && map_name && map_name[0]) {
            lv_label_set_text(widgets->lbl_map, map_name);
        } else {
            lv_label_set_text(widgets->lbl_map, "");
        }
    }

    // Update server time
    if (widgets->lbl_time) {
        if (valid && server_time && server_time[0]) {
            lv_label_set_text(widgets->lbl_time, server_time);
        } else {
            lv_label_set_text(widgets->lbl_time, "--:--");
        }
    }

    // Update day/night indicator
    if (widgets->day_night_indicator) {
        if (valid) {
            if (is_daytime) {
                lv_label_set_text(widgets->day_night_indicator, LV_SYMBOL_IMAGE);  // Sun
                lv_obj_set_style_text_color(widgets->day_night_indicator, lv_color_hex(0xFFD700), 0);
            } else {
                lv_label_set_text(widgets->day_night_indicator, LV_SYMBOL_IMAGE);  // Moon
                lv_obj_set_style_text_color(widgets->day_night_indicator, lv_color_hex(0x6B8EAD), 0);
            }
        } else {
            lv_label_set_text(widgets->day_night_indicator, "");
        }
    }

    // Update trend
    if (widgets->lbl_trend) {
        if (valid && trend_delta != 0) {
            char buf[16];
            if (trend_delta > 0) {
                snprintf(buf, sizeof(buf), LV_SYMBOL_UP "+%d", trend_delta);
                lv_obj_set_style_text_color(widgets->lbl_trend, COLOR_DAYZ_GREEN, 0);
            } else {
                snprintf(buf, sizeof(buf), LV_SYMBOL_DOWN "%d", trend_delta);
                lv_obj_set_style_text_color(widgets->lbl_trend, COLOR_DANGER, 0);
            }
            lv_label_set_text(widgets->lbl_trend, buf);
        } else {
            lv_label_set_text(widgets->lbl_trend, "---");
            lv_obj_set_style_text_color(widgets->lbl_trend, COLOR_TEXT_SECONDARY, 0);
        }
    }

    // Update border color based on validity
    if (valid) {
        lv_obj_set_style_border_color(widgets->container, COLOR_DAYZ_GREEN, 0);
        lv_obj_set_style_border_opa(widgets->container, LV_OPA_50, 0);
    } else {
        lv_obj_set_style_border_color(widgets->container, lv_color_hex(0x404040), 0);
        lv_obj_set_style_border_opa(widgets->container, LV_OPA_100, 0);
    }
}
