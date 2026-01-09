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
- Color-coded progress bar (green/yellow/orange/red based on player count)
- Animated progress bar transitions
- Server online/offline status indicator
- Day/night indicator with in-game server time
- **SD card usage indicator** in top bar
- **Screen saver** with configurable timeout (backlight off, touch to wake)
- 800x480 full-color touchscreen display

### Multi-Server Watch Dashboard
- Track up to **5 DayZ servers** simultaneously
- **Main server** with large display card
- **Up to 3 secondary servers** shown as compact cards below
- **Trend indicators** showing player count change over ~2 hours (↑/↓)
- Quick switching between servers via touch navigation
- Per-server alert thresholds and settings

### Player History & Charts
- **Historical player count chart** with time ranges:
  - 1 hour / 8 hours / 24 hours / 1 week
- Touch-selectable time range buttons

### Data Storage (SD Card)
- **JSON Lines format** for human-readable history files
- Daily history files: `/sdcard/history/server_X/YYYY-MM-DD.jsonl`
- **Server config export**: `/sdcard/servers.json` (auto-sync with settings)
- NVS backup for boot without SD card
- **~600 years** of storage capacity per server on 16GB SD card
- 1-year retention with automatic cleanup

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
+--------------------------------------------------+
| [Settings] [History]  < Server (1/4) >  SD:2% [↻]|
+--------------------------------------------------+
|                                                  |
|   ┌──────────────────────────────────────────┐   |
|   │  PLAYERS           42 / 60          ↑ +5 │   |
|   │  [████████████████████░░░░░░░░░░░░░░░░░] │   |
|   │  Day 14:32    Restart: ~2h 15m           │   |
|   │  Updated: 14:32:15 CET                   │   |
|   └──────────────────────────────────────────┘   |
|                                                  |
|   ┌─────────────┐ ┌─────────────┐ ┌────────────┐ |
|   │ Server 2    │ │ Server 3    │ │ Server 4   │ |
|   │   38/60 ↓-3 │ │   52/60 ↑+8 │ │   12/60 -- │ |
|   │ Night 02:15 │ │ Day 14:30   │ │ Day 08:45  │ |
|   └─────────────┘ └─────────────┘ └────────────┘ |
+--------------------------------------------------+
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
│   ├── main.c                    # Application entry point & UI screens
│   ├── config.h                  # Constants, pins, and configuration
│   ├── app_state.h/.c            # Centralized state management (thread-safe)
│   ├── events.h/.c               # Event queue for UI/logic decoupling
│   ├── drivers/
│   │   ├── buzzer.h/.c           # Buzzer hardware driver
│   │   ├── sd_card.h/.c          # SD card & CH422G IO expander
│   │   └── display.h/.c          # LCD + Touch + LVGL initialization
│   ├── services/
│   │   ├── wifi_manager.h/.c     # WiFi connection & SNTP sync
│   │   ├── battlemetrics.h/.c    # BattleMetrics API client (cJSON)
│   │   ├── settings_store.h/.c   # NVS settings persistence
│   │   └── history_store.h/.c    # Player history storage
│   ├── ui/
│   │   ├── ui_styles.h/.c        # Color definitions & shared styles
│   │   └── ui_widgets.h/.c       # Reusable widget factories
│   ├── idf_component.yml         # LVGL dependencies
│   └── CMakeLists.txt            # Component build config
├── build.ps1                     # Windows build script
├── flash.ps1                     # Windows flash script
├── checkports.ps1                # COM port detection helper
├── partitions.csv                # Custom partition table (3MB app)
├── CMakeLists.txt                # Project build config
└── sdkconfig.defaults            # ESP-IDF configuration
```

### Architecture Overview

The codebase follows a **modular layered architecture**:

- **Drivers Layer**: Hardware abstraction (buzzer, SD card, display)
- **Services Layer**: Business logic (API client, WiFi, storage)
- **UI Layer**: Presentation (styles, widget factories, screens)
- **Core**: State management and event system

All state is managed through `app_state` with mutex protection for thread safety. UI callbacks post events to a queue, which are processed in the main loop, ensuring clean separation between UI and business logic.

## Settings Reference

| Setting | Description |
|---------|-------------|
| **Refresh Interval** | How often to fetch server data (10-300 sec) |
| **Screen Off** | Screen saver timeout (Off, 5m-4h) |
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
- **cJSON** - JSON parsing library (built into ESP-IDF)

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

## Changelog

### v2.3.0 - Storage Layer Refactoring & SD Card Fix
- **CRITICAL FIX**: SD card now works correctly with all partition formats
  - Fixed superfloppy (no partition table) detection
  - Added partition structure diagnostics
  - ESP32 and PC now see the same files on SD card
- **Bug Fix**: `sd_card_verify_access()` now correctly returns false when access fails
- **Code Quality**: Major storage layer refactoring
  - New `storage_config.h` - centralized storage constants
  - New `nvs_keys.h` - NVS key generation macros (replaced 32+ duplicate patterns)
  - New `nvs_cache.h/c` - NVS handle caching for efficiency
  - New `path_validator.h/c` - path safety validation
  - New `io_expander.h/c` - CH422G I2C driver for SD_CS control
- **Config Fix**: FATFS sector size set to 512 bytes (was incorrectly 4096)
- **Build Helper**: Added `build_esp32.py` script for building from any environment

### v2.2.0 - Trend Fixes & Secondary History
- **Fixed 2-hour trend calculation**: Increased trend buffer from 4 to 240 data points
- **Stable trend indicator**: Shows `→ 0` when player count is stable (instead of blank)
- **Secondary server history**: All servers now record history even when shown as secondary
- **Seamless history merge**: When switching secondary→primary, history is loaded from JSON files
- **Debug logging**: Added trend calculation logging for troubleshooting

### v2.1.0 - Multi-Server Dashboard & JSON Storage
- **Multi-Server Watch Dashboard**: View main server + up to 3 secondary servers simultaneously
- **Trend Indicators**: Shows player count change over ~2 hours (↑/↓ with delta)
- **SD Card Status**: Shows usage percentage in top bar (color-coded)
- **JSON Storage**: Human-readable history files (`/sdcard/history/server_X/YYYY-MM-DD.jsonl`)
- **Config Export**: Server settings auto-saved to `/sdcard/servers.json`
- **Screen Saver**: Configurable backlight timeout (touch to wake)
- **Edge-to-edge dashboard layout**: Optimized screen real estate

### v2.0.0 - Major Architecture Refactoring
- Complete modular restructuring (drivers, services, UI layers)
- Thread-safe centralized state management
- Event-driven UI with clean separation of concerns
- Replaced manual JSON parsing with cJSON library
- Fixed GT911 touch controller initialization
- Fixed SD card CH422G I2C addressing

### v1.1.0 - Restart Countdown & Alerts
- Manual restart schedule configuration (CET timezone)
- Active buzzer support for player threshold alerts
- Day/night indicator with in-game server time
- SNTP time synchronization

### v1.0.0 - Initial Release
- Real-time player count monitoring via BattleMetrics API
- LVGL graphics with anti-aliased fonts
- Multi-server support (up to 5 servers)
- Player history charts
- WiFi configuration via touch UI

## License

MIT License - feel free to use and modify for your own projects.

## Credits

- [LVGL](https://lvgl.io/) for the graphics library
- [Waveshare](https://www.waveshare.com/) for the ESP32-S3-Touch-LCD-7
- [BattleMetrics](https://www.battlemetrics.com/) for the server API
- [Espressif](https://www.espressif.com/) for ESP-IDF
