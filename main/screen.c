// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The puck's main screen: see screen.h. Sizes are the app's (draw-targets.ts, reticle.ts)
// in the puck's pixels, which are smaller than a phone's CSS pixels.

#include "screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "fonts.h"
#include "menu.h"
#include "link.h"
#include "sky_common.h"
#include "texts.h"
#include "zoom.h"

// The app's colours (theme.ts, sky-style.ts), and night's full-strength red on black.
#define COLOR_TEXT 0xE8EBF5
#define COLOR_RETICLE 0xE5484D
#define COLOR_ON_TARGET 0x6FCF9F
#define COLOR_GUIDE 0xA9CBFF
#define COLOR_BACKING 0x05070F
#define COLOR_BAR_BACK 0x1C2438
#define COLOR_NIGHT 0xFF0000
#define COLOR_NIGHT_BAR_BACK 0x400000

#define CENTRE_PX (BOARD_SCREEN_PX / 2)
#define DEG_TO_RAD 0.017453292f

// The reticle: three plain rings, 0.5°, 2° and 4° across (the app's RETICLE_RINGS_DEG), or
// in the ring view one, the eyepiece's field.
static const float RETICLE_DIAMETERS_DEG[] = {0.5f, 2.0f, 4.0f};
#define RING_COUNT (sizeof(RETICLE_DIAMETERS_DEG) / sizeof(RETICLE_DIAMETERS_DEG[0]))
#define RING_WIDTH_PX 4
// While plate solving the reticle fades out and back once this often, never below this.
#define PULSE_PERIOD_MS 1200
#define PULSE_MIN_OPACITY 0.25f
// A tap this near the centre, or inside the outer ring if bigger, plate solves; but never
// where the bottom third's controls are.
#define SOLVE_TAP_MIN_RADIUS_PX 90
#define SOLVE_TAP_MAX_RADIUS_PX 150
#define SOLVE_TAP_LOWEST_Y 270

// The top edge's readout, along the curve, over this much of it, on a dark band this wide at
// the edge so it reads over the map. The text sits on a circle this far from the centre
// and stands outwards from it.
#define READOUT_RADIUS_PX 198
#define READOUT_SPAN_DEG 150
#define READOUT_BAND_PX 40
#define READOUT_BAND_OPA LV_OPA_70

// The bottom third, from top to bottom (centres, from the screen's top). It shows after a
// tap on the map, and goes again after this long without one.
#define NAME_Y 282
#define NAME_WIDTH_PX 380
#define BUTTONS_Y 344
#define BUTTON_PX 76
#define BUTTON_GAP_PX 140
#define BAR_Y 406
#define BAR_WIDTH_PX 230
#define BAR_HEIGHT_PX 14
#define DISTANCE_Y 438
#define CONTROLS_SHOW_MS 15000
// The bar's scale: full at the target, empty this far away, by the logarithm of the distance,
// so the last few degrees still move it.
#define BAR_FAR_DEG 180.0f

static lv_obj_t *rings[RING_COUNT];
static lv_obj_t *readout;
static lv_obj_t *readout_band;
static lv_obj_t *name_label;
static lv_obj_t *buttons[3]; // previous, Menu, next
static lv_obj_t *button_labels[3];
static lv_obj_t *bar;
static lv_obj_t *distance_label;

static bool night;
static bool on_target;
static bool has_view, has_name, has_target;
static float outer_ring_px; // the reticle's outer ring, for taps
static bool controls_on;    // the bottom third shows
static uint32_t controls_since;

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, "");
    lv_obj_set_clickable(label, false);
    return label;
}

// Shows or hides the bottom third's parts, as it stands now.
static void place_controls(void)
{
    if (controls_on && lv_tick_elaps(controls_since) > CONTROLS_SHOW_MS) controls_on = false;
    bool shown = has_view && controls_on && !zoom_active();
    lv_obj_set_hidden(name_label, !shown || !has_name);
    for (int i = 0; i < 3; i++) lv_obj_set_hidden(buttons[i], !shown);
    lv_obj_set_hidden(bar, !shown || !has_target);
    lv_obj_set_hidden(distance_label, !shown || !has_target);
}

// The previous and next buttons send `step`; Menu opens the menu. Each keeps the bottom
// third showing a while longer.
static void on_button(lv_event_t *event)
{
    controls_since = lv_tick_get();
    switch ((intptr_t)lv_event_get_user_data(event)) {
    case 0:
        link_send_command("{\"t\":\"step\",\"by\":-1}");
        break;
    case 1:
        menu_open();
        break;
    case 2:
        link_send_command("{\"t\":\"step\",\"by\":1}");
        break;
    }
}

