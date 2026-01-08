/**
 * DayZ Server Tracker - SD Card Driver Implementation
 */

#include "sd_card.h"
#include "config.h"
#include "driver/i2c.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ff.h"  // For f_getfree()
#include <dirent.h>  // For opendir/readdir
#include <stdio.h>   // For FILE operations

static const char *TAG = "sd_card";

// CH422G uses register address AS the I2C device address!
// Mode register: 0x24, Output register for pins 0-7: 0x38
#define CH422G_MODE_ADDR    0x24
#define CH422G_OUTPUT_ADDR  0x38
#define CH422G_MODE_OUTPUT  0x01    // Enable output mode for pins 0-7

static sdmmc_card_t *sd_card = NULL;
static bool sd_mounted = false;
static uint8_t ch422g_output_state = 0xFF;  // All outputs high by default (CS inactive)

static esp_err_t ch422g_write_state(uint8_t state) {
    // Step 1: Configure CH422G mode register (address 0x24)
    uint8_t mode = CH422G_MODE_OUTPUT;
    esp_err_t ret = i2c_master_write_to_device(TOUCH_I2C_NUM, CH422G_MODE_ADDR, &mode, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        return ret;
    }

    // Step 2: Write output state to output register (address 0x38)
    return i2c_master_write_to_device(TOUCH_I2C_NUM, CH422G_OUTPUT_ADDR, &state, 1, pdMS_TO_TICKS(100));
}

esp_err_t sd_card_init_io_expander(void) {
    // CH422G is on the same I2C bus as GT911 touch (already initialized)
    // Set all outputs high (SD_CS inactive)
    ch422g_output_state = 0xFF;

    esp_err_t ret = ch422g_write_state(ch422g_output_state);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CH422G IO expander initialized");
    } else {
        ESP_LOGW(TAG, "CH422G not found or failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

void sd_card_set_cs(bool active) {
    if (active) {
        ch422g_output_state &= ~CH422G_EXIO4_BIT;  // Low = active
    } else {
        ch422g_output_state |= CH422G_EXIO4_BIT;   // High = inactive
    }

    ch422g_write_state(ch422g_output_state);
}

esp_err_t sd_card_init(void) {
    ESP_LOGI(TAG, "Initializing SD card...");

    // First initialize CH422G
    sd_card_init_io_expander();

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
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
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
    sd_card_set_cs(true);

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &sd_card);

    // DO NOT release CS - keep it active for all future operations
    // sd_card_set_cs(false);  // REMOVED - was breaking SD communication

    if (ret != ESP_OK) {
        sd_card_set_cs(false);  // Only release on failure
        ESP_LOGW(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        sd_mounted = false;
        return ret;
    }

    ESP_LOGI(TAG, "SD card mount returned OK, verifying actual access...");

    // Print card info
    sdmmc_card_print_info(stdout, sd_card);

    // CRITICAL: Verify we can actually WRITE to the SD card
    // Mount can succeed but file operations may fail (broken IO expander, etc)
    FILE *test_file = fopen("/sdcard/.sd_write_test", "w");
    if (!test_file) {
        ESP_LOGE(TAG, "SD card mounted but WRITE TEST FAILED! Cannot create file.");
        sd_card_set_cs(false);
        sd_mounted = false;
        return ESP_FAIL;
    }

    // Try to write something
    int written = fprintf(test_file, "test");
    fclose(test_file);

    if (written <= 0) {
        ESP_LOGE(TAG, "SD card mounted but WRITE TEST FAILED! Cannot write data.");
        sd_card_set_cs(false);
        sd_mounted = false;
        return ESP_FAIL;
    }

    // Clean up test file
    remove("/sdcard/.sd_write_test");

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

    return ESP_OK;
}

// Track last verification time for periodic checks
static int64_t last_verify_time = 0;
#define SD_VERIFY_INTERVAL_SEC 30  // Re-verify every 30 seconds

bool sd_card_verify_access(void) {
    if (!sd_mounted) {
        return false;
    }

    // Quick write test to verify SD card is actually accessible
    FILE *test_file = fopen("/sdcard/.sd_verify", "w");
    if (!test_file) {
        ESP_LOGE(TAG, "SD card access FAILED - marking as unmounted!");
        sd_mounted = false;
        return false;
    }

    int written = fprintf(test_file, "v");
    fclose(test_file);
    remove("/sdcard/.sd_verify");

    if (written <= 0) {
        ESP_LOGE(TAG, "SD card write FAILED - marking as unmounted!");
        sd_mounted = false;
        return false;
    }

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
