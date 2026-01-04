/**
 * DayZ Server Tracker - Display Driver Implementation
 */

#include "display.h"
#include "../config.h"
#include "driver/i2c.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *touch_indev = NULL;

// CH422G uses register address AS the I2C device address!
// Mode register: 0x24, Output register for pins 0-7: 0x38
#define CH422G_MODE_ADDR    0x24
#define CH422G_OUTPUT_ADDR  0x38
#define CH422G_MODE_OUTPUT  0x01    // Enable output mode for pins 0-7

// CH422G output state (shared with sd_card driver via extern if needed)
static uint8_t ch422g_state = 0xFF;

static esp_err_t ch422g_init_and_write(uint8_t output_state) {
    // Step 1: Configure CH422G mode register (address 0x24)
    uint8_t mode = CH422G_MODE_OUTPUT;
    esp_err_t ret = i2c_master_write_to_device(TOUCH_I2C_NUM, CH422G_MODE_ADDR, &mode, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "CH422G mode config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 2: Write output state to output register (address 0x38)
    ret = i2c_master_write_to_device(TOUCH_I2C_NUM, CH422G_OUTPUT_ADDR, &output_state, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "CH422G output write failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ch422g_state = output_state;
    return ESP_OK;
}

static esp_err_t reset_gt911_via_ch422g(void) {
    ESP_LOGI(TAG, "Resetting GT911 via CH422G...");

    // Assert reset (EXIO1 = LOW, keeps SD_CS/EXIO4 high, all others high for safety)
    uint8_t reset_low = CH422G_EXIO4_BIT | 0xFD;  // All bits high except bit 1 (EXIO1)
    esp_err_t ret = ch422g_init_and_write(reset_low);
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "GT911 reset asserted (EXIO1=0)");

    // Hold reset for 20ms (GT911 needs at least 10ms)
    vTaskDelay(pdMS_TO_TICKS(20));

    // Deassert reset (EXIO1 = HIGH)
    uint8_t reset_high = CH422G_EXIO4_BIT | CH422G_EXIO1_BIT | 0xFF;
    ret = ch422g_init_and_write(reset_high);
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "GT911 reset released (EXIO1=1)");

    // Wait for GT911 to boot (needs ~55ms minimum)
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "GT911 reset complete");
    return ESP_OK;
}

static void scan_i2c_bus(void) {
    ESP_LOGI(TAG, "Scanning I2C bus...");
    uint8_t devices_found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(TOUCH_I2C_NUM, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  Found device at 0x%02X", addr);
            devices_found++;
        }
    }
    ESP_LOGI(TAG, "I2C scan complete, found %d device(s)", devices_found);
}

esp_err_t display_init_touch(void) {
    ESP_LOGI(TAG, "Initializing touch controller (GT911)...");

    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_I2C_SDA,
        .scl_io_num = TOUCH_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_I2C_NUM, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_I2C_NUM, I2C_MODE_MASTER, 0, 0, 0));

    // Give I2C bus time to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));

    // Scan I2C bus to see what devices are present
    scan_i2c_bus();

    // Try to reset GT911 via CH422G (may not work if CH422G not present)
    reset_gt911_via_ch422g();

    // Try both GT911 addresses (0x5D default, 0x14 alternate)
    uint8_t gt911_addresses[] = {0x5D, 0x14};
    esp_err_t ret = ESP_FAIL;

    for (int addr_idx = 0; addr_idx < 2; addr_idx++) {
        ESP_LOGI(TAG, "Trying GT911 at address 0x%02X", gt911_addresses[addr_idx]);

        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = {
            .dev_addr = gt911_addresses[addr_idx],
            .control_phase_bytes = 1,
            .dc_bit_offset = 0,
            .lcd_cmd_bits = 16,
            .lcd_param_bits = 0,
            .flags = {
                .disable_control_phase = 1,
            },
        };

        ret = esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)TOUCH_I2C_NUM,
                                        &tp_io_config, &tp_io_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to create panel IO for address 0x%02X", gt911_addresses[addr_idx]);
            continue;
        }

        esp_lcd_touch_config_t tp_cfg = {
            .x_max = LCD_WIDTH,
            .y_max = LCD_HEIGHT,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = GPIO_NUM_NC,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
        };

        ret = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "GT911 initialized successfully at address 0x%02X", gt911_addresses[addr_idx]);
            break;
        }

        ESP_LOGW(TAG, "GT911 init failed at address 0x%02X", gt911_addresses[addr_idx]);
        // Clean up the panel IO handle before trying next address
        esp_lcd_panel_io_del(tp_io_handle);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch controller initialization failed at all addresses!");
        return ret;
    }

    ESP_LOGI(TAG, "Touch controller initialized successfully");
    return ESP_OK;
}

