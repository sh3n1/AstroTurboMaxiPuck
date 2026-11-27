// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// AstroTurboMaxiPuck firmware entry point.
//
// It logs the chip, flash and PSRAM, tests the screen's colours (red, green, blue), joins the
// Wi-Fi, showing the puck's address, then shows a QR code for the app to pair with. Once
// linked, the sky map follows the app's view, with the main screen over it (screen.c). A tap
// on the reticle plate solves; the menu's Night switches night mode, on the phone too; the
// screen follows the app's night mode.

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "brightness.h"
#include "catalog.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link.h"
#include "field_log.h"
#include "log_history.h"
#include "qr_view.h"
#include "menu.h"
#include "player.h"
#include "screen.h"
#include "screenshot.h"
#include "sky_draw.h"
#include "view_glide.h"
#include "view_state.h"
#include "wifi.h"
#include "zoom.h"
#include "align.h"
#include "lx200.h"
#include "nvs_flash.h"
#include "splash.h"

static const char *TAG = "puck";

// The app's colours (src/app/core/theme.ts and sky-map/sky-style.ts in the app).
#define COLOR_SKY 0x05070F
#define COLOR_TEXT 0xE8EBF5
// Night mode: full-strength red on black, no grey (the app's AGENTS.md).
#define COLOR_NIGHT 0xFF0000
#define COLOR_NIGHT_SKY 0x000000

// The Wi-Fi and link status lines, near the bottom of the screen.
static lv_obj_t *status_label;
static lv_obj_t *link_label;
// The pairing QR code, in the middle, shown until the app links.
static lv_obj_t *pairing_qr;
// Its brightness step (QR_BRIGHTNESS), and a QR code waiting for the screen to dim before
// it shows. Both in the LVGL task, or with LVGL locked.
static unsigned qr_step;
static bool qr_waiting;

// The QR code's size, quiet zone included: as big as fits inside the round screen with the
// status lines under it, for big squares a phone camera reads at arm's length.
#define QR_SIZE_PX 300
#define QR_RAISE_PX (-30)
// A QR code shows when the link drops, often at night: it always comes up red at the
// screen's lowest brightness, never a white flash. Each tap on the screen brightens it a
// step, through these brightnesses (of 255) in red, then the same in white, then back to
// the first. A dazzled phone camera reads a dimmer step better.
static const uint8_t QR_BRIGHTNESS[] = {10, 30, 90, 150, 255};
#define QR_STEPS_PER_COLOUR (sizeof(QR_BRIGHTNESS) / sizeof(QR_BRIGHTNESS[0]))
#define QR_STEPS (2 * QR_STEPS_PER_COLOUR)
#define BRIGHTNESS_STEP_MS 50
// The start-up screen's brightness (of 255), low too, whatever the menu's setting.
#define SPLASH_BRIGHTNESS 30

// How often the screen looks at the shared view state.
#define VIEW_REFRESH_MS 33
// The longest step a glide takes in one frame, in ms of time elapsed.
#define GLIDE_MAX_STEP_MS 100.0f

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

// The status lines' colour: red with a red QR code or the start-up screen, as with night
// mode; else the palette's.
// With LVGL locked.
static bool night_shown;

static void color_status(void)
{
    bool qr = pairing_qr && (qr_waiting || !lv_obj_is_hidden(pairing_qr));
    bool red = night_shown || splash_shown() || (qr && qr_step < QR_STEPS_PER_COLOUR);
    lv_color_t text = lv_color_hex(red ? COLOR_NIGHT : COLOR_TEXT);
    lv_obj_set_style_text_color(status_label, text, 0);
    lv_obj_set_style_text_color(link_label, text, 0);
}

