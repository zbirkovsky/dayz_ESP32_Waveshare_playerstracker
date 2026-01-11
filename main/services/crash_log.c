/**
 * DayZ Server Tracker - Crash Log Service Implementation
 */

#include "crash_log.h"
#include "sd_card.h"
#include "sdkconfig.h"
#include "esp_core_dump.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static const char *TAG = "crash_log";

// Directory on SD card for crash logs
#define CRASH_LOG_DIR "/sdcard/crashes"

bool crash_log_exists(void) {
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
    esp_core_dump_summary_t summary;
    esp_err_t err = esp_core_dump_get_summary(&summary);
    return (err == ESP_OK);
#else
    // Check if core dump image exists via esp_core_dump_image_check
    esp_err_t err = esp_core_dump_image_check();
    return (err == ESP_OK);
#endif
}

esp_err_t crash_log_check_and_save(void) {
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
    // First check if coredump partition exists
    const esp_partition_t *core_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (core_part == NULL) {
        ESP_LOGW(TAG, "No coredump partition found - skipping crash check");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Checking for crash dump in partition at 0x%lx...", (unsigned long)core_part->address);

    esp_core_dump_summary_t summary;
    esp_err_t err = esp_core_dump_get_summary(&summary);

    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No crash dump found (normal boot), err=%d", err);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Crash dump detected! Saving to SD card...");

    // Check if SD card is available
    if (!sd_card_is_mounted()) {
        ESP_LOGE(TAG, "SD card not mounted, cannot save crash log");
        return ESP_ERR_NOT_FOUND;
    }

    // Create crashes directory if it doesn't exist
    struct stat st;
    if (stat(CRASH_LOG_DIR, &st) != 0) {
        if (mkdir(CRASH_LOG_DIR, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create crash log directory");
            return ESP_FAIL;
        }
    }

    // Generate filename with timestamp
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char filename[64];
    snprintf(filename, sizeof(filename), CRASH_LOG_DIR "/crash_%04d%02d%02d_%02d%02d%02d.txt",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    // Open file for writing
    FILE *f = fopen(filename, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create crash log file: %s", filename);
        return ESP_FAIL;
    }

    // Write crash summary
    fprintf(f, "=== ESP32 CRASH LOG ===\n\n");
    fprintf(f, "Crash Time: %04d-%02d-%02d %02d:%02d:%02d\n",
            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    fprintf(f, "Firmware: DayZ Server Tracker\n\n");

    // Exception info
    fprintf(f, "=== EXCEPTION INFO ===\n");
    fprintf(f, "Exception cause: %lu\n", (unsigned long)summary.ex_info.exc_cause);
    fprintf(f, "Exception address: 0x%08lx\n", (unsigned long)summary.ex_info.exc_vaddr);
    fprintf(f, "EPC1: 0x%08lx\n", (unsigned long)summary.exc_pc);

    // Task info
    fprintf(f, "\n=== CRASHED TASK ===\n");
    fprintf(f, "Task name: %s\n", summary.exc_task);
    fprintf(f, "Task TCB: 0x%08lx\n", (unsigned long)summary.exc_tcb);

    // Backtrace
    fprintf(f, "\n=== BACKTRACE ===\n");
    for (uint32_t i = 0; i < summary.exc_bt_info.depth && i < 16; i++) {
        fprintf(f, "%lu: 0x%08lx\n", (unsigned long)i, (unsigned long)summary.exc_bt_info.bt[i]);
    }
    fprintf(f, "Backtrace depth: %lu\n", (unsigned long)summary.exc_bt_info.depth);
    if (summary.exc_bt_info.corrupted) {
        fprintf(f, "WARNING: Backtrace may be corrupted!\n");
    }

    // Core dump info
    fprintf(f, "\n=== CORE DUMP INFO ===\n");
    fprintf(f, "Core dump version: %lu\n", (unsigned long)summary.core_dump_version);

    // App SHA256
    fprintf(f, "App ELF SHA256: %s\n", summary.app_elf_sha256);

    fprintf(f, "\n=== END OF CRASH LOG ===\n");

    fclose(f);

    ESP_LOGI(TAG, "Crash log saved to: %s", filename);

    // Erase the core dump from flash to prevent re-saving on next boot
    esp_err_t erase_err = esp_core_dump_image_erase();
    if (erase_err == ESP_OK) {
        ESP_LOGI(TAG, "Core dump erased from flash");
    } else {
        ESP_LOGW(TAG, "Failed to erase core dump: %d", erase_err);
    }

    return ESP_OK;
#else
    // Fallback for non-ELF or UART modes - just check if dump exists
    esp_err_t err = esp_core_dump_image_check();
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No crash dump found (normal boot)");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Crash dump detected but ELF format not enabled - cannot extract details");

    // Still erase it to prevent stale dump message on every boot
    esp_core_dump_image_erase();

    return ESP_OK;
#endif
}

bool crash_log_get_latest_path(char *buf, size_t size) {
    if (!buf || size == 0) return false;

    // Simple implementation - just check if directory exists
    struct stat st;
    if (stat(CRASH_LOG_DIR, &st) != 0) {
        return false;
    }

    // Return directory path (caller can list files)
    strncpy(buf, CRASH_LOG_DIR, size - 1);
    buf[size - 1] = '\0';
    return true;
}
