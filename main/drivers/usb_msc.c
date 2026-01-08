/**
 * DayZ Server Tracker - USB Mass Storage Driver Implementation
 * Uses ESP-IDF TinyUSB v2.0.0+ API
 */

#include "usb_msc.h"
#include "config.h"
#include "sd_card.h"
#include "display.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#include "tinyusb.h"
#include "tinyusb_msc.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"

static const char *TAG = "usb_msc";
static bool usb_msc_active = false;
static sdmmc_card_t *msc_card = NULL;
static tinyusb_msc_storage_handle_t storage_handle = NULL;

// CH422G communication
#define CH422G_MODE_ADDR    0x24
#define CH422G_OUTPUT_ADDR  0x38
#define CH422G_MODE_OUTPUT  0x01

static uint8_t ch422g_state = 0xFF;

static esp_err_t ch422g_write(uint8_t state) {
    uint8_t mode = CH422G_MODE_OUTPUT;
    esp_err_t ret = i2c_master_write_to_device(TOUCH_I2C_NUM, CH422G_MODE_ADDR, &mode, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) return ret;
    return i2c_master_write_to_device(TOUCH_I2C_NUM, CH422G_OUTPUT_ADDR, &state, 1, pdMS_TO_TICKS(100));
}

static void set_usb_mode(bool usb_mode) {
    if (usb_mode) {
        ch422g_state &= ~CH422G_EXIO5_BIT;  // Low = USB mode
    } else {
        ch422g_state |= CH422G_EXIO5_BIT;   // High = CAN mode
    }
    ch422g_write(ch422g_state);
}

// GT911 touch controller registers
#define GT911_ADDR          0x5D
#define GT911_POINT_INFO    0x814E  // Touch status register

static esp_err_t gt911_read_reg(uint16_t reg, uint8_t *data, size_t len) {
    uint8_t reg_addr[2] = { (reg >> 8) & 0xFF, reg & 0xFF };
    esp_err_t ret = i2c_master_write_to_device(TOUCH_I2C_NUM, GT911_ADDR, reg_addr, 2, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) return ret;
    return i2c_master_read_from_device(TOUCH_I2C_NUM, GT911_ADDR, data, len, pdMS_TO_TICKS(100));
}

static void gt911_clear_status(void) {
    uint8_t clear_cmd[3] = { 0x81, 0x4E, 0x00 };  // Write 0 to 0x814E
    i2c_master_write_to_device(TOUCH_I2C_NUM, GT911_ADDR, clear_cmd, 3, pdMS_TO_TICKS(100));
}

bool usb_msc_touch_detected(void) {
    ESP_LOGI(TAG, "Checking for touch on boot...");

    // Initialize I2C for touch controller
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_I2C_SDA,
        .scl_io_num = TOUCH_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };

    esp_err_t ret = i2c_param_config(TOUCH_I2C_NUM, &i2c_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed");
        return false;
    }

    ret = i2c_driver_install(TOUCH_I2C_NUM, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "I2C driver install failed");
        return false;
    }

    // Reset GT911 via CH422G EXIO1
    ch422g_state = 0xFF;
    ch422g_state &= ~CH422G_EXIO1_BIT;  // Assert reset (low)
    ch422g_write(ch422g_state);
    vTaskDelay(pdMS_TO_TICKS(20));
    ch422g_state |= CH422G_EXIO1_BIT;   // Release reset (high)
    ch422g_write(ch422g_state);
    vTaskDelay(pdMS_TO_TICKS(100));     // Wait for GT911 to initialize

    // Check for touch multiple times (debounce)
    int touch_count = 0;
    for (int i = 0; i < 10; i++) {
        uint8_t status = 0;
        ret = gt911_read_reg(GT911_POINT_INFO, &status, 1);

        if (ret == ESP_OK && (status & 0x80) && (status & 0x0F) > 0) {
            touch_count++;
            ESP_LOGI(TAG, "Touch detected (attempt %d, status=0x%02X)", i + 1, status);
        }

        gt911_clear_status();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Need at least 5 out of 10 checks with touch to confirm
    bool touched = (touch_count >= 5);
    ESP_LOGI(TAG, "Touch check result: %d/%d touches -> %s",
             touch_count, 10, touched ? "USB MODE" : "NORMAL MODE");

    // Clean up I2C if not entering USB mode (will be re-initialized by display_init)
    if (!touched) {
        i2c_driver_delete(TOUCH_I2C_NUM);
    }

    return touched;
}