// A short tap on the reticle plate solves (docs/puck.md, "Control messages"); anywhere else
// on the map it shows the player, whose buttons take their own taps (player.c). Runs in the LVGL task; the link sends the
// command from its own task.
static void on_tap(lv_event_t *event)
{
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    ESP_LOGI(TAG, "Tap at %d, %d", (int)point.x, (int)point.y);

    // Over a QR code, a tap brightens it a step (apply_brightness).
    if (pairing_qr && !lv_obj_is_hidden(pairing_qr)) {
        qr_step = (qr_step + 1) % QR_STEPS;
        qr_view_set_red(qr_step < QR_STEPS_PER_COLOUR);
        color_status();
        return;
    }

    view_state_t state;
    view_state_get(&state);
    // While zooming or aligning, only their own buttons do anything.
    if (!state.has_view || zoom_active() || align_active() || menu_active() || player_active()) return;
    if (screen_is_reticle(point.x, point.y)) {
        if (!state.solving) link_send_command("{\"t\":\"solveNow\",\"trigger\":\"tap\"}");
    } else {
        player_open(); // anywhere else on the map: the player shows
    }
}

// Night mode on or off, on the phone too (the menu's Night): the app answers with its state,
// which recolours the screen.
static void switch_night(void)
{
    view_state_t state;
    view_state_get(&state);
    link_send_command(state.night ? "{\"t\":\"night\",\"on\":false}" : "{\"t\":\"night\",\"on\":true}");
}


// Everything in the day's colours, or night's full-strength red.
static void apply_palette(bool night)
{
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(night ? COLOR_NIGHT_SKY : COLOR_SKY), 0);
    night_shown = night;
    color_status();
    screen_set_night(night);
    align_set_night(night);
    menu_set_night(night);
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

// Shows the newest view and state from the app. An LVGL timer: it runs in the LVGL task, on
// core 1, with LVGL already locked.
static void refresh_view(lv_timer_t *timer)
{
    static uint32_t shown_count;
    static char shown_target[VIEW_STATE_NAME_BYTES];
    static bool shown_view, shown_night;
    view_state_t state;
    view_state_get(&state);
    // Red until the phone says otherwise: from start-up, and from linking until the app's
    // state arrives, so a phone in night mode never sees a white line first.
    if (!state.has_state) state.night = true;
    bool night_changed = state.night != shown_night;
    if (night_changed) {
        shown_night = state.night;
        apply_palette(state.night);
    }
    // Plate solving: the reticle pulses until it ends, as the app's, and says when it failed.
    static uint32_t shown_failures;
    if (state.solve_failures != shown_failures) {
        shown_failures = state.solve_failures;
        screen_solve_failed();
    }
    screen_tick(state.solving);
    menu_show_state(&state);
    align_update();
    player_tick();
    player_set_text(state.target_list, state.target_name);
    // The menu covers the map, which waits; it closes when the link goes (once: it can still
    // be opened over the cable for a screenshot, screenshot.c).
    if (!state.has_view && shown_view) {
        menu_close();
        align_close();
        player_close();
    }
    if (menu_active()) return;
    static uint32_t shown_zoom, shown_player;
    // The view on the screen, gliding towards the newest (view_glide.c), and while it moves.
    static puck_view_msg_t glided;
    static bool gliding;
    static int64_t last_frame_us;
    if (!gliding && !night_changed && state.has_view == shown_view && state.view_count == shown_count &&
        strcmp(state.target_name, shown_target) == 0 && zoom_revision() == shown_zoom &&
        player_revision() == shown_player) {
        return; // nothing new
    }
    shown_view = state.has_view;
    shown_count = state.view_count;
    shown_zoom = zoom_revision();
    shown_player = player_revision();
    strcpy(shown_target, state.target_name);
    // The Wi-Fi and link lines show until the first view, where the bottom third goes.
    lv_obj_set_hidden(status_label, state.has_view);
    lv_obj_set_hidden(link_label, state.has_view);

    if (!state.has_view) {
        screen_hide();
        sky_draw_clear(state.night);
        glided.type = 0; // the next view is shown at once
        gliding = false;
        return;
    }
    // The puck's own field, not the phone's, except in the phone's eyepiece view (zoom.c).
    puck_view_msg_t newest = state.view;
    zoom_apply(&newest);
    // The time since the last frame, but no more than a few frames' worth: after a still
    // moment, a new view still glides in.
    int64_t now = esp_timer_get_time();
    float elapsed_ms = fminf((now - last_frame_us) / 1000.0f, GLIDE_MAX_STEP_MS);
    last_frame_us = now;
    gliding = view_glide_step(&glided, &newest, elapsed_ms);
    const puck_view_msg_t *v = &glided;
    sky_draw_frame(v, state.night, state.eyepiece_field_deg);
    screen_show(v, state.target_name, state.eyepiece_field_deg);
}

