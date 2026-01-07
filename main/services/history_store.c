/**
 * DayZ Server Tracker - History Store Implementation
 */

#include "history_store.h"
#include "../config.h"
#include "../drivers/sd_card.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "history_store";

// Helper to build file path for a server
static void build_history_file_path(int server_index, char *path, size_t path_size) {
    snprintf(path, path_size, "%s%d.bin", HISTORY_FILE_PREFIX, server_index);
}

// Helper to build NVS key for a server
static void build_nvs_key(int server_index, const char *suffix, char *key, size_t key_size) {
    snprintf(key, key_size, "h%d_%s", server_index, suffix);
}

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
    uint32_t timestamp = (uint32_t)now;

    state->history.entries[state->history.head].timestamp = timestamp;
    state->history.entries[state->history.head].player_count = (int16_t)player_count;

    state->history.head = (state->history.head + 1) % MAX_HISTORY_ENTRIES;
    if (state->history.count < MAX_HISTORY_ENTRIES) {
        state->history.count++;
    }

    state->history.unsaved_count++;
    int server_idx = state->settings.active_server_index;

    app_state_unlock();

    // Always append to JSON (primary storage)
    if (sd_card_is_mounted()) {
        history_append_entry_json(server_idx, timestamp, (int16_t)player_count);
    }

    // Periodic save for the active server (binary backup + NVS)
    if (state->history.unsaved_count >= HISTORY_SAVE_INTERVAL) {
        if (sd_card_is_mounted()) {
            history_save_to_sd(server_idx);  // Binary backup
        }
        history_save_to_nvs(server_idx);  // NVS backup
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

void history_save_to_sd(int server_index) {
    app_state_t *state = app_state_get();

    if (!sd_card_is_mounted() || !state->history.entries) {
        ESP_LOGD(TAG, "Cannot save: SD not mounted or no history");
        return;
    }

    char file_path[64];
    build_history_file_path(server_index, file_path, sizeof(file_path));

    sd_card_set_cs(true);

    FILE *f = fopen(file_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open history file for writing: %s", file_path);
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
    ESP_LOGI(TAG, "History saved to SD for server %d (%d entries)", server_index, state->history.count);
}

void history_load_from_sd(int server_index) {
    app_state_t *state = app_state_get();

    if (!sd_card_is_mounted() || !state->history.entries) {
        ESP_LOGD(TAG, "Cannot load: SD not mounted or no history buffer");
        return;
    }

    char file_path[64];
    build_history_file_path(server_index, file_path, sizeof(file_path));

    sd_card_set_cs(true);

    FILE *f = fopen(file_path, "rb");
    if (!f) {
        ESP_LOGI(TAG, "No history file found for server %d", server_index);
        sd_card_set_cs(false);
        return;
    }

    // Read header
    history_file_header_t header;
    if (fread(&header, sizeof(header), 1, f) != 1 || header.magic != HISTORY_FILE_MAGIC) {
        ESP_LOGW(TAG, "Invalid history file header for server %d", server_index);
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

    ESP_LOGI(TAG, "History loaded from SD for server %d (%d entries)", server_index, state->history.count);
}

void history_save_to_nvs(int server_index) {
    app_state_t *state = app_state_get();

    if (!state->history.entries || state->history.count == 0) return;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for history save");
        return;
    }

    char key_meta[16], key_data[16];
    build_nvs_key(server_index, "meta", key_meta, sizeof(key_meta));
    build_nvs_key(server_index, "data", key_data, sizeof(key_data));

    // Save metadata (head, count)
    uint32_t meta = ((uint32_t)state->history.head << 16) |
                    (state->history.count & 0xFFFF);
    nvs_set_u32(nvs, key_meta, meta);

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
        nvs_set_blob(nvs, key_data, temp, entries_to_save * sizeof(history_entry_t));
        free(temp);
    }

    nvs_commit(nvs);
    nvs_close(nvs);

    state->history.unsaved_count = 0;
    ESP_LOGI(TAG, "History backed up to NVS for server %d (%d entries)", server_index, entries_to_save);
}

void history_load_from_nvs(int server_index) {
    app_state_t *state = app_state_get();

    if (!state->history.entries) return;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "No NVS history backup found for server %d", server_index);
        return;
    }

    char key_meta[16], key_data[16];
    build_nvs_key(server_index, "meta", key_meta, sizeof(key_meta));
    build_nvs_key(server_index, "data", key_data, sizeof(key_data));

    // Load metadata
    uint32_t meta = 0;
    if (nvs_get_u32(nvs, key_meta, &meta) != ESP_OK) {
        nvs_close(nvs);
        return;
    }

    // Load entries
    size_t required_size = 0;
    if (nvs_get_blob(nvs, key_data, NULL, &required_size) != ESP_OK ||
        required_size == 0) {
        nvs_close(nvs);
        return;
    }

    history_entry_t *temp = malloc(required_size);
    if (temp && nvs_get_blob(nvs, key_data, temp, &required_size) == ESP_OK) {
        if (app_state_lock(100)) {
            int loaded_count = required_size / sizeof(history_entry_t);
            for (int i = 0; i < loaded_count && i < MAX_HISTORY_ENTRIES; i++) {
                state->history.entries[i] = temp[i];
            }
            state->history.count = loaded_count;
            state->history.head = loaded_count % MAX_HISTORY_ENTRIES;
            app_state_unlock();
        }
        ESP_LOGI(TAG, "History restored from NVS for server %d (%d entries)",
                 server_index, (int)(required_size / sizeof(history_entry_t)));
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

void history_switch_server(int old_server_index, int new_server_index) {
    app_state_t *state = app_state_get();

    ESP_LOGI(TAG, "Switching history from server %d to server %d", old_server_index, new_server_index);

    // Save current server's history (if we have one and there's data)
    if (old_server_index >= 0 && state->history.count > 0) {
        if (sd_card_is_mounted()) {
            history_save_to_sd(old_server_index);
        }
        history_save_to_nvs(old_server_index);
    }

    // Clear in-memory history
    history_clear();

    // Load new server's history (try SD binary first, then NVS)
    if (sd_card_is_mounted()) {
        history_load_from_sd(new_server_index);
    }

    // If no data loaded from SD binary, try NVS
    if (state->history.count == 0) {
        history_load_from_nvs(new_server_index);
    }

    // Also load recent data from JSON files (may have more recent data from secondary tracking)
    if (sd_card_is_mounted()) {
        time_t now;
        time(&now);
        uint32_t end_time = (uint32_t)now;
        uint32_t start_time = end_time - 604800;  // Last 7 days

        // Allocate temp buffer for JSON entries
        history_entry_t *json_entries = malloc(MAX_HISTORY_ENTRIES * sizeof(history_entry_t));
        if (json_entries) {
            int json_count = history_load_range_json(new_server_index, start_time, end_time,
                                                      json_entries, MAX_HISTORY_ENTRIES);
            if (json_count > 0) {
                ESP_LOGI(TAG, "Merging %d entries from JSON history", json_count);

                // Merge JSON entries with existing history
                // Find the latest timestamp in current history
                uint32_t latest_ts = 0;
                for (int i = 0; i < state->history.count; i++) {
                    history_entry_t entry;
                    if (history_get_entry(i, &entry) == 0 && entry.timestamp > latest_ts) {
                        latest_ts = entry.timestamp;
                    }
                }

                // Add JSON entries that are newer than existing history
                if (app_state_lock(100)) {
                    int added = 0;
                    for (int i = 0; i < json_count; i++) {
                        // Only add if newer than existing data (or if no existing data)
                        if (json_entries[i].timestamp > latest_ts ||
                            (latest_ts == 0 && state->history.count < MAX_HISTORY_ENTRIES)) {
                            state->history.entries[state->history.head] = json_entries[i];
                            state->history.head = (state->history.head + 1) % MAX_HISTORY_ENTRIES;
                            if (state->history.count < MAX_HISTORY_ENTRIES) {
                                state->history.count++;
                            }
                            added++;
                        }
                    }
                    app_state_unlock();

                    if (added > 0) {
                        ESP_LOGI(TAG, "Added %d new entries from JSON", added);
                    }
                }
            }
            free(json_entries);
        }
    }

    ESP_LOGI(TAG, "History switched to server %d (%d entries)", new_server_index, state->history.count);
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

// ============== JSON HISTORY STORAGE ==============

// Helper to build server directory path
static void build_json_dir_path(int server_index, char *path, size_t path_size) {
    snprintf(path, path_size, "%s/server_%d", HISTORY_JSON_DIR, server_index);
}

// Helper to build daily JSON file path
static void build_json_file_path(int server_index, const char *date_str, char *path, size_t path_size) {
    snprintf(path, path_size, "%s/server_%d/%s.jsonl", HISTORY_JSON_DIR, server_index, date_str);
}

// Helper to get date string from timestamp (YYYY-MM-DD)
static void timestamp_to_date_str(uint32_t ts, char *buf, size_t buf_size) {
    time_t t = (time_t)ts;
    struct tm *tm_info = localtime(&t);
    strftime(buf, buf_size, "%Y-%m-%d", tm_info);
}

// Helper to create directory if it doesn't exist
static esp_err_t ensure_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return ESP_OK;
        }
        return ESP_FAIL;  // Exists but not a directory
    }

    if (mkdir(path, 0755) == 0) {
        ESP_LOGI(TAG, "Created directory: %s", path);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to create directory: %s", path);
    return ESP_FAIL;
}

