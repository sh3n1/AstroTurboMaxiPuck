// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// AstroTurboMaxiPuck firmware entry point.
//
// For now a bring-up check of the board: it logs the chip, flash and PSRAM, then tests the
// screen (red, green and blue, then the app's sky colour with its three reticle rings), logs
// every touch, and joins the Wi-Fi, showing the puck's address on screen. Then it shows a QR
// code for the app to pair with, and once linked, what the app's view says: where the
// telescope points (Alt, Az), the field of view (the rings follow it), and the target.

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link.h"
#include "view_state.h"
#include "wifi.h"

static const char *TAG = "puck";

// The app's colours (src/app/core/theme.ts and sky-map/sky-style.ts in the app).
#define COLOR_SKY 0x05070F
#define COLOR_RETICLE 0xE5484D
#define COLOR_TEXT 0xE8EBF5

// The Wi-Fi and link status lines, near the bottom of the screen.
static lv_obj_t *status_label;
static lv_obj_t *link_label;
// The pairing QR code, in the middle, shown until the app links.
static lv_obj_t *pairing_qr;
// Where the telescope points and the field of view, near the top, once linked.
static lv_obj_t *pose_label;
static lv_obj_t *fov_label;
// The target's name and how far it is, above the status lines.
static lv_obj_t *target_label;

// The QR code's size: it fits well inside the round screen, with big enough squares for a
// phone camera at arm's length.
#define QR_SIZE_PX 280

// The app's reticle: three plain rings, 0.5°, 2° and 4° in radius. Until the app sends its
// field of view, the puck's diameter shows 10° of sky.
#define TEST_FOV_DEG 10.0f
static const float RETICLE_RADII_DEG[] = {0.5f, 2.0f, 4.0f};
#define RING_COUNT (sizeof(RETICLE_RADII_DEG) / sizeof(RETICLE_RADII_DEG[0]))
static lv_obj_t *rings[RING_COUNT];
// How often the screen looks at the shared view state.
#define VIEW_REFRESH_MS 33
#define DEG_TO_RAD 0.017453292f

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

// One plain ring, centred on the screen.
static lv_obj_t *make_ring(lv_obj_t *screen)
{
    lv_obj_t *ring = lv_obj_create(screen);
    lv_obj_remove_style_all(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(COLOR_RETICLE), 0);
    lv_obj_set_clickable(ring, false); // touches go to the screen behind
    return ring;
}

// Sizes the rings for a field of view, with the app's stereographic projection
// (docs/puck.md, "Drawing on the puck"): a ring of angle a has radius 2 tan(a / 2) x scale.
// Rings bigger than the screen, or too small to see, are hidden.
static void size_rings(float fov_deg)
{
    const float radius_px = BOARD_SCREEN_PX / 2.0f;
    const float scale = radius_px / (2.0f * tanf(fov_deg / 4.0f * DEG_TO_RAD));
    for (size_t i = 0; i < RING_COUNT; i++) {
        int r = (int)(2.0f * tanf(RETICLE_RADII_DEG[i] / 2.0f * DEG_TO_RAD) * scale);
        lv_obj_set_hidden(rings[i], r < 2 || r > radius_px);
        lv_obj_set_size(rings[i], r * 2, r * 2);
        lv_obj_center(rings[i]);
    }
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
// Shows the newest view from the app. An LVGL timer: it runs in the LVGL task, on core 1,
// with LVGL already locked.
static void refresh_view(lv_timer_t *timer)
{
    static uint32_t shown_count;
    static char shown_target[VIEW_STATE_NAME_BYTES];
    static bool shown_view;
    view_state_t state;
    view_state_get(&state);
    if (state.has_view == shown_view && state.view_count == shown_count &&
        strcmp(state.target_name, shown_target) == 0) {
        return; // nothing new
    }
    shown_view = state.has_view;
    shown_count = state.view_count;
    strcpy(shown_target, state.target_name);

    if (!state.has_view) {
        lv_label_set_text(pose_label, "");
        lv_label_set_text(fov_label, "");
        lv_label_set_text(target_label, "");
        size_rings(TEST_FOV_DEG);
        return;
    }
    const puck_view_msg_t *v = &state.view;
    char text[96];
    snprintf(text, sizeof(text), "Alt %.1f°  Az %.1f°", v->alt_deg, v->az_deg);
    lv_label_set_text(pose_label, text);
    snprintf(text, sizeof(text), "FOV %.1f°", v->fov_deg);
    lv_label_set_text(fov_label, text);
    if (isnan(v->target_dist_deg)) {
        lv_label_set_text(target_label, state.target_name);
    } else {
        snprintf(text, sizeof(text), "%s %.1f°", state.target_name, v->target_dist_deg);
        lv_label_set_text(target_label, text);
    }
    if (v->fov_deg > 0 && v->fov_deg < 360) size_rings(v->fov_deg);
}

static void draw_test_screen(void)
{
    lvgl_port_lock(0);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_SKY), 0);
    lv_obj_set_scrollable(screen, false);
    for (size_t i = 0; i < RING_COUNT; i++) rings[i] = make_ring(screen);
    size_rings(TEST_FOV_DEG);
    lv_obj_add_event_cb(screen, on_touch, LV_EVENT_PRESSED, NULL);

    status_label = make_status_label(screen, -40);
    link_label = make_status_label(screen, -75);
    target_label = make_status_label(screen, -115);
    pose_label = make_status_label(screen, 0);
    lv_obj_align(pose_label, LV_ALIGN_TOP_MID, 0, 50);
    fov_label = make_status_label(screen, 0);
    lv_obj_align(fov_label, LV_ALIGN_TOP_MID, 0, 88);
    lv_timer_create(refresh_view, VIEW_REFRESH_MS, NULL);

    // Dark squares on white, as cameras read best, with the quiet zone around it.
    pairing_qr = lv_qrcode_create(screen);
    lv_qrcode_set_size(pairing_qr, QR_SIZE_PX);
    lv_qrcode_set_dark_color(pairing_qr, lv_color_black());
    lv_qrcode_set_light_color(pairing_qr, lv_color_white());
    lv_qrcode_set_quiet_zone(pairing_qr, true);
    lv_obj_center(pairing_qr);
    lv_obj_set_hidden(pairing_qr, true);
    lvgl_port_unlock();
}

// The link's callbacks (link.h), run in the link task.
static void show_pairing(const char *qr_text)
{
    lvgl_port_lock(0);
    lv_qrcode_update(pairing_qr, qr_text, strlen(qr_text));
    lv_obj_set_hidden(pairing_qr, false);
    lv_label_set_text(link_label, "Scan in the app");
    lvgl_port_unlock();
}

static void show_link_state(const char *text)
{
    lvgl_port_lock(0);
    lv_label_set_text(link_label, text);
    if (strcmp(text, "Linked") == 0) lv_obj_set_hidden(pairing_qr, true);
    lvgl_port_unlock();
}

// Shows the Wi-Fi state on screen, and starts the link the first time it connects.
static void on_wifi_change(wifi_state_t state, const char *ip)
{
    static bool link_started = false;
    if (state == WIFI_CONNECTED && !link_started) {
        static const link_callbacks_t callbacks = {
            .show_pairing = show_pairing,
            .show_state = show_link_state,
        };
        link_started = link_start(&callbacks) == ESP_OK;
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