// The panel starts off and dark (board.c), so nothing shows before the start-up screen,
// not even the white its memory may hold: on, once LVGL has drawn and sent its first frame,
// in the LVGL task. The brightness rises after, from apply_brightness's next step.
static bool screen_on;

static void on_first_frame(lv_event_t *event)
{
    if (screen_on) return;
    screen_on = true;
    board_screen_on();
}

// The screen's brightness: the menu's setting, or the QR code's step while one shows. An
// LVGL timer, so it runs in the LVGL task, which also sends the screen its pixels: the
// brightness command never crosses a frame being sent.
static void apply_brightness(lv_timer_t *timer)
{
    static int shown = -1;
    if (!screen_on) return; // dark until the first frame is on the screen
    bool qr = pairing_qr && (qr_waiting || !lv_obj_is_hidden(pairing_qr));
    int level = qr               ? QR_BRIGHTNESS[qr_step % QR_STEPS_PER_COLOUR]
                : splash_shown() ? SPLASH_BRIGHTNESS
                                 : brightness_level();
    if (level != shown) {
        board_set_brightness((uint8_t)level);
        shown = level;
    }
    // A new QR code shows only now the screen is dim, so it never flashes.
    if (qr_waiting) {
        qr_waiting = false;
        lv_obj_set_hidden(pairing_qr, false);
    }
}

static void build_screen(void)
{
    lvgl_port_lock(0);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_SKY), 0);
    lv_obj_set_scrollable(screen, false);
    // The sky map first, so the main screen and QR code are drawn over it.
    sky_draw_create(screen);
    screen_create(screen);
    lv_obj_add_event_cb(screen, on_tap, LV_EVENT_SHORT_CLICKED, NULL);

    // The start-up screen over the map, under the status lines and the QR code.
    splash_create(screen);
    status_label = make_status_label(screen, -40);
    link_label = make_status_label(screen, -75);
    color_status();
    zoom_create(screen);
    align_create(screen);
    player_create(screen);
    lv_timer_create(refresh_view, VIEW_REFRESH_MS, NULL);

    // Dark squares on red or white, with the quiet zone around it (qr_view.c).
    // A little above the centre, clear of the status lines under it.
    pairing_qr = qr_view_create(screen, QR_SIZE_PX);
    if (pairing_qr) lv_obj_align(pairing_qr, LV_ALIGN_CENTER, 0, QR_RAISE_PX);
    lv_timer_create(apply_brightness, BRIGHTNESS_STEP_MS, NULL);
    menu_create(screen, switch_night); // last: over everything, the QR code too
    // The display on once LVGL has drawn and sent this screen (on_first_frame).
    lv_display_add_event_cb(lv_display_get_default(), on_first_frame, LV_EVENT_REFR_READY, NULL);
    lvgl_port_unlock();
}

// The pairing code last made, and whether another QR code shows instead: the one that joins
// the puck's own network, until a phone is on it. Both under LVGL's lock, as the link task
// and the Wi-Fi's write them.
static char pairing_text[160];
static bool pairing_after_loss;
static bool pairing_held;

