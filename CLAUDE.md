# DayZ Server Tracker - Claude Code Instructions

ESP32-S3 based DayZ server player count monitor using Waveshare ESP32-S3-Touch-LCD-7.

## Build & Flash Commands

| Command | Description |
|---------|-------------|
| `/build` | Build firmware only |
| `/flash` | Flash to COM5 |
| `/build-flash` | Build and flash to COM5 |
| `/monitor` | Monitor serial output (60s) |

### Manual Build/Flash (if commands don't work)

```bash
powershell -ExecutionPolicy Bypass -File - << 'PSSCRIPT'
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5.1"
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\python_env\idf5.5_py3.11_env"
$env:PATH = "C:\Espressif\tools\idf-git\2.44.0\cmd;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;$env:PATH"
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Set-Location "C:\DayZ_servertracker"
& "C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe" "C:\Espressif\frameworks\esp-idf-v5.5.1\tools\idf.py" -p COM5 build flash
PSSCRIPT
```

**Always use timeout 600000** (10 minutes) for build operations.

---

## Hardware: Waveshare ESP32-S3-Touch-LCD-7

| Spec | Value |
|------|-------|
| Display | 7" TFT LCD, 800x480, RGB565 |
| MCU | ESP32-S3 dual-core 240MHz |
| PSRAM | 8MB Octal |
| Flash | 16MB |
| Display Controller | ST7262 (16-bit RGB parallel) |
| Touch | GT911 capacitive (I2C) |
| I/O Expander | CH422G (I2C addr 0x24/0x38) |

### GPIO Pin Mapping

```c
// LCD Control
#define PIN_LCD_VSYNC  3
#define PIN_LCD_HSYNC  46
#define PIN_LCD_DE     5
#define PIN_LCD_PCLK   7

// LCD Data (RGB565 - B5 G6 R5)
// Blue:  14, 38, 18, 17, 10
// Green: 39, 0, 45, 48, 47, 21
// Red:   1, 2, 42, 41, 40

// I2C (Touch + CH422G)
#define PIN_I2C_SDA    8
#define PIN_I2C_SCL    9

// SD Card (directly wired)
#define PIN_SD_CLK     12
#define PIN_SD_CMD     11
#define PIN_SD_D0      13

// Buzzer
#define PIN_BUZZER     6   // SENSOR AD header
```

### CH422G I/O Expander

Controls backlight and touch reset via I2C:

```c
// CH422G registers (address IS the register)
#define CH422G_MODE_REG    0x24
#define CH422G_OUTPUT_REG  0x38

// Output bits
#define EXIO0  (1 << 0)  // SD card (directly wired, not used)
#define EXIO1  (1 << 1)  // GT911 touch reset
#define EXIO2  (1 << 2)  // Backlight control
#define EXIO3  (1 << 3)  // Available

// Set output high: write to 0x38 with bit set
// Example: backlight on
i2c_master_write_to_device(i2c_num, 0x38, &value, 1, timeout);
```

---

## Critical Display Configuration

### PSRAM + Bounce Buffers (REQUIRED)

Without bounce buffers, display flickers during WiFi/Flash operations.

**sdkconfig.defaults:**
```ini
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_ESP32S3_DATA_CACHE_64KB=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y
CONFIG_LCD_RGB_RESTART_IN_VSYNC=y
```

**RGB Panel Config:**
```c
esp_lcd_rgb_panel_config_t panel_config = {
    .timings = {
        .pclk_hz = 16000000,  // 16MHz
        .h_res = 800,
        .v_res = 480,
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
    .bounce_buffer_size_px = 10 * 800,  // CRITICAL!
    .psram_trans_align = 64,
    .flags.fb_in_psram = 1,
};
```

---

## LVGL Notes

### Thread Safety - ALWAYS Lock

```c
if (lvgl_port_lock(50)) {
    // LVGL operations here
    lv_label_set_text(label, "Hello");
    lvgl_port_unlock();
}
```

### LVGL 9.x API Changes

- `lv_obj_delete()` not `lv_obj_del()`
- `lv_obj_remove_flag()` not `lv_obj_clear_flag()`
- `lv_screen_load()` not `lv_scr_load()`

---

## Project Structure

```
main/
├── config.h              # Constants, pins, timeouts
├── app_state.h/.c        # Thread-safe state (mutex protected)
├── events.h/.c           # Event queue system
├── app_init.c            # Initialization sequence
├── drivers/
│   ├── buzzer.c/h        # Buzzer PWM
│   ├── display.c/h       # LCD + Touch + LVGL init
│   ├── sd_card.c/h       # SD card + CH422G
│   └── io_expander.c/h   # CH422G driver
├── services/
│   ├── wifi_manager.c/h  # WiFi + SNTP
│   ├── battlemetrics.c/h # API client (cJSON)
│   ├── settings_store.c/h # NVS persistence
│   ├── history_store.c/h  # Player history
│   └── nvs_cache.c/h      # Cached NVS reads
├── ui/
│   ├── ui_context.h       # Widget pointers
│   ├── ui_styles.c/h      # Colors, fonts
│   ├── ui_update.c/h      # Update functions
│   ├── screen_builder.c/h # Screen creation
│   └── screen_screensaver.c/h
├── power/
│   └── screensaver.c/h    # Screensaver + backlight
└── main.c                 # Entry point, main loop
```

---

## Common Issues & Solutions

| Issue | Solution |
|-------|----------|
| MSys/Mingw error | Use PowerShell heredoc method (see above) |
| Display flickering | Enable bounce buffers in panel config |
| Display drift | Enable `CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y` |
| Touch not working | Check GT911 reset via CH422G EXIO1 |
| Build cache error | Delete `build/` directory |
| Implicit function decl | Add missing `#include` |
| `strcasecmp` undefined | Add `#include <strings.h>` |

---

## Initialization Order (IMPORTANT)

1. `display_init()` - I2C bus must init first (needed for CH422G)
2. `sd_card_init()` - Uses I2C for CH422G communication
3. `wifi_manager_init()`
4. `buzzer_init()`
5. UI creation

---

## Memory

```c
// Check free memory
ESP_LOGI(TAG, "Heap: %lu, PSRAM: %lu",
    esp_get_free_heap_size(),
    heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

// Allocate from PSRAM
void *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
```

---

## Time / Timezone

```c
// CET timezone (Europe/Prague)
setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
tzset();

// Get current time
time_t now;
time(&now);
struct tm tm_buf;
localtime_r(&now, &tm_buf);

// Valid timestamp check (SNTP synced)
#define TIMESTAMP_MIN_VALID 1577836800  // 2020-01-01
```

---

## References

- `ESP32_development.md` - Detailed dev notes
- `HARDWARE_MANUAL.md` - Full hardware reference
- `.claude/flash_instructions.md` - Flash command details
