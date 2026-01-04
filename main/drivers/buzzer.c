/**
 * DayZ Server Tracker - Buzzer Driver Implementation
 */

#include "buzzer.h"
#include "../config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "buzzer";
static bool buzzer_initialized = false;

void buzzer_init(void) {
    if (!BUZZER_ENABLED || buzzer_initialized) return;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUZZER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(BUZZER_PIN, 0);
    buzzer_initialized = true;
    ESP_LOGI(TAG, "Buzzer initialized on GPIO%d", BUZZER_PIN);
}

bool buzzer_is_ready(void) {
    return BUZZER_ENABLED && buzzer_initialized;
}

void buzzer_beep(int duration_ms, int frequency_hz) {
    (void)frequency_hz;  // Active buzzer ignores frequency - has its own tone
    if (!BUZZER_ENABLED || !buzzer_initialized) return;

    gpio_set_level(BUZZER_PIN, 1);  // Turn ON
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_set_level(BUZZER_PIN, 0);  // Turn OFF
}

void buzzer_alert_restart(void) {
    // Three ascending beeps for "server restarted - fresh loot!"
    buzzer_beep(150, 800);
    vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_beep(150, 1000);
    vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_beep(200, 1200);
}

void buzzer_alert_threshold(void) {
    // Two quick beeps for player threshold
    buzzer_beep(100, 1000);
    vTaskDelay(pdMS_TO_TICKS(80));
    buzzer_beep(100, 1000);
}

void buzzer_test(void) {
    ESP_LOGI(TAG, "Testing buzzer...");
    buzzer_beep(200, 1000);     // 200ms beep at 1kHz
    vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_beep(200, 1500);     // Second beep at 1.5kHz
    ESP_LOGI(TAG, "Buzzer test complete");
}
