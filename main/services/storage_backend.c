/**
 * DayZ Server Tracker - Storage Backend Implementation
 */

#include "storage_backend.h"
#include "drivers/sd_card.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

static const char *TAG = "storage";

storage_result_t storage_atomic_write(const char *path, const void *data, size_t len) {
    if (!path || !data || len == 0) {
        return STORAGE_INVALID_PARAM;
    }

    // Build temp file path
    char tmp_path[128];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    // Write to temp file
    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create temp file: %s (errno=%d)", tmp_path, errno);
        return STORAGE_FAIL;
    }

    size_t written = fwrite(data, 1, len, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "Incomplete write: %d/%d bytes", (int)written, (int)len);
        remove(tmp_path);
        return STORAGE_FAIL;
    }

    // Atomic rename
    if (rename(tmp_path, path) != 0) {
        ESP_LOGE(TAG, "Failed to rename %s -> %s (errno=%d)", tmp_path, path, errno);
        remove(tmp_path);
        return STORAGE_FAIL;
    }

    ESP_LOGD(TAG, "Atomic write: %s (%d bytes)", path, (int)len);
    return STORAGE_OK;
}

storage_result_t storage_atomic_write_text(const char *path, const char *text) {
    if (!text) {
        return STORAGE_INVALID_PARAM;
    }
    return storage_atomic_write(path, text, strlen(text));
}

storage_result_t storage_append(const char *path, const void *data, size_t len) {
    if (!path || !data || len == 0) {
        return STORAGE_INVALID_PARAM;
    }

    FILE *f = fopen(path, "ab");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open for append: %s (errno=%d)", path, errno);
        return STORAGE_FAIL;
    }

    size_t written = fwrite(data, 1, len, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "Incomplete append: %d/%d bytes", (int)written, (int)len);
        return STORAGE_FAIL;
    }

    return STORAGE_OK;
}

storage_result_t storage_append_line(const char *path, const char *line) {
    if (!path || !line) {
        return STORAGE_INVALID_PARAM;
    }

    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open for append: %s (errno=%d)", path, errno);
        return STORAGE_FAIL;
    }

    int ret = fprintf(f, "%s\n", line);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to write line to: %s", path);
        return STORAGE_FAIL;
    }

    return STORAGE_OK;
}

storage_result_t storage_read(const char *path, void *data, size_t max_len, size_t *actual_len) {
    if (!path || !data || max_len == 0) {
        return STORAGE_INVALID_PARAM;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        if (errno == ENOENT) {
            return STORAGE_NOT_FOUND;
        }
        ESP_LOGE(TAG, "Failed to open for read: %s (errno=%d)", path, errno);
        return STORAGE_FAIL;
    }

    size_t read_len = fread(data, 1, max_len, f);
    fclose(f);

    if (actual_len) {
        *actual_len = read_len;
    }

    return STORAGE_OK;
}

bool storage_file_exists(const char *path) {
    if (!path) return false;

    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

bool storage_dir_exists(const char *path) {
    if (!path) return false;

    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

storage_result_t storage_mkdir_p(const char *path) {
    if (!path) {
        return STORAGE_INVALID_PARAM;
    }

    // Check if already exists
    if (storage_dir_exists(path)) {
        return STORAGE_OK;
    }

    // Make a mutable copy for parsing
    char tmp[128];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    // Create each directory in the path
    char *p = tmp;

    // Skip leading slash
    if (*p == '/') p++;

    while (*p) {
        // Find next slash
        char *slash = strchr(p, '/');
        if (slash) {
            *slash = '\0';
        }

        // Try to create this directory
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            // Check if it exists anyway
            if (!storage_dir_exists(tmp)) {
                ESP_LOGE(TAG, "Failed to create directory: %s (errno=%d)", tmp, errno);
                return STORAGE_FAIL;
            }
        }

        if (slash) {
            *slash = '/';
            p = slash + 1;
        } else {
            break;
        }
    }

    return STORAGE_OK;
}

storage_result_t storage_delete(const char *path) {
    if (!path) {
        return STORAGE_INVALID_PARAM;
    }

    if (!storage_file_exists(path)) {
        return STORAGE_NOT_FOUND;
    }

    if (remove(path) != 0) {
        ESP_LOGE(TAG, "Failed to delete: %s (errno=%d)", path, errno);
        return STORAGE_FAIL;
    }

    return STORAGE_OK;
}

storage_result_t storage_get_size(const char *path, size_t *size) {
    if (!path || !size) {
        return STORAGE_INVALID_PARAM;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        if (errno == ENOENT) {
            return STORAGE_NOT_FOUND;
        }
        return STORAGE_FAIL;
    }

    *size = (size_t)st.st_size;
    return STORAGE_OK;
}