void screen_create(lv_obj_t *screen)
{
    for (size_t i = 0; i < RING_COUNT; i++) {
        lv_obj_t *ring = lv_obj_create(screen);
        lv_obj_remove_style_all(ring);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring, RING_WIDTH_PX, 0);
        lv_obj_set_clickable(ring, false); // taps go to the screen behind
        rings[i] = ring;
    }

    // Along the top edge, centred on its top, read left to right, over a dark band.
    readout_band = lv_arc_create(screen);
    lv_obj_set_size(readout_band, BOARD_SCREEN_PX, BOARD_SCREEN_PX);
    lv_obj_center(readout_band);
    lv_arc_set_bg_angles(readout_band, 270 - READOUT_SPAN_DEG / 2 - 5, 270 + READOUT_SPAN_DEG / 2 + 5);
    lv_obj_remove_style(readout_band, NULL, LV_PART_KNOB);
    lv_obj_remove_style(readout_band, NULL, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(readout_band, READOUT_BAND_PX, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(readout_band, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(readout_band, lv_color_hex(COLOR_BACKING), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(readout_band, READOUT_BAND_OPA, LV_PART_MAIN);
    lv_obj_set_style_arc_width(readout_band, 0, LV_PART_INDICATOR);
    lv_obj_set_clickable(readout_band, false);
    readout = lv_arclabel_create(screen);
    lv_obj_set_size(readout, BOARD_SCREEN_PX, BOARD_SCREEN_PX);
    lv_obj_set_style_text_font(readout, &noto_sans_28, 0);
    lv_arclabel_set_radius(readout, READOUT_RADIUS_PX);
    lv_arclabel_set_angle_start(readout, 270 - READOUT_SPAN_DEG / 2);
    lv_arclabel_set_angle_size(readout, READOUT_SPAN_DEG);
    lv_arclabel_set_dir(readout, LV_ARCLABEL_DIR_CLOCKWISE);
    lv_arclabel_set_text_horizontal_align(readout, LV_ARCLABEL_TEXT_ALIGN_CENTER);
    lv_arclabel_set_text_vertical_align(readout, LV_ARCLABEL_TEXT_ALIGN_TRAILING);
    lv_arclabel_set_text(readout, "");
    lv_obj_set_clickable(readout, false);
    lv_obj_center(readout);

    name_label = make_label(screen, &noto_sans_28);
    lv_obj_set_width(name_label, NAME_WIDTH_PX);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_MID, 0, NAME_Y - 16);

    // Symbols, not words: the firmware holds no translations.
    static const char *symbols[3] = {LV_SYMBOL_PREV, LV_SYMBOL_LIST, LV_SYMBOL_NEXT};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_button_create(screen);
        lv_obj_set_size(b, BUTTON_PX, BUTTON_PX);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_BACKING), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_70, 0);
        lv_obj_set_style_border_width(b, 3, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_align(b, LV_ALIGN_TOP_MID, (i - 1) * BUTTON_GAP_PX, BUTTONS_Y - BUTTON_PX / 2);
        lv_obj_add_event_cb(b, on_button, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        button_labels[i] = make_label(b, &lv_font_montserrat_28);
        lv_label_set_text(button_labels[i], symbols[i]);
        lv_obj_center(button_labels[i]);
        buttons[i] = b;
    }

    bar = lv_bar_create(screen);
    lv_obj_set_size(bar, BAR_WIDTH_PX, BAR_HEIGHT_PX);
    lv_bar_set_range(bar, 0, 1000);
    lv_obj_set_clickable(bar, false);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, BAR_Y - BAR_HEIGHT_PX / 2);

    distance_label = make_label(screen, &noto_sans_28);
    lv_obj_align(distance_label, LV_ALIGN_TOP_MID, 0, DISTANCE_Y - 16);

    screen_set_night(false);
    screen_hide();
}

void screen_hide(void)
{
    for (size_t i = 0; i < RING_COUNT; i++) lv_obj_set_hidden(rings[i], true);
    lv_obj_set_hidden(readout, true);
    lv_obj_set_hidden(readout_band, true);
    has_view = controls_on = false;
    place_controls();
    outer_ring_px = 0;
}

// The reticle's colour: green on target, red otherwise; night's red either way.
static lv_color_t reticle_color(void)
{
    return lv_color_hex(night ? COLOR_NIGHT : on_target ? COLOR_ON_TARGET : COLOR_RETICLE);
}

