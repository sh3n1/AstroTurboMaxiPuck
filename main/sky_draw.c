// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The sky map: see sky_draw.h. Each step follows the app's map (sky-canvas.component.ts,
// draw-sky.ts, sky-style.ts) so both show the same sky. In order: the sky's colour, the
// grid and constellation lines, the stars (sky_stars.c) and the Moon and planets (written
// straight into the canvas's pixels, sky_pixels.c), then the top layer of markers
// (sky_objects.c) and the labels (sky_labels.c).

#include "sky_draw.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "catalog.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "moving.h"
#include "sky_icons.h"
#include "sky_common.h"
#include "sky_labels.h"
#include "sky_objects.h"
#include "sky_pixels.h"
#include "sky_stars.h"

static const char *TAG = "sky";

#define SIZE_PX SKY_SIZE_PX

// The app's colours (sky-style.ts).
#define COLOR_LINE_DAY 0x2A4170
#define COLOR_LINE_NIGHT 0x4F6FAE
#define COLOR_GRID_DAY 0x141C2E
#define COLOR_GRID_NIGHT 0x2A3550
#define COLOR_EYEPIECE_EDGE 0x3A4158
// The alt-az grid (the app's drawGrid): circles at these altitudes, lines this far apart.
static const float GRID_ALTITUDES_DEG[] = {30, 60};
#define GRID_AZIMUTH_STEP_DEG 45
// The Moon in the text's near-white, not the planets' yellow.
#define COLOR_MOON 0xE8EBF5
// The Moon's kind number (docs/puck.md, "Kinds").
#define KIND_MOON 10
// A planet's icon disc, as the app's ICON_DISC_PX (7), in the puck's pixels.
#define ICON_DISC_PX (7 * 1.5f)
// The catalogue's layout (docs/puck.md, "Format").
#define SECTION_LINES 2
#define LINE_RECORD_BYTES 16

typedef struct {
    float a[3], b[3]; // J2000 unit vectors of the two ends
} segment_t;

static lv_obj_t *canvas;
static lv_draw_buf_t draw_buf;

// Our own copy of the lines, ready to turn: built once from the kept catalogue, so a new one
// being written to flash meanwhile doesn't matter.
static segment_t *segments;
static size_t segment_count;
static char loaded_id[CATALOG_ID_CHARS + 1];

// The Moon and planets this frame: their discs here, their labels in sky_objects.c.
static moving_object_t moving[MOVING_MAX];
static size_t moving_count;
static float moving_sun[3]; // the Sun's J2000 vector, which lit sides face

// ---- The catalogue ----

// Builds our copy of the lines (and, in sky_stars.c and sky_objects.c, of the stars, objects
// and labels)
// when a new catalogue has been kept.
static void load_catalog(void)
{
    const char *id = catalog_kept_id();
    if (!id || strcmp(id, loaded_id) == 0) return;
    size_t bytes;
    const uint8_t *data = catalog_data(&bytes);
    if (!data || bytes < 8 || sky_read_u32(data) != SKY_CATALOG_MAGIC) return;

    heap_caps_free(segments);
    segments = NULL;
    segment_count = 0;

    uint32_t n;
    const uint8_t *p = sky_find_section(data, bytes, SECTION_LINES, &n, NULL);
    if (p && (segments = heap_caps_malloc(n * sizeof(segment_t), MALLOC_CAP_SPIRAM))) {
        for (uint32_t i = 0; i < n; i++, p += LINE_RECORD_BYTES) {
            sky_radec_vector(sky_read_f32(p), sky_read_f32(p + 4), segments[i].a);
            sky_radec_vector(sky_read_f32(p + 8), sky_read_f32(p + 12), segments[i].b);
        }
        segment_count = n;
    }
    sky_stars_load(data, bytes);
    sky_objects_load(data, bytes);
    strcpy(loaded_id, id);
    size_t faint, star_count = sky_stars_count(&faint);
    ESP_LOGI(TAG, "Catalogue %s: %u stars, %u faint stars, %u line segments", id, (unsigned)star_count,
             (unsigned)faint, (unsigned)segment_count);
}

// ---- Drawing ----

// A curve through local directions at (alt, az) steps, as the app's drawCurve: a step that
// jumps across the screen is the curve passing behind the view, and isn't drawn.
static void draw_altaz_curve(const sky_frame_t *f, uint32_t color, float alt0, float az0, float alt_step,
                             float az_step, int steps)
{
    float last_x = 0, last_y = 0;
    bool last_ok = false;
    for (int i = 0; i <= steps; i++) {
        float alt = (alt0 + i * alt_step) * SKY_DEG_TO_RAD, az = (az0 + i * az_step) * SKY_DEG_TO_RAD;
        float local[3] = {cosf(alt) * sinf(az), cosf(alt) * cosf(az), sinf(alt)};
        float x, y;
        bool ok = sky_project_local(f, local, &x, &y);
        if (ok && last_ok && fabsf(x - last_x) + fabsf(y - last_y) < SIZE_PX) {
            sky_line(last_x, last_y, x, y, 1, color, 1);
        }
        last_x = x;
        last_y = y;
        last_ok = ok;
    }
}

