/**
 * DayZ Server Tracker - UI Context Implementation
 */

#include "ui_context.h"
#include <string.h>

// Global UI context singleton
static ui_context_t g_ui_context;

ui_context_t* ui_context_get(void) {
    return &g_ui_context;
}

void ui_context_init(void) {
    memset(&g_ui_context, 0, sizeof(ui_context_t));
}