// Sizes the rings for a field of view, with the app's stereographic projection: a ring d
// across has radius 2 tan(d / 4) × scale. ring_deg: the ring view's one ring, or 0.
static void size_rings(float fov_deg, float ring_deg, bool shown)
{
    const float radius_px = BOARD_SCREEN_PX / 2.0f;
    const float scale = radius_px / (2.0f * tanf(fov_deg / 4.0f * DEG_TO_RAD));
    outer_ring_px = 0;
    for (size_t i = 0; i < RING_COUNT; i++) {
        float diameter = ring_deg > 0 ? ring_deg : RETICLE_DIAMETERS_DEG[i];
        int r = (int)(2.0f * tanf(diameter / 4.0f * DEG_TO_RAD) * scale);
        bool visible = shown && (ring_deg > 0 ? i == 0 : true) && r >= 2 && r <= radius_px;
        lv_obj_set_hidden(rings[i], !visible);
        if (visible) {
            lv_obj_set_size(rings[i], r * 2, r * 2);
            lv_obj_center(rings[i]);
            lv_obj_set_style_border_color(rings[i], reticle_color(), 0);
            if (r > outer_ring_px) outer_ring_px = r;
        }
    }
}

void screen_show(const puck_view_msg_t *v, const char *target_name, float eyepiece_field_deg)
{
    bool eyepiece = v->flags & PUCK_VIEW_FLAG_EYEPIECE;
    has_view = true;
    has_name = target_name[0] != '\0';
    has_target = has_name && !isnan(v->target_dist_deg);
    on_target = sky_on_target(v, eyepiece_field_deg);

    char alt_az[TEXTS_READOUT_BYTES], field[TEXTS_READOUT_BYTES], text[2 * TEXTS_READOUT_BYTES + 8];
    texts_alt_az(v->alt_deg, v->az_deg, alt_az);
    texts_field(v->fov_deg, field);
    snprintf(text, sizeof(text), "%s   %s", alt_az, field);
    lv_arclabel_set_text(readout, text);
    lv_obj_set_hidden(readout, false);
    lv_obj_set_hidden(readout_band, false);

    // The eyepiece view has no reticle, as the app's: its circle is drawn with the sky.
    float ring_deg = (v->flags & PUCK_VIEW_FLAG_RING) ? eyepiece_field_deg : 0;
    if (v->fov_deg > 0 && v->fov_deg < 360) size_rings(v->fov_deg, ring_deg, !eyepiece);

    lv_label_set_text(name_label, target_name);
    if (has_target) {
        float d = fmaxf(0, v->target_dist_deg);
        float close = on_target ? 1 : 1 - logf(1 + d) / logf(1 + BAR_FAR_DEG);
        lv_bar_set_value(bar, (int32_t)(fmaxf(0, fminf(1, close)) * 1000), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(night ? COLOR_NIGHT : on_target ? COLOR_ON_TARGET : COLOR_GUIDE),
                                  LV_PART_INDICATOR);
        snprintf(text, sizeof(text), d < 1 ? "%.2f°" : "%.1f°", d);
        lv_label_set_text(distance_label, text);
    }
    place_controls();
}

void screen_tick(bool solving)
{
    // The bottom third goes after a while without a tap.
    if (controls_on && lv_tick_elaps(controls_since) > CONTROLS_SHOW_MS) place_controls();

    static lv_opa_t shown = LV_OPA_COVER;
    float opacity = 1;
    if (solving) {
        float wave = 0.5f + 0.5f * cosf(2 * (float)M_PI * (lv_tick_get() % PULSE_PERIOD_MS) / PULSE_PERIOD_MS);
        opacity = PULSE_MIN_OPACITY + (1 - PULSE_MIN_OPACITY) * wave;
    }
    lv_opa_t opa = (lv_opa_t)(opacity * LV_OPA_COVER);
    if (opa == shown) return;
    shown = opa;
    for (size_t i = 0; i < RING_COUNT; i++) lv_obj_set_style_opa(rings[i], opa, 0);
}

void screen_set_night(bool is_night)
{
    night = is_night;
    lv_color_t text = lv_color_hex(night ? COLOR_NIGHT : COLOR_TEXT);
    lv_obj_t *labels[] = {name_label, distance_label, button_labels[0], button_labels[1], button_labels[2]};
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) lv_obj_set_style_text_color(labels[i], text, 0);
    lv_obj_set_style_text_color(readout, text, 0);
    for (int i = 0; i < 3; i++) lv_obj_set_style_border_color(buttons[i], text, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(night ? COLOR_NIGHT_BAR_BACK : COLOR_BAR_BACK), LV_PART_MAIN);
    for (size_t i = 0; i < RING_COUNT; i++) lv_obj_set_style_border_color(rings[i], reticle_color(), 0);
}

bool screen_is_reticle(int x, int y)
{
    if (y > SOLVE_TAP_LOWEST_Y) return false;
    float radius = fminf(SOLVE_TAP_MAX_RADIUS_PX, fmaxf(SOLVE_TAP_MIN_RADIUS_PX, outer_ring_px));
    int dx = x - CENTRE_PX, dy = y - CENTRE_PX;
    return dx * dx + dy * dy < radius * radius;
}

void screen_touched(void)
{
    controls_on = true;
    controls_since = lv_tick_get();
    place_controls();
}
