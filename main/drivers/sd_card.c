/**
 * DayZ Server Tracker - SD Card Driver Implementation
 */

#include "sd_card.h"
#include "io_expander.h"
#include "config.h"
#include "services/storage_config.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ff.h"  // For f_getfree()
#include <dirent.h>  // For opendir/readdir
#include <stdio.h>   // For FILE operations
#include <sys/stat.h>  // For stat()
#include <errno.h>   // For errno
#include <unistd.h>  // For fsync
#include <string.h>  // For memset
#include "esp_heap_caps.h"  // For heap_caps_malloc

static const char *TAG = "sd_card";

static sdmmc_card_t *sd_card = NULL;
static bool sd_mounted = false;

/**
 * DEBUG: Read raw sector from SD card and verify MBR signature
 * This bypasses the filesystem to check if we're reading the REAL card
 */
static void debug_verify_raw_sector(void) {
    if (!sd_card) {
        ESP_LOGE(TAG, "DEBUG: No card handle for raw sector read");
        return;
    }

    uint8_t *sector = heap_caps_malloc(512, MALLOC_CAP_DMA);
    if (!sector) {
        ESP_LOGE(TAG, "DEBUG: Failed to allocate sector buffer");
        return;
    }

    memset(sector, 0, 512);

    ESP_LOGI(TAG, "========== RAW SECTOR 0 (MBR) DEBUG ==========");

    // Read sector 0 (MBR) directly from SD card
    esp_err_t ret = sdmmc_read_sectors(sd_card, sector, 0, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DEBUG: Raw sector read FAILED: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "DEBUG: This confirms SPI is NOT talking to real SD card!");
        free(sector);
        return;
    }

    // Check MBR boot signature at offset 510-511 (should be 0x55, 0xAA)
    uint8_t sig1 = sector[510];
    uint8_t sig2 = sector[511];

    ESP_LOGI(TAG, "DEBUG: MBR signature bytes: 0x%02X 0x%02X (expected: 0x55 0xAA)", sig1, sig2);

    if (sig1 == 0x55 && sig2 == 0xAA) {
        ESP_LOGI(TAG, "DEBUG: *** MBR SIGNATURE VALID! Reading real SD card ***");

        // Parse partition table (offset 446-509)
        ESP_LOGI(TAG, "DEBUG: First partition entry:");
        uint8_t *part = &sector[446];
        ESP_LOGI(TAG, "  Boot flag: 0x%02X", part[0]);
        ESP_LOGI(TAG, "  Type: 0x%02X (0x0B/0x0C=FAT32, 0x06=FAT16)", part[4]);
        uint32_t lba_start = part[8] | (part[9]<<8) | (part[10]<<16) | (part[11]<<24);
        uint32_t lba_count = part[12] | (part[13]<<8) | (part[14]<<16) | (part[15]<<24);
        ESP_LOGI(TAG, "  LBA start: %lu, sectors: %lu", lba_start, lba_count);
    } else if (sig1 == 0x00 && sig2 == 0x00) {
        ESP_LOGE(TAG, "DEBUG: *** MBR is ALL ZEROS! Not reading real SD card! ***");
        ESP_LOGE(TAG, "DEBUG: Data is coming from uninitialized memory or wrong device");
    } else {
        ESP_LOGW(TAG, "DEBUG: *** MBR signature INVALID! Card may be corrupted or wrong device ***");
    }

    // Print first 32 bytes of MBR for analysis
    ESP_LOGI(TAG, "DEBUG: First 32 bytes of sector 0:");
    char hex_line[100];
    for (int row = 0; row < 2; row++) {
        int pos = 0;
        for (int col = 0; col < 16; col++) {
            pos += sprintf(&hex_line[pos], "%02X ", sector[row*16 + col]);
        }
        ESP_LOGI(TAG, "  %s", hex_line);
    }

    // Print last 16 bytes (including signature)
    ESP_LOGI(TAG, "DEBUG: Last 16 bytes of sector 0 (ends with signature):");
    char hex_end[100];
    int pos = 0;
    for (int i = 496; i < 512; i++) {
        pos += sprintf(&hex_end[pos], "%02X ", sector[i]);
    }
    ESP_LOGI(TAG, "  %s", hex_end);

    ESP_LOGI(TAG, "===============================================");

    free(sector);
}

