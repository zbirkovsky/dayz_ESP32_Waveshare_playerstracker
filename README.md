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

### Peak Hours Heatmap
- **7-day × 6-period heatmap** showing average player activity
- Time periods: 00-04, 04-08, 08-12, 12-16, 16-20, 20-24
- Color-coded cells (green→yellow→orange→red)
- Tap any cell to see exact average and sample count
- Based on 28 days of historical data

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

### Multi-WiFi Support
- Store up to **8 WiFi credentials** for different locations
- **Auto-connect**: Scans and connects to strongest known network on disconnect
- **WiFi scan UI**: Discover nearby networks with signal strength indicators
- Manage saved networks (connect, delete) from the WiFi Settings screen
- Manual SSID entry for hidden networks
- Backward-compatible migration from single-credential format

### Connectivity
- WiFi configuration via touch UI
- SNTP time synchronization for accurate CET timestamps
- Smart auto-reconnect: retries current network 3 times, then scans for alternatives
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
| [⚙] [📊] [🔥] [📶]    14:32 CET      SD:2%  [↻] |
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
│   ├── main.c                    # Entry point, event-driven main loop
│   ├── config.h                  # Constants, pins, and configuration
│   ├── app_state.h/.c            # Centralized state management (thread-safe)
│   ├── app_init.c                # Initialization sequence
│   ├── events.h/.c               # Event queue for UI/logic decoupling
│   ├── events/
│   │   └── event_handler.h/.c    # Event dispatch with deferred I/O support
│   ├── drivers/
│   │   ├── buzzer.h/.c           # Buzzer hardware driver
│   │   ├── display.h/.c          # LCD + Touch + LVGL initialization
│   │   ├── sd_card.h/.c          # SD card (SPI) driver
│   │   └── io_expander.h/.c      # CH422G I2C driver
│   ├── services/
│   │   ├── wifi_manager.h/.c     # WiFi + SNTP + multi-credential auto-connect
│   │   ├── battlemetrics.h/.c    # BattleMetrics API client (persistent HTTP)
│   │   ├── server_query.h/.c     # Background server polling task
│   │   ├── secondary_fetch.h/.c  # Secondary server background task
│   │   ├── settings_store.h/.c   # NVS settings persistence + JSON export
│   │   ├── history_store.h/.c    # Player history (JSON + binary + NVS)
│   │   ├── restart_manager.h/.c  # Server restart detection & countdown
│   │   └── alert_manager.h/.c    # Player threshold alerts
│   ├── ui/
│   │   ├── ui_context.h          # Widget pointer storage
│   │   ├── ui_styles.h/.c        # Color definitions & shared styles
│   │   ├── ui_widgets.h/.c       # Reusable widget factories
│   │   ├── ui_update.h/.c        # UI refresh functions (consolidated locks)
│   │   ├── ui_callbacks.h/.c     # Touch event callbacks
│   │   ├── screen_builder.h/.c   # Screen creation
│   │   ├── screen_history.h/.c   # History chart screen
│   │   ├── screen_heatmap.h/.c   # Peak hours heatmap screen
│   │   └── screen_screensaver.h/.c # Screensaver screen
│   └── power/
│       └── screensaver.h/.c      # Screensaver + power management
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

### v2.8.0 - Performance Optimization

Two rounds of deep performance work targeting I/O waste, CPU overhead, and UI responsiveness.

**Architecture: Event-driven main loop**
- Main loop now blocks on event queue instead of busy-polling (100ms idle wake)
- Server queries run in a dedicated background task with task notification wake
- Secondary server fetches use task notifications instead of flag polling
- Deferred I/O on server switch: UI updates instantly, history save/load runs after LVGL renders

**I/O & Storage**
- Cached file handle for JSON history append - eliminates 7 syscalls per entry (~20,000/day saved)
- Replaced cJSON per-line parsing with `sscanf` in history loader - zero malloc/free per entry
- Date-based filename filtering skips files outside requested range without opening them
- Removed `settings_export_to_json()` from every `settings_save()` - now only on server add/delete
- Reduced heatmap PSRAM allocation from 120KB to 24KB

