/**
 * DayZ Server Tracker - SD Card Driver Implementation
 */

#include "sd_card.h"
#include "../config.h"
#include "driver/i2c.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"

static const char *TAG = "sd_card";

static sdmmc_card_t *sd_card = NULL;
static bool sd_mounted = false;
static uint8_t ch422g_output_state = 0xFF;  // All outputs high by default (CS inactive)

esp_err_t sd_card_init_io_expander(void) {
    // CH422G is on the same I2C bus as GT911 touch (already initialized)
    // Set all outputs high (SD_CS inactive)
    ch422g_output_state = 0xFF;

    uint8_t data[2] = {CH422G_REG_OUT, ch422g_output_state};
    esp_err_t ret = i2c_master_write_to_device(TOUCH_I2C_NUM, CH422G_I2C_ADDR,
                                                data, 2, pdMS_TO_TICKS(100));

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

    uint8_t data[2] = {CH422G_REG_OUT, ch422g_output_state};
    i2c_master_write_to_device(TOUCH_I2C_NUM, CH422G_I2C_ADDR,
                               data, 2, pdMS_TO_TICKS(100));
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

    // Activate CS before mounting
    sd_card_set_cs(true);

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &sd_card);

    sd_card_set_cs(false);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        sd_mounted = false;
        return ret;
    }

    sd_mounted = true;
    ESP_LOGI(TAG, "SD card mounted successfully");

    // Print card info
    sdmmc_card_print_info(stdout, sd_card);

    return ESP_OK;
}

bool sd_card_is_mounted(void) {
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