esp_err_t history_init_json_dir(int server_index) {
    if (!sd_card_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    // Create root history directory
    esp_err_t ret = ensure_directory(HISTORY_JSON_DIR);
    if (ret != ESP_OK) return ret;

    // Create server-specific directory
    char server_dir[64];
    build_json_dir_path(server_index, server_dir, sizeof(server_dir));
    return ensure_directory(server_dir);
}

esp_err_t history_append_entry_json(int server_index, uint32_t ts, int16_t players) {
    if (!sd_card_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    // Ensure directory exists
    esp_err_t ret = history_init_json_dir(server_index);
    if (ret != ESP_OK) return ret;

    // Build file path for today
    char date_str[12];
    timestamp_to_date_str(ts, date_str, sizeof(date_str));

    char file_path[80];
    build_json_file_path(server_index, date_str, file_path, sizeof(file_path));

    sd_card_set_cs(true);

    // Check if file exists (need to write header if new)
    struct stat st;
    bool file_exists = (stat(file_path, &st) == 0);

    FILE *f = fopen(file_path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open JSON history file: %s", file_path);
        sd_card_set_cs(false);
        return ESP_FAIL;
    }

    // Write header if new file
    if (!file_exists) {
        app_state_t *state = app_state_get();
        server_config_t *server = &state->settings.servers[server_index];

        fprintf(f, "{\"v\":%d,\"sid\":\"%s\",\"d\":\"%s\"}\n",
                JSON_HISTORY_VERSION, server->server_id, date_str);
        ESP_LOGI(TAG, "Created new JSON history file: %s", file_path);
    }

    // Append data entry (compact format)
    fprintf(f, "{\"t\":%lu,\"p\":%d}\n", (unsigned long)ts, (int)players);

    fclose(f);
    sd_card_set_cs(false);

    return ESP_OK;
}

int history_load_range_json(int server_index, uint32_t start_time, uint32_t end_time,
                            history_entry_t *entries, int max_entries) {
    if (!sd_card_is_mounted() || !entries || max_entries <= 0) {
        return -1;
    }

    char server_dir[64];
    build_json_dir_path(server_index, server_dir, sizeof(server_dir));

    sd_card_set_cs(true);

    DIR *dir = opendir(server_dir);
    if (!dir) {
        ESP_LOGD(TAG, "No JSON history directory for server %d", server_index);
        sd_card_set_cs(false);
        return 0;
    }

    int loaded = 0;
    char line_buf[128];
    char file_path[128];  // Large enough for /sdcard/history/server_X/YYYY-MM-DD.jsonl
    struct dirent *entry;

    // Iterate through .jsonl files
    while ((entry = readdir(dir)) != NULL && loaded < max_entries) {
        // Skip non-.jsonl files (only allow YYYY-MM-DD.jsonl format, 16 chars)
        size_t name_len = strlen(entry->d_name);
        if (name_len != 16 || strcmp(entry->d_name + 10, ".jsonl") != 0) {
            continue;
        }

        snprintf(file_path, sizeof(file_path), "%s/%.16s", server_dir, entry->d_name);

        FILE *f = fopen(file_path, "r");
        if (!f) continue;

        // Read line by line
        while (fgets(line_buf, sizeof(line_buf), f) && loaded < max_entries) {
            // Skip header lines (contain "v" and "sid")
            if (strstr(line_buf, "\"sid\"") != NULL) {
                continue;
            }

            // Parse data line: {"t":timestamp,"p":players}
            uint32_t ts = 0;
            int16_t players = 0;

            char *t_ptr = strstr(line_buf, "\"t\":");
            char *p_ptr = strstr(line_buf, "\"p\":");

            if (t_ptr && p_ptr) {
                ts = (uint32_t)strtoul(t_ptr + 4, NULL, 10);
                players = (int16_t)atoi(p_ptr + 4);

                // Filter by time range
                if (ts >= start_time && ts <= end_time) {
                    entries[loaded].timestamp = ts;
                    entries[loaded].player_count = players;
                    loaded++;
                }
            }
        }

        fclose(f);
    }

    closedir(dir);
    sd_card_set_cs(false);

    // Sort entries by timestamp (oldest first) using simple bubble sort
    // (should be mostly sorted already from files)
    for (int i = 0; i < loaded - 1; i++) {
        for (int j = 0; j < loaded - i - 1; j++) {
            if (entries[j].timestamp > entries[j + 1].timestamp) {
                history_entry_t temp = entries[j];
                entries[j] = entries[j + 1];
                entries[j + 1] = temp;
            }
        }
    }

    ESP_LOGI(TAG, "Loaded %d JSON entries for server %d", loaded, server_index);
    return loaded;
}

int history_cleanup_old_files(int server_index, int days_to_keep) {
    if (!sd_card_is_mounted() || days_to_keep <= 0) {
        return 0;
    }

    char server_dir[64];
    build_json_dir_path(server_index, server_dir, sizeof(server_dir));

    // Calculate cutoff date
    time_t now;
    time(&now);
    time_t cutoff = now - (days_to_keep * 86400);

    char cutoff_date[12];
    struct tm *tm_info = localtime(&cutoff);
    strftime(cutoff_date, sizeof(cutoff_date), "%Y-%m-%d", tm_info);

    sd_card_set_cs(true);

    DIR *dir = opendir(server_dir);
    if (!dir) {
        sd_card_set_cs(false);
        return 0;
    }

    int deleted = 0;
    char file_path[128];  // Large enough for /sdcard/history/server_X/YYYY-MM-DD.jsonl
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        // Only process YYYY-MM-DD.jsonl files (exactly 16 chars)
        size_t name_len = strlen(entry->d_name);
        if (name_len != 16 || strcmp(entry->d_name + 10, ".jsonl") != 0) {
            continue;
        }

        // Extract date from filename (YYYY-MM-DD.jsonl)
        char file_date[12] = {0};
        strncpy(file_date, entry->d_name, 10);
        file_date[10] = '\0';

        // Compare dates lexicographically (YYYY-MM-DD format sorts correctly)
        if (strcmp(file_date, cutoff_date) < 0) {
            snprintf(file_path, sizeof(file_path), "%s/%.16s", server_dir, entry->d_name);

            if (remove(file_path) == 0) {
                ESP_LOGI(TAG, "Deleted old history file: %s", file_path);
                deleted++;
            }
        }
    }

    closedir(dir);
    sd_card_set_cs(false);

    if (deleted > 0) {
        ESP_LOGI(TAG, "Cleaned up %d old history files for server %d", deleted, server_index);
    }

    return deleted;
}

int history_get_json_file_count(int server_index) {
    if (!sd_card_is_mounted()) {
        return -1;
    }

    char server_dir[64];
    build_json_dir_path(server_index, server_dir, sizeof(server_dir));

    sd_card_set_cs(true);

    DIR *dir = opendir(server_dir);
    if (!dir) {
        sd_card_set_cs(false);
        return 0;
    }

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        size_t name_len = strlen(entry->d_name);
        if (name_len >= 7 && strcmp(entry->d_name + name_len - 6, ".jsonl") == 0) {
            count++;
        }
    }

    closedir(dir);
    sd_card_set_cs(false);

    return count;
}
