/**
 * DayZ Server Tracker - NVS Key Management
 * Macros and helpers for generating consistent NVS keys
 */

#ifndef NVS_KEYS_H
#define NVS_KEYS_H

#include <stdio.h>
#include "storage_config.h"

// ============== NVS KEY SUFFIXES ==============
// Server configuration keys (16 unique fields per server)
#define NVS_SUFFIX_ID       "id"        // Server ID
#define NVS_SUFFIX_NAME     "name"      // Display name
#define NVS_SUFFIX_MAP      "map"       // Map name
#define NVS_SUFFIX_IP       "ip"        // IP address
#define NVS_SUFFIX_PORT     "port"      // Port number
#define NVS_SUFFIX_MAX      "max"       // Max players
#define NVS_SUFFIX_ALERT    "alert"     // Alert threshold
#define NVS_SUFFIX_ALEN     "alen"      // Alerts enabled
#define NVS_SUFFIX_RCNT     "rcnt"      // Restart count
#define NVS_SUFFIX_RAVG     "ravg"      // Restart avg interval
#define NVS_SUFFIX_RLAST    "rlast"     // Last restart time
#define NVS_SUFFIX_RTIMES   "rtimes"    // Restart times blob
#define NVS_SUFFIX_RHR      "rhr"       // Restart hour
#define NVS_SUFFIX_RMIN     "rmin"      // Restart minute
#define NVS_SUFFIX_RINT     "rint"      // Restart interval hours
#define NVS_SUFFIX_RMAN     "rman"      // Manual restart set

// History keys
#define NVS_SUFFIX_META     "meta"      // History metadata
#define NVS_SUFFIX_DATA     "data"      // History data blob

/**
 * Generate a server-specific NVS key
 * Format: srv{index}_{suffix} (e.g., "srv0_id", "srv2_name")
 *
 * @param buf Output buffer (at least NVS_KEY_MAX_LEN + 1 bytes)
 * @param buf_size Buffer size
 * @param server_idx Server index (0-4)
 * @param suffix Key suffix (use NVS_SUFFIX_* constants)
 * @return Number of characters written, or -1 on truncation
 */
static inline int nvs_key_server(char *buf, size_t buf_size,
                                  int server_idx, const char *suffix) {
    int written = snprintf(buf, buf_size, "srv%d_%s", server_idx, suffix);
    // Check for truncation - key must be <= 15 chars for NVS
    if (written < 0 || (size_t)written >= buf_size || written > NVS_KEY_MAX_LEN) {
        return -1;
    }
    return written;
}

/**
 * Generate a history-specific NVS key
 * Format: h{index}_{suffix} (e.g., "h0_meta", "h1_data")
 *
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @param server_idx Server index
 * @param suffix Key suffix ("meta" or "data")
 * @return Number of characters written, or -1 on truncation
 */
static inline int nvs_key_history(char *buf, size_t buf_size,
                                   int server_idx, const char *suffix) {
    int written = snprintf(buf, buf_size, "h%d_%s", server_idx, suffix);
    if (written < 0 || (size_t)written >= buf_size || written > NVS_KEY_MAX_LEN) {
        return -1;
    }
    return written;
}

// ============== CONVENIENCE MACROS ==============

/**
 * Declare and initialize a server NVS key variable
 * Usage: NVS_KEY_SERVER(key, 0, NVS_SUFFIX_ID);
 */
#define NVS_KEY_SERVER(var, idx, suffix) \
    char var[NVS_KEY_MAX_LEN + 1]; \
    nvs_key_server(var, sizeof(var), idx, suffix)

/**
 * Declare and initialize a WiFi credential NVS key variable
 * Format: wf{index}_{suffix} (e.g., "wf0_ssid", "wf3_pass")
 * Usage: NVS_KEY_WIFI(key, 0, "ssid");
 */
#define NVS_KEY_WIFI(var, idx, suffix) \
    char var[NVS_KEY_MAX_LEN + 1]; \
    snprintf(var, sizeof(var), "wf%d_%s", idx, suffix)

/**
 * Declare and initialize a history NVS key variable
 * Usage: NVS_KEY_HISTORY(key, 0, NVS_SUFFIX_META);
 */
#define NVS_KEY_HISTORY(var, idx, suffix) \
    char var[NVS_KEY_MAX_LEN + 1]; \
    nvs_key_history(var, sizeof(var), idx, suffix)

#endif // NVS_KEYS_H
