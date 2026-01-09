/**
 * DayZ Server Tracker - NVS Handle Cache Implementation
 */

#include "nvs_cache.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "nvs_cache";

// Cached handles
static nvs_handle_t read_handle = 0;
static nvs_handle_t write_handle = 0;
static bool initialized = false;

// Mutex for thread safety
static SemaphoreHandle_t cache_mutex = NULL;

esp_err_t nvs_cache_init(void) {
    if (initialized) {
        ESP_LOGW(TAG, "NVS cache already initialized");
        return ESP_OK;
    }

    // Create mutex if needed
    if (cache_mutex == NULL) {
        cache_mutex = xSemaphoreCreateMutex();
        if (cache_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    // Open read handle
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &read_handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // Namespace doesn't exist yet - that's OK, open write handle first
        ESP_LOGI(TAG, "NVS namespace not found, will be created on first write");
        read_handle = 0;
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS read handle: %s", esp_err_to_name(ret));
        return ret;
    }

    // Open write handle
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &write_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS write handle: %s", esp_err_to_name(ret));
        if (read_handle != 0) {
            nvs_close(read_handle);
            read_handle = 0;
        }
        return ret;
    }

    // If read handle wasn't opened (namespace didn't exist), open it now
    if (read_handle == 0) {
        ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &read_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open NVS read handle after namespace creation: %s",
                     esp_err_to_name(ret));
            nvs_close(write_handle);
            write_handle = 0;
            return ret;
        }
    }

    initialized = true;
    ESP_LOGI(TAG, "NVS cache initialized successfully");
    return ESP_OK;
}

esp_err_t nvs_cache_get_read_handle(nvs_handle_t *handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!initialized || read_handle == 0) {
        ESP_LOGE(TAG, "NVS cache not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    *handle = read_handle;
    return ESP_OK;
}

esp_err_t nvs_cache_get_write_handle(nvs_handle_t *handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!initialized || write_handle == 0) {
        ESP_LOGE(TAG, "NVS cache not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    *handle = write_handle;
    return ESP_OK;
}

esp_err_t nvs_cache_commit(void) {
    if (!initialized || write_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    if (cache_mutex != NULL) {
        if (xSemaphoreTake(cache_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "Failed to acquire mutex for commit");
            return ESP_ERR_TIMEOUT;
        }
    }

    esp_err_t ret = nvs_commit(write_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(ret));
    }

    if (cache_mutex != NULL) {
        xSemaphoreGive(cache_mutex);
    }

    return ret;
}

bool nvs_cache_is_ready(void) {
    return initialized && write_handle != 0;
}

void nvs_cache_close(void) {
    if (cache_mutex != NULL) {
        xSemaphoreTake(cache_mutex, portMAX_DELAY);
    }

    if (read_handle != 0) {
        nvs_close(read_handle);
        read_handle = 0;
    }

    if (write_handle != 0) {
        nvs_close(write_handle);
        write_handle = 0;
    }

    initialized = false;

    if (cache_mutex != NULL) {
        xSemaphoreGive(cache_mutex);
    }

    ESP_LOGI(TAG, "NVS cache closed");
}

esp_err_t nvs_cache_reinit(void) {
    // Close any existing handles first
    nvs_cache_close();

    // Re-initialize
    return nvs_cache_init();
}
