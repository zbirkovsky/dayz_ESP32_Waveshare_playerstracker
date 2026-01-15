/**
 * DayZ Server Tracker - Peak Hours Heatmap Screen
 * Shows player activity patterns across days and 4-hour periods
 */

#include "screen_heatmap.h"
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "config.h"
#include "app_state.h"
#include "ui_styles.h"
#include "services/history_store.h"

static const char *TAG = "screen_heatmap";

// Module-level widgets
static heatmap_screen_widgets_t *s_widgets = NULL;

// Day names for display
static const char *day_names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

// Period names (4-hour blocks)
static const char *period_names[] = {"00-04", "04-08", "08-12", "12-16", "16-20", "20-24"};

// Heatmap colors (green -> yellow -> orange -> red)
#define HEATMAP_COLOR_EMPTY     lv_color_hex(0x333333)
#define HEATMAP_COLOR_LOW       lv_color_hex(0x4CAF50)
#define HEATMAP_COLOR_MED_LOW   lv_color_hex(0x8BC34A)
#define HEATMAP_COLOR_MEDIUM    lv_color_hex(0xFFEB3B)
#define HEATMAP_COLOR_MED_HIGH  lv_color_hex(0xFF9800)
#define HEATMAP_COLOR_HIGH      lv_color_hex(0xF44336)

// Layout constants for 6-period grid
#define CELL_WIDTH      100
#define CELL_HEIGHT     38
#define CELL_GAP        4
#define DAY_LABEL_WIDTH 50
#define PERIOD_LABEL_HEIGHT 25

// Forward declarations
static void on_cell_clicked(lv_event_t *e);

lv_color_t heatmap_get_color(int value, int min_val, int max_val) {
    if (value < 0) {
        return HEATMAP_COLOR_EMPTY;
    }

    int range = max_val - min_val;
    if (range <= 0) range = 1;

    int percent = ((value - min_val) * 100) / range;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    if (percent < 25) {
        return HEATMAP_COLOR_LOW;
    } else if (percent < 50) {
        return HEATMAP_COLOR_MED_LOW;
    } else if (percent < 70) {
        return HEATMAP_COLOR_MEDIUM;
    } else if (percent < 85) {
        return HEATMAP_COLOR_MED_HIGH;
    } else {
        return HEATMAP_COLOR_HIGH;
    }
}

