/**
 * DayZ Server Tracker - Configuration
 * All constants, pin definitions, and compile-time settings
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============== APPLICATION CONSTANTS ==============
#define APP_NAME            "DayZ Server Monitor"
#define APP_VERSION         "2.0.0"

#define MAX_SERVERS         5
#define MAX_HISTORY_ENTRIES 10080   // 7 days at 1 min intervals
#define NVS_NAMESPACE       "dayz_tracker"

// ============== DISPLAY SETTINGS ==============
// Waveshare ESP32-S3-Touch-LCD-7 (800x480)
#define LCD_WIDTH           800
#define LCD_HEIGHT          480

// LCD Pin Configuration
#define PIN_LCD_VSYNC       3
#define PIN_LCD_HSYNC       46
#define PIN_LCD_DE          5
#define PIN_LCD_PCLK        7
#define PIN_LCD_BL          -1

// LCD Data Pins - Blue
#define PIN_LCD_B0          14
#define PIN_LCD_B1          38
#define PIN_LCD_B2          18
#define PIN_LCD_B3          17
#define PIN_LCD_B4          10

// LCD Data Pins - Green
#define PIN_LCD_G0          39
#define PIN_LCD_G1          0
#define PIN_LCD_G2          45
#define PIN_LCD_G3          48
#define PIN_LCD_G4          47
#define PIN_LCD_G5          21

// LCD Data Pins - Red
#define PIN_LCD_R0          1
#define PIN_LCD_R1          2
#define PIN_LCD_R2          42
#define PIN_LCD_R3          41
#define PIN_LCD_R4          40

// ============== TOUCH CONFIGURATION ==============
// GT911 I2C Touch Controller
#define TOUCH_I2C_SDA       GPIO_NUM_8
#define TOUCH_I2C_SCL       GPIO_NUM_9
#define TOUCH_I2C_NUM       I2C_NUM_0

// ============== SD CARD CONFIGURATION ==============
// SPI Mode SD Card
#define SD_MOSI             GPIO_NUM_11
#define SD_MISO             GPIO_NUM_13
#define SD_CLK              GPIO_NUM_12
// SD_CS is controlled via CH422G IO expander EXIO4

// CH422G IO Expander (controls SD_CS and GT911 reset)
// CH422G address range is 0x20-0x27, device specific
#define CH422G_I2C_ADDR     0x24
#define CH422G_REG_OUT      0x01
#define CH422G_EXIO1_BIT    (1 << 1)    // GT911 touch reset pin (TP_RST)
#define CH422G_EXIO2_BIT    (1 << 2)    // LCD backlight control
#define CH422G_EXIO4_BIT    (1 << 4)    // SD_CS pin

// ============== BUZZER CONFIGURATION ==============
#define BUZZER_PIN          GPIO_NUM_6  // SENSOR AD pin on Waveshare board
#define BUZZER_ENABLED      true        // Set to false if no buzzer connected

// ============== RESTART DETECTION ==============
#define RESTART_DETECT_MIN_PLAYERS  5       // Server must have had at least this many players
#define RESTART_DETECT_DROP_TO      0       // Player count drops to this (0 = server restart)
#define MIN_RESTART_INTERVAL_SEC    1800    // Minimum 30 min between detected restarts
#define MAX_RESTART_HISTORY         10      // Track last 10 restarts per server

// ============== HISTORY STORAGE ==============
#define HISTORY_FILE_MAGIC      0xDA120001
#define HISTORY_FILE_PATH       "/sdcard/history.bin"
#define NVS_HISTORY_MAX         200
#define NVS_HISTORY_KEY         "hist_data"
#define NVS_HISTORY_META        "hist_meta"
#define HISTORY_SAVE_INTERVAL   10          // Save every 10 new entries

// ============== NETWORK ==============
#define HTTP_RESPONSE_BUFFER_SIZE   16384
#define HTTP_TIMEOUT_MS             10000
#define WIFI_CONNECT_TIMEOUT_MS     10000

// BattleMetrics API
#define BATTLEMETRICS_API_BASE      "https://api.battlemetrics.com/servers/"

// ============== UI TIMING ==============
#define ALERT_AUTO_HIDE_MS      10000
#define UI_LOCK_TIMEOUT_MS      100
#define LVGL_TASK_PRIORITY      4
#define LVGL_TASK_STACK         8192

// ============== DEFAULT VALUES ==============
#define DEFAULT_REFRESH_INTERVAL_SEC    30
#define DEFAULT_MAX_PLAYERS             60
#define DEFAULT_ALERT_THRESHOLD         50
#define MIN_REFRESH_INTERVAL_SEC        10
#define MAX_REFRESH_INTERVAL_SEC        300
#define DEFAULT_SCREENSAVER_TIMEOUT_SEC 300     // 5 minutes (0 = disabled)

// Default server for first boot
#define DEFAULT_SERVER_ID       "29986583"
#define DEFAULT_SERVER_NAME     "3833 | EUROPE - DE"
#define DEFAULT_SERVER_IP       "5.62.99.20"
#define DEFAULT_SERVER_PORT     11400

#endif // CONFIG_H
