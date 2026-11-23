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
#include "player.h"
#include "sky_common.h"
#include "texts.h"
#include "view_state.h"
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
// The bottom edge, while there is a target, as the top's: one line with its designations
// and name, then how far it is. A long name is cut, never the distance.
#define TARGET_SPAN_DEG 190 // up the sides, short of the top's readout (READOUT_SPAN_DEG)
#define TARGET_TEXT_PX (READOUT_RADIUS_PX * TARGET_SPAN_DEG * DEG_TO_RAD - 16)
#define TARGET_GAP "   "

static lv_obj_t *rings[RING_COUNT];
static lv_obj_t *readout;
static lv_obj_t *readout_band;
static lv_obj_t *edge_band, *edge_text;

static bool night;
static bool on_target;
static bool has_view, has_name, has_target;
static float outer_ring_px; // the reticle's outer ring, for taps

// A dark band along the edge, centred on centre_deg (LVGL's: 90 is the bottom, 270 the top).
static lv_obj_t *make_band(lv_obj_t *screen, int centre_deg, int span_deg, int width_px)
{
    lv_obj_t *band = lv_arc_create(screen);
    lv_obj_set_size(band, BOARD_SCREEN_PX, BOARD_SCREEN_PX);
    lv_obj_center(band);
    lv_arc_set_bg_angles(band, centre_deg - span_deg / 2 - 5, centre_deg + span_deg / 2 + 5);
    lv_obj_remove_style(band, NULL, LV_PART_KNOB);
    lv_obj_remove_style(band, NULL, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(band, width_px, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(band, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(band, lv_color_hex(COLOR_BACKING), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(band, READOUT_BAND_OPA, LV_PART_MAIN);
    lv_obj_set_style_arc_width(band, 0, LV_PART_INDICATOR);
    lv_obj_set_clickable(band, false);
    return band;
}

// Text along the bottom edge, read left to right, letters upright: counter-clockwise from
// the left. Too long, it ends in "...".
static lv_obj_t *make_bottom_text(lv_obj_t *screen, int radius_px)
{
    lv_obj_t *text = lv_arclabel_create(screen);
    lv_obj_set_size(text, BOARD_SCREEN_PX, BOARD_SCREEN_PX);
    lv_obj_set_style_text_font(text, &noto_sans_28, 0);
    lv_arclabel_set_radius(text, radius_px);
    // The arc still runs clockwise from its start, over the bottom; the letters go along it
    // from its far end, the left.
    lv_arclabel_set_angle_start(text, 90 - TARGET_SPAN_DEG / 2);
    lv_arclabel_set_angle_size(text, TARGET_SPAN_DEG);
    lv_arclabel_set_dir(text, LV_ARCLABEL_DIR_COUNTER_CLOCKWISE);
    lv_arclabel_set_text_horizontal_align(text, LV_ARCLABEL_TEXT_ALIGN_CENTER);
    lv_arclabel_set_text_vertical_align(text, LV_ARCLABEL_TEXT_ALIGN_CENTER);
    lv_arclabel_set_overflow(text, LV_ARCLABEL_OVERFLOW_ELLIPSIS);
    lv_arclabel_set_text(text, "");
    lv_obj_set_clickable(text, false);
    lv_obj_center(text);
    return text;
}

// How wide a UTF-8 text is in a font, in pixels; as many bytes as len.
static int text_width_px(const char *text, size_t len, const lv_font_t *font)
{
    int width = 0;
    const unsigned char *p = (const unsigned char *)text, *end = p + len;
    while (p < end) {
        uint32_t c = *p++;
        int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        c &= more == 3 ? 0x07 : more == 2 ? 0x0F : more == 1 ? 0x1F : 0x7F;
        while (more-- > 0 && p < end) c = c << 6 | (*p++ & 0x3F);
        width += lv_font_get_glyph_width(font, c, 0);
    }
    return width;
}

// The bottom edge's line: the names, cut on a whole character with "..." when the
// distance wouldn't fit after them, then the distance.
static void set_edge_text(const char *names, const char *place)
{
    const lv_font_t *font = &noto_sans_28;
    char line[VIEW_STATE_NAME_BYTES + TEXTS_READOUT_BYTES + 8];
    int tail_px = text_width_px(TARGET_GAP, strlen(TARGET_GAP), font) + text_width_px(place, strlen(place), font);
    size_t len = strlen(names);
    if (text_width_px(names, len, font) + tail_px > TARGET_TEXT_PX) {
        int dots_px = text_width_px("...", 3, font);
        while (len > 0 && text_width_px(names, len, font) + dots_px + tail_px > TARGET_TEXT_PX) {
            do len--;
            while (len > 0 && ((unsigned char)names[len] & 0xC0) == 0x80);
        }
        while (len > 0 && names[len - 1] == ' ') len--;
        snprintf(line, sizeof(line), "%.*s...%s%s", (int)len, names, TARGET_GAP, place);
    } else {
        snprintf(line, sizeof(line), "%s%s%s", names, place[0] ? TARGET_GAP : "", place);
    }
    lv_arclabel_set_text(edge_text, line);
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

    // The bottom edge: the band behind both lines, from the edge in.
    edge_band = make_band(screen, 90, TARGET_SPAN_DEG, READOUT_BAND_PX);
    edge_text = make_bottom_text(screen, READOUT_RADIUS_PX);

    screen_set_night(false);
    screen_hide();
}

void screen_hide(void)
{
    for (size_t i = 0; i < RING_COUNT; i++) lv_obj_set_hidden(rings[i], true);
    lv_obj_set_hidden(readout, true);
    lv_obj_set_hidden(readout_band, true);
    lv_obj_set_hidden(edge_band, true);
    lv_obj_set_hidden(edge_text, true);
    has_view = false;
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
    // The player over the map hides the readouts and the reticle; the zoom's buttons sit
    // where the bottom edge is.
    bool covered = player_active();
    lv_obj_set_hidden(readout, covered);
    lv_obj_set_hidden(readout_band, covered);

    // The eyepiece view has no reticle, as the app's: its circle is drawn with the sky.
    float ring_deg = (v->flags & PUCK_VIEW_FLAG_RING) ? eyepiece_field_deg : 0;
    if (v->fov_deg > 0 && v->fov_deg < 360) size_rings(v->fov_deg, ring_deg, !eyepiece && !covered);
    if (covered) outer_ring_px = 0;

    // The bottom edge: the names, then how far the target is, as the bottom third's distance.
    char away[16] = "";
    if (has_target) snprintf(away, sizeof(away), v->target_dist_deg < 1 ? "%.2f°" : "%.1f°", fmaxf(0, v->target_dist_deg));
    if (has_name) set_edge_text(target_name, away);
    bool edge = has_name && !covered && !zoom_active();
    lv_obj_set_hidden(edge_band, !edge);
    lv_obj_set_hidden(edge_text, !edge);
}

void screen_tick(bool solving)
{
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
    lv_obj_set_style_text_color(readout, text, 0);
    lv_obj_set_style_text_color(edge_text, text, 0);
    for (size_t i = 0; i < RING_COUNT; i++) lv_obj_set_style_border_color(rings[i], reticle_color(), 0);
}

bool screen_is_reticle(int x, int y)
{
    if (y > SOLVE_TAP_LOWEST_Y) return false;
    float radius = fminf(SOLVE_TAP_MAX_RADIUS_PX, fmaxf(SOLVE_TAP_MIN_RADIUS_PX, outer_ring_px));
    int dx = x - CENTRE_PX, dy = y - CENTRE_PX;
    return dx * dx + dy * dy < radius * radius;
}
