# DayZ Server Tracker

Real-time player count monitor for DayZ servers using an ESP32-S3 with a 7" touchscreen display.

![ESP32-S3 DayZ Tracker](https://img.shields.io/badge/ESP32--S3-DayZ%20Tracker-blue)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-green)
![License](https://img.shields.io/badge/License-MIT-yellow)

## Features

- Real-time player count display (e.g., 42/60)
- Color-coded progress bar (green/yellow/red based on capacity)
- Server online/offline status indicator
- Auto-refresh every 30 seconds
- 800x480 full-color display
- Uses BattleMetrics API (no API key required)

## Hardware Required

- **Waveshare ESP32-S3-Touch-LCD-7** (800x480 RGB display)
  - ESP32-S3 with 8MB PSRAM
  - 7" capacitive touchscreen
  - ST7262 display controller

## Display Preview

```
+---------------------------------------+
|           DAYZ SERVER                 |
|         3833 EUROPE-DE                |
|                                       |
|    PLAYERS       42/60                |
|                                       |
|    [================------]           |
|                                       |
|    ONLINE        14:32:15             |
|    5.62.99.20:11400                   |
+---------------------------------------+
```

## Quick Start

### 1. Prerequisites

- [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- Waveshare ESP32-S3-Touch-LCD-7

### 2. Configure WiFi

Edit `main/main.c` and update your WiFi credentials:

```c
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"
```

### 3. Configure Your Server (Optional)

To track a different DayZ server, update these values in `main/main.c`:

```c
#define DAYZ_SERVER_NAME "Your Server Name"
#define DAYZ_SERVER_IP   "xxx.xxx.xxx.xxx"
#define DAYZ_SERVER_PORT 2302
#define BATTLEMETRICS_SERVER_ID "your_server_id"
```

Find your server ID on [BattleMetrics](https://www.battlemetrics.com/servers/dayz).

### 4. Build and Flash

**Windows (PowerShell):**
```powershell
# Build
.\build.ps1

# Flash (adjust COM port as needed)
.\flash.ps1
```

**Linux/macOS:**
```bash
# Set target
idf.py set-target esp32s3

# Build
idf.py build

# Flash
idf.py -p /dev/ttyUSB0 flash
```

## Project Structure

```
DayZ_servertracker/
+-- main/
|   +-- main.c              # Main application code
|   +-- CMakeLists.txt      # Component build config
+-- build.ps1               # Windows build script
+-- flash.ps1               # Windows flash script
+-- checkports.ps1          # COM port detection helper
+-- CMakeLists.txt          # Project build config
+-- sdkconfig.defaults      # ESP-IDF configuration
+-- HARDWARE_MANUAL.md      # Detailed hardware reference
```

## Configuration

### sdkconfig.defaults

Key settings for stable display operation:

```ini
# PSRAM Configuration
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y

# Display stability
CONFIG_ESP32S3_DATA_CACHE_64KB=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
CONFIG_LCD_RGB_RESTART_IN_VSYNC=y

# SSL for HTTPS API calls
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Black screen | Verify GPIO pins match your board, check PSRAM is enabled |
| Display flickering | Bounce buffers are configured in `sdkconfig.defaults` |
| WiFi not connecting | Ensure 2.4GHz network (ESP32 doesn't support 5GHz) |
| Player count shows "---" | Check internet connection or server might be offline |
| Wrong COM port | Run `checkports.ps1` to find the correct port |

## API

Uses the [BattleMetrics API](https://www.battlemetrics.com/developers/documentation) to fetch server status. No API key required for basic queries.

## Hardware Reference

See [HARDWARE_MANUAL.md](HARDWARE_MANUAL.md) for detailed hardware configuration including:
- GPIO pin mapping
- Display timing parameters
- PSRAM configuration
- Alternative timing values

## License

MIT License - feel free to use and modify for your own projects.

## Credits

- [Waveshare](https://www.waveshare.com/) for the ESP32-S3-Touch-LCD-7
- [BattleMetrics](https://www.battlemetrics.com/) for the server API
- [Espressif](https://www.espressif.com/) for ESP-IDF
