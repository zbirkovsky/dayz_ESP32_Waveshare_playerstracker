/*
 * DayZ Server Player Tracker
 * For Waveshare ESP32-S3-Touch-LCD-7 (800x480)
 *
 * Monitors player count on DayZ server: 3833 | EUROPE - DE
 * IP: 5.62.99.20 Port: 11400
 *
 * Uses LVGL for smooth, anti-aliased fonts
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// LVGL includes
#include "lvgl.h"
#include "esp_lvgl_port.h"

// ============== CONFIGURE YOUR WIFI HERE ==============
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"
// ======================================================

// DayZ Server Info
#define DAYZ_SERVER_NAME "3833 | EUROPE - DE"
#define DAYZ_SERVER_IP   "5.62.99.20"
#define DAYZ_SERVER_PORT 11400
#define BATTLEMETRICS_SERVER_ID "29986583"

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

static const char *TAG = "DayZ_Tracker";

// WiFi event group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Display handle
static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_display_t *lvgl_disp = NULL;

// LVGL UI elements
static lv_obj_t *lbl_title = NULL;
static lv_obj_t *lbl_server = NULL;
static lv_obj_t *lbl_players = NULL;
static lv_obj_t *lbl_max = NULL;
static lv_obj_t *bar_players = NULL;
static lv_obj_t *lbl_status = NULL;
static lv_obj_t *lbl_update = NULL;
static lv_obj_t *lbl_ip = NULL;

// Server status
static int current_players = -1;
static int max_players = 60;
static char last_update[32] = "Never";
static bool wifi_connected = false;

// Custom colors
#define DAYZ_GREEN   lv_color_hex(0x7BC144)
#define DARK_BG      lv_color_hex(0x1A1A2E)
#define CARD_BG      lv_color_hex(0x16213E)

// Forward declarations
static void update_ui(void);

// WiFi event handler
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
    }
}

// Initialize WiFi
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

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init complete, connecting to %s", WIFI_SSID);
}

// HTTP response buffer
static char http_response[16384];
static int http_response_len = 0;

// HTTP event handler
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

// Parse player count from BattleMetrics JSON response
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

// Parse max players from response
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

// Query server status via BattleMetrics API
static void query_server_status(void) {
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected, skipping query");
        return;
    }

    http_response_len = 0;
    memset(http_response, 0, sizeof(http_response));

    char url[256];
    snprintf(url, sizeof(url),
        "https://api.battlemetrics.com/servers/%s",
        BATTLEMETRICS_SERVER_ID);

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

        if (players >= 0) {
            current_players = players;
            max_players = max_p > 0 ? max_p : 60;

            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);
            snprintf(last_update, sizeof(last_update), "%02d:%02d:%02d",
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

            ESP_LOGI(TAG, "Players: %d/%d", current_players, max_players);
        } else {
            ESP_LOGW(TAG, "Could not parse player count from response");
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

// Create the main UI
static void create_ui(void) {
    if (lvgl_port_lock(0)) {
        // Set dark background
        lv_obj_set_style_bg_color(lv_screen_active(), DARK_BG, 0);

        // Main container card
        lv_obj_t *card = lv_obj_create(lv_screen_active());
        lv_obj_set_size(card, 760, 440);
        lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(card, CARD_BG, 0);
        lv_obj_set_style_radius(card, 20, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, DAYZ_GREEN, 0);
        lv_obj_set_style_pad_all(card, 30, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        // Title - "DAYZ SERVER"
        lbl_title = lv_label_create(card);
        lv_label_set_text(lbl_title, "DAYZ SERVER");
        lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(lbl_title, DAYZ_GREEN, 0);
        lv_obj_set_style_text_letter_space(lbl_title, 4, 0);
        lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 0);

        // Server name
        lbl_server = lv_label_create(card);
        lv_label_set_text(lbl_server, "3833 | EUROPE - DE");
        lv_obj_set_style_text_font(lbl_server, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(lbl_server, lv_color_white(), 0);
        lv_obj_align(lbl_server, LV_ALIGN_TOP_MID, 0, 65);

        // Players container
        lv_obj_t *players_cont = lv_obj_create(card);
        lv_obj_set_size(players_cont, 400, 120);
        lv_obj_align(players_cont, LV_ALIGN_CENTER, 0, -20);
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
        bar_players = lv_bar_create(card);
        lv_obj_set_size(bar_players, 680, 35);
        lv_obj_align(bar_players, LV_ALIGN_CENTER, 0, 70);
        lv_bar_set_range(bar_players, 0, 60);
        lv_bar_set_value(bar_players, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar_players, lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_color(bar_players, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar_players, 10, 0);
        lv_obj_set_style_radius(bar_players, 10, LV_PART_INDICATOR);

        // Status line container
        lv_obj_t *status_cont = lv_obj_create(card);
        lv_obj_set_size(status_cont, 680, 50);
        lv_obj_align(status_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
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
        lv_label_set_text(lbl_ip, "5.62.99.20:11400");
        lv_obj_set_style_text_font(lbl_ip, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl_ip, lv_color_hex(0x666666), 0);
        lv_obj_align(lbl_ip, LV_ALIGN_RIGHT_MID, 0, 0);

        lvgl_port_unlock();
    }
}

// Update UI with current values
static void update_ui(void) {
    if (lvgl_port_lock(0)) {
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
            if (ratio > 0.7) {
                lv_obj_set_style_bg_color(bar_players, lv_color_hex(0xF44336), LV_PART_INDICATOR);
            } else if (ratio > 0.4) {
                lv_obj_set_style_bg_color(bar_players, lv_color_hex(0xFFEB3B), LV_PART_INDICATOR);
            } else {
                lv_obj_set_style_bg_color(bar_players, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
            }
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

        lvgl_port_unlock();
    }
}

// Initialize LCD display with LVGL
static void lcd_init(void) {
    ESP_LOGI(TAG, "Initializing LCD with LVGL...");

    // RGB LCD panel configuration - with bounce buffers for stable PSRAM access
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
        .num_fbs = 2,  // Double buffer for tear-free
        .bounce_buffer_size_px = 10 * LCD_WIDTH,  // CRITICAL: bounce buffer for PSRAM
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

    // Initialize LVGL port
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 6144,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // Add RGB display to LVGL - use RGB-specific function
    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel_handle,
        .buffer_size = LCD_WIDTH * LCD_HEIGHT,  // Full screen buffer for direct mode
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
            .direct_mode = true,  // Direct mode for RGB LCD
        },
    };

    // RGB-specific config with bounce buffer mode
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,       // Enable bounce buffer mode
            .avoid_tearing = true, // Avoid screen tearing
        },
    };

    lvgl_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);

    ESP_LOGI(TAG, "LCD with LVGL initialized successfully");
}

// Main app
void app_main(void) {
    ESP_LOGI(TAG, "DayZ Server Tracker Starting (LVGL)...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize LCD with LVGL
    lcd_init();

    // Create UI
    create_ui();

    // Initialize WiFi
    wifi_init();

    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected!");

    // Update UI to show online
    update_ui();

    // Main loop - query server every 30 seconds
    while (1) {
        query_server_status();
        update_ui();
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