**CPU & Rendering**
- Trend deltas pre-calculated when data arrives, UI reads cached value (O(1), no mutex)
- Consolidated LVGL locks: `ui_update_all()` does main + secondary + SD status in one lock
- History chart pre-samples data into stack buffer, populates chart without re-iterating
- Heatmap refresh deferred via LVGL timer to avoid watchdog timeout on screen load
- Persistent HTTP client with keep-alive (reuses TLS connection across queries)
- HTTP response buffer allocated in PSRAM (16KB off main heap)

**Responsiveness**
- Screensaver touch lock timeout increased from 10ms to 50ms (prevents dropped taps during renders)
- Screensaver wake no longer blocks for 130ms forced refresh

**Code Quality**
- Deduplicated 200+ line switch statement in event handler (single `handle_event()` function)
- ~250 fewer lines of code, ~3KB flash saved

### v2.7.0 - Multi-WiFi Credential Support
- **NEW: Multi-WiFi Storage**: Save up to 8 WiFi networks for portable use
  - Device remembers credentials across locations (home, office, hotspot, etc.)
  - Credentials persisted in NVS with automatic migration from single-WiFi format
- **NEW: WiFi Scan UI**: Redesigned WiFi Settings screen with network discovery
  - Scan for nearby networks with real-time signal strength (RSSI) display
  - Color-coded signal indicators (green/yellow/red)
  - Tap a known network to connect instantly
  - Tap an unknown network to enter password and save
  - Manual SSID entry for hidden networks
- **NEW: Saved Networks Management**: View, connect, and delete saved WiFi credentials
  - Connected network highlighted with status indicator
  - Connection info panel showing SSID, signal, IP, MAC, and time sync status
- **Smart Auto-Connect**: Automatic failover to known networks on disconnect
  - After 3 failed reconnect attempts, scans for alternative known networks
  - Connects to strongest available known network by RSSI
  - Seamless roaming between saved networks
- **17 files modified** across all layers (config, state, events, services, UI)

### v2.6.0 - Continuous Background Data Collection
- **24/7 Data Collection**: Server queries now run continuously even during screensaver
  - History and trend data recorded regardless of screen state
  - Leave device unattended for weeks - all data points captured
  - Essential for prediction features and long-term analytics
- **Screensaver Shows Live Data**: Player count updates every second while screen is dimmed
- **Display Artifact Fix**: Improved screen refresh on screensaver wake (multi-pass rendering)

### v2.5.0 - Peak Hours Heatmap & UI Improvements
- **NEW: Peak Hours Heatmap** - Visual analytics showing when your server is busiest
  - 7 days × 6 time periods (4-hour blocks)
  - Color-coded cells showing average player counts
  - Tap cells to see detailed statistics
  - Analyzes 28 days of historical data
- **CET Time Display** - Current time shown in top bar (replaces server navigation arrows)
- **Simplified Navigation** - Removed left/right server arrows (use settings to switch servers)
- **Screensaver Improvements** - Removed deep sleep (data now refreshes in background)
- **Code Architecture** - Added screen_heatmap module with optimized 42-cell grid

### v2.4.0 - History Graph Fixes & WiFi Diagnostics
- **CRITICAL FIX**: History graphs now display correct data for all time ranges
  - Fixed JSON loading bug that filtered out historical entries
  - 8-hour and 24-hour views now show accurate player counts
  - JSON files are now the primary data source (NVS is backup)
- **Boot-time history loading**: JSON history loads immediately on startup
- **New utility function**: `history_clear_all_storage()` to reset all history data
- **WiFi diagnostics**: Added helper functions for RSSI, IP address, and MAC address
- **Development docs**: Added `ESP32_development.md` with build/flash instructions for Claude Code
- **Build scripts**: Consolidated to single `build_and_flash.ps1` with PowerShell heredoc method

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