// With LVGL locked.
static void show_qr(const char *text)
{
    if (!pairing_qr) return;
    // A QR code coming up starts at the first step, red and dim, shown once the screen is.
    if (lv_obj_is_hidden(pairing_qr) && !qr_waiting) {
        qr_step = 0;
        qr_view_set_red(true);
    }
    if (qr_view_show(text) && lv_obj_is_hidden(pairing_qr)) qr_waiting = true;
    splash_hide(); // the start-up screen until the first QR code
    color_status();
}

static void show_pairing_now(void)
{
    if (!pairing_text[0]) return;
    show_qr(pairing_text);
    lv_label_set_text(link_label, pairing_after_loss ? "Link lost: reconnect" : "Scan in the app");
}

// The link's callbacks (link.h), run in the link task.
static void show_pairing(const char *qr_text, bool after_loss)
{
    lvgl_port_lock(0);
    strlcpy(pairing_text, qr_text, sizeof(pairing_text));
    pairing_after_loss = after_loss;
    if (!pairing_held) show_pairing_now();
    lvgl_port_unlock();
}

// The QR code that joins the puck's own network, with what to do in words. With LVGL locked.
static void show_join_qr(const char *what)
{
    char text[96];
    wifi_join_qr(text, sizeof(text));
    show_qr(text);
    lv_label_set_text(link_label, what);
    lv_label_set_text(status_label, wifi_own_name());
}

static void show_link_state(const char *text)
{
    lvgl_port_lock(0);
    lv_label_set_text(link_label, text);
    if (strcmp(text, "Linked") == 0 && pairing_qr) {
        qr_waiting = false;
        lv_obj_set_hidden(pairing_qr, true);
        color_status();
    }
    lvgl_port_unlock();
}

// Shows the Wi-Fi state on screen, and starts the link the first time the puck has an
// address: on the user's network, or its own. Before any link, in English.
static void on_wifi_change(wifi_state_t state, const char *ip)
{
    static bool link_started = false;
    // The log over the Wi-Fi, in a field build: only with an address on the user's network.
    field_log_network_up(state == WIFI_CONNECTED);
    if ((state == WIFI_CONNECTED || state == WIFI_OWN_WAITING) && !link_started) {
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
    case WIFI_OWN_WAITING:
        // The phone joins the puck's network first; then the pairing code.
        pairing_held = true;
        show_join_qr("Join the puck's Wi-Fi");
        break;
    case WIFI_OWN_JOINED:
        pairing_held = false;
        lv_label_set_text(status_label, ip);
        show_pairing_now();
        break;
    case WIFI_SETUP_WAITING:
        show_join_qr("Wi-Fi setup: join it");
        break;
    case WIFI_SETUP_JOINED:
        // The setup page, for the phone now on the puck's network.
        show_qr("http://192.168.4.1/");
        lv_label_set_text(link_label, "Open the page");
        lv_label_set_text(status_label, ip);
        break;
    }
    lvgl_port_unlock();
}

void app_main(void)
{
    log_history_start();
    field_log_start(); // the log over the Wi-Fi, in a field build only
    // After a reset the board's USB port takes about a second to reconnect to the computer,
    // and anything logged before that is lost. Wait, so the monitor sees the board info.
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "AstroTurboMaxiPuck starting");
    log_board_info();
    // The sky catalogue kept from an earlier link, if any. Without one the app sends it.
    catalog_init();

    if (!board_start()) {
        ESP_LOGE(TAG, "Screen didn't start");
        return;
    }

    // The brightness kept in NVS before anything shows, so the start-up screen doesn't
    // dazzle in the dark. wifi_start makes NVS ready too, and erases it if it must.
    nvs_flash_init();
    brightness_load();
    build_screen();
    screenshot_start();

    if (wifi_start(on_wifi_change) != ESP_OK) {
        on_wifi_change(WIFI_NOT_SET, "");
    }
    lx200_start();
}
