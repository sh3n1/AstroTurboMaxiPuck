// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Guided alignment, as the app's Align page: see align.h.

#include "align.h"

#include <string.h>

#include "board.h"
#include "esp_timer.h"
#include "fonts.h"
#include "icons.h"
#include "freertos/FreeRTOS.h"
#include "json_read.h"
#include "link.h"

// The big round button in the middle, as the Align page's, found by feel while looking in
// the eyepiece; ✗ small at the bottom; what to do at the top, in the phone's words.
#define BIG_PX 144
#define RING_GAP_PX 10
#define CLOSE_PX 76
#define CLOSE_Y_PX (-14)
#define CLEAR_PX 12 // between the ring and the close button, so they never touch
// The ring's middle: its bottom CLEAR_PX above the close button's top.
#define BIG_Y_PX (BOARD_SCREEN_PX / 2 + CLOSE_Y_PX - CLOSE_PX - CLEAR_PX - BIG_PX / 2 - RING_GAP_PX)
#define HEADING_Y_PX 44
#define HEADING_WIDTH_PX 290
#define DETAIL_WIDTH_PX 340
#define TEXT_BYTES 160
#define DONE_SHOWN_US (4 * 1000 * 1000) // once aligned, the result stays this long

// The app's colours (theme.ts, menu_chip.c): light blue to act, green once aligned; night red.
#define COLOR_PAGE 0x05070F
#define COLOR_TEXT 0xE8EBF5
#define COLOR_ACCENT 0xA9CBFF
#define COLOR_GOOD 0x6FCF9F
#define COLOR_INK 0x05070F
#define COLOR_DIM 0x1C2438
#define NIGHT_RED 0xFF0000
#define NIGHT_DIM 0x2A0000

typedef enum { STEP_NONE, STEP_CENTRE, STEP_SOLVING, STEP_FAILED, STEP_DONE } step_t;

static lv_obj_t *page, *heading, *detail, *ring, *big, *big_icon, *big_label, *close_button;
static bool active, night;
static step_t shown_step = STEP_NONE;
static int64_t done_at;

// The newest status, from the link task, and how many have come.
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static step_t status_step;
static char status_text[TEXT_BYTES], status_detail[TEXT_BYTES], status_button[TEXT_BYTES];
static unsigned status_count;

static void on_close(lv_event_t *event)
{
    align_close();
}

static void on_big(lv_event_t *event)
{
    if (shown_step == STEP_CENTRE || shown_step == STEP_FAILED) {
        // The phone answers "solving", which dims this button until the solve ends.
        link_send_command("{\"t\":\"alignNow\"}");
    } else if (shown_step == STEP_DONE) {
        align_close();
    }
}

// Colours for the step shown: the disc filled to act, dim while solving, green when done.
static void paint(void)
{
    if (!page) return;
    uint32_t text = night ? NIGHT_RED : COLOR_TEXT;
    uint32_t accent = night ? NIGHT_RED : shown_step == STEP_DONE ? COLOR_GOOD : COLOR_ACCENT;
    bool solving = shown_step == STEP_SOLVING;
    lv_obj_set_style_bg_color(page, lv_color_hex(night ? 0x000000 : COLOR_PAGE), 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(text), 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(text), 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(accent), 0);
    lv_obj_set_style_border_opa(ring, solving ? LV_OPA_40 : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(big, lv_color_hex(solving ? (night ? NIGHT_DIM : COLOR_DIM) : accent), 0);
    // On the filled disc, dark ink; in night mode black on red, as full-strength red text can't sit on red.
    uint32_t ink = solving ? text : (night ? 0x000000 : COLOR_INK);
    lv_obj_set_style_text_color(big_icon, lv_color_hex(ink), 0);
    lv_obj_set_style_text_color(big_label, lv_color_hex(ink), 0);
    lv_obj_set_style_border_color(close_button, lv_color_hex(text), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(close_button, 0), lv_color_hex(text), 0);
}

static lv_obj_t *text_label(lv_obj_t *parent, const lv_font_t *font, int width)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, "");
    return label;
}

