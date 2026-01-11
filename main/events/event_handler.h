/**
 * DayZ Server Tracker - Event Handler
 * Processes events from the event queue and dispatches UI/state changes
 */

#ifndef EVENT_HANDLER_H
#define EVENT_HANDLER_H

/**
 * Process all pending events in the event queue
 * Should be called regularly from the main loop
 */
void event_handler_process(void);

#endif // EVENT_HANDLER_H
