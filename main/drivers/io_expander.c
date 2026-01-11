/**
 * DayZ Server Tracker - CH422G IO Expander Driver Implementation
 *
 * This is the ONLY module that should write to CH422G.
 * All other drivers (display, sd_card, usb_msc) must use this module.
 */

#include "io_expander.h"
#include "config.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "io_exp";

// CH422G uses register address AS the I2C device address!
#define CH422G_MODE_ADDR    0x24
#define CH422G_OUTPUT_ADDR  0x38
#define CH422G_MODE_OUTPUT  0x01    // Enable output mode for pins 0-7

// THE ONLY CH422G state variable in the entire codebase
static uint8_t g_ch422g_state = 0xFF;
static SemaphoreHandle_t g_mutex = NULL;
static bool g_initialized = false;

/**
 * Write state to CH422G hardware
 */
static esp_err_t write_state(uint8_t state) {
    // Step 1: Configure CH422G mode register (address 0x24)
    uint8_t mode = CH422G_MODE_OUTPUT;
    esp_err_t ret = i2c_master_write_to_device(TOUCH_I2C_NUM, CH422G_MODE_ADDR,
                                                &mode, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CH422G mode write failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 2: Write output state to output register (address 0x38)
    ret = i2c_master_write_to_device(TOUCH_I2C_NUM, CH422G_OUTPUT_ADDR,
                                      &state, 1, pdMS_TO_TICKS(100));
    if (ret == ESP_OK) {
        g_ch422g_state = state;
        ESP_LOGI(TAG, "CH422G write OK: addr=0x%02X state=0x%02X (EXIO0=%d)",
                 CH422G_OUTPUT_ADDR, state, (state & 0x01) ? 1 : 0);
    } else {
        ESP_LOGE(TAG, "CH422G output write failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t io_expander_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initial state: all outputs HIGH
    // - EXIO0 (backlight) = HIGH (on)
    // - EXIO1 (GT911 reset) = HIGH (not in reset)
    // - EXIO4 (SD_CS) = HIGH (not selected)
    g_ch422g_state = 0xFF;
    esp_err_t ret = write_state(g_ch422g_state);

    if (ret == ESP_OK) {
        g_initialized = true;
        ESP_LOGI(TAG, "CH422G IO expander initialized, state=0x%02X", g_ch422g_state);
    } else {
        vSemaphoreDelete(g_mutex);
        g_mutex = NULL;
    }
    return ret;
}

void io_expander_set_sd_cs(bool active) {
    if (!g_initialized) {
        ESP_LOGW(TAG, "Not initialized, cannot set SD_CS");
        return;
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex for SD_CS");
        return;
    }

    uint8_t old_state = g_ch422g_state;
    if (active) {
        g_ch422g_state &= ~CH422G_EXIO4_BIT;  // Low = CS active
    } else {
        g_ch422g_state |= CH422G_EXIO4_BIT;   // High = CS inactive
    }
    esp_err_t ret = write_state(g_ch422g_state);

    xSemaphoreGive(g_mutex);

    ESP_LOGI(TAG, "SD_CS = %s (state: 0x%02X -> 0x%02X, write %s)",
             active ? "ACTIVE" : "INACTIVE",
             old_state, g_ch422g_state,
             ret == ESP_OK ? "OK" : "FAILED");
}

esp_err_t io_expander_reset_gt911(void) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Not initialized, cannot reset GT911");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Resetting GT911 touch controller...");

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex for GT911 reset");
        return ESP_ERR_TIMEOUT;
    }

    // Assert reset (EXIO1 = LOW), preserve other bits
    g_ch422g_state &= ~CH422G_EXIO1_BIT;
    esp_err_t ret = write_state(g_ch422g_state);
    if (ret != ESP_OK) {
        xSemaphoreGive(g_mutex);
        return ret;
    }

    ESP_LOGD(TAG, "GT911 reset asserted (EXIO1=0)");

    // Hold reset for 20ms (GT911 needs at least 10ms)
    vTaskDelay(pdMS_TO_TICKS(20));

    // Release reset (EXIO1 = HIGH)
    g_ch422g_state |= CH422G_EXIO1_BIT;
    ret = write_state(g_ch422g_state);

    xSemaphoreGive(g_mutex);

    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGD(TAG, "GT911 reset released (EXIO1=1)");

    // Wait for GT911 to boot (needs ~55ms minimum, use 100ms to be safe)
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "GT911 reset complete");
    return ESP_OK;
}