esp_err_t sd_card_init(void) {
    ESP_LOGI(TAG, "Initializing SD card...");

    // DEBUG: Print SPI pin configuration
    ESP_LOGI(TAG, "========== SD CARD SPI CONFIG DEBUG ==========");
    ESP_LOGI(TAG, "SD_MOSI = GPIO%d", SD_MOSI);
    ESP_LOGI(TAG, "SD_MISO = GPIO%d", SD_MISO);
    ESP_LOGI(TAG, "SD_CLK  = GPIO%d", SD_CLK);
    ESP_LOGI(TAG, "SD_CS   = CH422G EXIO4 (via IO expander)");

    // DEBUG: Print current CH422G state
    uint8_t ch422g_state = io_expander_get_state();
    ESP_LOGI(TAG, "CH422G state before init: 0x%02X", ch422g_state);
    ESP_LOGI(TAG, "  EXIO4 (SD_CS) bit: %s (bit4=%d)",
             (ch422g_state & 0x10) ? "HIGH (inactive)" : "LOW (active)",
             (ch422g_state >> 4) & 1);
    ESP_LOGI(TAG, "===============================================");

    // SPI bus configuration
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI,
        .miso_io_num = SD_MISO,
        .sclk_io_num = SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // SD card mount config
    // WARNING: format_if_mount_failed was causing data loss!
    // Disabled to preserve user data on SD card
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,  // CHANGED: was true, caused data loss
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    // SD card SPI device config
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = GPIO_NUM_NC;  // We control CS via CH422G
    slot_config.host_id = SPI2_HOST;

    // Activate CS before mounting and KEEP IT ACTIVE
    // Since we use GPIO_NUM_NC, the ESP-IDF driver can't control CS itself
    // We must keep CS active (LOW) permanently for SD card to work
    io_expander_set_sd_cs(true);

    // DEBUG: Verify CS is now LOW
    ch422g_state = io_expander_get_state();
    ESP_LOGI(TAG, "CH422G state after CS activate: 0x%02X (EXIO4 should be LOW)", ch422g_state);

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &sd_card);

    // DO NOT release CS - keep it active for all future operations
    // io_expander_set_sd_cs(false);  // REMOVED - was breaking SD communication

    if (ret != ESP_OK) {
        io_expander_set_sd_cs(false);  // Only release on failure
        ESP_LOGW(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        sd_mounted = false;
        return ret;
    }

    ESP_LOGI(TAG, "SD card mount returned OK, verifying actual access...");

    // Print card info
    sdmmc_card_print_info(stdout, sd_card);

    // DEBUG: Read raw sector 0 to verify we're talking to real SD card
    debug_verify_raw_sector();

    // CRITICAL: Verify we can actually WRITE to the SD card
    // Mount can succeed but file operations may fail (broken IO expander, etc)
    FILE *test_file = fopen("/sdcard/.sd_write_test", "w");
    if (!test_file) {
        ESP_LOGE(TAG, "SD card mounted but WRITE TEST FAILED! Cannot create file.");
        io_expander_set_sd_cs(false);
        sd_mounted = false;
        return ESP_FAIL;
    }

    // Try to write something
    int written = fprintf(test_file, "test");
    fclose(test_file);

    if (written <= 0) {
        ESP_LOGE(TAG, "SD card mounted but WRITE TEST FAILED! Cannot write data.");
        io_expander_set_sd_cs(false);
        sd_mounted = false;
        return ESP_FAIL;
    }

    // DON'T clean up - leave a permanent marker file!
    // remove("/sdcard/.sd_write_test");

    // Create a VERY OBVIOUS test file that user can search for
    FILE *marker = fopen("/sdcard/ESP32_WAS_HERE.txt", "w");
    if (marker) {
        fprintf(marker, "If you see this file, ESP32 SD card access is working!\n");
        fprintf(marker, "Created by DayZ Server Tracker\n");
        fflush(marker);
        fsync(fileno(marker));
        fclose(marker);
        ESP_LOGI(TAG, "*** CREATED MARKER FILE: /sdcard/ESP32_WAS_HERE.txt ***");
    } else {
        ESP_LOGE(TAG, "*** FAILED TO CREATE MARKER FILE! errno=%d ***", errno);
    }

    ESP_LOGI(TAG, "SD card write test PASSED - SD card is working!");
    sd_mounted = true;

    // List root directory contents
    DIR *dir = opendir("/sdcard");
    if (dir) {
        ESP_LOGI(TAG, "=== SD Card Root Contents ===");
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            ESP_LOGI(TAG, "  %s %s", entry->d_type == DT_DIR ? "[DIR]" : "[FILE]", entry->d_name);
        }
        closedir(dir);
        ESP_LOGI(TAG, "=============================");
    }

    // DEBUG: Check for PC_TEST.txt - this is the quick test from TODO
    // If user creates PC_TEST.txt on PC and ESP32 can see it, SPI is working
    FILE *pc_test = fopen("/sdcard/PC_TEST.txt", "r");
    if (pc_test) {
        char buf[128] = {0};
        size_t read_len = fread(buf, 1, sizeof(buf)-1, pc_test);
        fclose(pc_test);
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "*** FOUND PC_TEST.txt! SPI IS WORKING! ***");
        ESP_LOGI(TAG, "Content (%d bytes): %s", (int)read_len, buf);
        ESP_LOGI(TAG, "========================================");
    } else {
        ESP_LOGW(TAG, "PC_TEST.txt not found - create this file on PC to verify SPI connection");
    }

    // DEBUG: Look for common folders that might indicate wrong storage
    struct stat st;
    ESP_LOGI(TAG, "=== Storage Origin Check ===");
    if (stat("/sdcard/maps", &st) == 0 && S_ISDIR(st.st_mode)) {
        ESP_LOGI(TAG, "*** FOUND 'maps' folder! This is the REAL SD card! ***");
    } else {
        ESP_LOGW(TAG, "'maps' folder NOT found - if your SD has this folder, SPI is broken!");
    }

    // Check for WL (wear leveling) partition marker - indicates internal flash
    if (stat("/sdcard/wl_persist", &st) == 0) {
        ESP_LOGE(TAG, "*** FOUND wl_persist - THIS IS INTERNAL FLASH, NOT SD CARD! ***");
    }

    ESP_LOGI(TAG, "============================");

    return ESP_OK;
}

