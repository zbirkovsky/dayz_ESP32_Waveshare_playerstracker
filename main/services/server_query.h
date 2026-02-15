/**
 * DayZ Server Tracker - Server Query Service
 * Handles querying the BattleMetrics API and updating state
 */

#ifndef SERVER_QUERY_H
#define SERVER_QUERY_H

/**
 * Query the active server status from BattleMetrics API
 * Updates app state, history, alerts, and restart tracking
 */
void server_query_execute(void);

/**
 * Start the background server query task
 * Periodically calls server_query_execute() and posts EVT_DATA_UPDATED
 */
void server_query_task_start(void);

/**
 * Request an immediate refresh from the background task
 * Called when user presses the refresh button
 */
void server_query_request_refresh(void);

#endif // SERVER_QUERY_H
