/**
 * DayZ Server Tracker - Event Handler
 * Processes events from the event queue and dispatches UI/state changes
 */

#ifndef EVENT_HANDLER_H
#define EVENT_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Process all pending events in the event queue
 * Should be called regularly from the main loop
 */
void event_handler_process(void);

/**
 * Block until an event arrives (or timeout), then process all pending events.
 * Returns immediately when an event is available.
 * @param timeout_ms Max time to wait if no events pending
 */
void event_handler_process_blocking(uint32_t timeout_ms);

/**
 * Check if there is deferred heavy work pending (history I/O, NVS save)
 */
bool event_handler_has_deferred(void);

/**
 * Process deferred heavy work (call after yielding to let LVGL render)
 */
void event_handler_process_deferred(void);

#endif // EVENT_HANDLER_H
