/**
 * DayZ Server Tracker - History Screen Implementation
 */

#include "screen_history.h"
#include "app_state.h"
#include "services/history_store.h"
#include "ui_styles.h"
#include <time.h>
#include <stdio.h>

// Module-level widget references
static history_screen_widgets_t *g_widgets = NULL;

void screen_history_init(history_screen_widgets_t *widgets) {
    g_widgets = widgets;
}

void screen_history_refresh(void) {
    if (!g_widgets || !g_widgets->chart || !g_widgets->series) return;

    app_state_t *state = app_state_get();

    time_t now;
    time(&now);
    uint32_t range_seconds = history_range_to_seconds(state->ui.current_history_range);
    uint32_t cutoff_time = (uint32_t)now - range_seconds;

    int entries_in_range = 0;
    int total_count = history_get_count();

    // First pass: count entries and find min/max for dynamic scaling
    int min_players = 9999;
    int max_players = 0;

    for (int i = 0; i < total_count; i++) {
        history_entry_t entry;
        if (history_get_entry(i, &entry) == 0 && entry.timestamp >= cutoff_time && entry.player_count >= 0) {
            entries_in_range++;
            if (entry.player_count < min_players) min_players = entry.player_count;
            if (entry.player_count > max_players) max_players = entry.player_count;
        }
    }

    // Calculate dynamic Y-axis range with padding
    int range_min, range_max;
    if (entries_in_range == 0 || min_players == 9999) {
        // No data - use default range
        range_min = 0;
        range_max = 60;
    } else {
        // Add padding above and below the actual data
        range_min = (min_players > 10) ? (min_players - 10) : 0;
        range_max = max_players + 10;

        // Ensure minimum span of 20 for visibility
        if (range_max - range_min < 20) {
            range_max = range_min + 20;
        }

        // Round to nice numbers
        range_min = (range_min / 5) * 5;  // Round down to nearest 5
        range_max = ((range_max + 4) / 5) * 5;  // Round up to nearest 5
    }

    // Apply dynamic range to chart
    lv_chart_set_range(g_widgets->chart, LV_CHART_AXIS_PRIMARY_Y, range_min, range_max);

    // Update Y-axis labels to match dynamic range
    int step = (range_max - range_min) / 4;
    if (step < 1) step = 1;
    for (int i = 0; i < 5; i++) {
        if (g_widgets->lbl_y_axis[i]) {
            char label[16];
            snprintf(label, sizeof(label), "%d", range_max - (i * step));
            lv_label_set_text(g_widgets->lbl_y_axis[i], label);
        }
    }

    int display_points = (entries_in_range > 120) ? 120 : (entries_in_range > 0 ? entries_in_range : 1);
    int sample_rate = (entries_in_range > 120) ? (entries_in_range / 120) : 1;

    lv_chart_set_point_count(g_widgets->chart, display_points);
    lv_chart_set_all_value(g_widgets->chart, g_widgets->series, LV_CHART_POINT_NONE);

    int chart_idx = 0;
    int sample_counter = 0;

    for (int i = 0; i < total_count && chart_idx < display_points; i++) {
        history_entry_t entry;
        if (history_get_entry(i, &entry) == 0 &&
            entry.timestamp >= cutoff_time && entry.player_count >= 0) {
            if (sample_counter % sample_rate == 0) {
                lv_chart_set_value_by_id(g_widgets->chart, g_widgets->series, chart_idx++, entry.player_count);
            }
            sample_counter++;
        }
    }

    lv_chart_refresh(g_widgets->chart);

    // Update X-axis time labels (5 labels from oldest to newest)
    for (int i = 0; i < 5; i++) {
        if (g_widgets->lbl_x_axis[i]) {
            // Calculate time for this label position (0=oldest/left, 4=newest/right)
            time_t label_time = now - range_seconds + (i * range_seconds / 4);
            struct tm *tm_info = localtime(&label_time);

            char time_buf[16];
            if (state->ui.current_history_range == HISTORY_RANGE_1H) {
                // For 1 hour, show HH:MM
                snprintf(time_buf, sizeof(time_buf), "%02d:%02d",
                         tm_info->tm_hour, (tm_info->tm_min / 15) * 15);
            } else if (state->ui.current_history_range == HISTORY_RANGE_WEEK) {
                // For 1 week, show day name
                strftime(time_buf, sizeof(time_buf), "%a", tm_info);
            } else {
                // For 8h and 24h, show HH:00 (rounded to hours)
                snprintf(time_buf, sizeof(time_buf), "%02d:00", tm_info->tm_hour);
            }
            lv_label_set_text(g_widgets->lbl_x_axis[i], time_buf);
        }
    }

    // Update legend
    if (g_widgets->lbl_legend) {
        char legend_text[96];
        if (entries_in_range > 0) {
            snprintf(legend_text, sizeof(legend_text), "%s (%d readings, range %d-%d)",
                     history_range_to_label(state->ui.current_history_range), entries_in_range,
                     min_players, max_players);
        } else {
            snprintf(legend_text, sizeof(legend_text), "%s (no data)",
                     history_range_to_label(state->ui.current_history_range));
        }
        lv_label_set_text(g_widgets->lbl_legend, legend_text);
    }
}
