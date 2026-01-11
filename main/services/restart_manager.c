/**
 * DayZ Server Tracker - Restart Manager Implementation
 */

#include "restart_manager.h"
#include "config.h"
#include "drivers/buzzer.h"
#include "settings_store.h"
#include "esp_log.h"
#include <time.h>

static const char *TAG = "restart_mgr";

void restart_record(server_config_t *srv, uint32_t timestamp) {
    restart_history_t *rh = &srv->restart_history;

    if (rh->last_restart_time > 0 &&
        (timestamp - rh->last_restart_time) < MIN_RESTART_INTERVAL_SEC) {
        ESP_LOGW(TAG, "Ignoring restart - too soon after last one");
        return;
    }

    if (rh->restart_count < MAX_RESTART_HISTORY) {
        rh->restart_times[rh->restart_count] = timestamp;
        rh->restart_count++;
    } else {
        for (int i = 0; i < MAX_RESTART_HISTORY - 1; i++) {
            rh->restart_times[i] = rh->restart_times[i + 1];
        }
        rh->restart_times[MAX_RESTART_HISTORY - 1] = timestamp;
    }

    rh->last_restart_time = timestamp;

    if (rh->restart_count >= 2) {
        uint32_t total_interval = 0;
        for (int i = 1; i < rh->restart_count; i++) {
            total_interval += rh->restart_times[i] - rh->restart_times[i - 1];
        }
        rh->avg_interval_sec = total_interval / (rh->restart_count - 1);
        ESP_LOGI(TAG, "Server restart detected! Avg interval: %luh %lum",
                 rh->avg_interval_sec / 3600, (rh->avg_interval_sec % 3600) / 60);
    } else {
        ESP_LOGI(TAG, "Server restart detected at %lu", (unsigned long)timestamp);
    }

    // Save to NVS so restart time persists
    settings_save();

    buzzer_alert_restart();
}

void restart_check_for_restart(server_config_t *srv, int current_count) {
    restart_history_t *rh = &srv->restart_history;

    // Record restart if we see 0 players - the restart_record function
    // will reject if too soon after last recorded restart
    if (current_count == RESTART_DETECT_DROP_TO) {
        time_t now;
        time(&now);
        restart_record(srv, (uint32_t)now);
    }

    rh->last_known_players = current_count;
}

int restart_get_countdown(server_config_t *srv) {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    if (srv->manual_restart_set && srv->restart_interval_hours > 0) {
        int interval_sec = srv->restart_interval_hours * 3600;

        struct tm restart_tm = timeinfo;
        restart_tm.tm_hour = srv->restart_hour;
        restart_tm.tm_min = srv->restart_minute;
        restart_tm.tm_sec = 0;
        time_t first_restart_today = mktime(&restart_tm);

        time_t next_restart = first_restart_today;
        while (next_restart <= now) {
            next_restart += interval_sec;
        }
        while (next_restart > now + interval_sec) {
            next_restart -= interval_sec;
        }

        int countdown = (int)(next_restart - now);
        return countdown > 0 ? countdown : 0;
    }

    restart_history_t *rh = &srv->restart_history;
    if (rh->restart_count >= 2 && rh->avg_interval_sec > 0) {
        uint32_t predicted_next = rh->last_restart_time + rh->avg_interval_sec;
        if ((uint32_t)now >= predicted_next) {
            return 0;
        }
        return predicted_next - (uint32_t)now;
    }

    return -1;
}

void restart_format_countdown(int seconds, char *buf, size_t buf_size) {
    if (seconds < 0) {
        snprintf(buf, buf_size, "Unknown");
    } else if (seconds == 0) {
        snprintf(buf, buf_size, "Imminent!");
    } else {
        int hours = seconds / 3600;
        int mins = (seconds % 3600) / 60;
        if (hours > 0) {
            snprintf(buf, buf_size, "~%dh %dm", hours, mins);
        } else {
            snprintf(buf, buf_size, "~%dm", mins);
        }
    }
}

int restart_get_time_since_last(server_config_t *srv) {
    restart_history_t *rh = &srv->restart_history;

    if (rh->last_restart_time == 0) {
        return -1;
    }

    time_t now;
    time(&now);

    return (int)((uint32_t)now - rh->last_restart_time);
}

void restart_format_time_since(int seconds, char *buf, size_t buf_size) {
    if (seconds < 0) {
        snprintf(buf, buf_size, "No restart detected");
        return;
    }

    int hours = seconds / 3600;
    int mins = (seconds % 3600) / 60;

    if (hours >= 24) {
        int days = hours / 24;
        hours = hours % 24;
        snprintf(buf, buf_size, "%dd %dh ago", days, hours);
    } else if (hours > 0) {
        snprintf(buf, buf_size, "%dh %dm ago", hours, mins);
    } else if (mins > 0) {
        snprintf(buf, buf_size, "%dm ago", mins);
    } else {
        snprintf(buf, buf_size, "Just now");
    }
}

void restart_format_last_time(server_config_t *srv, char *buf, size_t buf_size) {
    restart_history_t *rh = &srv->restart_history;

    if (rh->last_restart_time == 0) {
        buf[0] = '\0';
        return;
    }

    time_t restart_time = (time_t)rh->last_restart_time;
    struct tm timeinfo;
    localtime_r(&restart_time, &timeinfo);

    // Format as HH:MM CET
    snprintf(buf, buf_size, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
}

void restart_check_stale_and_reset(server_config_t *srv) {
    restart_history_t *rh = &srv->restart_history;

    time_t now;
    time(&now);

    // If no restart recorded, or last restart is more than 12 hours ago,
    // reset to current time as baseline
    #define STALE_THRESHOLD_SEC (12 * 3600)

    if (rh->last_restart_time == 0 ||
        ((uint32_t)now - rh->last_restart_time) > STALE_THRESHOLD_SEC) {

        ESP_LOGI(TAG, "Restart data stale (>12h), resetting to now");

        // Clear history and start fresh
        rh->restart_count = 1;
        rh->restart_times[0] = (uint32_t)now;
        rh->last_restart_time = (uint32_t)now;
        rh->avg_interval_sec = 0;

        settings_save();
    }
}
