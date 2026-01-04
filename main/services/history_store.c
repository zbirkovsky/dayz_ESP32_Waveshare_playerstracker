/**
 * DayZ Server Tracker - History Store Implementation
 */

#include "history_store.h"
#include "../config.h"
#include "../drivers/sd_card.h"
#include <string.h>
#include <time.h>
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "history_store";

void history_init(void) {
    // History buffer is allocated in app_state_init
    ESP_LOGI(TAG, "History store initialized");
}

void history_add_entry(int player_count) {
    app_state_t *state = app_state_get();

    if (!state->history.entries) return;

    if (!app_state_lock(100)) return;

    time_t now;
    time(&now);

    state->history.entries[state->history.head].timestamp = (uint32_t)now;
    state->history.entries[state->history.head].player_count = (int16_t)player_count;

    state->history.head = (state->history.head + 1) % MAX_HISTORY_ENTRIES;
    if (state->history.count < MAX_HISTORY_ENTRIES) {
        state->history.count++;
    }

    state->history.unsaved_count++;

    app_state_unlock();

    // Periodic save
    if (state->history.unsaved_count >= HISTORY_SAVE_INTERVAL) {
        if (sd_card_is_mounted()) {
            history_save_to_sd();
        }
        history_save_to_nvs();
    }
}

int history_get_entry(int index, history_entry_t *entry) {
    app_state_t *state = app_state_get();

    if (!state->history.entries || index >= state->history.count) {
        return -1;
    }

    int actual_index;
    if (state->history.count < MAX_HISTORY_ENTRIES) {
        actual_index = index;
    } else {
        actual_index = (state->history.head + index) % MAX_HISTORY_ENTRIES;
    }

    *entry = state->history.entries[actual_index];
    return 0;
}

int history_get_count(void) {
    app_state_t *state = app_state_get();
    return state->history.count;
}

int history_count_in_range(uint32_t range_seconds) {
    app_state_t *state = app_state_get();

    time_t now;
    time(&now);
    uint32_t cutoff_time = (uint32_t)now - range_seconds;

    int count = 0;
    for (int i = 0; i < state->history.count; i++) {
        history_entry_t entry;
        if (history_get_entry(i, &entry) == 0 && entry.timestamp >= cutoff_time) {
            count++;
        }
    }

    return count;
}

void history_save_to_sd(void) {
    app_state_t *state = app_state_get();

    if (!sd_card_is_mounted() || !state->history.entries) {
        ESP_LOGD(TAG, "Cannot save: SD not mounted or no history");
        return;
    }

    sd_card_set_cs(true);

    FILE *f = fopen(HISTORY_FILE_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open history file for writing");
        sd_card_set_cs(false);
        return;
    }

    // Write header
    history_file_header_t header = {
        .magic = HISTORY_FILE_MAGIC,
        .head = state->history.head,
        .count = state->history.count
    };
    fwrite(&header, sizeof(header), 1, f);

    // Write history entries
    if (state->history.count > 0) {
        int entries_to_write = (state->history.count < MAX_HISTORY_ENTRIES)
                               ? state->history.count : MAX_HISTORY_ENTRIES;
        fwrite(state->history.entries, sizeof(history_entry_t), entries_to_write, f);
    }

    fclose(f);
    sd_card_set_cs(false);

    state->history.unsaved_count = 0;
    ESP_LOGI(TAG, "History saved to SD (%d entries)", state->history.count);
}

void history_load_from_sd(void) {
    app_state_t *state = app_state_get();

    if (!sd_card_is_mounted() || !state->history.entries) {
        ESP_LOGD(TAG, "Cannot load: SD not mounted or no history buffer");
        return;
    }

    sd_card_set_cs(true);

    FILE *f = fopen(HISTORY_FILE_PATH, "rb");
    if (!f) {
        ESP_LOGI(TAG, "No history file found on SD card");
        sd_card_set_cs(false);
        return;
    }

    // Read header
    history_file_header_t header;
    if (fread(&header, sizeof(header), 1, f) != 1 || header.magic != HISTORY_FILE_MAGIC) {
        ESP_LOGW(TAG, "Invalid history file header");
        fclose(f);
        sd_card_set_cs(false);
        return;
    }

    // Read entries
    if (app_state_lock(100)) {
        state->history.head = header.head;
        state->history.count = header.count;

        if (state->history.count > MAX_HISTORY_ENTRIES) {
            state->history.count = MAX_HISTORY_ENTRIES;
        }

        int entries_to_read = (state->history.count < MAX_HISTORY_ENTRIES)
                              ? state->history.count : MAX_HISTORY_ENTRIES;
        size_t read_count = fread(state->history.entries, sizeof(history_entry_t),
                                   entries_to_read, f);

        if (read_count != entries_to_read) {
            ESP_LOGW(TAG, "Partial history read: %d/%d", (int)read_count, entries_to_read);
            state->history.count = read_count;
        }

        app_state_unlock();
    }

    fclose(f);
    sd_card_set_cs(false);

    ESP_LOGI(TAG, "History loaded from SD (%d entries)", state->history.count);
}

