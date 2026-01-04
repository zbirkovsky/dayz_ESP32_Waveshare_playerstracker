/*
 * DayZ Server Player Tracker
 * For Waveshare ESP32-S3-Touch-LCD-7 (800x480)
 *
 * Features:
 * - Multi-server support (up to 5 servers)
 * - NVS settings storage
 * - Settings menu with on-screen keyboard
 * - Player history chart
 * - Visual alerts
 *
 * Uses LVGL for smooth, anti-aliased fonts
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// LVGL includes
#include "lvgl.h"
#include "esp_lvgl_port.h"

// Touch includes
#include "driver/i2c.h"
#include "esp_lcd_touch_gt911.h"

// SD Card includes
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

// SNTP for time synchronization
#include "esp_sntp.h"

// ============== CONSTANTS ==============
#define MAX_SERVERS          5
#define MAX_HISTORY_ENTRIES  10080  // 7 days at 1 min intervals
#define NVS_NAMESPACE        "dayz_tracker"

// Display settings (Waveshare ESP32-S3-Touch-LCD-7)
#define LCD_WIDTH  800
#define LCD_HEIGHT 480

// Waveshare ESP32-S3-Touch-LCD-7 Pin Configuration
#define PIN_LCD_VSYNC  3
#define PIN_LCD_HSYNC  46
#define PIN_LCD_DE     5
#define PIN_LCD_PCLK   7
#define PIN_LCD_B0     14
#define PIN_LCD_B1     38
#define PIN_LCD_B2     18
#define PIN_LCD_B3     17
#define PIN_LCD_B4     10
#define PIN_LCD_G0     39
#define PIN_LCD_G1     0
#define PIN_LCD_G2     45
#define PIN_LCD_G3     48
#define PIN_LCD_G4     47
#define PIN_LCD_G5     21
#define PIN_LCD_R0     1
#define PIN_LCD_R1     2
#define PIN_LCD_R2     42
#define PIN_LCD_R3     41
#define PIN_LCD_R4     40
#define PIN_LCD_BL     -1

// Touch Pin Configuration (GT911 I2C)
#define TOUCH_I2C_SDA  GPIO_NUM_8
#define TOUCH_I2C_SCL  GPIO_NUM_9
#define TOUCH_I2C_NUM  I2C_NUM_0

// SD Card Pin Configuration (SPI mode)
#define SD_MOSI        GPIO_NUM_11
#define SD_MISO        GPIO_NUM_13
#define SD_CLK         GPIO_NUM_12
// SD_CS is controlled via CH422G IO expander EXIO4

// CH422G IO Expander (for SD_CS control)
#define CH422G_I2C_ADDR     0x24
#define CH422G_REG_OUT      0x01
#define CH422G_EXIO4_BIT    (1 << 4)  // SD_CS pin

// Buzzer Configuration (connect passive buzzer to GPIO1)
#define BUZZER_PIN          GPIO_NUM_6  // SENSOR AD pin on Waveshare board
#define BUZZER_ENABLED      true  // Set to false if no buzzer connected

// Restart detection thresholds
#define RESTART_DETECT_MIN_PLAYERS  5   // Server must have had at least this many players
#define RESTART_DETECT_DROP_TO      0   // Player count drops to this (0 = server restart)
#define MIN_RESTART_INTERVAL_SEC    1800  // Minimum 30 min between detected restarts (avoid false positives)

static const char *TAG = "DayZ_Tracker";

// ============== DATA STRUCTURES ==============

// Restart tracking
#define MAX_RESTART_HISTORY 10  // Track last 10 restarts per server

typedef struct {
    uint32_t restart_times[MAX_RESTART_HISTORY];  // Unix timestamps of detected restarts
    uint8_t restart_count;                         // Number of recorded restarts
    uint32_t avg_interval_sec;                     // Calculated average interval between restarts
    uint32_t last_restart_time;                    // Most recent restart timestamp
    int16_t last_known_players;                    // Player count before restart (for detection)
} restart_history_t;

// Server configuration
typedef struct {
    char server_id[32];       // BattleMetrics server ID
    char display_name[64];    // User-friendly name
    char ip_address[32];      // Server IP
    uint16_t port;            // Server port
    uint16_t max_players;     // Max player capacity
    uint16_t alert_threshold; // Alert when players >= this
    bool alerts_enabled;
    bool active;              // Is this slot in use?
    restart_history_t restart_history;  // Restart pattern tracking
    // Manual restart schedule (CET timezone)
    uint8_t restart_hour;         // Known restart hour (0-23 CET)
    uint8_t restart_minute;       // Known restart minute (0-59)
    uint8_t restart_interval_hours; // Interval: 4, 6, 8, or 12 hours
    bool manual_restart_set;      // True if user manually set restart time
} server_config_t;

// Player history entry
typedef struct {
    uint32_t timestamp;       // Unix timestamp
    int16_t player_count;     // Player count (-1 if unknown)
} history_entry_t;

// Application settings
typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    uint16_t refresh_interval_sec;  // 10-300 seconds
    uint8_t active_server_index;
    uint8_t server_count;
    server_config_t servers[MAX_SERVERS];
    bool first_boot;
} app_settings_t;

// Screen IDs
typedef enum {
    SCREEN_MAIN = 0,
    SCREEN_SETTINGS,
    SCREEN_WIFI_SETTINGS,
    SCREEN_SERVER_SETTINGS,
    SCREEN_ADD_SERVER,
    SCREEN_HISTORY,
    SCREEN_ALERTS
} screen_id_t;

// ============== GLOBAL STATE ==============

static app_settings_t settings;
static history_entry_t *player_history = NULL;  // PSRAM allocated
static uint16_t history_head = 0;
static uint16_t history_count = 0;

// WiFi event group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Display handles
static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_display_t *lvgl_disp = NULL;

// Touch handle
static esp_lcd_touch_handle_t touch_handle = NULL;
static lv_indev_t *touch_indev = NULL;

// Current screen
static screen_id_t current_screen = SCREEN_MAIN;
static lv_obj_t *screen_main = NULL;
static lv_obj_t *screen_settings = NULL;
static lv_obj_t *screen_wifi = NULL;
static lv_obj_t *screen_server = NULL;
static lv_obj_t *screen_add_server = NULL;
static lv_obj_t *screen_history = NULL;

// Main screen UI elements
static lv_obj_t *main_card = NULL;
static lv_obj_t *lbl_title = NULL;
static lv_obj_t *lbl_server = NULL;
static lv_obj_t *lbl_server_time = NULL;  // In-game time with day/night indicator
static lv_obj_t *day_night_indicator = NULL;  // Visual day/night box
static lv_obj_t *lbl_players = NULL;
static lv_obj_t *lbl_max = NULL;
static lv_obj_t *bar_players = NULL;
static lv_obj_t *lbl_status = NULL;
static lv_obj_t *lbl_update = NULL;
static lv_obj_t *lbl_ip = NULL;
static lv_obj_t *lbl_restart = NULL;  // Restart countdown display
static lv_obj_t *btn_refresh = NULL;
static lv_obj_t *btn_settings = NULL;
static lv_obj_t *btn_history = NULL;
static lv_obj_t *btn_prev_server = NULL;
static lv_obj_t *btn_next_server = NULL;
static lv_obj_t *alert_overlay = NULL;

// Settings screen elements
static lv_obj_t *kb = NULL;  // On-screen keyboard
static lv_obj_t *ta_ssid = NULL;
static lv_obj_t *ta_password = NULL;
static lv_obj_t *ta_server_id = NULL;
static lv_obj_t *ta_server_name = NULL;
static lv_obj_t *slider_refresh = NULL;
static lv_obj_t *lbl_refresh_val = NULL;
static lv_obj_t *slider_alert = NULL;
static lv_obj_t *lbl_alert_val = NULL;
static lv_obj_t *sw_alerts = NULL;
static lv_obj_t *chart_history = NULL;
// Restart schedule UI
static lv_obj_t *roller_restart_hour = NULL;
static lv_obj_t *roller_restart_min = NULL;
static lv_obj_t *dropdown_restart_interval = NULL;
static lv_obj_t *sw_restart_manual = NULL;
static lv_chart_series_t *chart_series = NULL;

// Server status
static int current_players = -1;
static int max_players = 60;
static char last_update[32] = "Never";
static char server_time[16] = "";  // In-game server time (e.g., "10:12")
static bool is_daytime = true;     // true = day, false = night
static bool wifi_connected = false;
static volatile bool refresh_requested = false;
static volatile bool alert_active = false;
static int64_t alert_start_time = 0;

// History time range selection
typedef enum {
    HISTORY_RANGE_1H = 0,    // 60 points (1 hour)
    HISTORY_RANGE_8H,        // 480 points (8 hours)
    HISTORY_RANGE_24H,       // 1440 points (24 hours)
    HISTORY_RANGE_WEEK       // 10080 points (7 days)
} history_range_t;

static history_range_t current_history_range = HISTORY_RANGE_1H;
static lv_obj_t *lbl_history_legend = NULL;

// SD Card state
static sdmmc_card_t *sd_card = NULL;
static bool sd_mounted = false;
static uint8_t ch422g_output_state = 0xFF;  // All outputs high by default (CS inactive)
static int unsaved_history_count = 0;       // Track new entries since last save
#define HISTORY_SAVE_INTERVAL  10           // Save every 10 new entries

// Custom colors
#define DAYZ_GREEN   lv_color_hex(0x7BC144)
#define DARK_BG      lv_color_hex(0x1A1A2E)
#define CARD_BG      lv_color_hex(0x16213E)
#define ALERT_RED    lv_color_hex(0xFF4444)
#define ALERT_ORANGE lv_color_hex(0xFF8800)

// ============== FORWARD DECLARATIONS ==============
static void update_ui(void);
static void query_server_status(void);
static void create_main_screen(void);
static void create_settings_screen(void);
static void create_wifi_settings_screen(void);
static void create_server_settings_screen(void);
static void create_add_server_screen(void);
static void create_history_screen(void);
static void switch_to_screen(screen_id_t screen);
static void save_settings(void);
static void load_settings(void);
static void add_history_entry(int players);
static void check_alerts(void);
static void show_alert(const char *message, lv_color_t color);
static void refresh_history_chart(void);
static esp_err_t init_ch422g(void);
static void ch422g_set_sd_cs(bool active);
static esp_err_t init_sd_card(void);
static void save_history_to_sd(void);
static void load_history_from_sd(void);
static void save_history_to_nvs(void);
static void load_history_from_nvs(void);
static void hide_alert(void);

// ============== NVS SETTINGS ==============

static void load_settings(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);

    // Set defaults first
    memset(&settings, 0, sizeof(settings));
    settings.refresh_interval_sec = 30;
    settings.first_boot = true;
    settings.server_count = 0;
    settings.active_server_index = 0;

    if (err == ESP_OK) {
        size_t len;

        // Load WiFi credentials
        len = sizeof(settings.wifi_ssid);
        if (nvs_get_str(nvs, "wifi_ssid", settings.wifi_ssid, &len) == ESP_OK) {
            settings.first_boot = false;
        }

        len = sizeof(settings.wifi_password);
        nvs_get_str(nvs, "wifi_pass", settings.wifi_password, &len);

        // Load refresh interval
        nvs_get_u16(nvs, "refresh_int", &settings.refresh_interval_sec);
        if (settings.refresh_interval_sec < 10) settings.refresh_interval_sec = 10;
        if (settings.refresh_interval_sec > 300) settings.refresh_interval_sec = 300;

        // Load server count and active index
        uint8_t count = 0;
        nvs_get_u8(nvs, "server_count", &count);
        settings.server_count = count > MAX_SERVERS ? MAX_SERVERS : count;
        nvs_get_u8(nvs, "active_srv", &settings.active_server_index);

        // Load each server
        for (int i = 0; i < settings.server_count; i++) {
            char key[16];

            snprintf(key, sizeof(key), "srv%d_id", i);
            len = sizeof(settings.servers[i].server_id);
            nvs_get_str(nvs, key, settings.servers[i].server_id, &len);

            snprintf(key, sizeof(key), "srv%d_name", i);
            len = sizeof(settings.servers[i].display_name);
            nvs_get_str(nvs, key, settings.servers[i].display_name, &len);

            snprintf(key, sizeof(key), "srv%d_ip", i);
            len = sizeof(settings.servers[i].ip_address);
            nvs_get_str(nvs, key, settings.servers[i].ip_address, &len);

            snprintf(key, sizeof(key), "srv%d_port", i);
            nvs_get_u16(nvs, key, &settings.servers[i].port);

            snprintf(key, sizeof(key), "srv%d_max", i);
            nvs_get_u16(nvs, key, &settings.servers[i].max_players);
            if (settings.servers[i].max_players == 0) settings.servers[i].max_players = 60;

            snprintf(key, sizeof(key), "srv%d_alert", i);
            nvs_get_u16(nvs, key, &settings.servers[i].alert_threshold);

            uint8_t alerts_en = 0;
            snprintf(key, sizeof(key), "srv%d_alen", i);
            nvs_get_u8(nvs, key, &alerts_en);
            settings.servers[i].alerts_enabled = alerts_en > 0;

            // Load restart history
            snprintf(key, sizeof(key), "srv%d_rcnt", i);
            nvs_get_u8(nvs, key, &settings.servers[i].restart_history.restart_count);

            snprintf(key, sizeof(key), "srv%d_ravg", i);
            nvs_get_u32(nvs, key, &settings.servers[i].restart_history.avg_interval_sec);

            snprintf(key, sizeof(key), "srv%d_rlast", i);
            nvs_get_u32(nvs, key, &settings.servers[i].restart_history.last_restart_time);

            // Load restart times array
            snprintf(key, sizeof(key), "srv%d_rtimes", i);
            len = sizeof(settings.servers[i].restart_history.restart_times);
            nvs_get_blob(nvs, key, settings.servers[i].restart_history.restart_times, &len);

            // Initialize last_known_players to -1 (unknown)
            settings.servers[i].restart_history.last_known_players = -1;

            // Load manual restart schedule
            snprintf(key, sizeof(key), "srv%d_rhr", i);
            nvs_get_u8(nvs, key, &settings.servers[i].restart_hour);

            snprintf(key, sizeof(key), "srv%d_rmin", i);
            nvs_get_u8(nvs, key, &settings.servers[i].restart_minute);

            snprintf(key, sizeof(key), "srv%d_rint", i);
            nvs_get_u8(nvs, key, &settings.servers[i].restart_interval_hours);

            uint8_t manual_set = 0;
            snprintf(key, sizeof(key), "srv%d_rman", i);
            nvs_get_u8(nvs, key, &manual_set);
            settings.servers[i].manual_restart_set = manual_set > 0;

            settings.servers[i].active = true;
        }

        nvs_close(nvs);
    }

    // If first boot or no servers, add default server
    if (settings.server_count == 0) {
        strcpy(settings.servers[0].server_id, "29986583");
        strcpy(settings.servers[0].display_name, "3833 | EUROPE - DE");
        strcpy(settings.servers[0].ip_address, "5.62.99.20");
        settings.servers[0].port = 11400;
        settings.servers[0].max_players = 60;
        settings.servers[0].alert_threshold = 50;
        settings.servers[0].alerts_enabled = false;
        settings.servers[0].active = true;
        settings.server_count = 1;
    }

    ESP_LOGI(TAG, "Settings loaded: %d servers, refresh=%ds, first_boot=%d",
             settings.server_count, settings.refresh_interval_sec, settings.first_boot);
}

static void save_settings(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return;
    }

    // Save WiFi credentials
    nvs_set_str(nvs, "wifi_ssid", settings.wifi_ssid);
    nvs_set_str(nvs, "wifi_pass", settings.wifi_password);

    // Save refresh interval
    nvs_set_u16(nvs, "refresh_int", settings.refresh_interval_sec);

    // Save server count and active index
    nvs_set_u8(nvs, "server_count", settings.server_count);
    nvs_set_u8(nvs, "active_srv", settings.active_server_index);

    // Save each server
    for (int i = 0; i < settings.server_count; i++) {
        char key[16];

        snprintf(key, sizeof(key), "srv%d_id", i);
        nvs_set_str(nvs, key, settings.servers[i].server_id);

        snprintf(key, sizeof(key), "srv%d_name", i);
        nvs_set_str(nvs, key, settings.servers[i].display_name);

        snprintf(key, sizeof(key), "srv%d_ip", i);
        nvs_set_str(nvs, key, settings.servers[i].ip_address);

        snprintf(key, sizeof(key), "srv%d_port", i);
        nvs_set_u16(nvs, key, settings.servers[i].port);

        snprintf(key, sizeof(key), "srv%d_max", i);
        nvs_set_u16(nvs, key, settings.servers[i].max_players);

        snprintf(key, sizeof(key), "srv%d_alert", i);
        nvs_set_u16(nvs, key, settings.servers[i].alert_threshold);

        snprintf(key, sizeof(key), "srv%d_alen", i);
        nvs_set_u8(nvs, key, settings.servers[i].alerts_enabled ? 1 : 0);

        // Save restart history
        snprintf(key, sizeof(key), "srv%d_rcnt", i);
        nvs_set_u8(nvs, key, settings.servers[i].restart_history.restart_count);

        snprintf(key, sizeof(key), "srv%d_ravg", i);
        nvs_set_u32(nvs, key, settings.servers[i].restart_history.avg_interval_sec);

        snprintf(key, sizeof(key), "srv%d_rlast", i);
        nvs_set_u32(nvs, key, settings.servers[i].restart_history.last_restart_time);

        // Save restart times array as blob
        snprintf(key, sizeof(key), "srv%d_rtimes", i);
        nvs_set_blob(nvs, key, settings.servers[i].restart_history.restart_times,
                     sizeof(settings.servers[i].restart_history.restart_times));

        // Save manual restart schedule
        snprintf(key, sizeof(key), "srv%d_rhr", i);
        nvs_set_u8(nvs, key, settings.servers[i].restart_hour);

        snprintf(key, sizeof(key), "srv%d_rmin", i);
        nvs_set_u8(nvs, key, settings.servers[i].restart_minute);

        snprintf(key, sizeof(key), "srv%d_rint", i);
        nvs_set_u8(nvs, key, settings.servers[i].restart_interval_hours);

        snprintf(key, sizeof(key), "srv%d_rman", i);
        nvs_set_u8(nvs, key, settings.servers[i].manual_restart_set ? 1 : 0);
    }

    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Settings saved");
}

// ============== PLAYER HISTORY ==============

static void init_history(void) {
    // Allocate history buffer in PSRAM
    player_history = heap_caps_calloc(MAX_HISTORY_ENTRIES, sizeof(history_entry_t), MALLOC_CAP_SPIRAM);
    if (!player_history) {
        ESP_LOGE(TAG, "Failed to allocate history buffer in PSRAM");
        player_history = calloc(MAX_HISTORY_ENTRIES, sizeof(history_entry_t));
    }
    history_head = 0;
    history_count = 0;
    ESP_LOGI(TAG, "History buffer initialized (%d entries)", MAX_HISTORY_ENTRIES);
}

static void add_history_entry(int players) {
    if (!player_history) return;

    time_t now;
    time(&now);

    player_history[history_head].timestamp = (uint32_t)now;
    player_history[history_head].player_count = (int16_t)players;

    history_head = (history_head + 1) % MAX_HISTORY_ENTRIES;
    if (history_count < MAX_HISTORY_ENTRIES) {
        history_count++;
    }

    // Periodic save to SD card and NVS backup
    unsaved_history_count++;
    if (unsaved_history_count >= HISTORY_SAVE_INTERVAL) {
        if (sd_mounted) {
            save_history_to_sd();
        }
        // Always save to NVS as backup (more reliable)
        save_history_to_nvs();
    }
}

static int get_history_entry(int index, history_entry_t *entry) {
    if (!player_history || index >= history_count) return -1;

    int actual_index;
    if (history_count < MAX_HISTORY_ENTRIES) {
        actual_index = index;
    } else {
        actual_index = (history_head + index) % MAX_HISTORY_ENTRIES;
    }

    *entry = player_history[actual_index];
    return 0;
}

// ============== CH422G IO EXPANDER & SD CARD ==============

// Initialize CH422G IO expander (shares I2C with touch controller)
static esp_err_t init_ch422g(void) {
    // CH422G is on the same I2C bus as GT911 touch (already initialized)
    // Set all outputs high (SD_CS inactive)
    ch422g_output_state = 0xFF;

    uint8_t data[2] = {CH422G_REG_OUT, ch422g_output_state};
    esp_err_t ret = i2c_master_write_to_device(TOUCH_I2C_NUM, CH422G_I2C_ADDR, data, 2, pdMS_TO_TICKS(100));

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CH422G IO expander initialized");
    } else {
        ESP_LOGW(TAG, "CH422G not found or failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

// Control SD card chip select via CH422G EXIO4
static void ch422g_set_sd_cs(bool active) {
    if (active) {
        ch422g_output_state &= ~CH422G_EXIO4_BIT;  // Low = active
    } else {
        ch422g_output_state |= CH422G_EXIO4_BIT;   // High = inactive
    }

    uint8_t data[2] = {CH422G_REG_OUT, ch422g_output_state};
    i2c_master_write_to_device(TOUCH_I2C_NUM, CH422G_I2C_ADDR, data, 2, pdMS_TO_TICKS(100));
}

// Initialize SD card in SPI mode
static esp_err_t init_sd_card(void) {
    ESP_LOGI(TAG, "Initializing SD card...");

    // First initialize CH422G
    init_ch422g();

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

    // SD card SPI device config - use GPIO as CS (we'll control via CH422G)
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = GPIO_NUM_NC;  // We control CS via CH422G
    slot_config.host_id = SPI2_HOST;

    // Activate CS before mounting
    ch422g_set_sd_cs(true);

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &sd_card);

    ch422g_set_sd_cs(false);

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

// History file header structure
typedef struct {
    uint32_t magic;         // 0xDAYZ0001
    uint16_t head;          // history_head
    uint16_t count;         // history_count
} history_file_header_t;

#define HISTORY_FILE_MAGIC  0xDA120001
#define HISTORY_FILE_PATH   "/sdcard/history.bin"

// Save history to SD card
static void save_history_to_sd(void) {
    if (!sd_mounted || !player_history) {
        ESP_LOGD(TAG, "Cannot save: SD not mounted or no history");
        return;
    }

    ch422g_set_sd_cs(true);

    FILE *f = fopen(HISTORY_FILE_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open history file for writing");
        ch422g_set_sd_cs(false);
        return;
    }

    // Write header
    history_file_header_t header = {
        .magic = HISTORY_FILE_MAGIC,
        .head = history_head,
        .count = history_count
    };
    fwrite(&header, sizeof(header), 1, f);

    // Write history entries
    if (history_count > 0) {
        int entries_to_write = (history_count < MAX_HISTORY_ENTRIES) ? history_count : MAX_HISTORY_ENTRIES;
        fwrite(player_history, sizeof(history_entry_t), entries_to_write, f);
    }

    fclose(f);
    ch422g_set_sd_cs(false);

    unsaved_history_count = 0;
    ESP_LOGI(TAG, "History saved to SD (%d entries)", history_count);
}

// Load history from SD card
static void load_history_from_sd(void) {
    if (!sd_mounted || !player_history) {
        ESP_LOGD(TAG, "Cannot load: SD not mounted or no history buffer");
        return;
    }

    ch422g_set_sd_cs(true);

    FILE *f = fopen(HISTORY_FILE_PATH, "rb");
    if (!f) {
        ESP_LOGI(TAG, "No history file found on SD card");
        ch422g_set_sd_cs(false);
        return;
    }

    // Read header
    history_file_header_t header;
    if (fread(&header, sizeof(header), 1, f) != 1 || header.magic != HISTORY_FILE_MAGIC) {
        ESP_LOGW(TAG, "Invalid history file header");
        fclose(f);
        ch422g_set_sd_cs(false);
        return;
    }

    // Read entries
    history_head = header.head;
    history_count = header.count;

    if (history_count > MAX_HISTORY_ENTRIES) {
        history_count = MAX_HISTORY_ENTRIES;
    }

    int entries_to_read = (history_count < MAX_HISTORY_ENTRIES) ? history_count : MAX_HISTORY_ENTRIES;
    size_t read_count = fread(player_history, sizeof(history_entry_t), entries_to_read, f);

    fclose(f);
    ch422g_set_sd_cs(false);

    if (read_count != entries_to_read) {
        ESP_LOGW(TAG, "Partial history read: %d/%d", (int)read_count, entries_to_read);
        history_count = read_count;
    }

    ESP_LOGI(TAG, "History loaded from SD (%d entries)", history_count);
}

// ============== NVS HISTORY BACKUP ==============
// Store recent history in NVS as backup (limited to 200 entries due to NVS size)
#define NVS_HISTORY_MAX  200
#define NVS_HISTORY_KEY  "hist_data"
#define NVS_HISTORY_META "hist_meta"

static void save_history_to_nvs(void) {
    if (!player_history || history_count == 0) return;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for history save");
        return;
    }

    // Save metadata (head, count)
    uint32_t meta = ((uint32_t)history_head << 16) | (history_count & 0xFFFF);
    nvs_set_u32(nvs, NVS_HISTORY_META, meta);

    // Save most recent entries (up to NVS_HISTORY_MAX)
    int entries_to_save = (history_count < NVS_HISTORY_MAX) ? history_count : NVS_HISTORY_MAX;
    int start_idx = (history_count > NVS_HISTORY_MAX) ? (history_count - NVS_HISTORY_MAX) : 0;

    // Create temporary buffer with recent entries in order
    history_entry_t *temp = malloc(entries_to_save * sizeof(history_entry_t));
    if (temp) {
        for (int i = 0; i < entries_to_save; i++) {
            history_entry_t entry;
            if (get_history_entry(start_idx + i, &entry) == 0) {
                temp[i] = entry;
            }
        }
        nvs_set_blob(nvs, NVS_HISTORY_KEY, temp, entries_to_save * sizeof(history_entry_t));
        free(temp);
    }

    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "History backed up to NVS (%d entries)", entries_to_save);
}

static void load_history_from_nvs(void) {
    if (!player_history) return;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "No NVS history backup found");
        return;
    }

    // Load metadata
    uint32_t meta = 0;
    if (nvs_get_u32(nvs, NVS_HISTORY_META, &meta) != ESP_OK) {
        nvs_close(nvs);
        return;
    }

    // Load entries
    size_t required_size = 0;
    if (nvs_get_blob(nvs, NVS_HISTORY_KEY, NULL, &required_size) != ESP_OK || required_size == 0) {
        nvs_close(nvs);
        return;
    }

    history_entry_t *temp = malloc(required_size);
    if (temp && nvs_get_blob(nvs, NVS_HISTORY_KEY, temp, &required_size) == ESP_OK) {
        int loaded_count = required_size / sizeof(history_entry_t);
        for (int i = 0; i < loaded_count && i < MAX_HISTORY_ENTRIES; i++) {
            player_history[i] = temp[i];
        }
        history_count = loaded_count;
        history_head = loaded_count % MAX_HISTORY_ENTRIES;
        ESP_LOGI(TAG, "History restored from NVS (%d entries)", loaded_count);
    }
    if (temp) free(temp);

    nvs_close(nvs);
}

// ============== WIFI ==============

static bool sntp_initialized = false;

static void init_sntp(void) {
    if (sntp_initialized) return;

    ESP_LOGI(TAG, "Initializing SNTP for CET timezone...");

    // Set timezone to CET (Central European Time) with DST
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    sntp_initialized = true;
    ESP_LOGI(TAG, "SNTP initialized");
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        ESP_LOGI(TAG, "Disconnected, retrying...");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // Initialize SNTP for time sync
        init_sntp();
    }
}

static void wifi_init(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, settings.wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, settings.wifi_password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init complete, connecting to %s", settings.wifi_ssid);
}

static void wifi_reconnect(void) {
    ESP_LOGI(TAG, "Reconnecting WiFi with new credentials...");
    esp_wifi_stop();

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, settings.wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, settings.wifi_password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// ============== HTTP API ==============

static char http_response[16384];
static int http_response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (http_response_len + evt->data_len < sizeof(http_response) - 1) {
                memcpy(http_response + http_response_len, evt->data, evt->data_len);
                http_response_len += evt->data_len;
                http_response[http_response_len] = 0;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

static int parse_player_count(const char *json) {
    const char *players_str = strstr(json, "\"players\":");
    if (players_str) {
        players_str += 10;
        while (*players_str == ' ') players_str++;
        return atoi(players_str);
    }

    players_str = strstr(json, "\"player_count\":");
    if (players_str) {
        players_str += 15;
        while (*players_str == ' ') players_str++;
        return atoi(players_str);
    }

    return -1;
}

static int parse_max_players(const char *json) {
    const char *max_str = strstr(json, "\"maxPlayers\":");
    if (max_str) {
        max_str += 13;
        while (*max_str == ' ') max_str++;
        return atoi(max_str);
    }

    max_str = strstr(json, "\"max_players\":");
    if (max_str) {
        max_str += 14;
        while (*max_str == ' ') max_str++;
        return atoi(max_str);
    }

    return 60;
}

// Parse server in-game time from BattleMetrics details
// Format: "time": "10:12" or "time":"10:12" (24-hour format)
static void parse_server_time(const char *json) {
    // Look for "time" field in the details object
    const char *time_field = strstr(json, "\"time\"");
    if (!time_field) {
        server_time[0] = '\0';
        return;
    }

    // Move past "time"
    time_field += 6;  // Skip past "time"

    // Skip any whitespace and colon
    while (*time_field == ' ' || *time_field == ':') time_field++;

    // Should now be at the opening quote of the value
    if (*time_field == '"') {
        time_field++;  // Skip opening quote

        // Copy time string until closing quote (format "HH:MM")
        int i = 0;
        while (*time_field && *time_field != '"' && i < 15) {
            server_time[i++] = *time_field++;
        }
        server_time[i] = '\0';

        // Parse hour to determine day/night
        // Format is "HH:MM" so atoi will get the hour
        int hour = atoi(server_time);

        // DayZ day/night cycle:
        // Sunrise ~05:00, Sunset ~21:00 (varies by server settings)
        // Using 05:00-21:00 as daytime for better accuracy
        is_daytime = (hour >= 5 && hour < 21);

        ESP_LOGI(TAG, "Server time: %s, hour=%d (%s)", server_time, hour, is_daytime ? "Day" : "Night");
    } else {
        server_time[0] = '\0';
    }
}

// ============== BUZZER ==============

static bool buzzer_initialized = false;

static void buzzer_init(void) {
    if (!BUZZER_ENABLED || buzzer_initialized) return;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUZZER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(BUZZER_PIN, 0);
    buzzer_initialized = true;
    ESP_LOGI(TAG, "Buzzer initialized on GPIO%d", BUZZER_PIN);
}

// Simple beep (blocking)
// For ACTIVE buzzer: just turn on/off (has built-in oscillator)
// For PASSIVE buzzer: would need PWM square wave
static void buzzer_beep(int duration_ms, int frequency_hz) {
    (void)frequency_hz;  // Active buzzer ignores frequency - has its own tone
    if (!BUZZER_ENABLED || !buzzer_initialized) return;

    gpio_set_level(BUZZER_PIN, 1);  // Turn ON
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_set_level(BUZZER_PIN, 0);  // Turn OFF
}

// Alert patterns
static void buzzer_alert_restart(void) {
    // Three ascending beeps for "server restarted - fresh loot!"
    buzzer_beep(150, 800);
    vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_beep(150, 1000);
    vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_beep(200, 1200);
}

static void buzzer_alert_threshold(void) {
    // Two quick beeps for player threshold
    buzzer_beep(100, 1000);
    vTaskDelay(pdMS_TO_TICKS(80));
    buzzer_beep(100, 1000);
}

// ============== RESTART DETECTION ==============

// Record a detected restart
static void record_restart(server_config_t *srv, uint32_t timestamp) {
    restart_history_t *rh = &srv->restart_history;

    // Check minimum interval to avoid false positives
    if (rh->last_restart_time > 0 &&
        (timestamp - rh->last_restart_time) < MIN_RESTART_INTERVAL_SEC) {
        ESP_LOGW(TAG, "Ignoring restart - too soon after last one");
        return;
    }

    // Shift history and add new restart
    if (rh->restart_count < MAX_RESTART_HISTORY) {
        rh->restart_times[rh->restart_count] = timestamp;
        rh->restart_count++;
    } else {
        // Shift array to make room for new entry
        for (int i = 0; i < MAX_RESTART_HISTORY - 1; i++) {
            rh->restart_times[i] = rh->restart_times[i + 1];
        }
        rh->restart_times[MAX_RESTART_HISTORY - 1] = timestamp;
    }

    rh->last_restart_time = timestamp;

    // Calculate average interval between restarts
    if (rh->restart_count >= 2) {
        uint32_t total_interval = 0;
        for (int i = 1; i < rh->restart_count; i++) {
            total_interval += rh->restart_times[i] - rh->restart_times[i - 1];
        }
        rh->avg_interval_sec = total_interval / (rh->restart_count - 1);

        int hours = rh->avg_interval_sec / 3600;
        int mins = (rh->avg_interval_sec % 3600) / 60;
        ESP_LOGI(TAG, "Server restart detected! Avg interval: %dh %dm", hours, mins);
    } else {
        ESP_LOGI(TAG, "First server restart recorded");
    }

    // Sound the alert
    buzzer_alert_restart();
}

// Check if a restart just happened
static void check_for_restart(server_config_t *srv, int current_count) {
    restart_history_t *rh = &srv->restart_history;

    // Detect restart: players dropped from >=5 to 0
    if (rh->last_known_players >= RESTART_DETECT_MIN_PLAYERS &&
        current_count == RESTART_DETECT_DROP_TO) {

        time_t now;
        time(&now);
        record_restart(srv, (uint32_t)now);
    }

    // Update last known player count
    rh->last_known_players = current_count;
}

// Calculate seconds until next predicted restart
static int get_restart_countdown(server_config_t *srv) {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);  // CET timezone (already set via SNTP)

    // Priority 1: Manual restart schedule
    if (srv->manual_restart_set && srv->restart_interval_hours > 0) {
        int interval_sec = srv->restart_interval_hours * 3600;

        // Calculate today's first restart time at restart_hour:restart_minute
        struct tm restart_tm = timeinfo;
        restart_tm.tm_hour = srv->restart_hour;
        restart_tm.tm_min = srv->restart_minute;
        restart_tm.tm_sec = 0;
        time_t first_restart_today = mktime(&restart_tm);

        // Find the next restart from now
        time_t next_restart = first_restart_today;

        // If first restart today is in the past, find the next one
        while (next_restart <= now) {
            next_restart += interval_sec;
        }

        // If we went too far back, start from beginning of day
        while (next_restart > now + interval_sec) {
            next_restart -= interval_sec;
        }

        int countdown = (int)(next_restart - now);
        return countdown > 0 ? countdown : 0;
    }

    // Priority 2: Auto-detected pattern
    restart_history_t *rh = &srv->restart_history;
    if (rh->restart_count >= 2 && rh->avg_interval_sec > 0) {
        uint32_t predicted_next = rh->last_restart_time + rh->avg_interval_sec;

        if ((uint32_t)now >= predicted_next) {
            return 0;  // Overdue / imminent
        }
        return predicted_next - (uint32_t)now;
    }

    return -1;  // Unknown
}

// Format countdown as string (e.g., "~2h 15m")
static void format_countdown(int seconds, char *buf, size_t buf_size) {
    if (seconds < 0) {
        snprintf(buf, buf_size, "Unknown");
        return;
    }

    if (seconds == 0) {
        snprintf(buf, buf_size, "Imminent!");
        return;
    }

    int hours = seconds / 3600;
    int mins = (seconds % 3600) / 60;

    if (hours > 0) {
        snprintf(buf, buf_size, "~%dh %dm", hours, mins);
    } else {
        snprintf(buf, buf_size, "~%dm", mins);
    }
}

static void query_server_status(void) {
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected, skipping query");
        return;
    }

    if (settings.server_count == 0) return;

    server_config_t *srv = &settings.servers[settings.active_server_index];

    http_response_len = 0;
    memset(http_response, 0, sizeof(http_response));

    char url[256];
    snprintf(url, sizeof(url),
        "https://api.battlemetrics.com/servers/%s",
        srv->server_id);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));

        int players = parse_player_count(http_response);
        int max_p = parse_max_players(http_response);
        parse_server_time(http_response);

        if (players >= 0) {
            current_players = players;
            max_players = max_p > 0 ? max_p : srv->max_players;
            srv->max_players = max_players;

            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);
            snprintf(last_update, sizeof(last_update), "%02d:%02d:%02d CET",
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

            ESP_LOGI(TAG, "Players: %d/%d", current_players, max_players);

            // Add to history
            add_history_entry(current_players);

            // Check for alerts
            check_alerts();

            // Check for server restart (player drop detection)
            check_for_restart(srv, current_players);
        } else {
            ESP_LOGW(TAG, "Could not parse player count from response");
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

// ============== ALERTS ==============

static void check_alerts(void) {
    if (settings.server_count == 0) return;

    server_config_t *srv = &settings.servers[settings.active_server_index];

    if (!srv->alerts_enabled) return;

    // Server full alert
    if (current_players >= max_players) {
        if (!alert_active) {
            buzzer_alert_threshold();  // Beep only on first trigger!
        }
        show_alert("SERVER FULL!", ALERT_RED);
        return;
    }

    // Threshold alert
    if (srv->alert_threshold > 0 && current_players >= srv->alert_threshold) {
        if (!alert_active) {
            buzzer_alert_threshold();  // Beep only on first trigger!
        }
        char msg[64];
        snprintf(msg, sizeof(msg), "ALERT: %d+ players!", srv->alert_threshold);
        show_alert(msg, ALERT_ORANGE);
        return;
    }

    // Clear alert if conditions no longer met
    if (alert_active) {
        hide_alert();
    }
}

static void show_alert(const char *message, lv_color_t color) {
    if (!lvgl_port_lock(100)) return;

    alert_active = true;
    alert_start_time = esp_timer_get_time() / 1000;

    if (alert_overlay) {
        lv_obj_delete(alert_overlay);
    }

    // Create alert banner at top of screen (doesn't block button clicks)
    alert_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(alert_overlay, LCD_WIDTH - 160, 60);  // Leave space for buttons on sides
    lv_obj_align(alert_overlay, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(alert_overlay, color, 0);
    lv_obj_set_style_bg_opa(alert_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(alert_overlay, 0, 0);
    lv_obj_set_style_radius(alert_overlay, 0, 0);
    lv_obj_clear_flag(alert_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(alert_overlay, LV_OBJ_FLAG_CLICKABLE);  // Allow clicks to pass through

    lv_obj_t *lbl = lv_label_create(alert_overlay);
    lv_label_set_text(lbl, message);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Alert shown: %s", message);
}

static void hide_alert(void) {
    if (!alert_active) return;

    if (lvgl_port_lock(100)) {
        if (alert_overlay) {
            lv_obj_delete(alert_overlay);
            alert_overlay = NULL;
        }
        alert_active = false;
        lvgl_port_unlock();
    }
}

// ============== UI CALLBACKS ==============

static void on_refresh_clicked(lv_event_t *e) {
    ESP_LOGI(TAG, "Refresh button pressed");
    refresh_requested = true;
}

static void on_card_clicked(lv_event_t *e) {
    ESP_LOGI(TAG, "Card tapped - refreshing");
    refresh_requested = true;
}

static void on_settings_clicked(lv_event_t *e) {
    ESP_LOGI(TAG, "Settings button pressed");
    switch_to_screen(SCREEN_SETTINGS);
}

static void on_history_clicked(lv_event_t *e) {
    ESP_LOGI(TAG, "History button pressed");
    switch_to_screen(SCREEN_HISTORY);
}

static void on_back_clicked(lv_event_t *e) {
    switch_to_screen(SCREEN_MAIN);
}

static void on_prev_server_clicked(lv_event_t *e) {
    if (settings.server_count <= 1) return;

    if (settings.active_server_index == 0) {
        settings.active_server_index = settings.server_count - 1;
    } else {
        settings.active_server_index--;
    }

    current_players = -1;
    server_time[0] = '\0';  // Clear server time on switch
    refresh_requested = true;
    save_settings();
    ESP_LOGI(TAG, "Switched to server %d", settings.active_server_index);
}

static void on_next_server_clicked(lv_event_t *e) {
    if (settings.server_count <= 1) return;

    settings.active_server_index = (settings.active_server_index + 1) % settings.server_count;

    current_players = -1;
    server_time[0] = '\0';  // Clear server time on switch
    refresh_requested = true;
    save_settings();
    ESP_LOGI(TAG, "Switched to server %d", settings.active_server_index);
}

static void on_wifi_settings_clicked(lv_event_t *e) {
    switch_to_screen(SCREEN_WIFI_SETTINGS);
}

static void on_server_settings_clicked(lv_event_t *e) {
    switch_to_screen(SCREEN_SERVER_SETTINGS);
}

static void on_add_server_clicked(lv_event_t *e) {
    switch_to_screen(SCREEN_ADD_SERVER);
}

static void on_keyboard_event(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    (void)lv_keyboard_get_textarea(obj);  // Unused but may be needed later

    if (lv_event_get_code(e) == LV_EVENT_READY) {
        // Keyboard closed with OK
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    } else if (lv_event_get_code(e) == LV_EVENT_CANCEL) {
        // Keyboard cancelled
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_textarea_clicked(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    if (kb) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view(ta, LV_ANIM_ON);
    }
}

static void on_wifi_save_clicked(lv_event_t *e) {
    if (lvgl_port_lock(100)) {
        strncpy(settings.wifi_ssid, lv_textarea_get_text(ta_ssid), sizeof(settings.wifi_ssid) - 1);
        strncpy(settings.wifi_password, lv_textarea_get_text(ta_password), sizeof(settings.wifi_password) - 1);
        settings.first_boot = false;
        lvgl_port_unlock();
    }

    save_settings();
    wifi_reconnect();
    switch_to_screen(SCREEN_SETTINGS);
}

static void on_refresh_slider_changed(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    settings.refresh_interval_sec = lv_slider_get_value(slider);

    // Update label in real-time
    if (lbl_refresh_val) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d sec", settings.refresh_interval_sec);
        lv_label_set_text(lbl_refresh_val, buf);
    }

    save_settings();
}

static void on_alert_slider_changed(lv_event_t *e) {
    if (settings.server_count == 0) return;

    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    settings.servers[settings.active_server_index].alert_threshold = val;
    save_settings();

    // Update value label
    if (lbl_alert_val) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d players", val);
        lv_label_set_text(lbl_alert_val, buf);
    }
}

// Restart schedule event handlers
static void on_restart_hour_changed(lv_event_t *e) {
    if (settings.server_count == 0) return;
    lv_obj_t *roller = lv_event_get_target(e);
    settings.servers[settings.active_server_index].restart_hour = lv_roller_get_selected(roller);
    save_settings();
}

static void on_restart_min_changed(lv_event_t *e) {
    if (settings.server_count == 0) return;
    lv_obj_t *roller = lv_event_get_target(e);
    // Convert roller index to actual minutes (0, 5, 10, 15, ... 55)
    settings.servers[settings.active_server_index].restart_minute = lv_roller_get_selected(roller) * 5;
    save_settings();
}

static void on_restart_interval_changed(lv_event_t *e) {
    if (settings.server_count == 0) return;
    lv_obj_t *dd = lv_event_get_target(e);
    int sel = lv_dropdown_get_selected(dd);
    // Options: 0=4h, 1=6h, 2=8h, 3=12h
    const uint8_t intervals[] = {4, 6, 8, 12};
    settings.servers[settings.active_server_index].restart_interval_hours = intervals[sel];
    save_settings();
}

static void on_restart_manual_switch_changed(lv_event_t *e) {
    if (settings.server_count == 0) return;
    lv_obj_t *sw = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    settings.servers[settings.active_server_index].manual_restart_set = enabled;
    save_settings();
}

static void on_alerts_switch_changed(lv_event_t *e) {
    if (settings.server_count == 0) return;

    lv_obj_t *sw = lv_event_get_target(e);
    settings.servers[settings.active_server_index].alerts_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    save_settings();
}

static void on_server_save_clicked(lv_event_t *e) {
    if (settings.server_count >= MAX_SERVERS) {
        ESP_LOGW(TAG, "Max servers reached");
        switch_to_screen(SCREEN_SETTINGS);
        return;
    }

    if (lvgl_port_lock(100)) {
        int idx = settings.server_count;

        strncpy(settings.servers[idx].server_id, lv_textarea_get_text(ta_server_id),
                sizeof(settings.servers[idx].server_id) - 1);
        strncpy(settings.servers[idx].display_name, lv_textarea_get_text(ta_server_name),
                sizeof(settings.servers[idx].display_name) - 1);

        settings.servers[idx].max_players = 60;
        settings.servers[idx].alert_threshold = 50;
        settings.servers[idx].alerts_enabled = false;
        settings.servers[idx].active = true;
        settings.servers[idx].port = 0;
        strcpy(settings.servers[idx].ip_address, "");

        settings.server_count++;
        settings.active_server_index = idx;

        lvgl_port_unlock();
    }

    save_settings();
    current_players = -1;
    server_time[0] = '\0';
    refresh_requested = true;
    switch_to_screen(SCREEN_MAIN);
}

static void on_delete_server_clicked(lv_event_t *e) {
    if (settings.server_count <= 1) {
        ESP_LOGW(TAG, "Cannot delete last server");
        return;
    }

    int idx = settings.active_server_index;

    // Shift servers down
    for (int i = idx; i < settings.server_count - 1; i++) {
        settings.servers[i] = settings.servers[i + 1];
    }

    settings.server_count--;
    if (settings.active_server_index >= settings.server_count) {
        settings.active_server_index = settings.server_count - 1;
    }

    save_settings();
    current_players = -1;
    server_time[0] = '\0';
    refresh_requested = true;
    switch_to_screen(SCREEN_MAIN);
}

// ============== UI CREATION ==============

static lv_obj_t* create_back_button(lv_obj_t *parent) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 100, 40);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, on_back_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);

    return btn;
}

static void create_main_screen(void) {
    screen_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_main, DARK_BG, 0);

    // Main container card
    main_card = lv_obj_create(screen_main);
    lv_obj_set_size(main_card, 760, 400);
    lv_obj_align(main_card, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(main_card, CARD_BG, 0);
    lv_obj_set_style_radius(main_card, 20, 0);
    lv_obj_set_style_border_width(main_card, 2, 0);
    lv_obj_set_style_border_color(main_card, DAYZ_GREEN, 0);
    lv_obj_set_style_pad_all(main_card, 30, 0);
    lv_obj_clear_flag(main_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(main_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(main_card, on_card_clicked, LV_EVENT_CLICKED, NULL);

    // Top bar with buttons
    // Settings button (top-left)
    btn_settings = lv_btn_create(screen_main);
    lv_obj_set_size(btn_settings, 50, 50);
    lv_obj_align(btn_settings, LV_ALIGN_TOP_LEFT, 20, 10);
    lv_obj_set_style_bg_color(btn_settings, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(btn_settings, 25, 0);
    lv_obj_add_event_cb(btn_settings, on_settings_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_set = lv_label_create(btn_settings);
    lv_label_set_text(lbl_set, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(lbl_set, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl_set);

    // History button (next to settings)
    btn_history = lv_btn_create(screen_main);
    lv_obj_set_size(btn_history, 50, 50);
    lv_obj_align(btn_history, LV_ALIGN_TOP_LEFT, 80, 10);
    lv_obj_set_style_bg_color(btn_history, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(btn_history, 25, 0);
    lv_obj_add_event_cb(btn_history, on_history_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_hist = lv_label_create(btn_history);
    lv_label_set_text(lbl_hist, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(lbl_hist, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl_hist);

    // Server navigation buttons
    btn_prev_server = lv_btn_create(screen_main);
    lv_obj_set_size(btn_prev_server, 50, 50);
    lv_obj_align(btn_prev_server, LV_ALIGN_TOP_MID, -100, 10);
    lv_obj_set_style_bg_color(btn_prev_server, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(btn_prev_server, 25, 0);
    lv_obj_add_event_cb(btn_prev_server, on_prev_server_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_prev = lv_label_create(btn_prev_server);
    lv_label_set_text(lbl_prev, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(lbl_prev, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl_prev);

    btn_next_server = lv_btn_create(screen_main);
    lv_obj_set_size(btn_next_server, 50, 50);
    lv_obj_align(btn_next_server, LV_ALIGN_TOP_MID, 100, 10);
    lv_obj_set_style_bg_color(btn_next_server, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(btn_next_server, 25, 0);
    lv_obj_add_event_cb(btn_next_server, on_next_server_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_next = lv_label_create(btn_next_server);
    lv_label_set_text(lbl_next, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(lbl_next, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl_next);

    // Refresh button (top-right)
    btn_refresh = lv_btn_create(screen_main);
    lv_obj_set_size(btn_refresh, 110, 50);
    lv_obj_align(btn_refresh, LV_ALIGN_TOP_RIGHT, -20, 10);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_radius(btn_refresh, 25, 0);
    lv_obj_add_event_cb(btn_refresh, on_refresh_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_refresh = lv_label_create(btn_refresh);
    lv_label_set_text(lbl_refresh, LV_SYMBOL_REFRESH " Refresh");
    lv_obj_set_style_text_font(lbl_refresh, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_refresh);

    // Title - "DayZ Server Monitor"
    lbl_title = lv_label_create(main_card);
    lv_label_set_text(lbl_title, "DayZ Server Monitor");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_title, DAYZ_GREEN, 0);
    lv_obj_set_style_text_letter_space(lbl_title, 4, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, -10);

    // Server name
    lbl_server = lv_label_create(main_card);
    lv_label_set_text(lbl_server, "Loading...");
    lv_obj_set_style_text_font(lbl_server, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_server, lv_color_white(), 0);
    lv_obj_align(lbl_server, LV_ALIGN_TOP_MID, 0, 45);

    // Server in-game time with visual day/night indicator
    // Create a container to hold indicator + time text
    lv_obj_t *time_cont = lv_obj_create(main_card);
    lv_obj_set_size(time_cont, 200, 30);
    lv_obj_align(time_cont, LV_ALIGN_TOP_MID, 0, 75);
    lv_obj_set_style_bg_opa(time_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(time_cont, 0, 0);
    lv_obj_set_style_pad_all(time_cont, 0, 0);
    lv_obj_clear_flag(time_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Day/night visual indicator (colored circle)
    day_night_indicator = lv_obj_create(time_cont);
    lv_obj_set_size(day_night_indicator, 20, 20);
    lv_obj_align(day_night_indicator, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(day_night_indicator, 10, 0);  // Make it circular
    lv_obj_set_style_bg_color(day_night_indicator, lv_color_hex(0xFFD700), 0);  // Default: yellow (day)
    lv_obj_set_style_border_width(day_night_indicator, 0, 0);
    lv_obj_add_flag(day_night_indicator, LV_OBJ_FLAG_HIDDEN);  // Hide until we have data

    // Time text
    lbl_server_time = lv_label_create(time_cont);
    lv_label_set_text(lbl_server_time, "");
    lv_obj_set_style_text_font(lbl_server_time, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_server_time, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(lbl_server_time, LV_ALIGN_LEFT_MID, 28, 0);

    // Players container
    lv_obj_t *players_cont = lv_obj_create(main_card);
    lv_obj_set_size(players_cont, 400, 100);
    lv_obj_align(players_cont, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_opa(players_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(players_cont, 0, 0);
    lv_obj_clear_flag(players_cont, LV_OBJ_FLAG_SCROLLABLE);

    // "PLAYERS" label
    lv_obj_t *lbl_players_title = lv_label_create(players_cont);
    lv_label_set_text(lbl_players_title, "PLAYERS");
    lv_obj_set_style_text_font(lbl_players_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_players_title, lv_color_hex(0x00D4FF), 0);
    lv_obj_align(lbl_players_title, LV_ALIGN_LEFT_MID, 0, 0);

    // Big player number
    lbl_players = lv_label_create(players_cont);
    lv_label_set_text(lbl_players, "---");
    lv_obj_set_style_text_font(lbl_players, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_players, lv_color_white(), 0);
    lv_obj_align(lbl_players, LV_ALIGN_CENTER, 20, 0);

    // Max players
    lbl_max = lv_label_create(players_cont);
    lv_label_set_text(lbl_max, "/60");
    lv_obj_set_style_text_font(lbl_max, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_max, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_max, LV_ALIGN_RIGHT_MID, 0, 5);

    // Progress bar
    bar_players = lv_bar_create(main_card);
    lv_obj_set_size(bar_players, 680, 35);
    lv_obj_align(bar_players, LV_ALIGN_CENTER, 0, 55);
    lv_bar_set_range(bar_players, 0, 60);
    lv_bar_set_value(bar_players, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_players, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(bar_players, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_players, 10, 0);
    lv_obj_set_style_radius(bar_players, 10, LV_PART_INDICATOR);

    // Restart countdown label (below progress bar)
    lbl_restart = lv_label_create(main_card);
    lv_label_set_text(lbl_restart, "");
    lv_obj_set_style_text_font(lbl_restart, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_restart, lv_color_hex(0xFF6B6B), 0);  // Soft red
    lv_obj_align(lbl_restart, LV_ALIGN_CENTER, 0, 95);

    // Status line container
    lv_obj_t *status_cont = lv_obj_create(main_card);
    lv_obj_set_size(status_cont, 680, 50);
    lv_obj_align(status_cont, LV_ALIGN_BOTTOM_MID, 0, 10);
    lv_obj_set_style_bg_opa(status_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_cont, 0, 0);
    lv_obj_clear_flag(status_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Status indicator
    lbl_status = lv_label_create(status_cont);
    lv_label_set_text(lbl_status, "CONNECTING...");
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xFFA500), 0);
    lv_obj_align(lbl_status, LV_ALIGN_LEFT_MID, 0, 0);

    // Last update time
    lbl_update = lv_label_create(status_cont);
    lv_label_set_text(lbl_update, "");
    lv_obj_set_style_text_font(lbl_update, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_update, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_update, LV_ALIGN_CENTER, 0, 0);

    // IP address
    lbl_ip = lv_label_create(status_cont);
    lv_label_set_text(lbl_ip, "");
    lv_obj_set_style_text_font(lbl_ip, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_ip, lv_color_hex(0x666666), 0);
    lv_obj_align(lbl_ip, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void create_settings_screen(void) {
    screen_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_settings, DARK_BG, 0);

    create_back_button(screen_settings);

    // Title
    lv_obj_t *title = lv_label_create(screen_settings);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Settings options container (scrollable)
    lv_obj_t *cont = lv_obj_create(screen_settings);
    lv_obj_set_size(cont, 700, 400);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(cont, CARD_BG, 0);
    lv_obj_set_style_radius(cont, 15, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(cont, 15, 0);
    lv_obj_set_style_pad_row(cont, 10, 0);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // WiFi Settings button
    lv_obj_t *btn_wifi = lv_btn_create(cont);
    lv_obj_set_size(btn_wifi, 660, 60);
    lv_obj_set_style_bg_color(btn_wifi, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_radius(btn_wifi, 10, 0);
    lv_obj_add_event_cb(btn_wifi, on_wifi_settings_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_wifi = lv_label_create(btn_wifi);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI "  WiFi Settings");
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_wifi);

    // Server Settings button
    lv_obj_t *btn_server = lv_btn_create(cont);
    lv_obj_set_size(btn_server, 660, 60);
    lv_obj_set_style_bg_color(btn_server, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_radius(btn_server, 10, 0);
    lv_obj_add_event_cb(btn_server, on_server_settings_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_server_btn = lv_label_create(btn_server);
    lv_label_set_text(lbl_server_btn, LV_SYMBOL_LIST "  Server Settings");
    lv_obj_set_style_text_font(lbl_server_btn, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_server_btn);

    // Refresh interval row
    lv_obj_t *refresh_cont = lv_obj_create(cont);
    lv_obj_set_size(refresh_cont, 660, 55);
    lv_obj_set_style_bg_opa(refresh_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(refresh_cont, 0, 0);
    lv_obj_clear_flag(refresh_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_refresh_title = lv_label_create(refresh_cont);
    lv_label_set_text(lbl_refresh_title, "Refresh:");
    lv_obj_set_style_text_font(lbl_refresh_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_refresh_title, lv_color_white(), 0);
    lv_obj_align(lbl_refresh_title, LV_ALIGN_LEFT_MID, 0, 0);

    slider_refresh = lv_slider_create(refresh_cont);
    lv_obj_set_size(slider_refresh, 350, 20);
    lv_obj_align(slider_refresh, LV_ALIGN_CENTER, 30, 0);
    lv_slider_set_range(slider_refresh, 10, 300);
    lv_slider_set_value(slider_refresh, settings.refresh_interval_sec, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_refresh, on_refresh_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_refresh_val = lv_label_create(refresh_cont);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d sec", settings.refresh_interval_sec);
    lv_label_set_text(lbl_refresh_val, buf);
    lv_obj_set_style_text_font(lbl_refresh_val, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_refresh_val, DAYZ_GREEN, 0);
    lv_obj_align(lbl_refresh_val, LV_ALIGN_RIGHT_MID, 0, 0);

    // Alerts enabled row
    lv_obj_t *alerts_row = lv_obj_create(cont);
    lv_obj_set_size(alerts_row, 660, 45);
    lv_obj_set_style_bg_opa(alerts_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(alerts_row, 0, 0);
    lv_obj_clear_flag(alerts_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_alerts_title = lv_label_create(alerts_row);
    lv_label_set_text(lbl_alerts_title, "Alerts Enabled:");
    lv_obj_set_style_text_font(lbl_alerts_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_alerts_title, lv_color_white(), 0);
    lv_obj_align(lbl_alerts_title, LV_ALIGN_LEFT_MID, 0, 0);

    sw_alerts = lv_switch_create(alerts_row);
    lv_obj_align(sw_alerts, LV_ALIGN_RIGHT_MID, 0, 0);
    if (settings.server_count > 0 && settings.servers[settings.active_server_index].alerts_enabled) {
        lv_obj_add_state(sw_alerts, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_alerts, on_alerts_switch_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // Alert threshold row
    lv_obj_t *threshold_row = lv_obj_create(cont);
    lv_obj_set_size(threshold_row, 660, 55);
    lv_obj_set_style_bg_opa(threshold_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(threshold_row, 0, 0);
    lv_obj_clear_flag(threshold_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_threshold = lv_label_create(threshold_row);
    lv_label_set_text(lbl_threshold, "Threshold:");
    lv_obj_set_style_text_font(lbl_threshold, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_threshold, lv_color_white(), 0);
    lv_obj_align(lbl_threshold, LV_ALIGN_LEFT_MID, 0, 0);

    slider_alert = lv_slider_create(threshold_row);
    lv_obj_set_size(slider_alert, 350, 20);
    lv_obj_align(slider_alert, LV_ALIGN_CENTER, 30, 0);
    // Use server's max_players for range (default 60 if not set)
    int slider_max = (settings.server_count > 0 && settings.servers[settings.active_server_index].max_players > 0)
                     ? settings.servers[settings.active_server_index].max_players : 60;
    lv_slider_set_range(slider_alert, 1, slider_max);
    int threshold = (settings.server_count > 0) ? settings.servers[settings.active_server_index].alert_threshold : (slider_max / 2);
    if (threshold > slider_max) threshold = slider_max;  // Clamp to valid range
    lv_slider_set_value(slider_alert, threshold, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_alert, on_alert_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // Value label showing current threshold
    lbl_alert_val = lv_label_create(threshold_row);
    char alert_buf[32];
    snprintf(alert_buf, sizeof(alert_buf), "%d players", threshold);
    lv_label_set_text(lbl_alert_val, alert_buf);
    lv_obj_set_style_text_font(lbl_alert_val, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_alert_val, DAYZ_GREEN, 0);
    lv_obj_align(lbl_alert_val, LV_ALIGN_RIGHT_MID, 0, 0);

    // === RESTART SCHEDULE SECTION ===
    lv_obj_t *restart_title = lv_label_create(cont);
    lv_label_set_text(restart_title, "Restart Schedule (CET)");
    lv_obj_set_style_text_font(restart_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(restart_title, lv_color_hex(0xFF9800), 0);

    // Manual restart enable row
    lv_obj_t *restart_enable_row = lv_obj_create(cont);
    lv_obj_set_size(restart_enable_row, 660, 45);
    lv_obj_set_style_bg_opa(restart_enable_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(restart_enable_row, 0, 0);
    lv_obj_clear_flag(restart_enable_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_restart_enable = lv_label_create(restart_enable_row);
    lv_label_set_text(lbl_restart_enable, "Manual Schedule:");
    lv_obj_set_style_text_font(lbl_restart_enable, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_restart_enable, lv_color_white(), 0);
    lv_obj_align(lbl_restart_enable, LV_ALIGN_LEFT_MID, 0, 0);

    sw_restart_manual = lv_switch_create(restart_enable_row);
    lv_obj_align(sw_restart_manual, LV_ALIGN_RIGHT_MID, 0, 0);
    if (settings.server_count > 0 && settings.servers[settings.active_server_index].manual_restart_set) {
        lv_obj_add_state(sw_restart_manual, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_restart_manual, on_restart_manual_switch_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // Restart time row (Hour : Minute)
    lv_obj_t *restart_time_row = lv_obj_create(cont);
    lv_obj_set_size(restart_time_row, 660, 60);
    lv_obj_set_style_bg_opa(restart_time_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(restart_time_row, 0, 0);
    lv_obj_clear_flag(restart_time_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_time = lv_label_create(restart_time_row);
    lv_label_set_text(lbl_time, "Known restart:");
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
    lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 0, 0);

    // Hour roller (0-23)
    roller_restart_hour = lv_roller_create(restart_time_row);
    lv_roller_set_options(roller_restart_hour,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23",
        LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_restart_hour, 2);
    lv_obj_set_size(roller_restart_hour, 60, 50);
    lv_obj_align(roller_restart_hour, LV_ALIGN_CENTER, -20, 0);
    uint8_t srv_hour = (settings.server_count > 0) ? settings.servers[settings.active_server_index].restart_hour : 0;
    lv_roller_set_selected(roller_restart_hour, srv_hour, LV_ANIM_OFF);
    lv_obj_add_event_cb(roller_restart_hour, on_restart_hour_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_colon = lv_label_create(restart_time_row);
    lv_label_set_text(lbl_colon, ":");
    lv_obj_set_style_text_font(lbl_colon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_colon, lv_color_white(), 0);
    lv_obj_align(lbl_colon, LV_ALIGN_CENTER, 25, 0);

    // Minute roller (0-59 in 5-min increments)
    roller_restart_min = lv_roller_create(restart_time_row);
    lv_roller_set_options(roller_restart_min,
        "00\n05\n10\n15\n20\n25\n30\n35\n40\n45\n50\n55",
        LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_restart_min, 2);
    lv_obj_set_size(roller_restart_min, 60, 50);
    lv_obj_align(roller_restart_min, LV_ALIGN_CENTER, 70, 0);
    uint8_t srv_min = (settings.server_count > 0) ? settings.servers[settings.active_server_index].restart_minute : 0;
    lv_roller_set_selected(roller_restart_min, srv_min / 5, LV_ANIM_OFF);  // Convert to index
    lv_obj_add_event_cb(roller_restart_min, on_restart_min_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // Interval dropdown
    lv_obj_t *lbl_interval = lv_label_create(restart_time_row);
    lv_label_set_text(lbl_interval, "every");
    lv_obj_set_style_text_font(lbl_interval, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_interval, lv_color_white(), 0);
    lv_obj_align(lbl_interval, LV_ALIGN_CENTER, 130, 0);

    dropdown_restart_interval = lv_dropdown_create(restart_time_row);
    lv_dropdown_set_options(dropdown_restart_interval, "4h\n6h\n8h\n12h");
    lv_obj_set_size(dropdown_restart_interval, 70, 40);
    lv_obj_align(dropdown_restart_interval, LV_ALIGN_RIGHT_MID, 0, 0);
    // Set selected based on current interval
    uint8_t srv_interval = (settings.server_count > 0) ? settings.servers[settings.active_server_index].restart_interval_hours : 4;
    int interval_idx = 0;
    if (srv_interval == 6) interval_idx = 1;
    else if (srv_interval == 8) interval_idx = 2;
    else if (srv_interval == 12) interval_idx = 3;
    lv_dropdown_set_selected(dropdown_restart_interval, interval_idx);
    lv_obj_add_event_cb(dropdown_restart_interval, on_restart_interval_changed, LV_EVENT_VALUE_CHANGED, NULL);
}

static void create_wifi_settings_screen(void) {
    screen_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_wifi, DARK_BG, 0);

    create_back_button(screen_wifi);

    // Title
    lv_obj_t *title = lv_label_create(screen_wifi);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // SSID input
    lv_obj_t *lbl_ssid = lv_label_create(screen_wifi);
    lv_label_set_text(lbl_ssid, "WiFi Network (SSID):");
    lv_obj_set_style_text_font(lbl_ssid, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_ssid, lv_color_white(), 0);
    lv_obj_align(lbl_ssid, LV_ALIGN_TOP_LEFT, 50, 70);

    ta_ssid = lv_textarea_create(screen_wifi);
    lv_obj_set_size(ta_ssid, 400, 45);
    lv_obj_align(ta_ssid, LV_ALIGN_TOP_LEFT, 50, 95);
    lv_textarea_set_text(ta_ssid, settings.wifi_ssid);
    lv_textarea_set_one_line(ta_ssid, true);
    lv_textarea_set_placeholder_text(ta_ssid, "Enter SSID");
    lv_obj_add_event_cb(ta_ssid, on_textarea_clicked, LV_EVENT_CLICKED, NULL);

    // Password input
    lv_obj_t *lbl_pass = lv_label_create(screen_wifi);
    lv_label_set_text(lbl_pass, "Password:");
    lv_obj_set_style_text_font(lbl_pass, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_pass, lv_color_white(), 0);
    lv_obj_align(lbl_pass, LV_ALIGN_TOP_LEFT, 50, 150);

    ta_password = lv_textarea_create(screen_wifi);
    lv_obj_set_size(ta_password, 400, 45);
    lv_obj_align(ta_password, LV_ALIGN_TOP_LEFT, 50, 175);
    lv_textarea_set_text(ta_password, settings.wifi_password);
    lv_textarea_set_one_line(ta_password, true);
    lv_textarea_set_password_mode(ta_password, true);
    lv_textarea_set_placeholder_text(ta_password, "Enter password");
    lv_obj_add_event_cb(ta_password, on_textarea_clicked, LV_EVENT_CLICKED, NULL);

    // Save button
    lv_obj_t *btn_save = lv_btn_create(screen_wifi);
    lv_obj_set_size(btn_save, 150, 50);
    lv_obj_align(btn_save, LV_ALIGN_TOP_RIGHT, -50, 150);
    lv_obj_set_style_bg_color(btn_save, DAYZ_GREEN, 0);
    lv_obj_set_style_radius(btn_save, 10, 0);
    lv_obj_add_event_cb(btn_save, on_wifi_save_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, LV_SYMBOL_OK " Save");
    lv_obj_set_style_text_font(lbl_save, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_save);

    // Keyboard
    kb = lv_keyboard_create(screen_wifi);
    lv_obj_set_size(kb, LCD_WIDTH, 220);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta_ssid);
    lv_obj_add_event_cb(kb, on_keyboard_event, LV_EVENT_ALL, NULL);
}

static void create_server_settings_screen(void) {
    screen_server = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_server, DARK_BG, 0);

    create_back_button(screen_server);

    // Title
    lv_obj_t *title = lv_label_create(screen_server);
    lv_label_set_text(title, "Server Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Server list
    lv_obj_t *list = lv_obj_create(screen_server);
    lv_obj_set_size(list, 700, 300);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(list, CARD_BG, 0);
    lv_obj_set_style_radius(list, 15, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list, 15, 0);
    lv_obj_set_style_pad_row(list, 10, 0);

    for (int i = 0; i < settings.server_count; i++) {
        lv_obj_t *item = lv_obj_create(list);
        lv_obj_set_size(item, 660, 50);
        lv_obj_set_style_bg_color(item, (i == settings.active_server_index) ? lv_color_hex(0x2196F3) : lv_color_hex(0x333333), 0);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, settings.servers[i].display_name);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);

        lv_obj_t *lbl_id = lv_label_create(item);
        char id_buf[48];
        snprintf(id_buf, sizeof(id_buf), "ID: %s", settings.servers[i].server_id);
        lv_label_set_text(lbl_id, id_buf);
        lv_obj_set_style_text_font(lbl_id, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_id, lv_color_hex(0xAAAAAA), 0);
        lv_obj_align(lbl_id, LV_ALIGN_RIGHT_MID, -10, 0);
    }

    // Bottom buttons
    lv_obj_t *btn_add = lv_btn_create(screen_server);
    lv_obj_set_size(btn_add, 200, 50);
    lv_obj_align(btn_add, LV_ALIGN_BOTTOM_LEFT, 50, -20);
    lv_obj_set_style_bg_color(btn_add, DAYZ_GREEN, 0);
    lv_obj_set_style_radius(btn_add, 10, 0);
    lv_obj_add_event_cb(btn_add, on_add_server_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_add = lv_label_create(btn_add);
    lv_label_set_text(lbl_add, LV_SYMBOL_PLUS " Add Server");
    lv_obj_set_style_text_font(lbl_add, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_add);

    if (settings.server_count > 1) {
        lv_obj_t *btn_del = lv_btn_create(screen_server);
        lv_obj_set_size(btn_del, 200, 50);
        lv_obj_align(btn_del, LV_ALIGN_BOTTOM_RIGHT, -50, -20);
        lv_obj_set_style_bg_color(btn_del, ALERT_RED, 0);
        lv_obj_set_style_radius(btn_del, 10, 0);
        lv_obj_add_event_cb(btn_del, on_delete_server_clicked, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl_del = lv_label_create(btn_del);
        lv_label_set_text(lbl_del, LV_SYMBOL_TRASH " Delete Active");
        lv_obj_set_style_text_font(lbl_del, &lv_font_montserrat_18, 0);
        lv_obj_center(lbl_del);
    }
}

static void create_add_server_screen(void) {
    screen_add_server = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_add_server, DARK_BG, 0);

    create_back_button(screen_add_server);

    // Title
    lv_obj_t *title = lv_label_create(screen_add_server);
    lv_label_set_text(title, "Add Server");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Server ID input
    lv_obj_t *lbl_id = lv_label_create(screen_add_server);
    lv_label_set_text(lbl_id, "BattleMetrics Server ID:");
    lv_obj_set_style_text_font(lbl_id, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_id, lv_color_white(), 0);
    lv_obj_align(lbl_id, LV_ALIGN_TOP_LEFT, 50, 70);

    ta_server_id = lv_textarea_create(screen_add_server);
    lv_obj_set_size(ta_server_id, 400, 45);
    lv_obj_align(ta_server_id, LV_ALIGN_TOP_LEFT, 50, 95);
    lv_textarea_set_text(ta_server_id, "");
    lv_textarea_set_one_line(ta_server_id, true);
    lv_textarea_set_placeholder_text(ta_server_id, "e.g., 29986583");
    lv_obj_add_event_cb(ta_server_id, on_textarea_clicked, LV_EVENT_CLICKED, NULL);

    // Server name input
    lv_obj_t *lbl_name = lv_label_create(screen_add_server);
    lv_label_set_text(lbl_name, "Display Name:");
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_name, lv_color_white(), 0);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 50, 150);

    ta_server_name = lv_textarea_create(screen_add_server);
    lv_obj_set_size(ta_server_name, 400, 45);
    lv_obj_align(ta_server_name, LV_ALIGN_TOP_LEFT, 50, 175);
    lv_textarea_set_text(ta_server_name, "");
    lv_textarea_set_one_line(ta_server_name, true);
    lv_textarea_set_placeholder_text(ta_server_name, "e.g., My DayZ Server");
    lv_obj_add_event_cb(ta_server_name, on_textarea_clicked, LV_EVENT_CLICKED, NULL);

    // Save button
    lv_obj_t *btn_save = lv_btn_create(screen_add_server);
    lv_obj_set_size(btn_save, 150, 50);
    lv_obj_align(btn_save, LV_ALIGN_TOP_RIGHT, -50, 150);
    lv_obj_set_style_bg_color(btn_save, DAYZ_GREEN, 0);
    lv_obj_set_style_radius(btn_save, 10, 0);
    lv_obj_add_event_cb(btn_save, on_server_save_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, LV_SYMBOL_OK " Add");
    lv_obj_set_style_text_font(lbl_save, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_save);

    // Keyboard (reuse from wifi screen concept)
    lv_obj_t *kb_add = lv_keyboard_create(screen_add_server);
    lv_obj_set_size(kb_add, LCD_WIDTH, 220);
    lv_obj_align(kb_add, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb_add, ta_server_id);
}

// Get number of data points for current time range
static int get_points_for_range(history_range_t range) {
    switch (range) {
        case HISTORY_RANGE_1H:   return 60;    // 1 hour
        case HISTORY_RANGE_8H:   return 480;   // 8 hours
        case HISTORY_RANGE_24H:  return 1440;  // 24 hours
        case HISTORY_RANGE_WEEK: return 10080; // 7 days
        default:                 return 60;
    }
}

// Get range label text
static const char* get_range_label(history_range_t range) {
    switch (range) {
        case HISTORY_RANGE_1H:   return "Last 1 Hour";
        case HISTORY_RANGE_8H:   return "Last 8 Hours";
        case HISTORY_RANGE_24H:  return "Last 24 Hours";
        case HISTORY_RANGE_WEEK: return "Last 7 Days";
        default:                 return "Last 1 Hour";
    }
}

// Refresh history chart with current range
static void refresh_history_chart(void) {
    if (!chart_history || !chart_series) return;

    int target_points = get_points_for_range(current_history_range);

    // Limit chart point count to reasonable display (60-120 points max on screen)
    int display_points = (target_points > 120) ? 120 : target_points;
    int sample_rate = (target_points > 120) ? (target_points / 120) : 1;

    lv_chart_set_point_count(chart_history, display_points);
    lv_chart_set_all_value(chart_history, chart_series, LV_CHART_POINT_NONE);

    int available = (history_count < target_points) ? history_count : target_points;
    int start_idx = (history_count > target_points) ? (history_count - target_points) : 0;

    int chart_idx = 0;
    for (int i = 0; i < available && chart_idx < display_points; i += sample_rate) {
        history_entry_t entry;
        if (get_history_entry(start_idx + i, &entry) == 0 && entry.player_count >= 0) {
            lv_chart_set_value_by_id(chart_history, chart_series, chart_idx++, entry.player_count);
        }
    }

    lv_chart_refresh(chart_history);

    // Update legend
    if (lbl_history_legend) {
        char legend_text[64];
        snprintf(legend_text, sizeof(legend_text), "%s (%d readings)", get_range_label(current_history_range), available);
        lv_label_set_text(lbl_history_legend, legend_text);
    }
}

// Time range button callback
static void on_history_range_clicked(lv_event_t *e) {
    history_range_t range = (history_range_t)(intptr_t)lv_event_get_user_data(e);
    current_history_range = range;

    // Update button styles - highlight selected
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *parent = lv_obj_get_parent(btn);

    // Reset all buttons in container
    uint32_t child_cnt = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        lv_obj_set_style_bg_color(child, CARD_BG, 0);
    }

    // Highlight selected button
    lv_obj_set_style_bg_color(btn, DAYZ_GREEN, 0);

    refresh_history_chart();
}

static void create_history_screen(void) {
    screen_history = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_history, DARK_BG, 0);

    create_back_button(screen_history);

    // Title
    lv_obj_t *title = lv_label_create(screen_history);
    lv_label_set_text(title, "Player History");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Time range button container
    lv_obj_t *btn_cont = lv_obj_create(screen_history);
    lv_obj_set_size(btn_cont, 500, 45);
    lv_obj_align(btn_cont, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Time range buttons
    const char *btn_labels[] = {"1 Hour", "8 Hours", "24 Hours", "1 Week"};
    history_range_t ranges[] = {HISTORY_RANGE_1H, HISTORY_RANGE_8H, HISTORY_RANGE_24H, HISTORY_RANGE_WEEK};

    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(btn_cont);
        lv_obj_set_size(btn, 110, 35);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_bg_color(btn, (i == current_history_range) ? DAYZ_GREEN : CARD_BG, 0);
        lv_obj_add_event_cb(btn, on_history_range_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)ranges[i]);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, btn_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);
    }

    // Chart
    chart_history = lv_chart_create(screen_history);
    lv_obj_set_size(chart_history, 700, 310);
    lv_obj_align(chart_history, LV_ALIGN_CENTER, 0, 35);
    lv_obj_set_style_bg_color(chart_history, CARD_BG, 0);
    lv_obj_set_style_radius(chart_history, 15, 0);
    lv_obj_set_style_border_width(chart_history, 0, 0);
    lv_obj_set_style_line_color(chart_history, lv_color_hex(0x444444), LV_PART_MAIN);

    lv_chart_set_type(chart_history, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart_history, LV_CHART_AXIS_PRIMARY_Y, 0, 70);
    lv_chart_set_div_line_count(chart_history, 5, 5);

    chart_series = lv_chart_add_series(chart_history, DAYZ_GREEN, LV_CHART_AXIS_PRIMARY_Y);

    // Legend
    lbl_history_legend = lv_label_create(screen_history);
    lv_obj_set_style_text_font(lbl_history_legend, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_history_legend, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_history_legend, LV_ALIGN_BOTTOM_MID, 0, -10);

    // Populate chart using refresh function
    refresh_history_chart();
}

// ============== SCREEN NAVIGATION ==============

static void switch_to_screen(screen_id_t screen) {
    if (!lvgl_port_lock(100)) return;

    // Delete old screens to free memory (except main)
    if (screen != SCREEN_SETTINGS && screen_settings) {
        lv_obj_delete(screen_settings);
        screen_settings = NULL;
    }
    if (screen != SCREEN_WIFI_SETTINGS && screen_wifi) {
        lv_obj_delete(screen_wifi);
        screen_wifi = NULL;
    }
    if (screen != SCREEN_SERVER_SETTINGS && screen_server) {
        lv_obj_delete(screen_server);
        screen_server = NULL;
    }
    if (screen != SCREEN_ADD_SERVER && screen_add_server) {
        lv_obj_delete(screen_add_server);
        screen_add_server = NULL;
    }
    if (screen != SCREEN_HISTORY && screen_history) {
        lv_obj_delete(screen_history);
        screen_history = NULL;
    }

    current_screen = screen;

    switch (screen) {
        case SCREEN_MAIN:
            if (!screen_main) create_main_screen();
            lv_screen_load(screen_main);
            update_ui();
            break;

        case SCREEN_SETTINGS:
            create_settings_screen();
            lv_screen_load(screen_settings);
            break;

        case SCREEN_WIFI_SETTINGS:
            create_wifi_settings_screen();
            lv_screen_load(screen_wifi);
            break;

        case SCREEN_SERVER_SETTINGS:
            create_server_settings_screen();
            lv_screen_load(screen_server);
            break;

        case SCREEN_ADD_SERVER:
            create_add_server_screen();
            lv_screen_load(screen_add_server);
            break;

        case SCREEN_HISTORY:
            create_history_screen();
            lv_screen_load(screen_history);
            break;

        default:
            break;
    }

    lvgl_port_unlock();
}

// ============== UPDATE UI ==============

static void update_ui(void) {
    if (current_screen != SCREEN_MAIN) return;
    if (!lvgl_port_lock(100)) return;

    server_config_t *srv = (settings.server_count > 0) ?
                           &settings.servers[settings.active_server_index] : NULL;

    // Update server name
    if (srv) {
        lv_label_set_text(lbl_server, srv->display_name);

        // Update IP
        if (strlen(srv->ip_address) > 0) {
            char ip_buf[48];
            snprintf(ip_buf, sizeof(ip_buf), "%s:%d", srv->ip_address, srv->port);
            lv_label_set_text(lbl_ip, ip_buf);
        } else {
            lv_label_set_text(lbl_ip, "");
        }
    }

    // Update server in-game time with visual day/night indicator
    if (strlen(server_time) > 0) {
        char time_buf[32];
        snprintf(time_buf, sizeof(time_buf), "%s %s",
                 server_time, is_daytime ? "DAY" : "NIGHT");
        lv_label_set_text(lbl_server_time, time_buf);

        // Show and color the visual indicator
        lv_obj_clear_flag(day_night_indicator, LV_OBJ_FLAG_HIDDEN);
        if (is_daytime) {
            // Bright yellow/orange for day (sun)
            lv_obj_set_style_bg_color(day_night_indicator, lv_color_hex(0xFFD700), 0);
            lv_obj_set_style_text_color(lbl_server_time, lv_color_hex(0xFFD700), 0);
        } else {
            // Blue/purple for night (moon)
            lv_obj_set_style_bg_color(day_night_indicator, lv_color_hex(0x4169E1), 0);
            lv_obj_set_style_text_color(lbl_server_time, lv_color_hex(0x6495ED), 0);
        }
    } else {
        lv_label_set_text(lbl_server_time, "");
        lv_obj_add_flag(day_night_indicator, LV_OBJ_FLAG_HIDDEN);
    }

    // Update player count
    if (current_players >= 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", current_players);
        lv_label_set_text(lbl_players, buf);

        snprintf(buf, sizeof(buf), "/%d", max_players);
        lv_label_set_text(lbl_max, buf);

        // Update progress bar
        lv_bar_set_range(bar_players, 0, max_players);
        lv_bar_set_value(bar_players, current_players, LV_ANIM_ON);

        // Change bar color based on player count
        float ratio = (float)current_players / max_players;
        if (ratio >= 1.0) {
            lv_obj_set_style_bg_color(bar_players, ALERT_RED, LV_PART_INDICATOR);
        } else if (ratio > 0.7) {
            lv_obj_set_style_bg_color(bar_players, lv_color_hex(0xF44336), LV_PART_INDICATOR);
        } else if (ratio > 0.4) {
            lv_obj_set_style_bg_color(bar_players, lv_color_hex(0xFFEB3B), LV_PART_INDICATOR);
        } else {
            lv_obj_set_style_bg_color(bar_players, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
        }
    } else {
        lv_label_set_text(lbl_players, "---");
    }

    // Update status
    if (wifi_connected) {
        lv_label_set_text(lbl_status, "ONLINE");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x4CAF50), 0);
    } else {
        lv_label_set_text(lbl_status, "OFFLINE");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xF44336), 0);
    }

    // Update timestamp
    if (strcmp(last_update, "Never") != 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Updated: %s", last_update);
        lv_label_set_text(lbl_update, buf);
    }

    // Update restart countdown
    if (srv && lbl_restart) {
        restart_history_t *rh = &srv->restart_history;
        int countdown = get_restart_countdown(srv);

        if (countdown >= 0) {
            // We have a prediction - show source (manual vs auto)
            char countdown_buf[64];
            char time_str[16];
            format_countdown(countdown, time_str, sizeof(time_str));
            const char *mode = srv->manual_restart_set ? "" : " (auto)";
            snprintf(countdown_buf, sizeof(countdown_buf), "Restart: %s%s", time_str, mode);
            lv_label_set_text(lbl_restart, countdown_buf);

            // Color based on urgency
            if (countdown == 0) {
                lv_obj_set_style_text_color(lbl_restart, lv_color_hex(0xFF4444), 0);  // Bright red
            } else if (countdown < 1800) {  // Less than 30 min
                lv_obj_set_style_text_color(lbl_restart, lv_color_hex(0xFFA500), 0);  // Orange
            } else {
                lv_obj_set_style_text_color(lbl_restart, lv_color_hex(0x66BB6A), 0);  // Green
            }
        } else if (rh->restart_count == 1) {
            // One restart detected, need one more
            lv_label_set_text(lbl_restart, "Restart: 1 detected, learning...");
            lv_obj_set_style_text_color(lbl_restart, lv_color_hex(0xFFEB3B), 0);  // Yellow
        } else if (!srv->manual_restart_set) {
            // No restarts detected yet, no manual schedule
            lv_label_set_text(lbl_restart, "Restart: Set schedule in Settings");
            lv_obj_set_style_text_color(lbl_restart, lv_color_hex(0x888888), 0);  // Gray
        } else {
            lv_label_set_text(lbl_restart, "");
        }
    }

    // Show/hide server navigation buttons based on server count
    if (settings.server_count <= 1) {
        lv_obj_add_flag(btn_prev_server, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btn_next_server, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(btn_prev_server, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btn_next_server, LV_OBJ_FLAG_HIDDEN);
    }

    lvgl_port_unlock();
}

// ============== TOUCH INIT ==============

static esp_err_t touch_init(void) {
    ESP_LOGI(TAG, "Initializing touch controller (GT911)...");

    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_I2C_SDA,
        .scl_io_num = TOUCH_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_I2C_NUM, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_I2C_NUM, I2C_MODE_MASTER, 0, 0, 0));

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)TOUCH_I2C_NUM, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_WIDTH,
        .y_max = LCD_HEIGHT,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle));
    ESP_LOGI(TAG, "Touch controller initialized successfully");

    return ESP_OK;
}

// ============== LCD INIT ==============

static void lcd_init(void) {
    ESP_LOGI(TAG, "Initializing LCD with LVGL...");

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .timings = {
            .pclk_hz = 16000000,
            .h_res = LCD_WIDTH,
            .v_res = LCD_HEIGHT,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 16,
            .vsync_front_porch = 16,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,
        .bounce_buffer_size_px = 10 * LCD_WIDTH,
        .psram_trans_align = 64,
        .hsync_gpio_num = PIN_LCD_HSYNC,
        .vsync_gpio_num = PIN_LCD_VSYNC,
        .de_gpio_num = PIN_LCD_DE,
        .pclk_gpio_num = PIN_LCD_PCLK,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            PIN_LCD_B0, PIN_LCD_B1, PIN_LCD_B2, PIN_LCD_B3, PIN_LCD_B4,
            PIN_LCD_G0, PIN_LCD_G1, PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5,
            PIN_LCD_R0, PIN_LCD_R1, PIN_LCD_R2, PIN_LCD_R3, PIN_LCD_R4,
        },
        .flags.fb_in_psram = 1,
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 8192,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel_handle,
        .buffer_size = LCD_WIDTH * LCD_HEIGHT,
        .double_buffer = false,
        .hres = LCD_WIDTH,
        .vres = LCD_HEIGHT,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = false,
            .swap_bytes = false,
            .direct_mode = true,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };

    lvgl_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);

    ESP_ERROR_CHECK(touch_init());

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = touch_handle,
    };
    touch_indev = lvgl_port_add_touch(&touch_cfg);

    ESP_LOGI(TAG, "LCD with LVGL and touch initialized successfully");
}

// ============== MAIN ==============

void app_main(void) {
    ESP_LOGI(TAG, "DayZ Server Tracker Starting...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Load settings from NVS
    load_settings();

    // Initialize buzzer
    buzzer_init();

    // Test beep on startup to verify buzzer connection
    ESP_LOGI(TAG, "Testing buzzer...");
    buzzer_beep(200, 1000);  // 200ms beep at 1kHz
    vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_beep(200, 1500);  // Second beep at 1.5kHz
    ESP_LOGI(TAG, "Buzzer test complete");

    // Initialize history buffer
    init_history();

    // Initialize SD card and load saved history
    if (init_sd_card() == ESP_OK) {
        load_history_from_sd();
    }

    // If no history loaded from SD, try NVS backup
    if (history_count == 0) {
        ESP_LOGI(TAG, "No SD history, trying NVS backup...");
        load_history_from_nvs();
    }

    // Initialize LCD with LVGL
    lcd_init();

    // Create main screen
    if (lvgl_port_lock(1000)) {
        create_main_screen();
        lv_screen_load(screen_main);
        lvgl_port_unlock();
    }

    // Check if first boot (no WiFi configured)
    if (settings.first_boot || strlen(settings.wifi_ssid) == 0) {
        ESP_LOGI(TAG, "First boot detected, showing settings");
        // Set default credentials for backward compatibility
        strcpy(settings.wifi_ssid, "meshnetwork2131");
        strcpy(settings.wifi_password, "9696Polikut.");
        settings.first_boot = false;
        save_settings();
    }

    // Initialize WiFi
    wifi_init();

    // Wait for WiFi connection (with timeout for UI responsiveness)
    ESP_LOGI(TAG, "Waiting for WiFi...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected!");
    } else {
        ESP_LOGW(TAG, "WiFi connection timeout, will retry in background");
    }

    // Update UI
    update_ui();

    // Main loop
    while (1) {
        // Only query if on main screen
        if (current_screen == SCREEN_MAIN) {
            query_server_status();
            update_ui();
        }

        // Wait for refresh interval, checking for manual refresh requests
        int wait_cycles = (settings.refresh_interval_sec * 1000) / 100;
        for (int i = 0; i < wait_cycles; i++) {
            if (refresh_requested) {
                refresh_requested = false;
                ESP_LOGI(TAG, "Manual refresh triggered");
                break;
            }

            // Auto-hide alerts after 10 seconds
            if (alert_active) {
                int64_t now = esp_timer_get_time() / 1000;
                if (now - alert_start_time > 10000) {
                    hide_alert();
                }
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
