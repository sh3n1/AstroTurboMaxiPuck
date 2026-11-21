// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// AstroTurboMaxiPuck firmware entry point.
//
// For now a bring-up check of the board: it logs the chip, flash and PSRAM, then tests the
// screen (red, green and blue, then the app's sky colour with its three reticle rings), logs
// every touch, and joins the Wi-Fi, showing the puck's address on screen.

#include <inttypes.h>

#include "board.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link_test.h"
#include "wifi.h"

static const char *TAG = "puck";

// The app's colours (src/app/core/theme.ts and sky-map/sky-style.ts in the app).
#define COLOR_SKY 0x05070F
#define COLOR_RETICLE 0xE5484D
#define COLOR_TEXT 0xE8EBF5

// The Wi-Fi and link status lines, near the bottom of the screen.
static lv_obj_t *status_label;
static lv_obj_t *link_label;

// The app's reticle: three plain rings, 0.5°, 2° and 4° in radius. For this test the
// puck's diameter shows 10° of sky; later the phone sends the real field of view.
#define TEST_FOV_DEG 10.0f
static const float RETICLE_RADII_DEG[] = {0.5f, 2.0f, 4.0f};

// Print the chip, flash and PSRAM details once at start-up.
static void log_board_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "Chip: ESP32-S3 rev %d.%d, %d cores",
             chip.revision / 100, chip.revision % 100, chip.cores);

    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Flash: %" PRIu32 " MB", flash_size / (1024 * 1024));
    }

    ESP_LOGI(TAG, "PSRAM: %u MB", (unsigned)(esp_psram_get_size() / (1024 * 1024)));
    ESP_LOGI(TAG, "Free heap: internal %u KB, PSRAM %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

// Fills the screen with one colour, for the colour test.
static void fill_screen(uint32_t color)
{
    lvgl_port_lock(0);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(color), 0);
    lvgl_port_unlock();
}

// Logs where the screen was touched.
static void on_touch(lv_event_t *event)
{
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    ESP_LOGI(TAG, "Touch at %d, %d", (int)point.x, (int)point.y);
}

// Draws one plain ring, centred on the screen.
static void draw_ring(lv_obj_t *screen, int radius_px)
{
    lv_obj_t *ring = lv_obj_create(screen);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, radius_px * 2, radius_px * 2);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(COLOR_RETICLE), 0);
    lv_obj_set_clickable(ring, false); // touches go to the screen behind
    lv_obj_center(ring);
}

// A line of status text, centred, this far from the bottom. Big text: the puck is read at
// arm's length in the dark.
static lv_obj_t *make_status_label(lv_obj_t *screen, int y_from_bottom)
{
    lv_obj_t *label = lv_label_create(screen);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, y_from_bottom);
    lv_label_set_text(label, "");
    return label;
}

// The sky colour with the reticle, and touches logged.
static void draw_test_screen(void)
{
    const float px_per_deg = BOARD_SCREEN_PX / TEST_FOV_DEG;

    lvgl_port_lock(0);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_SKY), 0);
    lv_obj_set_scrollable(screen, false);
    for (size_t i = 0; i < sizeof(RETICLE_RADII_DEG) / sizeof(RETICLE_RADII_DEG[0]); i++) {
        draw_ring(screen, (int)(RETICLE_RADII_DEG[i] * px_per_deg));
    }
    lv_obj_add_event_cb(screen, on_touch, LV_EVENT_PRESSED, NULL);

    status_label = make_status_label(screen, -60);
    link_label = make_status_label(screen, -100);
    lvgl_port_unlock();
}

// Shows the link test's state on screen.
static void on_link_state(const char *text)
{
    lvgl_port_lock(0);
    lv_label_set_text(link_label, text);
    lvgl_port_unlock();
}

// Shows the Wi-Fi state on screen, and starts the link test the first time it connects.
static void on_wifi_change(wifi_state_t state, const char *ip)
{
    static bool link_started = false;
    if (state == WIFI_CONNECTED && !link_started) {
        link_started = link_test_start(on_link_state) == ESP_OK;
    }

    lvgl_port_lock(0);
    switch (state) {
    case WIFI_NOT_SET:
        lv_label_set_text(status_label, "No Wi-Fi set");
        break;
    case WIFI_CONNECTING:
        lv_label_set_text(status_label, "Joining Wi-Fi...");
        break;
    case WIFI_CONNECTED:
        lv_label_set_text(status_label, ip);
        break;
    }
    lvgl_port_unlock();
}

void app_main(void)
{
    // After a reset the board's USB port takes about a second to reconnect to the computer,
    // and anything logged before that is lost. Wait, so the monitor sees the board info.
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "AstroTurboMaxiPuck starting");
    log_board_info();

    if (!board_start()) {
        ESP_LOGE(TAG, "Screen didn't start");
        return;
    }

    // Colour test: red, green, blue, half a second each, to check the colour order.
    const uint32_t test_colors[] = {0xFF0000, 0x00FF00, 0x0000FF};
    for (size_t i = 0; i < 3; i++) {
        fill_screen(test_colors[i]);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    draw_test_screen();
    ESP_LOGI(TAG, "Test screen drawn: touch it to log the position");

    if (wifi_start(on_wifi_change) != ESP_OK) {
        on_wifi_change(WIFI_NOT_SET, "");
    }
}
