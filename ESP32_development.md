# ESP32 Development Notes for Claude

This document contains important pitfalls, solutions, and best practices discovered while developing for this ESP32-S3 project.

## Environment Setup

### Critical Issue: MSys/Mingw Detection

**Problem:** ESP-IDF v5.5+ refuses to run when `MSYSTEM` environment variables are set (common in Git Bash/MinGW terminals).

**Error message:**
```
MSys/Mingw is no longer supported. Please follow the getting started guide...
```

**Solution:** Must clear ALL these environment variables before running idf.py:
- `MSYSTEM`
- `MSYSTEM_CHOST`
- `MSYSTEM_CARCH`
- `MSYSTEM_PREFIX`
- `MINGW_PREFIX`
- `MINGW_PACKAGE_PREFIX`

**Working approach:** Use PowerShell script file (not inline commands):
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "build_and_flash.ps1"
```

Inline PowerShell commands through bash get mangled. Always use a `.ps1` script file.

---

## Correct Paths (This System)

| Component | Path |
|-----------|------|
| **IDF_PATH** | `C:\Users\zbirk\esp\v5.5.1\esp-idf` |
| **IDF_TOOLS_PATH** | `C:\Espressif` |
| **Python env** | `C:\Espressif\python_env\idf5.5_py3.11_env` |
| **CMake** | `C:\Espressif\tools\cmake\3.30.2\bin` |
| **Ninja** | `C:\Espressif\tools\ninja\1.12.1` |
| **Toolchain** | `C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin` |

**WRONG path (common mistake):** `C:\Espressif\frameworks\esp-idf-v5.5.1`

---

## Build Commands

### Recommended: Use the PowerShell script
```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:/DayZ_servertracker/build_and_flash.ps1"
```

### Timeout
Always use `timeout 600000` (10 minutes) for build operations.

### Build only (no flash)
Edit the script to change `build flash` to just `build`.

---

## Common Build Errors

### 1. Implicit function declaration
**Error:** `warning: implicit declaration of function 'xxx'`
**Solution:** Add the missing `#include` header.

### 2. strcasecmp undefined
**Error:** `implicit declaration of function 'strcasecmp'`
**Solution:** Add `#include <strings.h>` (note: string**s**.h, not string.h)

### 3. Circular includes
**Error:** Unknown type errors when types seem defined
**Solution:** Move typedefs to a lower-level header file that doesn't have circular dependencies.

### 4. CMake cache conflict
**Error:** Build fails after changing source files
**Solution:** Delete the build directory:
```bash
rm -rf build
```

---

## LVGL Notes

### Thread Safety
Always wrap LVGL operations with lock/unlock:
```c
if (lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) {
    // LVGL operations here
    lvgl_port_unlock();
}
```

### Object deletion
Use `lv_obj_delete()` not `lv_obj_del()` (LVGL 9.x API change).

### Removing flags
Use `lv_obj_remove_flag()` not `lv_obj_clear_flag()` for some flags.

---

## SD Card / File System

