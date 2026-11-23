// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The puck's own zoom: see zoom.h. The field and the buttons are only touched in the LVGL
// task; the rules come from the link task, so they are copied under a lock.

#include "zoom.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "fonts.h"
#include "freertos/FreeRTOS.h"
#include "icons.h"
#include "json_read.h"
#include "menu_chip.h"

static const char *TAG = "zoom";

#define MAX_STAR_LIMITS 8
#define BUTTON_PX 76

typedef struct {
    float min_deg, max_deg, step, start_deg;
    float limits[MAX_STAR_LIMITS][2]; // [field, magnitude], widest first
    int limit_count;
} rules_t;

// The app's own rules (sky-style.ts), until it sends them: they are the same.
static rules_t rules = {
    .min_deg = 5, .max_deg = 120, .step = 1.5f, .start_deg = 20,
    .limits = {{90, 5.5f}, {40, 6}, {0, 6.5f}}, .limit_count = 3,
};
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

static float field_deg;        // 0 until the rules give where to start
static float field_before;     // for ✗
static bool active;
static uint32_t revision;
static lv_obj_t *buttons[4];   // −, +, ✗, ✓

static rules_t current_rules(void)
{
    taskENTER_CRITICAL(&lock);
    rules_t r = rules;
    taskEXIT_CRITICAL(&lock);
    return r;
}

void zoom_set_rules(const char *json)
{
    // {"t":"mapRules","field":{"minDeg":5,"maxDeg":120,"step":1.5,"startDeg":20},
    //  "starLimits":[[90,5.5],[40,6],[0,6.5]]}
    rules_t r = current_rules();
    json_float(json, "minDeg", &r.min_deg);
    json_float(json, "maxDeg", &r.max_deg);
    json_float(json, "step", &r.step);
    json_float(json, "startDeg", &r.start_deg);
    const char *p = strstr(json, "\"starLimits\":[");
    if (p) {
        p += strlen("\"starLimits\":[");
        int n = 0;
        while (n < MAX_STAR_LIMITS && *p == '[') {
            char *end;
            float field = strtof(p + 1, &end);
            if (*end != ',') break;
            float mag = strtof(end + 1, &end);
            if (*end != ']') break;
            r.limits[n][0] = field;
            r.limits[n][1] = mag;
            n++;
            p = end + 1;
            if (*p == ',') p++;
        }
        if (n > 0) r.limit_count = n;
    }
    if (r.min_deg <= 0 || r.max_deg <= r.min_deg || r.step <= 1) return; // not sensible: keep ours
    taskENTER_CRITICAL(&lock);
    rules = r;
    taskEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Rules: %.0f to %.0f degrees, step %.2f", r.min_deg, r.max_deg, r.step);
}

static float clamp_field(const rules_t *r, float deg)
{
    return fmaxf(r->min_deg, fminf(r->max_deg, deg));
}

static void show_buttons(bool show)
{
    for (int i = 0; i < 4; i++) lv_obj_set_hidden(buttons[i], !show);
}

// −, +, ✗ and ✓, as the button's user data 0 to 3. A bubble hears every event: taps only.
static void on_button(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    int which = (int)(intptr_t)lv_event_get_user_data(event);
    rules_t r = current_rules();
    switch (which) {
    case 0: field_deg = clamp_field(&r, field_deg * r.step); break; // zoom out: a wider field
    case 1: field_deg = clamp_field(&r, field_deg / r.step); break; // zoom in
    case 2: // ✗: back to the field before, and close
        field_deg = field_before;
        active = false;
        show_buttons(false);
        break;
    case 3: // ✓: keep it, and close
        active = false;
        show_buttons(false);
        break;
    }
    revision++;
}

void zoom_create(lv_obj_t *screen)
{
    static const char *icons[4] = {ICON_REMOVE, ICON_ADD, ICON_CLOSE, ICON_CHECK};
    // − and + at the left and right edges, ✗ and ✓ low on either side, all within the circle.
    static const struct {
        lv_align_t align;
        int32_t x, y;
    } places[4] = {
        {LV_ALIGN_LEFT_MID, 20, 0},
        {LV_ALIGN_RIGHT_MID, -20, 0},
        {LV_ALIGN_BOTTOM_LEFT, 90, -60},
        {LV_ALIGN_BOTTOM_RIGHT, -90, -60},
    };
    // The menu's bubbles (menu_chip.c), which follow night mode with it: the icon bright on
    // a dark tint, and ✓ filled, as a bubble that is on.
    chip_styles_init();
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = chip_bubble(screen, BUTTON_PX, icons[i], on_button, (void *)(intptr_t)i);
        lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &material_symbols_32, 0);
        lv_obj_align(b, places[i].align, places[i].x, places[i].y);
        if (i == 3) chip_bubble_set_on(b, true);
        buttons[i] = b;
    }
    show_buttons(false);
}

void zoom_open(void)
{
    if (active) return;
    active = true;
    field_before = field_deg;
    show_buttons(true);
}

bool zoom_active(void)
{
    return active;
}

uint32_t zoom_revision(void)
{
    return revision;
}

void zoom_apply(puck_view_msg_t *view)
{
    if (view->flags & PUCK_VIEW_FLAG_EYEPIECE) return; // the eyepiece's field, as the phone's
    rules_t r = current_rules();
    if (field_deg <= 0) field_deg = clamp_field(&r, r.start_deg);
    view->fov_deg = field_deg;
    // The faintest stars for our field, by the app's table: the first whose field ours is at least.
    for (int i = 0; i < r.limit_count; i++) {
        if (field_deg >= r.limits[i][0]) {
            view->star_limit_mag = r.limits[i][1];
            return;
        }
    }
    if (r.limit_count > 0) view->star_limit_mag = r.limits[r.limit_count - 1][1];
}
