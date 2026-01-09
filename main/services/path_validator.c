/**
 * DayZ Server Tracker - Path Validation Implementation
 */

#include "path_validator.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

path_error_t path_validate(const char *path) {
    // Check for null
    if (path == NULL) {
        return PATH_ERR_NULL;
    }

    // Check for empty
    size_t len = strlen(path);
    if (len == 0) {
        return PATH_ERR_EMPTY;
    }

    // Check length
    if (len > STORAGE_PATH_MAX_LEN) {
        return PATH_ERR_TOO_LONG;
    }

    // Check for absolute path (must start with /)
    if (path[0] != '/') {
        return PATH_ERR_NOT_ABSOLUTE;
    }

    // Check for directory traversal (..)
    // Look for: /../, /..$, ^../, ^..$
    if (strstr(path, "/../") != NULL ||
        strstr(path, "/..") == path + len - 3 ||  // Ends with /..
        strncmp(path, "../", 3) == 0 ||
        strcmp(path, "..") == 0) {
        return PATH_ERR_TRAVERSAL;
    }

    // Also check for encoded traversal or double dots anywhere suspicious
    const char *p = path;
    while ((p = strstr(p, "..")) != NULL) {
        // Check if .. is a path component (surrounded by / or at boundaries)
        bool at_start = (p == path);
        bool after_slash = (p > path && *(p - 1) == '/');
        bool before_slash = (*(p + 2) == '/' || *(p + 2) == '\0');

        if ((at_start || after_slash) && before_slash) {
            return PATH_ERR_TRAVERSAL;
        }
        p += 2;
    }

    return PATH_OK;
}

bool path_is_under_sdcard(const char *path) {
    // First do basic validation
    path_error_t err = path_validate(path);
    if (err != PATH_OK) {
        return false;
    }

    // Check if path starts with SD mount point
    size_t mount_len = strlen(SD_MOUNT_POINT);
    if (strncmp(path, SD_MOUNT_POINT, mount_len) != 0) {
        return false;
    }

    // Must be exactly the mount point or followed by /
    if (path[mount_len] != '\0' && path[mount_len] != '/') {
        return false;
    }

    return true;
}

int path_build_safe(char *buf, size_t buf_size, const char *format, ...) {
    if (buf == NULL || buf_size == 0 || format == NULL) {
        return -1;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(buf, buf_size, format, args);
    va_end(args);

    // Check for error or truncation
    if (written < 0) {
        buf[0] = '\0';
        return -1;
    }

    if ((size_t)written >= buf_size) {
        // Truncation occurred - null terminate and return error
        buf[buf_size - 1] = '\0';
        return -1;
    }

    // Also validate the resulting path isn't too long for our system
    if (written > STORAGE_PATH_MAX_LEN) {
        return -1;
    }

    return written;
}

const char *path_error_str(path_error_t err) {
    switch (err) {
        case PATH_OK:
            return "OK";
        case PATH_ERR_NULL:
            return "NULL pointer";
        case PATH_ERR_EMPTY:
            return "Empty path";
        case PATH_ERR_TOO_LONG:
            return "Path too long";
        case PATH_ERR_NOT_ABSOLUTE:
            return "Not absolute path";
        case PATH_ERR_TRAVERSAL:
            return "Directory traversal detected";
        case PATH_ERR_NOT_UNDER_ROOT:
            return "Path not under root";
        case PATH_ERR_TRUNCATED:
            return "Path truncated";
        default:
            return "Unknown error";
    }
}
