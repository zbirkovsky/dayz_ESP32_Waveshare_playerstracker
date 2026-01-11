/**
 * DayZ Server Tracker v2.0
 * For Waveshare ESP32-S3-Touch-LCD-7 (800x480)
 *
 * Refactored architecture with modular components:
 * - config.h: All constants and pin definitions
 * - app_state: Centralized state management
 * - events: Event queue for UI/logic decoupling
 * - drivers: Hardware abstraction (buzzer, sd_card, display)
 * - services: Business logic (battlemetrics, wifi, settings, history)
 * - ui: Styles and widget factories
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"

// Application modules
#include "config.h"
#include "app_state.h"
#include "events.h"
#include "drivers/buzzer.h"
#include "drivers/sd_card.h"
#include "drivers/display.h"
#include "drivers/io_expander.h"
#include "services/wifi_manager.h"
#include "services/battlemetrics.h"
#include "services/settings_store.h"
#include "services/history_store.h"
#include "services/secondary_fetch.h"
#include "services/restart_manager.h"
#include "services/alert_manager.h"
#include "services/server_query.h"
#include "ui/ui_styles.h"
#include "ui/ui_widgets.h"
#include "ui/screen_history.h"
#include "ui/ui_main.h"
#include "ui/ui_callbacks.h"
#include "drivers/usb_msc.h"
#include "power/screensaver.h"
#include "events/event_handler.h"
#include "app_init.h"
#include "ui/ui_context.h"
#include "ui/screen_builder.h"
#include "ui/ui_update.h"

static const char *TAG = "main";

// ============== UI WIDGET ACCESS ==============
#define screen_main (ui_context_get()->screen_main)


// ============== MAIN ==============

void app_main(void) {
    // Phase 1: System initialization (NVS, state, events, settings, buzzer, history)
    if (app_init_system()) {
        return;  // USB mass storage mode entered
    }

    // Phase 2: Display initialization (LVGL, touch, SD card, history loading)
    lv_display_t *disp = app_init_display();
    if (!disp) {
        return;
    }

    // Initialize UI context (holds all widget pointers)
    ui_context_init();

    // Phase 3: Create and show main screen
    if (lvgl_port_lock(1000)) {
        screen_builder_create_main();
        lv_screen_load(screen_main);
        lvgl_port_unlock();
    }

    // Initialize screensaver module
    screensaver_init();

    // Phase 4: Network initialization (WiFi, time sync, secondary fetch)
    app_init_network();

    // Give LVGL task time to process before starting main loop
    vTaskDelay(pdMS_TO_TICKS(500));

    // Get state reference for main loop
    app_state_t *state = app_state_get();

    // Initial UI update
    ui_update_main();
    ui_update_sd_status();
    ui_update_secondary();

    // Main loop
    while (1) {
        // Yield to other tasks (especially LVGL) at start of each loop
        vTaskDelay(pdMS_TO_TICKS(10));

        // Process any pending events
        event_handler_process();

        // Query server status (on main screen OR during screensaver for background data)
        if (app_state_get_current_screen() == SCREEN_MAIN || screensaver_is_active()) {
            server_query_execute();
            // Yield after HTTP request to let LVGL process
            vTaskDelay(pdMS_TO_TICKS(50));
            // Only update UI if not in screensaver mode
            if (!screensaver_is_active()) {
                ui_update_main();
                ui_update_secondary();  // Also update secondary servers display
                ui_update_sd_status();
            }
        }

        // Wait for refresh interval
        int wait_cycles = (state->settings.refresh_interval_sec * 1000) / 100;
        for (int i = 0; i < wait_cycles; i++) {
            // Process events during wait
            event_handler_process();

            // Check for manual refresh
            if (app_state_consume_refresh_request()) {
                ESP_LOGI(TAG, "Manual refresh triggered");
                break;
            }

            // Auto-hide alerts
            alert_check_auto_hide();

            // Screensaver and touch handling
            screensaver_tick();

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