// The alt-az grid, under everything else (the app's drawGrid).
static void draw_grid(const sky_frame_t *f, bool night)
{
    uint32_t color = sky_for_night(night ? COLOR_GRID_NIGHT : COLOR_GRID_DAY, night);
    for (size_t i = 0; i < sizeof(GRID_ALTITUDES_DEG) / sizeof(GRID_ALTITUDES_DEG[0]); i++) {
        draw_altaz_curve(f, color, GRID_ALTITUDES_DEG[i], 0, 0, 3, 120);
    }
    for (int az = 0; az < 360; az += GRID_AZIMUTH_STEP_DEG) {
        draw_altaz_curve(f, color, 0, (float)az, 5, 0, 18);
    }
}

// Outside the eyepiece's circle goes black, with a thin edge (the app's drawEyepieceMask).
static void draw_eyepiece_mask(const sky_frame_t *f, float field_deg, bool night)
{
    if (field_deg <= 0) return;
    sky_mask_outside(sky_radius_px(f, field_deg), sky_for_night(COLOR_EYEPIECE_EDGE, night));
}

// The eyepiece's guide (the app's drawEyepieceGuide): chevrons just outside the circle,
// towards a target that isn't in it. The app turns them with the view but doesn't mirror
// them as the image is, so the mirroring in targetDirDeg is undone.
static void draw_eyepiece_guide(const sky_frame_t *f, float field_deg, bool night)
{
    const puck_view_msg_t *v = f->view;
    if (field_deg <= 0 || isnan(v->target_dist_deg) || isnan(v->target_dir_deg)) return;
    if (v->target_dist_deg < field_deg / 2) return; // in view
    float dir = v->target_dir_deg;
    if (v->flags & PUCK_VIEW_FLAG_MIRROR_Y) dir = 180 - dir;
    if (v->flags & PUCK_VIEW_FLAG_MIRROR_X) dir = -dir;
    float angle = dir * SKY_DEG_TO_RAD;
    float radius = sky_radius_px(f, field_deg) + 8 * SKY_STAR_SCALE;
    sky_draw_chevrons(SKY_CENTRE_PX + sinf(angle) * radius, SKY_CENTRE_PX - cosf(angle) * radius, angle,
                      v->target_dist_deg, night);
}

static void draw_lines(const sky_frame_t *f, bool night)
{
    uint32_t color = sky_for_night(night ? COLOR_LINE_NIGHT : COLOR_LINE_DAY, night);
    for (size_t i = 0; i < segment_count; i++) {
        float ax, ay, bx, by;
        bool a_below, b_below;
        if (sky_far_off(f, segments[i].a) && sky_far_off(f, segments[i].b)) continue;
        if (!sky_project(f, segments[i].a, &ax, &ay, &a_below) ||
            !sky_project(f, segments[i].b, &bx, &by, &b_below)) {
            continue;
        }
        // Both ends off the screen on the same side: nothing to see.
        if ((ax < 0 && bx < 0) || (ay < 0 && by < 0) || (ax > SIZE_PX && bx > SIZE_PX) ||
            (ay > SIZE_PX && by > SIZE_PX)) {
            continue;
        }
        sky_line(ax, ay, bx, by, 2, color, a_below && b_below ? SKY_BELOW_HORIZON_OPACITY : 1);
    }
}

// The Moon, planets and asteroids, over the stars, in the app's colour for the solar system:
// a phase for the Moon and Venus, turned to the Sun; the planets' own icons (sky_icons.c),
// their disc never smaller than the app's 7 pixels; or else a disc of their real size, but
// never smaller than a bright star.
static void draw_moving(const sky_frame_t *f, bool night)
{
    const float *sun = moving_sun;
    for (size_t i = 0; i < moving_count; i++) {
        const moving_object_t *o = &moving[i];
        if (o->show_below != 255 && f->view->fov_deg >= o->show_below) continue;
        float x, y;
        bool below;
        if (!sky_project(f, o->v, &x, &y, &below)) continue;
        bool moon = o->kind == KIND_MOON;
        float min_px = (moon ? 6.0f : 3.0f) * SKY_STAR_SCALE;
        float radius = fmaxf(min_px, sky_radius_px(f, o->size_arcmin / 60));
        if (x < -radius || y < -radius || x > SIZE_PX + radius || y > SIZE_PX + radius) continue;
        float alpha = below && f->fade_below ? SKY_BELOW_HORIZON_OPACITY : 1;
        uint32_t color = sky_for_night(moon ? COLOR_MOON : SKY_COLOR_SOLAR_SYSTEM, night);
        if (isnan(o->phase)) {
            float icon_disc = fmaxf(ICON_DISC_PX, sky_radius_px(f, o->size_arcmin / 60));
            if (!sky_icons_draw(o->id, x, y, icon_disc, color, alpha)) sky_disc(x, y, radius, color, alpha);
            continue;
        }
        // The lit side faces the Sun: the screen direction of a tiny step from the object
        // towards the Sun, as the app's directionOnScreen.
        float along = sun[0] * o->v[0] + sun[1] * o->v[1] + sun[2] * o->v[2];
        float step[3];
        for (int k = 0; k < 3; k++) step[k] = o->v[k] + 0.001f * (sun[k] - along * o->v[k]);
        float sx, sy;
        bool step_below;
        float dir_x = 1, dir_y = 0;
        if (sky_project(f, step, &sx, &sy, &step_below) && (sx != x || sy != y)) {
            float length = sqrtf((sx - x) * (sx - x) + (sy - y) * (sy - y));
            dir_x = (sx - x) / length;
            dir_y = (sy - y) / length;
        }
        sky_phase(x, y, radius, dir_x, dir_y, o->phase, color, alpha);
    }
}