void history_save_to_nvs(void) {
    app_state_t *state = app_state_get();

    if (!state->history.entries || state->history.count == 0) return;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for history save");
        return;
    }

    // Save metadata (head, count)
    uint32_t meta = ((uint32_t)state->history.head << 16) |
                    (state->history.count & 0xFFFF);
    nvs_set_u32(nvs, NVS_HISTORY_META, meta);

    // Save most recent entries (up to NVS_HISTORY_MAX)
    int entries_to_save = (state->history.count < NVS_HISTORY_MAX)
                          ? state->history.count : NVS_HISTORY_MAX;
    int start_idx = (state->history.count > NVS_HISTORY_MAX)
                    ? (state->history.count - NVS_HISTORY_MAX) : 0;

    // Create temporary buffer with recent entries in order
    history_entry_t *temp = malloc(entries_to_save * sizeof(history_entry_t));
    if (temp) {
        for (int i = 0; i < entries_to_save; i++) {
            history_entry_t entry;
            if (history_get_entry(start_idx + i, &entry) == 0) {
                temp[i] = entry;
            }
        }
        nvs_set_blob(nvs, NVS_HISTORY_KEY, temp, entries_to_save * sizeof(history_entry_t));
        free(temp);
    }

    nvs_commit(nvs);
    nvs_close(nvs);

    state->history.unsaved_count = 0;
    ESP_LOGI(TAG, "History backed up to NVS (%d entries)", entries_to_save);
}

void history_load_from_nvs(void) {
    app_state_t *state = app_state_get();

    if (!state->history.entries) return;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "No NVS history backup found");
        return;
    }

    // Load metadata
    uint32_t meta = 0;
    if (nvs_get_u32(nvs, NVS_HISTORY_META, &meta) != ESP_OK) {
        nvs_close(nvs);
        return;
    }

    // Load entries
    size_t required_size = 0;
    if (nvs_get_blob(nvs, NVS_HISTORY_KEY, NULL, &required_size) != ESP_OK ||
        required_size == 0) {
        nvs_close(nvs);
        return;
    }

    history_entry_t *temp = malloc(required_size);
    if (temp && nvs_get_blob(nvs, NVS_HISTORY_KEY, temp, &required_size) == ESP_OK) {
        if (app_state_lock(100)) {
            int loaded_count = required_size / sizeof(history_entry_t);
            for (int i = 0; i < loaded_count && i < MAX_HISTORY_ENTRIES; i++) {
                state->history.entries[i] = temp[i];
            }
            state->history.count = loaded_count;
            state->history.head = loaded_count % MAX_HISTORY_ENTRIES;
            app_state_unlock();
        }
        ESP_LOGI(TAG, "History restored from NVS (%d entries)",
                 (int)(required_size / sizeof(history_entry_t)));
    }
    if (temp) free(temp);

    nvs_close(nvs);
}

void history_clear(void) {
    app_state_t *state = app_state_get();

    if (app_state_lock(100)) {
        state->history.head = 0;
        state->history.count = 0;
        state->history.unsaved_count = 0;
        if (state->history.entries) {
            memset(state->history.entries, 0,
                   MAX_HISTORY_ENTRIES * sizeof(history_entry_t));
        }
        app_state_unlock();
    }

    ESP_LOGI(TAG, "History cleared");
}

uint32_t history_range_to_seconds(history_range_t range) {
    switch (range) {
        case HISTORY_RANGE_1H:   return 3600;        // 1 hour
        case HISTORY_RANGE_8H:   return 28800;       // 8 hours
        case HISTORY_RANGE_24H:  return 86400;       // 24 hours
        case HISTORY_RANGE_WEEK: return 604800;      // 7 days
        default:                 return 3600;
    }
}

const char* history_range_to_label(history_range_t range) {
    switch (range) {
        case HISTORY_RANGE_1H:   return "Last 1 Hour";
        case HISTORY_RANGE_8H:   return "Last 8 Hours";
        case HISTORY_RANGE_24H:  return "Last 24 Hours";
        case HISTORY_RANGE_WEEK: return "Last 7 Days";
        default:                 return "Last 1 Hour";
    }
}