static lv_obj_t *round_obj(lv_obj_t *parent, int size)
{
    lv_obj_t *o = lv_button_create(parent);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

void align_create(lv_obj_t *screen)
{
    // A page over the map, as the app's Align page replaces its map.
    page = lv_obj_create(screen);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_clickable(page, true); // taps stay on it, not on the map
    lv_obj_set_scrollable(page, false);

    heading = text_label(page, &noto_sans_28, HEADING_WIDTH_PX);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, HEADING_Y_PX);
    detail = text_label(page, &noto_sans_20, DETAIL_WIDTH_PX);
    lv_obj_align_to(detail, heading, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    // One thin ring around a filled disc, like the reticle's rings and the Align page's button.
    ring = lv_obj_create(page);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, BIG_PX + 2 * RING_GAP_PX, BIG_PX + 2 * RING_GAP_PX);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, BIG_Y_PX);
    lv_obj_set_clickable(ring, false);

    big = round_obj(page, BIG_PX);
    lv_obj_align(big, LV_ALIGN_CENTER, 0, BIG_Y_PX);
    lv_obj_add_event_cb(big, on_big, LV_EVENT_CLICKED, NULL);
    lv_obj_set_flex_flow(big, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(big, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(big, 4, 0);
    big_icon = lv_label_create(big);
    lv_obj_set_style_text_font(big_icon, &material_symbols_48, 0);
    lv_label_set_text(big_icon, ICON_ALIGNMENT);
    big_label = lv_label_create(big);
    lv_obj_set_style_text_font(big_label, &noto_sans_28, 0);
    lv_label_set_text(big_label, "");

    close_button = round_obj(page, CLOSE_PX);
    lv_obj_set_style_bg_opa(close_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(close_button, 3, 0);
    lv_obj_align(close_button, LV_ALIGN_BOTTOM_MID, 0, CLOSE_Y_PX);
    lv_obj_add_event_cb(close_button, on_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *x = lv_label_create(close_button);
    lv_obj_set_style_text_font(x, &material_symbols_32, 0);
    lv_label_set_text(x, ICON_CLOSE);
    lv_obj_center(x);

    paint();
    lv_obj_set_hidden(page, true);
}

void align_set_night(bool on)
{
    night = on;
    paint();
}

void align_open(void)
{
    // The words come with the phone's answer ("Centre Vega").
    lv_label_set_text(heading, "");
    lv_label_set_text(detail, "");
    lv_label_set_text(big_label, "");
    lv_obj_set_hidden(big, true);
    lv_obj_set_hidden(ring, true);
    shown_step = STEP_NONE;
    done_at = 0;
    active = true;
    lv_obj_set_hidden(page, false);
}

bool align_active(void)
{
    return active;
}

void align_close(void)
{
    active = false;
    done_at = 0;
    if (page) lv_obj_set_hidden(page, true);
}

void align_set_status(const char *json)
{
    char step[12] = "", text[TEXT_BYTES] = "", more[TEXT_BYTES] = "", button[TEXT_BYTES] = "";
    json_string(json, "step", step, sizeof(step));
    json_string(json, "text", text, sizeof(text));
    json_string(json, "detail", more, sizeof(more));
    json_string(json, "button", button, sizeof(button));
    step_t s = strcmp(step, "centre") == 0    ? STEP_CENTRE
               : strcmp(step, "solving") == 0 ? STEP_SOLVING
               : strcmp(step, "failed") == 0  ? STEP_FAILED
               : strcmp(step, "done") == 0    ? STEP_DONE
                                              : STEP_NONE;
    taskENTER_CRITICAL(&lock);
    status_step = s;
    strcpy(status_text, text);
    strcpy(status_detail, more);
    strcpy(status_button, button);
    status_count++;
    taskEXIT_CRITICAL(&lock);
}

void align_update(void)
{
    static unsigned shown_count;
    if (done_at && esp_timer_get_time() - done_at > DONE_SHOWN_US) align_close(); // back to the map
    if (status_count == shown_count) return;
    static char text[TEXT_BYTES], more[TEXT_BYTES], button[TEXT_BYTES];
    step_t step;
    taskENTER_CRITICAL(&lock);
    shown_count = status_count;
    step = status_step;
    strcpy(text, status_text);
    strcpy(more, status_detail);
    strcpy(button, status_button);
    taskEXIT_CRITICAL(&lock);
    if (!active || step == STEP_NONE) return;
    shown_step = step;
    lv_label_set_text(heading, text);
    lv_label_set_text(detail, more);
    lv_obj_align_to(detail, heading, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
    lv_label_set_text(big_label, button);
    // The Align page's icons: the crosshair to align or retry, a tick once aligned.
    bool done = step == STEP_DONE;
    lv_obj_set_style_text_font(big_icon, done ? &material_symbols_32 : &material_symbols_48, 0);
    lv_label_set_text(big_icon, done ? ICON_CHECK : ICON_ALIGNMENT);
    lv_obj_set_hidden(big, false);
    lv_obj_set_hidden(ring, false);
    paint();
    done_at = done ? esp_timer_get_time() : 0;
}