bool sky_draw_create(lv_obj_t *screen)
{
    uint16_t *pixels = heap_caps_calloc(SIZE_PX * SIZE_PX, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!pixels || !sky_labels_create(screen)) {
        ESP_LOGE(TAG, "No memory for the sky");
        return false;
    }
    lv_draw_buf_init(&draw_buf, SIZE_PX, SIZE_PX, LV_COLOR_FORMAT_RGB565, SIZE_PX * sizeof(uint16_t),
                     pixels, SIZE_PX * SIZE_PX * sizeof(uint16_t));
    canvas = lv_canvas_create(screen);
    lv_canvas_set_draw_buf(canvas, &draw_buf);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_set_clickable(canvas, false); // taps go to the screen behind
    lv_obj_move_to_index(canvas, 0);     // behind everything else
    sky_pixels = pixels;
    sky_fill(SKY_COLOR_SKY);
    return true;
}

void sky_draw_clear(bool night)
{
    if (!canvas) return;
    sky_fill(sky_for_night(SKY_COLOR_SKY, night));
    lv_obj_invalidate(canvas);
}

// How long each step of a frame takes, on average, logged every 100 frames: to see where
// the time goes (at 30 views a second there are 33 ms for it all).
enum { STEP_FILL, STEP_LINES, STEP_STARS, STEP_MOVING, STEP_TOP, STEP_LABELS, STEP_COUNT };
static const char *STEP_NAMES[STEP_COUNT] = {"fill", "grid+lines", "stars", "moon+planets", "objects", "labels+mask"};
static int64_t step_us[STEP_COUNT];

static int64_t step_done(int step, int64_t since)
{
    int64_t now = esp_timer_get_time();
    step_us[step] += now - since;
    return now;
}

void sky_draw_frame(const puck_view_msg_t *view, bool night, float eyepiece_field_deg)
{
    if (!canvas) return;
    load_catalog();
    int64_t started = esp_timer_get_time();
    int64_t t = started;
    sky_fill(sky_for_night(SKY_COLOR_SKY, night));
    t = step_done(STEP_FILL, t);
    if (view->fov_deg > 0 && view->fov_deg < 360) {
        sky_frame_t f = sky_frame(view);
        moving_count = moving_get(moving, moving_sun);
        // The eyepiece view shows no grid and no constellation lines, as the app's.
        if (f.fade_below) {
            draw_grid(&f, night);
            draw_lines(&f, night);
        }
        t = step_done(STEP_LINES, t);
        // The eyepiece view shows the faint stars too, as the app's.
        sky_stars_draw(&f, night, !f.fade_below);
        t = step_done(STEP_STARS, t);
        draw_moving(&f, night);
        t = step_done(STEP_MOVING, t);
        sky_objects_draw(&f, night, eyepiece_field_deg, moving, moving_count);
        t = step_done(STEP_TOP, t);
        sky_labels_draw();
        // The eyepiece view: black outside its circle, over everything, as the app draws it
        // last, then the guide to a target outside it.
        if (!f.fade_below) {
            draw_eyepiece_mask(&f, eyepiece_field_deg, night);
            draw_eyepiece_guide(&f, eyepiece_field_deg, night);
        }
        t = step_done(STEP_LABELS, t);
    }
    lv_obj_invalidate(canvas);
    static unsigned frames;
    static int64_t total_us;
    total_us += esp_timer_get_time() - started;
    if (++frames == 100) {
        char steps[160];
        int at = 0;
        for (int i = 0; i < STEP_COUNT && at < (int)sizeof(steps); i++) {
            at += snprintf(steps + at, sizeof(steps) - at, " %s %u", STEP_NAMES[i], (unsigned)(step_us[i] / frames));
        }
        ESP_LOGI(TAG, "Drawing the sky: %u us a frame:%s", (unsigned)(total_us / frames), steps);
        frames = 0;
        total_us = 0;
        memset(step_us, 0, sizeof(step_us));
    }
}
