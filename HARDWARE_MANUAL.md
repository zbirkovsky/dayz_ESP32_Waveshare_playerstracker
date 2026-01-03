# Waveshare ESP32-S3-Touch-LCD-7 Hardware Manual

## Hardware Specifications

| Specification | Value |
|--------------|-------|
| **Display** | 7" TFT LCD, 800x480 pixels, 65K colors |
| **MCU** | ESP32-S3 (Xtensa dual-core 240MHz) |
| **PSRAM** | 8MB Octal PSRAM |
| **Flash** | 16MB |
| **Display Controller** | ST7262 |
| **Interface** | 16-bit RGB parallel |
| **Touch** | GT911 capacitive touch (I2C) |

---

## Critical Configuration for Stable Display

### Problem: Flickering / Fragmented UI / Wrong Scale

If you see any of these symptoms:
- Display shows only a fragment of the UI
- Content appears "zoomed in" or incorrectly scaled
- Flickering or visual corruption
- Display drifts or shifts during WiFi operations

**Root Cause:** PSRAM bandwidth starvation. The RGB LCD peripheral cannot read frame data fast enough from PSRAM.

### Solution: Enable Bounce Buffers

Bounce buffers create internal SRAM intermediaries between PSRAM and the LCD peripheral.

---

## Required sdkconfig.defaults

Create `sdkconfig.defaults` with these settings:

```ini
# SPIRAM/PSRAM Configuration for Waveshare ESP32-S3-Touch-LCD-7
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y

# Allow allocating from PSRAM
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384

# LCD needs PSRAM for frame buffer - CRITICAL for stable display
CONFIG_ESP32S3_DATA_CACHE_64KB=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y

# Enable PSRAM XIP for better performance with bounce buffers
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y

# Auto-restart LCD DMA in VSYNC to recover from any shifts
CONFIG_LCD_RGB_RESTART_IN_VSYNC=y
```

---

## RGB LCD Panel Configuration (main.c)

### GPIO Pin Mapping

```c
// Control Pins
#define PIN_LCD_VSYNC  3
#define PIN_LCD_HSYNC  46
#define PIN_LCD_DE     5
#define PIN_LCD_PCLK   7

// Blue Channel (5 bits)
#define PIN_LCD_B0     14   // DATA0
#define PIN_LCD_B1     38   // DATA1
#define PIN_LCD_B2     18   // DATA2
#define PIN_LCD_B3     17   // DATA3
#define PIN_LCD_B4     10   // DATA4

// Green Channel (6 bits)
#define PIN_LCD_G0     39   // DATA5
#define PIN_LCD_G1     0    // DATA6
#define PIN_LCD_G2     45   // DATA7
#define PIN_LCD_G3     48   // DATA8
#define PIN_LCD_G4     47   // DATA9
#define PIN_LCD_G5     21   // DATA10

// Red Channel (5 bits)
#define PIN_LCD_R0     1    // DATA11
#define PIN_LCD_R1     2    // DATA12
#define PIN_LCD_R2     42   // DATA13
#define PIN_LCD_R3     41   // DATA14
#define PIN_LCD_R4     40   // DATA15

#define PIN_LCD_BL     -1   // Backlight (always on)
```

### Display Dimensions

```c
#define LCD_WIDTH  800
#define LCD_HEIGHT 480
```

### RGB Panel Configuration Structure

```c
esp_lcd_rgb_panel_config_t panel_config = {
    .clk_src = LCD_CLK_SRC_DEFAULT,
    .timings = {
        .pclk_hz = 16000000,           // 16MHz pixel clock
        .h_res = 800,                   // Horizontal resolution
        .v_res = 480,                   // Vertical resolution
        .hsync_pulse_width = 4,
        .hsync_back_porch = 8,
        .hsync_front_porch = 8,
        .vsync_pulse_width = 4,
        .vsync_back_porch = 16,
        .vsync_front_porch = 16,
        .flags.pclk_active_neg = 1,     // Pixel clock polarity
    },
    .data_width = 16,                   // 16-bit RGB565
    .num_fbs = 1,                       // Single frame buffer

    // *** CRITICAL: BOUNCE BUFFER - THIS PREVENTS FLICKERING ***
    .bounce_buffer_size_px = 10 * 800,  // 8000 pixels = ~16KB

    .psram_trans_align = 64,            // PSRAM alignment
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
    .flags.fb_in_psram = 1,             // Frame buffer in PSRAM
};
```

