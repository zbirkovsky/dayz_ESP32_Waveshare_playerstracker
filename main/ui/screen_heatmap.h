/**
 * DayZ Server Tracker - Peak Hours Heatmap Screen
 * Shows player activity patterns across days and hours
 */

#ifndef SCREEN_HEATMAP_H
#define SCREEN_HEATMAP_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#define HEATMAP_PERIODS 6   // 4-hour blocks instead of 24 hours
#define HEATMAP_DAYS 7

// Single cell accumulator
typedef struct {
    uint16_t sum;       // Sum of player counts
    uint8_t count;      // Number of samples
} heatmap_cell_t;

// Complete heatmap data
typedef struct {
    heatmap_cell_t cells[HEATMAP_DAYS][HEATMAP_PERIODS];  // [day][period]
    int16_t min_avg;    // Minimum average found
    int16_t max_avg;    // Maximum average found
    bool valid;         // Data loaded successfully
} heatmap_data_t;

// Widget pointers for heatmap screen
typedef struct {
    lv_obj_t *screen;
    lv_obj_t *grid_container;
    lv_obj_t *cells[HEATMAP_DAYS][HEATMAP_PERIODS];       // 7x6 = 42 cells
    lv_obj_t *cell_labels[HEATMAP_DAYS][HEATMAP_PERIODS]; // Labels inside cells
    lv_obj_t *lbl_cell_info;
    heatmap_data_t data;
} heatmap_screen_widgets_t;

/**
 * Initialize the heatmap screen module
 * @param widgets Pointer to widget structure to populate
 */
void screen_heatmap_init(heatmap_screen_widgets_t *widgets);

/**
 * Refresh the heatmap with current data
 * Recalculates from history and updates cell colors
 */
void screen_heatmap_refresh(void);

/**
 * Schedule a deferred heatmap refresh via LVGL timer
 * Shows "..." loading state immediately, calculates data after 50ms
 */
void screen_heatmap_schedule_refresh(void);

/**
 * Calculate heatmap data from history
 * @param server_index Server to analyze
 * @param heatmap Output heatmap data
 */
void heatmap_calculate(int server_index, heatmap_data_t *heatmap);

/**
 * Get color for a heatmap value
 * @param value Player count average
 * @param min_val Minimum value in dataset
 * @param max_val Maximum value in dataset
 * @return LVGL color
 */
lv_color_t heatmap_get_color(int value, int min_val, int max_val);

#endif // SCREEN_HEATMAP_H
