/**
 * DayZ Server Tracker - History Store Implementation
 */

#include "history_store.h"
#include "storage_paths.h"
#include "storage_backend.h"
#include "storage_config.h"
#include "nvs_keys.h"
#include "path_validator.h"
#include "config.h"
#include "drivers/sd_card.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>  // For fsync
#include "nvs.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "history_store";

// Track if root history directory was created successfully
static bool g_history_dir_created = false;

// Use storage_paths module for path building - wrapper functions for compatibility
static void build_history_file_path(int server_index, char *path, size_t path_size) {
    storage_path_history_bin(server_index, path, path_size);
}

static void build_nvs_key(int server_index, const char *suffix, char *key, size_t key_size) {
    // Use nvs_keys.h history key generator
    nvs_key_history(key, key_size, server_index, suffix);
}

static void build_json_dir_path(int server_index, char *path, size_t path_size) {
    storage_path_history_dir(server_index, path, path_size);
}

static void build_json_file_path(int server_index, const char *date_str, char *path, size_t path_size) {
    storage_path_history_json(server_index, date_str, path, path_size);
}

static void timestamp_to_date_str(uint32_t ts, char *buf, size_t buf_size) {
    storage_timestamp_to_date(ts, buf, buf_size);
}

void history_init(void) {
    // History buffer is allocated in app_state_init
    ESP_LOGI(TAG, "History store initialized");

    // Pre-create the history directory structure if SD card is available
    if (sd_card_is_mounted()) {
        // Create root history directory
        if (mkdir(HISTORY_JSON_DIR, 0755) == 0) {
            g_history_dir_created = true;
            ESP_LOGI(TAG, "Created history root directory: %s", HISTORY_JSON_DIR);
        } else {
            // Check if it already exists
            struct stat st;
            if (stat(HISTORY_JSON_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
                g_history_dir_created = true;
                ESP_LOGI(TAG, "History root directory exists: %s", HISTORY_JSON_DIR);
            }
        }

        // Pre-create directories for all configured servers
        if (g_history_dir_created) {
            app_state_t *state = app_state_get();
            for (int i = 0; i < state->settings.server_count; i++) {
                if (state->settings.servers[i].active) {
                    char server_dir[64];
                    build_json_dir_path(i, server_dir, sizeof(server_dir));
                    if (mkdir(server_dir, 0755) == 0) {
                        ESP_LOGI(TAG, "Created server directory: %s", server_dir);
                    }
                    // Ignore errors - directory might already exist
                }
            }
        }
    }
}

// NVS_SAVE_INTERVAL defined in storage_config.h

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
    int current_count = state->history.count;
    int unsaved = state->history.unsaved_count;

    app_state_unlock();

    ESP_LOGI(TAG, "History entry added: players=%d, total=%d, unsaved=%d",
             player_count, current_count, unsaved);

    // Try SD card first (if working)
    if (sd_card_is_mounted()) {
        history_append_entry_json(server_idx, timestamp, (int16_t)player_count);
    }

    // Save to NVS frequently (primary reliable storage when SD fails)
    if (unsaved >= NVS_SAVE_INTERVAL) {
        history_save_to_nvs(server_idx);

        // Also do SD binary backup if available
        if (sd_card_is_mounted()) {
            history_save_to_sd(server_idx);
        }
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

    // CS stays active permanently after mount - no toggling needed

    FILE *f = fopen(file_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open history file for writing: %s", file_path);
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

    // CS stays active permanently after mount - no toggling needed

    FILE *f = fopen(file_path, "rb");
    if (!f) {
        ESP_LOGI(TAG, "No history file found for server %d", server_index);
            return;
    }

    // Read header
    history_file_header_t header;
    if (fread(&header, sizeof(header), 1, f) != 1 || header.magic != HISTORY_FILE_MAGIC) {
        ESP_LOGW(TAG, "Invalid history file header for server %d", server_index);
        fclose(f);
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

    // ALWAYS save current server's history to NVS first (most reliable)
    if (old_server_index >= 0 && state->history.count > 0) {
        ESP_LOGI(TAG, "Saving %d entries for server %d before switch", state->history.count, old_server_index);
        history_save_to_nvs(old_server_index);  // NVS first - most reliable

        // Also try SD if available
        if (sd_card_is_mounted()) {
            history_save_to_sd(old_server_index);
        }
    }

    // Clear in-memory history
    history_clear();

    // Load new server's history - NVS FIRST (more reliable than SD)
    ESP_LOGI(TAG, "Loading history for server %d from NVS...", new_server_index);
    history_load_from_nvs(new_server_index);
    int nvs_count = state->history.count;
    ESP_LOGI(TAG, "Loaded %d entries from NVS", nvs_count);

    // If NVS is empty, try SD binary backup
    if (nvs_count == 0 && sd_card_is_mounted()) {
        ESP_LOGI(TAG, "NVS empty, trying SD binary backup...");
        history_load_from_sd(new_server_index);
        ESP_LOGI(TAG, "Loaded %d entries from SD binary", state->history.count);
    }

    // Try to load JSON data from SD (primary source for secondary servers)
    if (sd_card_is_mounted()) {
        time_t now;
        time(&now);
        uint32_t end_time = (uint32_t)now;
        uint32_t start_time = end_time - 604800;  // Last 7 days

        ESP_LOGI(TAG, "Loading JSON history for server %d (time range: %lu to %lu)",
                 new_server_index, (unsigned long)start_time, (unsigned long)end_time);

        // Allocate temp buffer for JSON entries
        history_entry_t *json_entries = malloc(MAX_HISTORY_ENTRIES * sizeof(history_entry_t));
        if (json_entries) {
            int json_count = history_load_range_json(new_server_index, start_time, end_time,
                                                      json_entries, MAX_HISTORY_ENTRIES);
            ESP_LOGI(TAG, "JSON load returned %d entries", json_count);

            if (json_count > 0) {
                // Clear buffer and use JSON as primary source (it has complete history)
                // JSON files are the authoritative source - NVS is just a backup
                if (app_state_lock(100)) {
                    state->history.head = 0;
                    state->history.count = 0;

                    // Load ALL JSON entries (already sorted oldest-first)
                    for (int i = 0; i < json_count && state->history.count < MAX_HISTORY_ENTRIES; i++) {
                        state->history.entries[state->history.head] = json_entries[i];
                        state->history.head = (state->history.head + 1) % MAX_HISTORY_ENTRIES;
                        state->history.count++;
                    }
                    app_state_unlock();

                    ESP_LOGI(TAG, "Loaded %d entries from JSON history", json_count);
                }
            }
            free(json_entries);
        } else {
            ESP_LOGE(TAG, "Failed to allocate memory for JSON entries buffer");
        }
    } else {
        ESP_LOGW(TAG, "SD card not mounted - skipping JSON history load");
    }

    ESP_LOGI(TAG, "History switched to server %d (%d entries)", new_server_index, state->history.count);
}

void history_load_json_for_server(int server_index) {
    if (!sd_card_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted - cannot load JSON history");
        return;
    }

    app_state_t *state = app_state_get();

    time_t now;
    time(&now);
    uint32_t end_time = (uint32_t)now;
    uint32_t start_time = end_time - 604800;  // Last 7 days

    ESP_LOGI(TAG, "Loading JSON history for server %d on boot", server_index);

    // Allocate temp buffer for JSON entries
    history_entry_t *json_entries = malloc(MAX_HISTORY_ENTRIES * sizeof(history_entry_t));
    if (!json_entries) {
        ESP_LOGE(TAG, "Failed to allocate memory for JSON entries buffer");
        return;
    }

    int json_count = history_load_range_json(server_index, start_time, end_time,
                                              json_entries, MAX_HISTORY_ENTRIES);
    ESP_LOGI(TAG, "JSON load returned %d entries", json_count);

    if (json_count > 0) {
        // Clear buffer and load JSON as primary source
        if (app_state_lock(100)) {
            state->history.head = 0;
            state->history.count = 0;

            // Load ALL JSON entries (already sorted oldest-first)
            for (int i = 0; i < json_count && state->history.count < MAX_HISTORY_ENTRIES; i++) {
                state->history.entries[state->history.head] = json_entries[i];
                state->history.head = (state->history.head + 1) % MAX_HISTORY_ENTRIES;
                state->history.count++;
            }
            app_state_unlock();

            ESP_LOGI(TAG, "Loaded %d entries from JSON history", state->history.count);
        }
    }

    free(json_entries);
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

// Use wrapper functions that delegate to storage_paths module (defined at top of file)

// Helper to create directory if it doesn't exist (with retry)
static esp_err_t ensure_directory(const char *path) {
    struct stat st;

    // Try stat first - if it succeeds and is a directory, we're done
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return ESP_OK;
        }
        return ESP_FAIL;  // Exists but not a directory
    }

    // stat failed - try to create directory with retries
    for (int retry = 0; retry < 3; retry++) {
        if (mkdir(path, 0755) == 0) {
            ESP_LOGI(TAG, "Created directory: %s", path);
            return ESP_OK;
        }

        // Check if it was created by another task in the meantime
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return ESP_OK;
        }

        // Small delay before retry
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGE(TAG, "Failed to create directory after retries: %s", path);
    return ESP_FAIL;
}

esp_err_t history_init_json_dir(int server_index) {
    if (!sd_card_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    // Create root history directory (only try once if it failed before)
    if (!g_history_dir_created) {
        esp_err_t ret = ensure_directory(HISTORY_JSON_DIR);
        if (ret == ESP_OK) {
            g_history_dir_created = true;
        } else {
            // Try without relying on stat - just try mkdir directly
            if (mkdir(HISTORY_JSON_DIR, 0755) == 0) {
                g_history_dir_created = true;
                ESP_LOGI(TAG, "Created history root directory on fallback");
            } else {
                return ret;
            }
        }
    }

    // Create server-specific directory
    char server_dir[64];
    build_json_dir_path(server_index, server_dir, sizeof(server_dir));
    return ensure_directory(server_dir);
}

esp_err_t history_append_entry_json(int server_index, uint32_t ts, int16_t players) {
    if (!sd_card_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    // Skip entries with invalid timestamp (SNTP not synced yet)
    if (ts < STORAGE_TIMESTAMP_MIN_VALID) {
        ESP_LOGW(TAG, "Skipping entry with invalid timestamp: %lu", (unsigned long)ts);
        return ESP_OK;
    }

    // Build paths
    char server_dir[64];
    build_json_dir_path(server_index, server_dir, sizeof(server_dir));

    char date_str[12];
    timestamp_to_date_str(ts, date_str, sizeof(date_str));

    char file_path[80];
    build_json_file_path(server_index, date_str, file_path, sizeof(file_path));

    // Create directories (ignore if exists)
    mkdir(HISTORY_JSON_DIR, 0755);
    mkdir(server_dir, 0755);

    // Verify directories exist
    struct stat st;
    if (stat(HISTORY_JSON_DIR, &st) != 0 || stat(server_dir, &st) != 0) {
        ESP_LOGE(TAG, "Failed to create history directories");
        return ESP_FAIL;
    }

    // Check if file exists (need to write header if new)
    bool file_exists = (stat(file_path, &st) == 0);

    FILE *f = fopen(file_path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open: %s (errno=%d)", file_path, errno);
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
    int written = fprintf(f, "{\"t\":%lu,\"p\":%d}\n", (unsigned long)ts, (int)players);
    if (written <= 0) {
        ESP_LOGE(TAG, "fprintf FAILED! ret=%d errno=%d", written, errno);
        fclose(f);
        return ESP_FAIL;
    }

    // Force flush to disk
    int flush_ret = fflush(f);
    if (flush_ret != 0) {
        ESP_LOGE(TAG, "fflush FAILED! ret=%d errno=%d", flush_ret, errno);
        fclose(f);
        return ESP_FAIL;
    }

    int sync_ret = fsync(fileno(f));
    if (sync_ret != 0) {
        ESP_LOGE(TAG, "fsync FAILED! ret=%d errno=%d", sync_ret, errno);
        fclose(f);
        return ESP_FAIL;
    }

    fclose(f);

    return ESP_OK;
}

int history_load_range_json(int server_index, uint32_t start_time, uint32_t end_time,
                            history_entry_t *entries, int max_entries) {
    if (!sd_card_is_mounted() || !entries || max_entries <= 0) {
        return -1;
    }

    char server_dir[64];
    build_json_dir_path(server_index, server_dir, sizeof(server_dir));

    // CS stays active permanently after mount - no toggling needed

    DIR *dir = opendir(server_dir);
    if (!dir) {
        ESP_LOGD(TAG, "No JSON history directory for server %d", server_index);
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
            // Parse JSON line using cJSON
            cJSON *json = cJSON_Parse(line_buf);
            if (!json) {
                continue;  // Skip invalid JSON lines
            }

            // Skip header lines (contain "sid")
            cJSON *sid = cJSON_GetObjectItem(json, "sid");
            if (sid) {
                cJSON_Delete(json);
                continue;
            }

            // Parse data line: {"t":timestamp,"p":players}
            cJSON *ts_item = cJSON_GetObjectItem(json, "t");
            cJSON *players_item = cJSON_GetObjectItem(json, "p");

            if (ts_item && players_item && cJSON_IsNumber(ts_item) && cJSON_IsNumber(players_item)) {
                uint32_t ts = (uint32_t)ts_item->valuedouble;
                int16_t players = (int16_t)players_item->valueint;

                // Filter by time range
                if (ts >= start_time && ts <= end_time) {
                    entries[loaded].timestamp = ts;
                    entries[loaded].player_count = players;
                    loaded++;
                }
            }

            cJSON_Delete(json);
        }

        fclose(f);
    }

    closedir(dir);

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

    // CS stays active permanently after mount - no toggling needed

    DIR *dir = opendir(server_dir);
    if (!dir) {
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

    // CS stays active permanently after mount - no toggling needed

    DIR *dir = opendir(server_dir);
    if (!dir) {
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

    return count;
}

void history_clear_all_storage(void) {
    ESP_LOGW(TAG, "=== CLEARING ALL HISTORY DATA ===");

    // Clear in-memory buffer
    history_clear();

    // Clear NVS for all servers
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        for (int i = 0; i < 5; i++) {  // Up to 5 servers
            char key_meta[16], key_data[16];
            build_nvs_key(i, "meta", key_meta, sizeof(key_meta));
            build_nvs_key(i, "data", key_data, sizeof(key_data));
            nvs_erase_key(nvs, key_meta);
            nvs_erase_key(nvs, key_data);
        }
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "NVS history cleared");
    }

    // Delete all JSON files from SD card
    if (sd_card_is_mounted()) {
        for (int server_idx = 0; server_idx < 5; server_idx++) {
            char server_dir[64];
            build_json_dir_path(server_idx, server_dir, sizeof(server_dir));

            DIR *dir = opendir(server_dir);
            if (!dir) continue;

            char file_path[320];  // Large enough for server_dir + any filename
            struct dirent *entry;
            int deleted = 0;

            while ((entry = readdir(dir)) != NULL) {
                size_t name_len = strlen(entry->d_name);
                // Only process .jsonl files with reasonable name length
                if (name_len >= 7 && name_len < 64 && strcmp(entry->d_name + name_len - 6, ".jsonl") == 0) {
                    snprintf(file_path, sizeof(file_path), "%s/%s", server_dir, entry->d_name);
                    if (remove(file_path) == 0) {
                        deleted++;
                    }
                }
            }
            closedir(dir);

            if (deleted > 0) {
                ESP_LOGI(TAG, "Deleted %d JSON files for server %d", deleted, server_idx);
            }
        }

        // Also delete binary history files
        remove("/sdcard/hist_0.bin");
        remove("/sdcard/hist_1.bin");
        remove("/sdcard/hist_2.bin");
        remove("/sdcard/hist_3.bin");
        remove("/sdcard/hist_4.bin");
        ESP_LOGI(TAG, "Binary history files deleted");
    }

    ESP_LOGW(TAG, "=== ALL HISTORY DATA CLEARED ===");
}
