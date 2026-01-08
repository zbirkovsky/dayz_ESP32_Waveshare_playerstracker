/**
 * DayZ Server Tracker - Storage Backend
 * Unified storage abstraction with atomic writes
 */

#ifndef STORAGE_BACKEND_H
#define STORAGE_BACKEND_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Storage result codes
typedef enum {
    STORAGE_OK = 0,
    STORAGE_FAIL,
    STORAGE_NOT_FOUND,
    STORAGE_NO_SPACE,
    STORAGE_INVALID_PARAM
} storage_result_t;

/**
 * Write data to a file atomically (write to .tmp, then rename)
 * This ensures the file is never left in a corrupted state
 * @param path Target file path
 * @param data Data to write
 * @param len Length of data
 * @return STORAGE_OK on success
 */
storage_result_t storage_atomic_write(const char *path, const void *data, size_t len);

/**
 * Write text to a file atomically
 * @param path Target file path
 * @param text Null-terminated string
 * @return STORAGE_OK on success
 */
storage_result_t storage_atomic_write_text(const char *path, const char *text);

/**
 * Append data to a file (not atomic - for log-style files)
 * @param path Target file path
 * @param data Data to append
 * @param len Length of data
 * @return STORAGE_OK on success
 */
storage_result_t storage_append(const char *path, const void *data, size_t len);

/**
 * Append text line to a file with flush
 * @param path Target file path
 * @param line Null-terminated string (newline added automatically)
 * @return STORAGE_OK on success
 */
storage_result_t storage_append_line(const char *path, const char *line);

/**
 * Read entire file into buffer
 * @param path File path
 * @param data Output buffer (caller allocates)
 * @param max_len Maximum bytes to read
 * @param actual_len Output: actual bytes read
 * @return STORAGE_OK on success, STORAGE_NOT_FOUND if file doesn't exist
 */
storage_result_t storage_read(const char *path, void *data, size_t max_len, size_t *actual_len);

/**
 * Check if a file exists
 * @param path File path
 * @return true if file exists and is readable
 */
bool storage_file_exists(const char *path);

/**
 * Check if a directory exists
 * @param path Directory path
 * @return true if directory exists
 */
bool storage_dir_exists(const char *path);

/**
 * Create directory (and parents if needed)
 * @param path Directory path
 * @return STORAGE_OK on success or if already exists
 */
storage_result_t storage_mkdir_p(const char *path);

/**
 * Delete a file
 * @param path File path
 * @return STORAGE_OK on success, STORAGE_NOT_FOUND if doesn't exist
 */
storage_result_t storage_delete(const char *path);

/**
 * Get file size
 * @param path File path
 * @param size Output: file size in bytes
 * @return STORAGE_OK on success
 */
storage_result_t storage_get_size(const char *path, size_t *size);

#endif // STORAGE_BACKEND_H
