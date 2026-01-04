# DayZ Server Monitor

Real-time player count monitor for DayZ servers using an ESP32-S3 with a 7" touchscreen display.

![ESP32-S3 DayZ Tracker](https://img.shields.io/badge/ESP32--S3-DayZ%20Monitor-blue)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-green)
![LVGL](https://img.shields.io/badge/LVGL-v9.2-orange)
![License](https://img.shields.io/badge/License-MIT-yellow)

## Features

### Display & UI
- **Smooth anti-aliased fonts** using LVGL graphics library
- Real-time player count display (e.g., 42/60)
- Modern dark theme with card-based UI
- Color-coded progress bar (green/yellow/red based on capacity)
- Animated progress bar transitions
- Server online/offline status indicator
- Day/night indicator with in-game server time
- 800x480 full-color touchscreen display

### Multi-Server Support
- Track up to **5 DayZ servers**
- Quick switching between servers via touch navigation
- Per-server alert thresholds and settings

### Player History & Charts
- **Historical player count chart** with time ranges:
  - 1 hour / 8 hours / 24 hours / 1 week
- SD card persistence for history data
- Touch-selectable time range buttons

### Smart Alerts
- **Configurable alert threshold** (beep when players >= X)
- **Active buzzer support** via SENSOR AD GPIO6 pin
- Visual alert banners on screen
- Beeps only once when threshold first triggered (not repeated)

### Restart Countdown Timer
- **Manual restart schedule** - set known restart time and interval
- Supports common intervals: 4h, 6h, 8h, 12h
- All times in **CET timezone**
- Countdown display: "Restart: ~2h 15m"
- Auto-detection fallback (learns from player count drops)
- Color-coded urgency (green > 30min, orange < 30min, red = imminent)

### Connectivity
- WiFi configuration via touch UI
- SNTP time synchronization for accurate CET timestamps
- Auto-reconnect on network drops
- BattleMetrics API integration (no API key required)

## Hardware Required

### Main Board
- **Waveshare ESP32-S3-Touch-LCD-7** (800x480 RGB display)
  - ESP32-S3 with 8MB PSRAM
  - 7" capacitive touchscreen
  - ST7262 display controller
  - SD card slot

### Optional: Buzzer for Alerts
- **3-pin active buzzer module** (e.g., Elegoo)
- Connect to **SENSOR AD** header:
  - S (Signal) → AD pin (GPIO6)
  - Middle → 3V3
  - - (Minus) → GND

## Display Preview

```
+------------------------------------------+
|           DayZ Server Monitor            |
|        < Server Name (1/3) >             |
|                                          |
|     PLAYERS        42        /60         |
|                                          |
|     [====================--------]       |
|                                          |
|  Day 14:32    Restart: ~2h 15m           |
|  Updated: 14:32:15 CET                   |
+------------------------------------------+
```

## Quick Start

### 1. Prerequisites

- [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- Waveshare ESP32-S3-Touch-LCD-7
- (Optional) Active buzzer module for alerts

### 2. Build and Flash

**Windows (PowerShell):**
```powershell
# Build
.\build.ps1

# Flash (adjust COM port in flash.ps1 as needed)
.\flash.ps1
```

**Linux/macOS:**
```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

### 3. Initial Setup

On first boot:
1. Go to **Settings** → **WiFi Settings**
2. Enter your WiFi credentials (2.4GHz only)
3. Device will connect and start fetching server data

### 4. Add Your Server

1. Go to **Settings** → **Server Settings** → **Add Server**
2. Enter BattleMetrics Server ID (find it on [BattleMetrics](https://www.battlemetrics.com/servers/dayz))
3. Enter a display name for the server

### 5. Configure Restart Schedule (Optional)

1. Go to **Settings** → scroll to **"Restart Schedule (CET)"**
2. Enable **"Manual Schedule"**
3. Set the **hour:minute** of a known restart (CET timezone)
4. Select the restart **interval** (4h, 6h, 8h, or 12h)

## Project Structure

```
DayZ_servertracker/
├── main/
│   ├── main.c              # Main application code
│   ├── idf_component.yml   # LVGL dependencies
│   └── CMakeLists.txt      # Component build config
├── build.ps1               # Windows build script
├── flash.ps1               # Windows flash script
├── checkports.ps1          # COM port detection helper
├── partitions.csv          # Custom partition table (3MB app)
├── CMakeLists.txt          # Project build config
└── sdkconfig.defaults      # ESP-IDF configuration
```

## Settings Reference

| Setting | Description |
|---------|-------------|
| **Refresh Interval** | How often to fetch server data (10-300 sec) |
| **Alerts Enabled** | Toggle buzzer/visual alerts |
| **Alert Threshold** | Beep when player count >= this value |
| **Manual Schedule** | Enable manual restart time input |
| **Known Restart** | Hour:minute of a known restart (CET) |
| **Interval** | Time between restarts (4h/6h/8h/12h) |

## Dependencies

Managed automatically via ESP-IDF component manager:

- **LVGL v9.2** - Graphics library with anti-aliased font rendering
- **esp_lvgl_port v2.4** - ESP-IDF LVGL integration for RGB displays
- **esp_lcd_touch_gt911** - Touch controller driver

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Black screen | Verify GPIO pins match your board, check PSRAM is enabled |
| Display flickering | Ensure bounce buffers are enabled |
| WiFi not connecting | Ensure 2.4GHz network (ESP32 doesn't support 5GHz) |
| Player count shows "---" | Check internet connection or server might be offline |
| Wrong COM port | Run `checkports.ps1` to find the correct port |
| Buzzer not working | Ensure active buzzer connected to SENSOR AD (GPIO6) |
| Restart countdown wrong | Verify CET timezone, check manual schedule settings |

## API

Uses the [BattleMetrics API](https://www.battlemetrics.com/developers/documentation) to fetch server status. No API key required for basic queries.

## License

MIT License - feel free to use and modify for your own projects.

## Credits

- [LVGL](https://lvgl.io/) for the graphics library
- [Waveshare](https://www.waveshare.com/) for the ESP32-S3-Touch-LCD-7
- [BattleMetrics](https://www.battlemetrics.com/) for the server API
- [Espressif](https://www.espressif.com/) for ESP-IDF