// Track last verification time for periodic checks
static int64_t last_verify_time = 0;
static int verify_fail_count = 0;
// SD_VERIFY_INTERVAL_SEC and SD_VERIFY_FAIL_THRESHOLD defined in storage_config.h

bool sd_card_verify_access(void) {
    if (!sd_mounted) {
        return false;
    }

    // Use a simple read test instead of write - faster and less disruptive
    // Just check if we can stat the root directory
    struct stat st;
    if (stat(SD_MOUNT_POINT, &st) != 0 || !S_ISDIR(st.st_mode)) {
        verify_fail_count++;
        ESP_LOGW(TAG, "SD card access check failed (%d/%d)",
                 verify_fail_count, SD_VERIFY_FAIL_THRESHOLD);

        if (verify_fail_count >= SD_VERIFY_FAIL_THRESHOLD) {
            ESP_LOGE(TAG, "SD card access FAILED after %d attempts - marking as unmounted!",
                     SD_VERIFY_FAIL_THRESHOLD);
            sd_mounted = false;
        }
        // FIX: Return false when access check fails (was returning true!)
        return false;
    }

    // Success - reset fail count
    verify_fail_count = 0;
    return true;
}

bool sd_card_is_mounted(void) {
    if (!sd_mounted) {
        return false;
    }

    // Periodically verify actual SD card access
    int64_t now = esp_timer_get_time() / 1000000;  // Convert to seconds
    if (now - last_verify_time > SD_VERIFY_INTERVAL_SEC) {
        last_verify_time = now;
        return sd_card_verify_access();
    }

    return sd_mounted;
}

void sd_card_deinit(void) {
    if (sd_mounted) {
        esp_vfs_fat_sdcard_unmount("/sdcard", sd_card);
        sd_card = NULL;
        sd_mounted = false;
        ESP_LOGI(TAG, "SD card unmounted");
    }
}

esp_err_t sd_card_get_space(uint32_t *total_mb, uint32_t *free_mb) {
    if (!sd_mounted || !total_mb || !free_mb) {
        return ESP_ERR_INVALID_STATE;
    }

    FATFS *fs;
    DWORD fre_clust;

    // Get volume information and free clusters
    FRESULT res = f_getfree("0:", &fre_clust, &fs);
    if (res != FR_OK) {
        ESP_LOGE(TAG, "f_getfree failed: %d", res);
        return ESP_FAIL;
    }

    // Calculate total and free space
    // Total sectors = (total clusters) * (sectors per cluster)
    // Free sectors = (free clusters) * (sectors per cluster)
    uint64_t tot_sect = (uint64_t)(fs->n_fatent - 2) * fs->csize;
    uint64_t fre_sect = (uint64_t)fre_clust * fs->csize;

    // Convert to MB (sector size is typically 512 bytes)
    *total_mb = (uint32_t)((tot_sect * 512) / (1024 * 1024));
    *free_mb = (uint32_t)((fre_sect * 512) / (1024 * 1024));

    return ESP_OK;
}

int sd_card_get_usage_percent(void) {
    uint32_t total_mb, free_mb;

    if (sd_card_get_space(&total_mb, &free_mb) != ESP_OK) {
        return -1;
    }

    if (total_mb == 0) {
        return 0;
    }

    // Calculate percentage used
    uint32_t used_mb = total_mb - free_mb;
    return (int)((used_mb * 100) / total_mb);
}
