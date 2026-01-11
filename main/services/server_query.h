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

#endif // SERVER_QUERY_H