esp_err_t io_expander_set_backlight(bool on) {
    if (!g_initialized) {
        ESP_LOGW(TAG, "Not initialized, cannot set backlight");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex for backlight");
        return ESP_ERR_TIMEOUT;
    }

    uint8_t old_state = g_ch422g_state;
    if (on) {
        g_ch422g_state |= CH422G_EXIO0_BIT;
    } else {
        g_ch422g_state &= ~CH422G_EXIO0_BIT;
    }

    ESP_LOGI(TAG, "Backlight %s: 0x%02X -> 0x%02X", on ? "ON" : "OFF", old_state, g_ch422g_state);
    esp_err_t ret = write_state(g_ch422g_state);

    if (ret != ESP_OK) {
        // Revert in-memory state on failure
        g_ch422g_state = old_state;
        ESP_LOGE(TAG, "Backlight I2C write failed: %s", esp_err_to_name(ret));
    }

    xSemaphoreGive(g_mutex);
    return ret;
}

void io_expander_test_backlight(void) {
    ESP_LOGW(TAG, "=== BACKLIGHT TEST START ===");
    ESP_LOGW(TAG, "Testing EXIO0 bit toggle - watch for screen flicker!");

    // Test 1: Try setting bit LOW (might be active-low)
    ESP_LOGW(TAG, "Test 1: Setting EXIO0=0 (state will be 0xFE if starting from 0xFF)");
    io_expander_set_backlight(false);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Test 2: Set bit HIGH
    ESP_LOGW(TAG, "Test 2: Setting EXIO0=1 (state will be 0xFF)");
    io_expander_set_backlight(true);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Test 3: Try writing directly to different addresses to find backlight
    ESP_LOGW(TAG, "Test 3: Direct write 0x00 to output register");
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint8_t test_state = 0x00;  // All bits low
        write_state(test_state);
        xSemaphoreGive(g_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Restore to all high
    ESP_LOGW(TAG, "Test 4: Restore to 0xFF");
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_ch422g_state = 0xFF;
        write_state(g_ch422g_state);
        xSemaphoreGive(g_mutex);
    }

    ESP_LOGW(TAG, "=== BACKLIGHT TEST END ===");
}

bool io_expander_get_backlight_state(void) {
    return (g_ch422g_state & CH422G_EXIO0_BIT) != 0;
}

void io_expander_set_usb_mode(bool usb_mode) {
    if (!g_initialized) {
        ESP_LOGW(TAG, "Not initialized, cannot set USB mode");
        return;
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex for USB mode");
        return;
    }

    if (usb_mode) {
        g_ch422g_state &= ~CH422G_EXIO5_BIT;  // Low = USB mode
    } else {
        g_ch422g_state |= CH422G_EXIO5_BIT;   // High = CAN mode
    }
    write_state(g_ch422g_state);

    xSemaphoreGive(g_mutex);

    ESP_LOGI(TAG, "USB mode = %s", usb_mode ? "USB" : "CAN");
}

uint8_t io_expander_get_state(void) {
    return g_ch422g_state;
}

void io_expander_reset_state(void) {
    // Called when I2C driver is deleted - forces reinitialization on next use
    if (g_mutex) {
        vSemaphoreDelete(g_mutex);
        g_mutex = NULL;
    }
    g_initialized = false;
    g_ch422g_state = 0xFF;
    ESP_LOGI(TAG, "IO expander state reset - will reinitialize on next use");
}
