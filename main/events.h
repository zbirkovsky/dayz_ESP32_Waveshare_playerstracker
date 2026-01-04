/**
 * DayZ Server Tracker - Event System
 * Decouples UI from business logic via event queue
 */

#ifndef EVENTS_H
#define EVENTS_H

#include <stdint.h>
#include <stdbool.h>
#include "app_state.h"

// ============== EVENT TYPES ==============

typedef enum {
    EVT_NONE = 0,

    // Navigation events
    EVT_SCREEN_CHANGE,

    // Data events
    EVT_REFRESH_DATA,
    EVT_DATA_UPDATED,

    // Settings events
    EVT_WIFI_SAVE,
    EVT_SERVER_ADD,
    EVT_SERVER_DELETE,
    EVT_SETTINGS_CHANGED,

    // Alert events
    EVT_ALERT_SHOW,
    EVT_ALERT_HIDE,
    EVT_ALERT_THRESHOLD_TRIGGERED,
    EVT_SERVER_RESTART_DETECTED,

    // Server switching
    EVT_SERVER_NEXT,
    EVT_SERVER_PREV,

} event_type_t;

// ============== EVENT DATA ==============

typedef struct {
    char ssid[33];
    char password[65];
} wifi_data_t;

typedef struct {
    char server_id[32];
    char display_name[64];
} server_add_data_t;

typedef struct {
    char message[64];
    uint32_t color;  // Hex color
} alert_data_t;

typedef struct {
    event_type_t type;
    union {
        screen_id_t screen;
        wifi_data_t wifi;
        server_add_data_t server;
        alert_data_t alert;
        int server_index;
    } data;
} app_event_t;

// ============== EVENT QUEUE API ==============

/**
 * Initialize the event queue system
 */
void events_init(void);

/**
 * Post an event to the queue
 * @param event Event to post
 * @return true if posted successfully
 */
bool events_post(const app_event_t *event);

/**
 * Post a simple event (no data)
 * @param type Event type
 */
bool events_post_simple(event_type_t type);

/**
 * Post a screen change event
 * @param screen Target screen
 */
bool events_post_screen_change(screen_id_t screen);

/**
 * Post a WiFi save event
 * @param ssid WiFi SSID
 * @param password WiFi password
 */
bool events_post_wifi_save(const char *ssid, const char *password);

/**
 * Post a server add event
 * @param server_id BattleMetrics server ID
 * @param display_name Display name
 */
bool events_post_server_add(const char *server_id, const char *display_name);

/**
 * Post an alert event
 * @param message Alert message
 * @param color Alert color (hex)
 */
bool events_post_alert(const char *message, uint32_t color);

/**
 * Receive an event from the queue (non-blocking)
 * @param event Output event
 * @return true if event received
 */
bool events_receive(app_event_t *event);

/**
 * Receive an event from the queue (blocking)
 * @param event Output event
 * @param timeout_ms Timeout in milliseconds
 * @return true if event received
 */
bool events_receive_blocking(app_event_t *event, uint32_t timeout_ms);

/**
 * Get number of pending events
 */
int events_pending_count(void);

#endif // EVENTS_H