void heatmap_calculate(int server_index, heatmap_data_t *heatmap) {
    ESP_LOGI(TAG, "Calculating heatmap for server %d", server_index);

    memset(heatmap, 0, sizeof(heatmap_data_t));

    time_t now;
    time(&now);
    uint32_t end_time = (uint32_t)now;
    uint32_t start_time = end_time - (28 * 86400);  // 28 days back

    // Allocate buffer for history entries in PSRAM
    int max_entries = 10000;
    history_entry_t *entries = heap_caps_malloc(
        max_entries * sizeof(history_entry_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (!entries) {
        ESP_LOGE(TAG, "Failed to allocate entry buffer");
        return;
    }

    int count = history_load_range_json(server_index, start_time, end_time,
                                         entries, max_entries);

    ESP_LOGI(TAG, "Loaded %d history entries", count);

    if (count <= 0) {
        heap_caps_free(entries);
        return;
    }

    // Bucket entries into period/day slots (4-hour periods)
    for (int i = 0; i < count; i++) {
        time_t ts = (time_t)entries[i].timestamp;
        struct tm tm_buf;
        localtime_r(&ts, &tm_buf);

        // Convert to Monday=0 format (tm_wday has Sunday=0)
        int day = (tm_buf.tm_wday + 6) % 7;
        int period = tm_buf.tm_hour / 4;  // 0-5 for 6 periods

        if (period >= HEATMAP_PERIODS) period = HEATMAP_PERIODS - 1;

        // Accumulate (with overflow protection)
        if (heatmap->cells[day][period].count < 255) {
            heatmap->cells[day][period].sum += entries[i].player_count;
            heatmap->cells[day][period].count++;
        }
    }

    heap_caps_free(entries);

    // Calculate min/max averages for normalization
    heatmap->min_avg = INT16_MAX;
    heatmap->max_avg = 0;

    for (int d = 0; d < HEATMAP_DAYS; d++) {
        for (int p = 0; p < HEATMAP_PERIODS; p++) {
            if (heatmap->cells[d][p].count > 0) {
                int avg = heatmap->cells[d][p].sum / heatmap->cells[d][p].count;
                if (avg < heatmap->min_avg) heatmap->min_avg = avg;
                if (avg > heatmap->max_avg) heatmap->max_avg = avg;
            }
        }
    }

    if (heatmap->min_avg == INT16_MAX) {
        heatmap->min_avg = 0;
        heatmap->max_avg = 60;
    }

    heatmap->valid = true;
    ESP_LOGI(TAG, "Heatmap calculated: min=%d, max=%d", heatmap->min_avg, heatmap->max_avg);
}

static void on_cell_clicked(lv_event_t *e) {
    if (!s_widgets) return;

    int cell_index = (int)(intptr_t)lv_event_get_user_data(e);
    int day = cell_index / HEATMAP_PERIODS;
    int period = cell_index % HEATMAP_PERIODS;

    heatmap_cell_t *cell = &s_widgets->data.cells[day][period];

    char buf[80];
    if (cell->count > 0) {
        int avg = cell->sum / cell->count;
        snprintf(buf, sizeof(buf), "%s %s - Avg: %d players (%d samples)",
                 day_names[day], period_names[period], avg, cell->count);
    } else {
        snprintf(buf, sizeof(buf), "%s %s - No data",
                 day_names[day], period_names[period]);
    }

    if (s_widgets->lbl_cell_info) {
        lv_label_set_text(s_widgets->lbl_cell_info, buf);
    }
}

void screen_heatmap_init(heatmap_screen_widgets_t *widgets) {
    s_widgets = widgets;

    if (!widgets->screen) return;

    ESP_LOGI(TAG, "Creating heatmap screen...");

    // Create grid container
    widgets->grid_container = lv_obj_create(widgets->screen);
    lv_obj_set_size(widgets->grid_container, 720, 340);
    lv_obj_align(widgets->grid_container, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(widgets->grid_container, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_width(widgets->grid_container, 0, 0);
    lv_obj_set_style_radius(widgets->grid_container, 10, 0);
    lv_obj_set_style_pad_all(widgets->grid_container, 10, 0);
    lv_obj_clear_flag(widgets->grid_container, LV_OBJ_FLAG_SCROLLABLE);

    // Period labels at top (6 labels)
    for (int p = 0; p < HEATMAP_PERIODS; p++) {
        lv_obj_t *lbl = lv_label_create(widgets->grid_container);
        lv_label_set_text(lbl, period_names[p]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, COLOR_TEXT_MUTED, 0);
        lv_obj_set_pos(lbl, DAY_LABEL_WIDTH + (p * (CELL_WIDTH + CELL_GAP)) + 30, 0);
    }

    // Day labels and cells (7 days x 6 periods = 42 cells)
    for (int d = 0; d < HEATMAP_DAYS; d++) {
        // Day label
        lv_obj_t *day_lbl = lv_label_create(widgets->grid_container);
        lv_label_set_text(day_lbl, day_names[d]);
        lv_obj_set_style_text_font(day_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(day_lbl, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_pos(day_lbl, 5, PERIOD_LABEL_HEIGHT + (d * (CELL_HEIGHT + CELL_GAP)) + 10);

        // Period cells for this day
        for (int p = 0; p < HEATMAP_PERIODS; p++) {
            lv_obj_t *cell = lv_obj_create(widgets->grid_container);
            lv_obj_set_size(cell, CELL_WIDTH, CELL_HEIGHT);
            lv_obj_set_pos(cell,
                          DAY_LABEL_WIDTH + (p * (CELL_WIDTH + CELL_GAP)),
                          PERIOD_LABEL_HEIGHT + (d * (CELL_HEIGHT + CELL_GAP)));
            lv_obj_set_style_bg_color(cell, HEATMAP_COLOR_EMPTY, 0);
            lv_obj_set_style_radius(cell, 6, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

            // Label inside cell showing average
            lv_obj_t *cell_lbl = lv_label_create(cell);
            lv_label_set_text(cell_lbl, "-");
            lv_obj_set_style_text_font(cell_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(cell_lbl, COLOR_TEXT_PRIMARY, 0);
            lv_obj_center(cell_lbl);
            widgets->cell_labels[d][p] = cell_lbl;

            // Make clickable
            lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
            int cell_index = d * HEATMAP_PERIODS + p;
            lv_obj_add_event_cb(cell, on_cell_clicked, LV_EVENT_CLICKED,
                               (void*)(intptr_t)cell_index);

            widgets->cells[d][p] = cell;
        }
    }

    // Cell info label at bottom
    widgets->lbl_cell_info = lv_label_create(widgets->screen);
    lv_label_set_text(widgets->lbl_cell_info, "Tap a cell to see details");
    lv_obj_set_style_text_font(widgets->lbl_cell_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(widgets->lbl_cell_info, COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(widgets->lbl_cell_info, LV_ALIGN_BOTTOM_MID, 0, -15);

    ESP_LOGI(TAG, "Heatmap screen created (42 cells)");
}

void screen_heatmap_refresh(void) {
    if (!s_widgets) return;

    app_state_t *state = app_state_get();
    int server_index = state->settings.active_server_index;

    // Calculate heatmap data
    heatmap_calculate(server_index, &s_widgets->data);

    // Update cell colors and labels
    for (int d = 0; d < HEATMAP_DAYS; d++) {
        for (int p = 0; p < HEATMAP_PERIODS; p++) {
            if (!s_widgets->cells[d][p]) continue;

            heatmap_cell_t *cell = &s_widgets->data.cells[d][p];
            lv_color_t color;
            char label_buf[8];

            if (cell->count > 0) {
                int avg = cell->sum / cell->count;
                color = heatmap_get_color(avg, s_widgets->data.min_avg,
                                          s_widgets->data.max_avg);
                snprintf(label_buf, sizeof(label_buf), "%d", avg);
            } else {
                color = HEATMAP_COLOR_EMPTY;
                snprintf(label_buf, sizeof(label_buf), "NULL");
            }

            lv_obj_set_style_bg_color(s_widgets->cells[d][p], color, 0);

            // Update cell label
            if (s_widgets->cell_labels[d][p]) {
                lv_label_set_text(s_widgets->cell_labels[d][p], label_buf);
                lv_obj_center(s_widgets->cell_labels[d][p]);
            }
        }
    }

    // Update info label
    if (s_widgets->lbl_cell_info && s_widgets->data.valid) {
        lv_label_set_text(s_widgets->lbl_cell_info, "Tap a cell to see details (28 days data)");
    }
}
