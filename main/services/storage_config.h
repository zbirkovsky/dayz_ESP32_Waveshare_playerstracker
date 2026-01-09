/**
 * DayZ Server Tracker - Storage Configuration
 * Centralized storage-related constants, buffer sizes, and timeouts
 */

#ifndef STORAGE_CONFIG_H
#define STORAGE_CONFIG_H

// ============== SD CARD CONFIGURATION ==============
#define SD_VERIFY_INTERVAL_SEC      60      // Re-verify SD access periodically
#define SD_VERIFY_FAIL_THRESHOLD    3       // Consecutive failures before marking unmounted
#define SD_MAX_TRANSFER_SIZE        4000    // SPI transfer size
#define SD_MAX_OPEN_FILES           5       // Maximum concurrent open files
#define SD_ALLOCATION_UNIT_SIZE     (16 * 1024)  // FAT allocation unit

// ============== NVS CONFIGURATION ==============
#define NVS_SAVE_INTERVAL           3       // Save to NVS every N history entries
#define NVS_KEY_MAX_LEN             15      // NVS key max length (ESP-IDF limit)
#define NVS_HISTORY_MAX_ENTRIES     500     // Max history entries in NVS backup

// ============== PATH CONFIGURATION ==============
#define STORAGE_PATH_MAX_LEN        128     // Maximum path length
#define STORAGE_FILENAME_MAX_LEN    64      // Maximum filename length
#define STORAGE_DATE_STR_LEN        12      // "YYYY-MM-DD" + null

// Path prefixes and directories
#define SD_MOUNT_POINT              "/sdcard"
#define STORAGE_HISTORY_JSON_DIR    "/sdcard/history"
#define STORAGE_HISTORY_BIN_PREFIX  "/sdcard/hist_"
#define STORAGE_CONFIG_JSON_FILE    "/sdcard/servers.json"

// ============== HISTORY STORAGE ==============
#define STORAGE_HISTORY_FILE_MAGIC  0xDA120002  // Binary history file magic
#define STORAGE_HISTORY_RETENTION   365         // Days to keep history
#define STORAGE_JSON_VERSION        1           // JSON format version
#define STORAGE_MAX_JSON_SIZE       32768       // 32KB max config file

// Minimum valid timestamp (Nov 2023 - for SNTP sync check)
#define STORAGE_TIMESTAMP_MIN_VALID 1700000000

// ============== TIMEOUTS ==============
#define STORAGE_RETRY_COUNT         3
#define STORAGE_RETRY_DELAY_MS      100
#define STORAGE_NVS_TIMEOUT_MS      100

#endif // STORAGE_CONFIG_H
