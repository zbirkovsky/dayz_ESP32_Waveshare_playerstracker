/**
 * DayZ Server Tracker - Map Background Implementation
 */

#include "map_background.h"
#include "esp_log.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <errno.h>

static const char *TAG = "map_bg";

// Widget references (set by init)
static lv_obj_t *img_map_bg = NULL;
static lv_obj_t *bg_overlay = NULL;
static char current_map_bg[32] = "";

const char* map_format_name(const char *raw_map) {
    if (!raw_map || raw_map[0] == '\0') return "";

    // Common DayZ maps
    if (strcasecmp(raw_map, "chernarusplus") == 0) return "Chernarus";
    if (strcasecmp(raw_map, "enoch") == 0) return "Livonia";
    if (strcasecmp(raw_map, "sakhal") == 0) return "Sakhal";
    if (strcasecmp(raw_map, "deerisle") == 0) return "Deer Isle";
    if (strcasecmp(raw_map, "namalsk") == 0) return "Namalsk";
    if (strcasecmp(raw_map, "esseker") == 0) return "Esseker";
    if (strcasecmp(raw_map, "takistan") == 0) return "Takistan";
    if (strcasecmp(raw_map, "chiemsee") == 0) return "Chiemsee";
    if (strcasecmp(raw_map, "rostow") == 0) return "Rostow";
    if (strcasecmp(raw_map, "valning") == 0) return "Valning";
    if (strcasecmp(raw_map, "banov") == 0) return "Banov";
    if (strcasecmp(raw_map, "iztek") == 0) return "Iztek";
    if (strcasecmp(raw_map, "melkart") == 0) return "Melkart";
    if (strcasecmp(raw_map, "exclusionzone") == 0) return "Exclusion Zone";
    if (strcasecmp(raw_map, "pripyat") == 0) return "Pripyat";

    // Return raw name if not recognized (capitalize first letter)
    static char formatted[32];
    strncpy(formatted, raw_map, sizeof(formatted) - 1);
    formatted[sizeof(formatted) - 1] = '\0';
    if (formatted[0] >= 'a' && formatted[0] <= 'z') {
        formatted[0] -= 32;  // Capitalize
    }
    return formatted;
}

void map_background_init(lv_obj_t *img_widget, lv_obj_t *overlay_widget) {
    img_map_bg = img_widget;
    bg_overlay = overlay_widget;
    current_map_bg[0] = '\0';
}

void map_background_load(const char *map_name) {
    if (!img_map_bg || !map_name) {
        ESP_LOGW(TAG, "load: widget=%p, map=%s", img_map_bg, map_name ? map_name : "NULL");
        return;
    }

    // Skip if same map already loaded
    if (strcmp(current_map_bg, map_name) == 0) return;

    // Build path: S:/sdcard/maps/<mapname>.png
    // 'S' is the LVGL POSIX filesystem drive letter (ASCII 83)
    static char img_path[64];
    snprintf(img_path, sizeof(img_path), "S:/sdcard/maps/%s.png", map_name);

    // Check if file exists using standard POSIX (skip the 'S:' prefix)
    const char *posix_path = img_path + 2;  // Skip "S:" prefix
    ESP_LOGI(TAG, "Trying to load map: %s (POSIX: %s)", img_path, posix_path);

    FILE *f = fopen(posix_path, "r");
    if (f) {
        fclose(f);
        lv_img_set_src(img_map_bg, img_path);
        lv_obj_clear_flag(img_map_bg, LV_OBJ_FLAG_HIDDEN);
        if (bg_overlay) {
            lv_obj_clear_flag(bg_overlay, LV_OBJ_FLAG_HIDDEN);
        }
        strncpy(current_map_bg, map_name, sizeof(current_map_bg) - 1);
        current_map_bg[sizeof(current_map_bg) - 1] = '\0';
        ESP_LOGI(TAG, "Map background loaded: %s", img_path);
    } else {
        // No image for this map - hide background
        lv_obj_add_flag(img_map_bg, LV_OBJ_FLAG_HIDDEN);
        if (bg_overlay) {
            lv_obj_add_flag(bg_overlay, LV_OBJ_FLAG_HIDDEN);
        }
        current_map_bg[0] = '\0';
        ESP_LOGW(TAG, "Map image not found: %s (errno=%d)", posix_path, errno);
    }
}

void map_background_clear(void) {
    if (img_map_bg) {
        lv_obj_add_flag(img_map_bg, LV_OBJ_FLAG_HIDDEN);
    }
    if (bg_overlay) {
        lv_obj_add_flag(bg_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    current_map_bg[0] = '\0';
}