esp_err_t display_init_lcd(void) {
    ESP_LOGI(TAG, "Initializing LCD panel...");

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .timings = {
            .pclk_hz = 16000000,
            .h_res = LCD_WIDTH,
            .v_res = LCD_HEIGHT,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 16,
            .vsync_front_porch = 16,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,
        .bounce_buffer_size_px = 10 * LCD_WIDTH,
        .psram_trans_align = 64,
        .hsync_gpio_num = PIN_LCD_HSYNC,
        .vsync_gpio_num = PIN_LCD_VSYNC,
        .de_gpio_num = PIN_LCD_DE,
        .pclk_gpio_num = PIN_LCD_PCLK,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            PIN_LCD_B0, PIN_LCD_B1, PIN_LCD_B2, PIN_LCD_B3, PIN_LCD_B4,
            PIN_LCD_G0, PIN_LCD_G1, PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5,
            PIN_LCD_R0, PIN_LCD_R1, PIN_LCD_R2, PIN_LCD_R3, PIN_LCD_R4,
        },
        .flags.fb_in_psram = 1,
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    ESP_LOGI(TAG, "LCD panel initialized successfully");
    return ESP_OK;
}

lv_display_t* display_init_lvgl(void) {
    ESP_LOGI(TAG, "Initializing LVGL...");

    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = LVGL_TASK_PRIORITY,
        .task_stack = LVGL_TASK_STACK,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel_handle,
        .buffer_size = LCD_WIDTH * LCD_HEIGHT,
        .double_buffer = false,
        .hres = LCD_WIDTH,
        .vres = LCD_HEIGHT,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = false,
            .swap_bytes = false,
            .direct_mode = true,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };

    lvgl_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);

    // Add touch input device
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = touch_handle,
    };
    touch_indev = lvgl_port_add_touch(&touch_cfg);

    ESP_LOGI(TAG, "LVGL initialized successfully");
    return lvgl_disp;
}

lv_display_t* display_init(void) {
    ESP_LOGI(TAG, "Full display initialization...");

    if (display_init_lcd() != ESP_OK) {
        ESP_LOGE(TAG, "LCD init failed");
        return NULL;
    }

    if (display_init_touch() != ESP_OK) {
        ESP_LOGE(TAG, "Touch init failed");
        return NULL;
    }

    lv_display_t *disp = display_init_lvgl();
    if (!disp) {
        ESP_LOGE(TAG, "LVGL init failed");
        return NULL;
    }

    ESP_LOGI(TAG, "Display initialization complete");
    return disp;
}

lv_indev_t* display_get_touch_indev(void) {
    return touch_indev;
}

esp_lcd_panel_handle_t display_get_panel(void) {
    return panel_handle;
}

void display_set_backlight(bool on) {
    if (on) {
        ch422g_state |= CH422G_EXIO2_BIT;   // High = backlight ON
    } else {
        ch422g_state &= ~CH422G_EXIO2_BIT;  // Low = backlight OFF
    }
    ch422g_init_and_write(ch422g_state);
    ESP_LOGI(TAG, "Backlight %s", on ? "ON" : "OFF");
}
