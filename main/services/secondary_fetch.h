/**
 * DayZ Server Tracker - Secondary Server Fetch Service
 * Background task that periodically fetches data for non-active servers
 */

#ifndef SECONDARY_FETCH_H
#define SECONDARY_FETCH_H

/**
 * Initialize the secondary fetch service
 * Creates the background task (but doesn't start fetching)
 */
void secondary_fetch_init(void);

/**
 * Start the secondary fetch task
 * Will begin periodic fetching of secondary server data
 */
void secondary_fetch_start(void);

/**
 * Stop the secondary fetch task
 * Use when switching servers or entering settings
 */
void secondary_fetch_stop(void);

/**
 * Request an immediate refresh of secondary servers
 * Call this when switching active server
 */
void secondary_fetch_refresh_now(void);

#endif // SECONDARY_FETCH_H