---

## Key Configuration Rules

### 1. Always Enable Bounce Buffers When Using PSRAM

```c
.bounce_buffer_size_px = 10 * LCD_WIDTH,  // NEVER set to 0
.flags.fb_in_psram = 1,
```

**Why:** Without bounce buffers, the LCD peripheral reads directly from PSRAM. PSRAM shares the SPI bus with Flash, causing bandwidth starvation during code execution or WiFi operations.

### 2. Pixel Clock Limits

| PSRAM Type | Max PCLK (no bounce) | Max PCLK (with bounce) |
|------------|---------------------|------------------------|
| Quad 80MHz | ~11 MHz | ~16 MHz |
| Octal 80MHz | ~22 MHz | ~21 MHz (Waveshare) |
| Octal 120MHz | ~30 MHz | Higher |

For Waveshare ESP32-S3-Touch-LCD-7: Use **16 MHz** with bounce buffers.

### 3. Required Kconfig Options

| Option | Purpose |
|--------|---------|
| `CONFIG_ESP32S3_DATA_CACHE_64KB=y` | Larger cache for LCD data |
| `CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y` | Prevents display drift |
| `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` | Execute code from PSRAM |
| `CONFIG_SPIRAM_RODATA=y` | Store const data in PSRAM |
| `CONFIG_LCD_RGB_RESTART_IN_VSYNC=y` | Auto-recovery from shifts |

---

## Frame Buffer Allocation

```c
// Allocate frame buffer in PSRAM (768KB for 800x480 RGB565)
uint16_t *frame_buffer = heap_caps_malloc(
    LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
    MALLOC_CAP_SPIRAM
);

// Clear the buffer
memset(frame_buffer, 0, LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t));
```

---

## Alternative Timing Parameters

If you experience issues with the default timing, try these alternative values:

### Alternative 1 (From ST7262 Datasheet)
```c
.hsync_pulse_width = 48,
.hsync_back_porch = 88,
.hsync_front_porch = 40,
.vsync_pulse_width = 3,
.vsync_back_porch = 32,
.vsync_front_porch = 13,
```

### Alternative 2 (Conservative)
```c
.pclk_hz = 12000000,  // Lower pixel clock (12MHz)
.hsync_pulse_width = 4,
.hsync_back_porch = 43,
.hsync_front_porch = 8,
.vsync_pulse_width = 4,
.vsync_back_porch = 12,
.vsync_front_porch = 8,
```

---

## Troubleshooting Guide

| Symptom | Cause | Solution |
|---------|-------|----------|
| Flickering | No bounce buffers | Set `bounce_buffer_size_px = 10 * LCD_WIDTH` |
| Fragmented display | PSRAM bandwidth | Enable bounce buffers + reduce PCLK |
| Display drift | Cache line size | Enable `CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y` |
| Corruption during WiFi | Bus contention | Enable `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` |
| Black screen | Wrong GPIO pins | Verify pin mapping matches your board revision |
| Colors inverted | PCLK polarity | Toggle `pclk_active_neg` (0 or 1) |

---

## Build Commands

```bash
# Clean build (required after changing sdkconfig.defaults)
idf.py fullclean

# Set target
idf.py set-target esp32s3

# Build
idf.py build

# Flash
idf.py -p COM6 flash

# Monitor serial output
idf.py -p COM6 monitor
```

---

## Color Format (RGB565)

```c
// RGB565: 5 bits Red, 6 bits Green, 5 bits Blue
#define RGB565(r, g, b) (((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F))

// Common colors
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
```

---

## References

- [ESP-IDF RGB LCD Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html)
- [ESP-FAQ LCD Troubleshooting](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html)
- [ST7262 Datasheet](https://focuslcds.com/wp-content/uploads/Drivers/ST7262.pdf)
- [Waveshare ESP32-S3-Touch-LCD-7 Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7)