static esp_err_t init_sd_for_msc(void) {
    ESP_LOGI(TAG, "Initializing SD card for MSC...");

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
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // SD card host config
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = GPIO_NUM_NC;  // CS via CH422G
    slot_config.host_id = SPI2_HOST;

    // Activate CS
    ch422g_state &= ~CH422G_EXIO4_BIT;
    ch422g_write(ch422g_state);

    // Probe card
    ret = sdspi_host_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SDSPI host init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_dev_handle_t handle;
    ret = sdspi_host_init_device(&slot_config, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SDSPI device init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    msc_card = malloc(sizeof(sdmmc_card_t));
    if (!msc_card) {
        return ESP_ERR_NO_MEM;
    }

    ret = sdmmc_card_init(&host, msc_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card init failed: %s", esp_err_to_name(ret));
        free(msc_card);
        msc_card = NULL;
        return ret;
    }

    sdmmc_card_print_info(stdout, msc_card);
    return ESP_OK;
}

esp_err_t usb_msc_init(void) {
    ESP_LOGI(TAG, "Initializing USB Mass Storage mode...");

    // I2C is already initialized by usb_msc_touch_detected()
    // Set USB mode on CH422G (select USB instead of CAN)
    set_usb_mode(true);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Initialize SD card
    esp_err_t ret = init_sd_for_msc();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card init for MSC failed");
        return ret;
    }

    // Configure TinyUSB driver
    const tinyusb_config_t tusb_cfg = {
        .port = TINYUSB_PORT_FULL_SPEED_0,
        .phy = {
            .skip_setup = false,
            .self_powered = false,
            .vbus_monitor_io = -1,
        },
        .task = {
            .size = 4096,
            .priority = 5,
            .xCoreID = 0,
        },
        .descriptor = {
            .device = NULL,      // Use defaults from Kconfig
            .qualifier = NULL,
            .string = NULL,
            .string_count = 0,
            .full_speed_config = NULL,
            .high_speed_config = NULL,
        },
        .event_cb = NULL,
        .event_arg = NULL,
    };

    ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Install MSC driver
    const tinyusb_msc_driver_config_t msc_driver_cfg = {
        .user_flags = { .val = 0 },
        .callback = NULL,
        .callback_arg = NULL,
    };

    ret = tinyusb_msc_install_driver(&msc_driver_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MSC driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Create SD card storage
    const tinyusb_msc_storage_config_t storage_cfg = {
        .medium = {
            .card = msc_card,
        },
        .fat_fs = {
            .base_path = "/usb",
            .config = {
                .format_if_mount_failed = true,
                .max_files = 5,
                .allocation_unit_size = 16 * 1024,
            },
            .do_not_format = false,
            .format_flags = 0,  // FM_ANY
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,  // Expose to USB host
    };

    ret = tinyusb_msc_new_storage_sdmmc(&storage_cfg, &storage_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MSC storage init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    usb_msc_active = true;
    ESP_LOGI(TAG, "USB Mass Storage initialized successfully");

    return ESP_OK;
}

static void show_usb_screen(void) {
    // Initialize display (minimal, just for showing USB mode screen)
    display_init();

    if (lvgl_port_lock(100)) {
        lv_obj_t *scr = lv_scr_act();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

        // USB icon (using text)
        lv_obj_t *icon = lv_label_create(scr);
        lv_label_set_text(icon, LV_SYMBOL_USB);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(0x4ade80), 0);
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, -60);

        // Title
        lv_obj_t *title = lv_label_create(scr);
        lv_label_set_text(title, "USB Storage Mode");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 10);

        // Instructions
        lv_obj_t *info = lv_label_create(scr);
        lv_label_set_text(info, "SD card is accessible from your PC.\n"
                               "Safely eject before unplugging.");
        lv_obj_set_style_text_font(info, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(info, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(info, LV_ALIGN_CENTER, 0, 70);

        // Card info
        if (msc_card) {
            char card_info[64];
            uint64_t capacity = (uint64_t)msc_card->csd.capacity * msc_card->csd.sector_size / (1024 * 1024);
            snprintf(card_info, sizeof(card_info), "Card: %lluMB", (unsigned long long)capacity);

            lv_obj_t *card_lbl = lv_label_create(scr);
            lv_label_set_text(card_lbl, card_info);
            lv_obj_set_style_text_font(card_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(card_lbl, lv_color_hex(0x666666), 0);
            lv_obj_align(card_lbl, LV_ALIGN_BOTTOM_MID, 0, -20);
        }

        lvgl_port_unlock();
    }
}

void usb_msc_task(void) {
    ESP_LOGI(TAG, "Entering USB MSC task loop");

    show_usb_screen();

    // Just keep LVGL running
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));

        if (lvgl_port_lock(10)) {
            lv_timer_handler();
            lvgl_port_unlock();
        }
    }
}

bool usb_msc_is_active(void) {
    return usb_msc_active;
}
