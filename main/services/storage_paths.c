/**
 * DayZ Server Tracker - Storage Paths Implementation
 */

#include "storage_paths.h"
#include "storage_config.h"
#include "path_validator.h"
#include "config.h"
#include <stdio.h>
#include <time.h>

void storage_path_history_bin(int server_idx, char *buf, size_t buf_size) {
    path_build_safe(buf, buf_size, "%s%d.bin", STORAGE_HISTORY_BIN_PREFIX, server_idx);
}

void storage_path_history_dir(int server_idx, char *buf, size_t buf_size) {
    path_build_safe(buf, buf_size, "%s/server_%d", STORAGE_HISTORY_JSON_DIR, server_idx);
}

void storage_path_history_json(int server_idx, const char *date_str, char *buf, size_t buf_size) {
    path_build_safe(buf, buf_size, "%s/server_%d/%s.jsonl", STORAGE_HISTORY_JSON_DIR, server_idx, date_str);
}

void storage_path_config(char *buf, size_t buf_size) {
    path_build_safe(buf, buf_size, "%s", STORAGE_CONFIG_JSON_FILE);
}

void storage_path_history_root(char *buf, size_t buf_size) {
    path_build_safe(buf, buf_size, "%s", STORAGE_HISTORY_JSON_DIR);
}

void storage_nvs_key(int server_idx, const char *suffix, char *key, size_t key_size) {
    snprintf(key, key_size, "h%d_%s", server_idx, suffix);
}

void storage_timestamp_to_date(uint32_t timestamp, char *buf, size_t buf_size) {
    time_t t = (time_t)timestamp;
    struct tm *tm_info = localtime(&t);
    strftime(buf, buf_size, "%Y-%m-%d", tm_info);
}
