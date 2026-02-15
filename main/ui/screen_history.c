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

    int total_count = history_get_count();

    // Single pass: count entries, find min/max, and collect sampled values
    int entries_in_range = 0;
    int min_players = 9999;
    int max_players = 0;

    // Temporary buffer for sampled player counts (max 120 chart points)
    int16_t sampled[120];
    int sampled_count = 0;

    // First, count entries in range to determine sample rate
    for (int i = 0; i < total_count; i++) {
        history_entry_t entry;
        if (history_get_entry(i, &entry) == 0 && entry.timestamp >= cutoff_time && entry.player_count >= 0) {
            entries_in_range++;
        }
    }

    int display_points = (entries_in_range > 120) ? 120 : (entries_in_range > 0 ? entries_in_range : 1);
    int sample_rate = (entries_in_range > 120) ? (entries_in_range / 120) : 1;

    // Second pass: collect samples and track min/max
    int sample_counter = 0;
    for (int i = 0; i < total_count && sampled_count < display_points; i++) {
        history_entry_t entry;
        if (history_get_entry(i, &entry) == 0 &&
            entry.timestamp >= cutoff_time && entry.player_count >= 0) {
            if (entry.player_count < min_players) min_players = entry.player_count;
            if (entry.player_count > max_players) max_players = entry.player_count;
            if (sample_counter % sample_rate == 0) {
                sampled[sampled_count++] = entry.player_count;
            }
            sample_counter++;
        }
    }

    // Calculate dynamic Y-axis range with padding
    int range_min, range_max;
    if (entries_in_range == 0 || min_players == 9999) {
        range_min = 0;
        range_max = 60;
    } else {
        range_min = (min_players > 10) ? (min_players - 10) : 0;
        range_max = max_players + 10;
        if (range_max - range_min < 20) {
            range_max = range_min + 20;
        }
        range_min = (range_min / 5) * 5;
        range_max = ((range_max + 4) / 5) * 5;
    }

    // Apply dynamic range to chart
    lv_chart_set_range(g_widgets->chart, LV_CHART_AXIS_PRIMARY_Y, range_min, range_max);

    // Update Y-axis labels
    int step = (range_max - range_min) / 4;
    if (step < 1) step = 1;
    for (int i = 0; i < 5; i++) {
        if (g_widgets->lbl_y_axis[i]) {
            char label[16];
            snprintf(label, sizeof(label), "%d", range_max - (i * step));
            lv_label_set_text(g_widgets->lbl_y_axis[i], label);
        }
    }

    // Populate chart from buffer
    lv_chart_set_point_count(g_widgets->chart, display_points);
    lv_chart_set_all_value(g_widgets->chart, g_widgets->series, LV_CHART_POINT_NONE);

    for (int i = 0; i < sampled_count; i++) {
        lv_chart_set_value_by_id(g_widgets->chart, g_widgets->series, i, sampled[i]);
    }

    lv_chart_refresh(g_widgets->chart);

    // Update X-axis time labels
    for (int i = 0; i < 5; i++) {
        if (g_widgets->lbl_x_axis[i]) {
            time_t label_time = now - range_seconds + (i * range_seconds / 4);
            struct tm *tm_info = localtime(&label_time);

            char time_buf[16];
            if (state->ui.current_history_range == HISTORY_RANGE_1H) {
                snprintf(time_buf, sizeof(time_buf), "%02d:%02d",
                         tm_info->tm_hour, (tm_info->tm_min / 15) * 15);
            } else if (state->ui.current_history_range == HISTORY_RANGE_WEEK) {
                strftime(time_buf, sizeof(time_buf), "%a", tm_info);
            } else {
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
