/**
 * DayZ Server Tracker - Path Validation
 * Safe path construction and validation for SD card operations
 */

#ifndef PATH_VALIDATOR_H
#define PATH_VALIDATOR_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "storage_config.h"

// Path validation error codes
typedef enum {
    PATH_OK = 0,
    PATH_ERR_NULL,           // NULL pointer passed
    PATH_ERR_EMPTY,          // Empty path
    PATH_ERR_TOO_LONG,       // Path exceeds STORAGE_PATH_MAX_LEN
    PATH_ERR_NOT_ABSOLUTE,   // Path doesn't start with /
    PATH_ERR_TRAVERSAL,      // Path contains .. (directory traversal)
    PATH_ERR_NOT_UNDER_ROOT, // Path not under expected root directory
    PATH_ERR_TRUNCATED,      // Path was truncated during construction
} path_error_t;

/**
 * Validate a path for safety
 * Checks: non-null, non-empty, length, absolute path, no traversal
 *
 * @param path Path to validate
 * @return PATH_OK if valid, error code otherwise
 */
path_error_t path_validate(const char *path);

/**
 * Check if path is under the SD card mount point
 * Also performs basic validation
 *
 * @param path Path to check
 * @return true if path is valid and under /sdcard
 */
bool path_is_under_sdcard(const char *path);

/**
 * Safely build a path with snprintf, detecting truncation
 *
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @param format Printf-style format string
 * @param ... Format arguments
 * @return Number of characters written (excluding null), or -1 on truncation/error
 */
int path_build_safe(char *buf, size_t buf_size, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

/**
 * Get human-readable error string for path error code
 *
 * @param err Path error code
 * @return Static string describing the error
 */
const char *path_error_str(path_error_t err);

#endif // PATH_VALIDATOR_H
