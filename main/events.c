/**
 * DayZ Server Tracker - Event System Implementation
 */

#include "events.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "events";

#define EVENT_QUEUE_SIZE 32

static QueueHandle_t event_queue = NULL;

void events_init(void) {
    if (event_queue) {
        // Already initialized
        return;
    }

    event_queue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(app_event_t));
    if (!event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
    } else {
        ESP_LOGI(TAG, "Event queue initialized (size=%d)", EVENT_QUEUE_SIZE);
    }
}

bool events_post(const app_event_t *event) {
    if (!event_queue || !event) return false;

    if (xQueueSend(event_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full, dropping event type %d", event->type);
        return false;
    }

    return true;
}

bool events_post_simple(event_type_t type) {
    app_event_t event = { .type = type };
    return events_post(&event);
}

bool events_post_screen_change(screen_id_t screen) {
    app_event_t event = {
        .type = EVT_SCREEN_CHANGE,
        .data.screen = screen
    };
    return events_post(&event);
}

bool events_post_wifi_save(const char *ssid, const char *password) {
    app_event_t event = { .type = EVT_WIFI_SAVE };
    strncpy(event.data.wifi.ssid, ssid, sizeof(event.data.wifi.ssid) - 1);
    strncpy(event.data.wifi.password, password, sizeof(event.data.wifi.password) - 1);
    return events_post(&event);
}

bool events_post_server_add(const char *server_id, const char *display_name, const char *map_name) {
    app_event_t event = { .type = EVT_SERVER_ADD };
    strncpy(event.data.server.server_id, server_id,
            sizeof(event.data.server.server_id) - 1);
    strncpy(event.data.server.display_name, display_name,
            sizeof(event.data.server.display_name) - 1);
    if (map_name) {
        strncpy(event.data.server.map_name, map_name,
                sizeof(event.data.server.map_name) - 1);
    }
    return events_post(&event);
}

bool events_post_alert(const char *message, uint32_t color) {
    app_event_t event = { .type = EVT_ALERT_SHOW };
    strncpy(event.data.alert.message, message,
            sizeof(event.data.alert.message) - 1);
    event.data.alert.color = color;
    return events_post(&event);
}

bool events_post_secondary_click(int slot) {
    app_event_t event = {
        .type = EVT_SECONDARY_SERVER_CLICKED,
        .data.secondary.slot = slot
    };
    return events_post(&event);
}

bool events_receive(app_event_t *event) {
    if (!event_queue || !event) return false;

    return xQueueReceive(event_queue, event, 0) == pdTRUE;
}

bool events_receive_blocking(app_event_t *event, uint32_t timeout_ms) {
    if (!event_queue || !event) return false;

    return xQueueReceive(event_queue, event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

int events_pending_count(void) {
    if (!event_queue) return 0;
    return uxQueueMessagesWaiting(event_queue);
}
