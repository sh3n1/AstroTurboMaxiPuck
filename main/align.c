// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Guided alignment over the map: see align.h.

#include "align.h"

#include <string.h>

#include "esp_timer.h"
#include "fonts.h"
#include "freertos/FreeRTOS.h"
#include "icons.h"
#include "json_read.h"
#include "link.h"

// As the zoom's buttons (zoom.c), in the same places: ✗ low on the left, Align low on the right.
#define BUTTON_PX 76
#define BUTTON_X_PX 90
#define BUTTON_Y_PX (-60)
#define COLOR_BUTTON 0xE8EBF5
#define COLOR_NIGHT 0xFF0000
#define COLOR_BG 0x05070F
// The phone's words, above the reticle, on a dark band so the stars don't hide them.
#define TEXT_Y_PX 64
#define TEXT_WIDTH_PX 320
#define TEXT_BYTES 160
#define DONE_SHOWN_US (4 * 1000 * 1000) // once aligned, the words stay this long

typedef enum { STEP_NONE, STEP_CENTRE, STEP_SOLVING, STEP_FAILED, STEP_DONE } step_t;

static lv_obj_t *cancel_button, *align_button, *words;
static bool active;
static int64_t done_at;

// The newest status, from the link task, and how many have come.
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static step_t status_step;
static char status_text[TEXT_BYTES];
static unsigned status_count;

static void show(bool on)
{
    active = on;
    lv_obj_set_hidden(cancel_button, !on);
    lv_obj_set_hidden(align_button, !on);
    lv_obj_set_hidden(words, !on || lv_label_get_text(words)[0] == '\0');
}

static void on_cancel(lv_event_t *event)
{
    align_close();
}

static void on_align(lv_event_t *event)
{
    // The phone answers "solving", which hides this button until the solve ends.
    link_send_command("{\"t\":\"alignNow\"}");
}

static lv_obj_t *round_button(lv_obj_t *screen, const char *icon, lv_align_t align, int x, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(screen);
    lv_obj_set_size(b, BUTTON_PX, BUTTON_PX);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(b, 3, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_align(b, align, x, BUTTON_Y_PX);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(b);
    lv_obj_set_style_text_font(label, &material_symbols_32, 0);
    lv_label_set_text(label, icon);
    lv_obj_center(label);
    return b;
}

void align_create(lv_obj_t *screen)
{
    cancel_button = round_button(screen, ICON_CLOSE, LV_ALIGN_BOTTOM_LEFT, BUTTON_X_PX, on_cancel);
    align_button = round_button(screen, ICON_ALIGNMENT, LV_ALIGN_BOTTOM_RIGHT, -BUTTON_X_PX, on_align);
    words = lv_label_create(screen);
    lv_obj_set_width(words, TEXT_WIDTH_PX);
    lv_label_set_long_mode(words, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(words, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(words, &noto_sans_20, 0);
    lv_obj_set_style_bg_color(words, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(words, LV_OPA_80, 0);
    lv_obj_set_style_radius(words, 12, 0);
    lv_obj_set_style_pad_all(words, 8, 0);
    lv_obj_align(words, LV_ALIGN_TOP_MID, 0, TEXT_Y_PX);
    lv_label_set_text(words, "");
    align_set_night(false);
    show(false);
}

void align_set_night(bool night)
{
    lv_color_t color = lv_color_hex(night ? COLOR_NIGHT : COLOR_BUTTON);
    lv_obj_t *buttons[] = {cancel_button, align_button};
    for (int i = 0; i < 2; i++) {
        if (!buttons[i]) continue;
        lv_obj_set_style_border_color(buttons[i], color, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(buttons[i], 0), color, 0);
    }
    if (words) lv_obj_set_style_text_color(words, color, 0);
}

void align_open(void)
{
    // The words come with the phone's answer ("Centre Vega").
    lv_label_set_text(words, "");
    done_at = 0;
    show(true);
}

bool align_active(void)
{
    return active;
}

void align_close(void)
{
    if (cancel_button) show(false);
}

void align_set_status(const char *json)
{
    char step[12] = "", text[TEXT_BYTES] = "";
    json_string(json, "step", step, sizeof(step));
    json_string(json, "text", text, sizeof(text));
    step_t s = strcmp(step, "centre") == 0    ? STEP_CENTRE
               : strcmp(step, "solving") == 0 ? STEP_SOLVING
               : strcmp(step, "failed") == 0  ? STEP_FAILED
               : strcmp(step, "done") == 0    ? STEP_DONE
                                              : STEP_NONE;
    taskENTER_CRITICAL(&lock);
    status_step = s;
    strcpy(status_text, text);
    status_count++;
    taskEXIT_CRITICAL(&lock);
}

void align_update(void)
{
    static unsigned shown_count;
    if (done_at && esp_timer_get_time() - done_at > DONE_SHOWN_US) {
        done_at = 0;
        show(false); // aligned: back to the map alone
    }
    if (status_count == shown_count) return;
    step_t step;
    char text[TEXT_BYTES];
    taskENTER_CRITICAL(&lock);
    shown_count = status_count;
    step = status_step;
    strcpy(text, status_text);
    taskEXIT_CRITICAL(&lock);
    if (!active || step == STEP_NONE) return;
    lv_label_set_text(words, text);
    lv_obj_set_hidden(words, text[0] == '\0');
    // Align while centring, and again after a failure; not while solving, nor once aligned.
    lv_obj_set_hidden(align_button, step == STEP_SOLVING || step == STEP_DONE);
    if (step == STEP_DONE) done_at = esp_timer_get_time();
}