### Path format
Use forward slashes or properly escaped backslashes:
- Good: `/sdcard/history/server_0/`
- Bad: `\sdcard\history\server_0\`

### File operations
Always check `sd_card_is_mounted()` before file operations.

### JSON files
Use `.jsonl` extension for JSON Lines format (one JSON object per line).

---

## Memory Management

### PSRAM allocation
```c
void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
```

### Checking free memory
```c
ESP_LOGI(TAG, "Free heap: %lu, PSRAM: %lu",
         esp_get_free_heap_size(),
         heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

---

## Time Functions

### Getting current time
```c
time_t now;
time(&now);
struct tm tm_buf;
struct tm *tm_info = localtime_r(&now, &tm_buf);
```

### Minimum valid timestamp
Timestamps before ~2020 indicate SNTP hasn't synced yet:
```c
#define STORAGE_TIMESTAMP_MIN_VALID 1577836800  // 2020-01-01
```

---

## Debugging Tips

### Serial monitor
Monitor may crash with Unicode characters. Use plain ASCII in logs when possible.

### Log levels
```c
ESP_LOGE(TAG, "Error: %s", msg);   // Always shown
ESP_LOGW(TAG, "Warning: %s", msg); // Warnings
ESP_LOGI(TAG, "Info: %s", msg);    // Info
ESP_LOGD(TAG, "Debug: %s", msg);   // Debug (often disabled)
```

---

## Project Structure

```
DayZ_servertracker/
├── main/
│   ├── main.c              # Entry point, UI, event loop
│   ├── app_state.c/h       # Centralized state management
│   ├── config.h            # Constants and pin definitions
│   ├── events.c/h          # Event queue system
│   ├── drivers/            # Hardware abstraction
│   │   ├── buzzer.c/h
│   │   ├── display.c/h
│   │   └── sd_card.c/h
│   ├── services/           # Business logic
│   │   ├── battlemetrics.c/h
│   │   ├── history_store.c/h
│   │   ├── settings_store.c/h
│   │   └── wifi_manager.c/h
│   └── ui/                 # UI components
│       ├── ui_styles.c/h
│       └── ui_widgets.c/h
├── sdcard/                 # SD card test files
├── build_and_flash.ps1     # Working build script
├── HARDWARE_MANUAL.md      # Hardware configuration
└── ESP32_DEVELOPMENT.md    # This file
```

---

## Things That DON'T Work

1. **Inline PowerShell commands** - Get mangled when passed through bash
2. **env -i** - Doesn't fully clear MSys environment
3. **unset MSYSTEM** - Not sufficient, other vars remain
4. **Running idf.py directly from bash** - MSys detection blocks it
5. **cmd.exe /c with complex commands** - Doesn't execute properly

---

## Things That DO Work

1. **PowerShell script file** - `powershell.exe -File script.ps1`
2. **Setting $env:VAR = $null** - Properly clears PowerShell environment
3. **Full PATH replacement** - Include only needed tool paths
4. **Absolute paths everywhere** - Avoid relative path issues
5. **PowerShell heredoc from bash** - See below (PREFERRED METHOD)

---

## PREFERRED METHOD: PowerShell Heredoc from Bash

This approach works reliably from Claude Code's bash shell:

### Build Only
```bash
powershell -ExecutionPolicy Bypass -File - << 'PSSCRIPT'
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5.1"
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\python_env\idf5.5_py3.11_env"
$env:PATH = "C:\Espressif\tools\idf-git\2.44.0\cmd;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;$env:PATH"
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Set-Location "C:\DayZ_servertracker"
& "C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe" "C:\Espressif\frameworks\esp-idf-v5.5.1\tools\idf.py" build
PSSCRIPT
```

### Build and Flash (COM5)
```bash
powershell -ExecutionPolicy Bypass -File - << 'PSSCRIPT'
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5.1"
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\python_env\idf5.5_py3.11_env"
$env:PATH = "C:\Espressif\tools\idf-git\2.44.0\cmd;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;$env:PATH"
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Set-Location "C:\DayZ_servertracker"
& "C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe" "C:\Espressif\frameworks\esp-idf-v5.5.1\tools\idf.py" -p COM5 flash
PSSCRIPT
```

### Monitor Serial Output
```bash
powershell -ExecutionPolicy Bypass -File - << 'PSSCRIPT'
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5.1"
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\python_env\idf5.5_py3.11_env"
$env:PATH = "C:\Espressif\tools\idf-git\2.44.0\cmd;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;$env:PATH"
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Set-Location "C:\DayZ_servertracker"
& "C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe" "C:\Espressif\frameworks\esp-idf-v5.5.1\tools\idf.py" -p COM5 monitor
PSSCRIPT
```

### Why This Works
- `powershell -File -` reads script from stdin (the heredoc)
- `<< 'PSSCRIPT'` is a bash heredoc with single quotes (no variable expansion)
- Environment variables are set explicitly in PowerShell
- `Remove-Item Env:MSYSTEM` clears the MSys indicator that ESP-IDF rejects
- Direct Python call bypasses shell detection issues

### Timeout Note
Always use `timeout 300000` (5 minutes) for build operations in Bash tool.
