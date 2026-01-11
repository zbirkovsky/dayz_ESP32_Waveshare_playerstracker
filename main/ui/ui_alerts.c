/**
 * DayZ Server Tracker - Alert UI Rendering Implementation
 */

#include "ui_alerts.h"
#include "config.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "ui_alerts";

// Static alert overlay widget
static lv_obj_t *alert_overlay = NULL;

void ui_alerts_init(void) {
    alert_overlay = NULL;
    ESP_LOGI(TAG, "Alert UI initialized");
}

void ui_alerts_show(const char *message, uint32_t color_hex) {
    if (!message) return;

    if (!lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "Failed to acquire LVGL lock for alert show");
        return;
    }

    // Delete existing overlay if present
    if (alert_overlay) {
        lv_obj_delete(alert_overlay);
        alert_overlay = NULL;
    }

    // Create new overlay on active screen
    alert_overlay = lv_obj_create(lv_screen_active());
    if (!alert_overlay) {
        ESP_LOGE(TAG, "Failed to create alert overlay");
        lvgl_port_unlock();
        return;
    }

    lv_obj_set_size(alert_overlay, LCD_WIDTH - 160, 60);
    lv_obj_align(alert_overlay, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(alert_overlay, lv_color_hex(color_hex), 0);
    lv_obj_set_style_bg_opa(alert_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(alert_overlay, 0, 0);
    lv_obj_set_style_radius(alert_overlay, 0, 0);
    lv_obj_clear_flag(alert_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(alert_overlay, LV_OBJ_FLAG_CLICKABLE);

    // Create label inside overlay
    lv_obj_t *lbl = lv_label_create(alert_overlay);
    lv_label_set_text(lbl, message);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Alert shown: %s", message);
}

void ui_alerts_hide(void) {
    if (!alert_overlay) return;

    if (!lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "Failed to acquire LVGL lock for alert hide");
        return;
    }

    if (alert_overlay) {
        lv_obj_delete(alert_overlay);
        alert_overlay = NULL;
    }

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Alert hidden");
}

bool ui_alerts_is_visible(void) {
    return alert_overlay != NULL;
}
