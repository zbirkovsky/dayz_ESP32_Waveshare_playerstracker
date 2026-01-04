/**
 * DayZ Server Tracker - Buzzer Driver
 * Controls the active buzzer for alerts
 */

#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>

/**
 * Initialize the buzzer GPIO
 */
void buzzer_init(void);

/**
 * Check if buzzer is enabled and initialized
 */
bool buzzer_is_ready(void);

/**
 * Simple beep for specified duration
 * @param duration_ms Duration in milliseconds
 * @param frequency_hz Frequency (ignored for active buzzer)
 */
void buzzer_beep(int duration_ms, int frequency_hz);

/**
 * Play restart alert pattern (3 ascending beeps)
 */
void buzzer_alert_restart(void);

/**
 * Play threshold alert pattern (2 quick beeps)
 */
void buzzer_alert_threshold(void);

/**
 * Play startup test beeps
 */
void buzzer_test(void);

#endif // BUZZER_H
