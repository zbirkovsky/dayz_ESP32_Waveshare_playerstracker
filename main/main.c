/*
 * DayZ Server Player Tracker
 * For Waveshare ESP32-S3-Touch-LCD-7 (800x480)
 * 
 * Monitors player count on DayZ server: 3833 | EUROPE - DE
 * IP: 5.62.99.20 Port: 11400
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

// Waveshare ESP32-S3-Touch-LCD-7 Pin Configuration (CORRECTED)
// Control pins
#define PIN_LCD_VSYNC  3
#define PIN_LCD_HSYNC  46
#define PIN_LCD_DE     5
#define PIN_LCD_PCLK   7
// Data pins: DATA0-4=Blue, DATA5-10=Green, DATA11-15=Red
#define PIN_LCD_B0     14   // DATA0
#define PIN_LCD_B1     38   // DATA1
#define PIN_LCD_B2     18   // DATA2
#define PIN_LCD_B3     17   // DATA3
#define PIN_LCD_B4     10   // DATA4
#define PIN_LCD_G0     39   // DATA5
#define PIN_LCD_G1     0    // DATA6
#define PIN_LCD_G2     45   // DATA7
#define PIN_LCD_G3     48   // DATA8
#define PIN_LCD_G4     47   // DATA9
#define PIN_LCD_G5     21   // DATA10
#define PIN_LCD_R0     1    // DATA11
#define PIN_LCD_R1     2    // DATA12
#define PIN_LCD_R2     42   // DATA13
#define PIN_LCD_R3     41   // DATA14
#define PIN_LCD_R4     40   // DATA15
#define PIN_LCD_BL     -1   // Backlight (not used or directly powered)

static const char *TAG = "DayZ_Tracker";

// WiFi event group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Display handle
static esp_lcd_panel_handle_t panel_handle = NULL;
static uint16_t *frame_buffer = NULL;

// Server status
static int current_players = -1;
static int max_players = 60;
static char last_update[32] = "Never";
static bool wifi_connected = false;

// Colors (RGB565)
#define COLOR_BLACK      0x0000
#define COLOR_WHITE      0xFFFF
#define COLOR_RED        0xF800
#define COLOR_GREEN      0x07E0
#define COLOR_BLUE       0x001F
#define COLOR_YELLOW     0xFFE0
#define COLOR_CYAN       0x07FF
#define COLOR_DARK_GRAY  0x4208
#define COLOR_DAYZ_GREEN 0x3DE0  // DayZ-ish green

// Simple 8x8 font for numbers and basic chars
static const uint8_t font8x8[][8] = {
    // 0-9
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, // 0
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // 1
    {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00}, // 2
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // 3
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00}, // 4
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // 5
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}, // 6
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, // 7
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // 8
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}, // 9
    // Special chars
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, // : (10)
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // - (11)
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // . (12)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // space (13)
    {0x3C,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, // A (14)
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, // B (15)
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}, // C (16)
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, // D (17)
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00}, // E (18)
    {0x06,0x06,0x36,0x7E,0x36,0x06,0x06,0x00}, // I (19) - actually looks like I
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, // H (20)
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00}, // I (21)
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, // P (22)
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, // L (23)
    {0x66,0x76,0x7E,0x6E,0x66,0x66,0x66,0x00}, // N (24)
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // O (25)
    {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00}, // R (26)
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}, // S (27)
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // T (28)
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // U (29)
    {0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x00}, // Y (30)
    {0x06,0x0C,0x18,0x30,0x60,0x40,0x00,0x00}, // / (31)
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00}, // F (32)
    {0x66,0x66,0x66,0x66,0x3C,0x3C,0x18,0x00}, // V (33)
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}, // Z (34)
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}, // M (35)
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // W (36)
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}, // X (37)
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}, // K (38)
    {0x1E,0x06,0x06,0x06,0x66,0x66,0x3C,0x00}, // J (39)
    {0x3C,0x66,0x66,0x66,0x6E,0x3C,0x0E,0x00}, // Q (40)
};

// Get font index for character
static int get_font_index(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c == ':') return 10;
    if (c == '-') return 11;
    if (c == '.') return 12;
    if (c == ' ') return 13;
    if (c == 'A' || c == 'a') return 14;
    if (c == 'B' || c == 'b') return 15;
    if (c == 'C' || c == 'c') return 16;
    if (c == 'D' || c == 'd') return 17;
    if (c == 'E' || c == 'e') return 18;
    if (c == 'G' || c == 'g') return 16; // use C
    if (c == 'H' || c == 'h') return 20;
    if (c == 'I' || c == 'i') return 21;
    if (c == 'P' || c == 'p') return 22;
    if (c == 'L' || c == 'l') return 23;
    if (c == 'N' || c == 'n') return 24;
    if (c == 'O' || c == 'o') return 25;
    if (c == 'R' || c == 'r') return 26;
    if (c == 'S' || c == 's') return 27;
    if (c == 'T' || c == 't') return 28;
    if (c == 'U' || c == 'u') return 29;
    if (c == 'Y' || c == 'y') return 30;
    if (c == '/') return 31;
    if (c == 'F' || c == 'f') return 32;
    if (c == 'V' || c == 'v') return 33;
    if (c == 'Z' || c == 'z') return 34;
    if (c == 'M' || c == 'm') return 35;
    if (c == 'W' || c == 'w') return 36;
    if (c == 'X' || c == 'x') return 37;
    if (c == 'K' || c == 'k') return 38;
    if (c == 'J' || c == 'j') return 39;
    if (c == 'Q' || c == 'q') return 40;
    return 13; // space for unknown
}

// Draw a single pixel
static void draw_pixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < LCD_WIDTH && y >= 0 && y < LCD_HEIGHT) {
        frame_buffer[y * LCD_WIDTH + x] = color;
    }
}

// Fill rectangle
static void fill_rect(int x, int y, int w, int h, uint16_t color) {
    for (int j = y; j < y + h && j < LCD_HEIGHT; j++) {
        for (int i = x; i < x + w && i < LCD_WIDTH; i++) {
            if (i >= 0 && j >= 0) {
                frame_buffer[j * LCD_WIDTH + i] = color;
            }
        }
    }
}

// Draw character at scale
static void draw_char_scaled(int x, int y, char c, uint16_t color, int scale) {
    int idx = get_font_index(c);
    if (idx < 0 || idx >= sizeof(font8x8)/8) return;
    
    for (int row = 0; row < 8; row++) {
        uint8_t line = font8x8[idx][row];
        for (int col = 0; col < 8; col++) {
            if (line & (0x80 >> col)) {
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

// Draw string at scale
static void draw_string_scaled(int x, int y, const char *str, uint16_t color, int scale) {
    int offset = 0;
    while (*str) {
        draw_char_scaled(x + offset, y, *str, color, scale);
        offset += 8 * scale + scale; // char width + spacing
        str++;
    }
}

// Draw progress bar
static void draw_progress_bar(int x, int y, int w, int h, int value, int max_val) {
    // Border
    fill_rect(x, y, w, h, COLOR_WHITE);
    fill_rect(x + 2, y + 2, w - 4, h - 4, COLOR_DARK_GRAY);
    
    // Fill based on value
    if (max_val > 0 && value >= 0) {
        int fill_w = ((w - 8) * value) / max_val;
        uint16_t fill_color = COLOR_GREEN;
        if (value > max_val * 0.7) fill_color = COLOR_RED;
        else if (value > max_val * 0.4) fill_color = COLOR_YELLOW;
        fill_rect(x + 4, y + 4, fill_w, h - 8, fill_color);
    }
}

// Clear screen with gradient
static void clear_screen_gradient(void) {
    for (int y = 0; y < LCD_HEIGHT; y++) {
        // Dark gradient from top to bottom
        uint8_t r = (y * 20) / LCD_HEIGHT;
        uint8_t g = (y * 30) / LCD_HEIGHT;
        uint8_t b = (y * 20) / LCD_HEIGHT;
        uint16_t color = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
        for (int x = 0; x < LCD_WIDTH; x++) {
            frame_buffer[y * LCD_WIDTH + x] = color;
        }
    }
}

// Draw the main UI
static void draw_ui(void) {
    clear_screen_gradient();
    
    // Title - "DAYZ SERVER"
    draw_string_scaled(220, 30, "DAYZ SERVER", COLOR_DAYZ_GREEN, 5);
    
    // Server name
    draw_string_scaled(250, 120, "3833 EUROPE-DE", COLOR_WHITE, 3);
    
    // Player count - big numbers
    char player_str[32];
    if (current_players >= 0) {
        snprintf(player_str, sizeof(player_str), "%d", current_players);
    } else {
        snprintf(player_str, sizeof(player_str), "---");
    }
    
    // "PLAYERS" label
    draw_string_scaled(100, 200, "PLAYERS", COLOR_CYAN, 3);
    
    // Big player number
    int num_x = 350;
    if (current_players >= 10) num_x = 320;
    if (current_players >= 100) num_x = 290; // unlikely but just in case
    draw_string_scaled(num_x, 180, player_str, COLOR_WHITE, 10);
    
    // Max players
    char max_str[16];
    snprintf(max_str, sizeof(max_str), "/%d", max_players);
    draw_string_scaled(480, 220, max_str, COLOR_DARK_GRAY, 4);
    
    // Progress bar
    draw_progress_bar(100, 320, 600, 40, current_players, max_players);
    
    // Status line
    if (wifi_connected) {
        draw_string_scaled(100, 400, "ONLINE", COLOR_GREEN, 3);
    } else {
        draw_string_scaled(100, 400, "OFFLINE", COLOR_RED, 3);
    }
    
    // Last update
    draw_string_scaled(300, 400, last_update, COLOR_DARK_GRAY, 2);
    
    // IP address
    draw_string_scaled(100, 440, "5.62.99.20:11400", COLOR_DARK_GRAY, 2);
    
    // Refresh display
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_WIDTH, LCD_HEIGHT, frame_buffer);
}

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
static char http_response[16384];  // Increased for BattleMetrics API
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
    // Look for "players" field in JSON
    // BattleMetrics format: "players": X,
    const char *players_str = strstr(json, "\"players\":");
    if (players_str) {
        players_str += 10; // skip "players":
        while (*players_str == ' ') players_str++;
        return atoi(players_str);
    }
    
    // Alternative: look for "player_count"
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
    
    return 60; // default
}

// Query server status via BattleMetrics API
static void query_server_status(void) {
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected, skipping query");
        return;
    }
    
    http_response_len = 0;
    memset(http_response, 0, sizeof(http_response));
    
    // BattleMetrics API endpoint - direct server ID query
    char url[256];
    snprintf(url, sizeof(url),
        "https://api.battlemetrics.com/servers/%s",
        BATTLEMETRICS_SERVER_ID);
    
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,  // For HTTPS
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
        
        // Parse response
        int players = parse_player_count(http_response);
        int max_p = parse_max_players(http_response);
        
        if (players >= 0) {
            current_players = players;
            max_players = max_p > 0 ? max_p : 60;
            
            // Update timestamp
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

// Initialize LCD display
static void lcd_init(void) {
    ESP_LOGI(TAG, "Initializing LCD...");

    // Backlight (only if pin is defined)
#if PIN_LCD_BL >= 0
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_LCD_BL
    };
    gpio_config(&bk_gpio_config);
    gpio_set_level(PIN_LCD_BL, 1);
#endif
    
    // Allocate frame buffer in PSRAM
    frame_buffer = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!frame_buffer) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer!");
        return;
    }
    memset(frame_buffer, 0, LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t));
    
    // RGB LCD panel configuration
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 16000000,  // 16MHz - official Waveshare timing
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
        .num_fbs = 1,
        .bounce_buffer_size_px = 10 * LCD_WIDTH,  // Bounce buffer for stable PSRAM access
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
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    
    ESP_LOGI(TAG, "LCD initialized successfully");
}

// Main app
void app_main(void) {
    ESP_LOGI(TAG, "DayZ Server Tracker Starting...");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize LCD
    lcd_init();
    
    // Draw initial UI
    draw_ui();
    
    // Initialize WiFi
    wifi_init();
    
    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected!");
    
    // Main loop - query server every 30 seconds
    while (1) {
        query_server_status();
        draw_ui();
        vTaskDelay(pdMS_TO_TICKS(30000)); // 30 second refresh
    }
}
