// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The sky map: see sky_draw.h. Each step follows the app's map (sky-canvas.component.ts,
// draw-sky.ts, sky-style.ts) so both show the same sky. In order: the sky's colour, the
// constellation lines, the stars and the Moon and planets (written straight into the
// canvas's pixels, which is quickest for thousands of dots), then the top layer of markers
// and labels (sky_objects.c).

#include "sky_draw.h"

#include <math.h>
#include <string.h>

#include "catalog.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "moving.h"
#include "sky_common.h"
#include "sky_objects.h"

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
// The dark side of a phased disc, so the whole disc still shows faintly.
#define DARK_SIDE_ALPHA 0.15f
// The Moon in the text's near-white, not the planets' yellow.
#define COLOR_MOON 0xE8EBF5
// The Moon's kind number (docs/puck.md, "Kinds").
#define KIND_MOON 10
static const uint32_t STAR_TINTS[] = {0xB9D0FF, 0xF4F6FF, 0xFFF3D6, 0xFFD2A1, 0xFFB08A};
#define TINT_COUNT (sizeof(STAR_TINTS) / sizeof(STAR_TINTS[0]))
// The puck's pixels are smaller than a phone's CSS pixels: stars are drawn this much bigger,
// to look the same size at arm's length.
#define STAR_SCALE 1.5f

// The catalogue's layout (docs/puck.md, "Format").
#define SECTION_STARS 1
#define SECTION_LINES 2
#define STAR_RECORD_BYTES 12
#define LINE_RECORD_BYTES 16

typedef struct {
    float v[3];      // J2000 unit vector
    float mag;
    uint8_t tint;    // index in STAR_TINTS
} star_t;

typedef struct {
    float a[3], b[3]; // J2000 unit vectors of the two ends
} segment_t;

static lv_obj_t *canvas;
static lv_draw_buf_t draw_buf;
static uint16_t *pixels; // RGB565, SIZE_PX × SIZE_PX, in PSRAM

// Our own copy of the catalogue, ready to turn: built once from the kept catalogue, so a
// new one being written to flash meanwhile doesn't matter.
static star_t *stars;
static size_t star_count;
static segment_t *segments;
static size_t segment_count;
static char loaded_id[CATALOG_ID_CHARS + 1];

// The Moon and planets this frame: their discs here, their labels in sky_objects.c.
static moving_object_t moving[MOVING_MAX];
static size_t moving_count;
static float moving_sun[3]; // the Sun's J2000 vector, which lit sides face

// ---- Pixels ----

static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

// Blends a colour over a pixel by alpha (0 to 1).
static void blend(uint16_t *pixel, uint32_t rgb, float alpha)
{
    if (alpha <= 0) return;
    if (alpha >= 1) {
        *pixel = rgb565(rgb);
        return;
    }
    uint16_t d = *pixel;
    float dr = (d >> 11) * (255.0f / 31), dg = (d >> 5 & 0x3F) * (255.0f / 63), db = (d & 0x1F) * (255.0f / 31);
    float r = dr + ((rgb >> 16 & 0xFF) - dr) * alpha;
    float g = dg + ((rgb >> 8 & 0xFF) - dg) * alpha;
    float b = db + ((rgb & 0xFF) - db) * alpha;
    *pixel = rgb565((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b);
}

static void fill(uint32_t rgb)
{
    uint16_t c = rgb565(rgb);
    for (size_t i = 0; i < SIZE_PX * SIZE_PX; i++) pixels[i] = c;
}

// A round dot with a soft edge, centred at (x, y).
static void draw_disc(float x, float y, float radius, uint32_t rgb, float alpha)
{
    int x0 = (int)floorf(x - radius - 1), x1 = (int)ceilf(x + radius + 1);
    int y0 = (int)floorf(y - radius - 1), y1 = (int)ceilf(y + radius + 1);
    if (x1 < 0 || y1 < 0 || x0 >= SIZE_PX || y0 >= SIZE_PX) return;
    for (int py = y0 < 0 ? 0 : y0; py <= y1 && py < SIZE_PX; py++) {
        for (int px = x0 < 0 ? 0 : x0; px <= x1 && px < SIZE_PX; px++) {
            float dx = px + 0.5f - x, dy = py + 0.5f - y;
            float cover = radius + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cover > 0) blend(&pixels[py * SIZE_PX + px], rgb, alpha * fminf(cover, 1));
        }
    }
}

// A disc lit on one side, as the Moon or Venus: (sun_x, sun_y) is the screen direction
// towards the Sun, and lit the lit fraction. The terminator is half an ellipse whose width
// shrinks to nothing at half phase, as the app's phaseIcon.
static void draw_phase(float x, float y, float radius, float sun_x, float sun_y, float lit, uint32_t rgb,
                       float alpha)
{
    float k = fmaxf(0, fminf(1, lit));
    int x0 = (int)floorf(x - radius - 1), x1 = (int)ceilf(x + radius + 1);
    int y0 = (int)floorf(y - radius - 1), y1 = (int)ceilf(y + radius + 1);
    for (int py = y0 < 0 ? 0 : y0; py <= y1 && py < SIZE_PX; py++) {
        for (int px = x0 < 0 ? 0 : x0; px <= x1 && px < SIZE_PX; px++) {
            float dx = px + 0.5f - x, dy = py + 0.5f - y;
            float cover = radius + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cover <= 0) continue;
            // Along the Sun's direction (u) and across it (v), in the disc's radii.
            float u = (dx * sun_x + dy * sun_y) / radius, v = (dy * sun_x - dx * sun_y) / radius;
            float half_chord = sqrtf(fmaxf(0, 1 - v * v));
            bool lit_here = u > -(2 * k - 1) * half_chord;
            blend(&pixels[py * SIZE_PX + px], rgb, alpha * fminf(cover, 1) * (lit_here ? 1 : DARK_SIDE_ALPHA));
        }
    }
}

// ---- The catalogue ----

// Builds our copy of the stars and lines (and, in sky_objects.c, the objects and labels)
// when a new catalogue has been kept.
static void load_catalog(void)
{
    const char *id = catalog_kept_id();
    if (!id || strcmp(id, loaded_id) == 0) return;
    size_t bytes;
    const uint8_t *data = catalog_data(&bytes);
    if (!data || bytes < 8 || sky_read_u32(data) != SKY_CATALOG_MAGIC) return;

    heap_caps_free(stars);
    heap_caps_free(segments);
    stars = NULL;
    segments = NULL;
    star_count = segment_count = 0;

    uint32_t n;
    const uint8_t *p = sky_find_section(data, bytes, SECTION_STARS, &n, NULL);
    if (p && (stars = heap_caps_malloc(n * sizeof(star_t), MALLOC_CAP_SPIRAM))) {
        for (uint32_t i = 0; i < n; i++, p += STAR_RECORD_BYTES) {
            sky_radec_vector(sky_read_f32(p), sky_read_f32(p + 4), stars[i].v);
            int16_t mag100;
            memcpy(&mag100, p + 8, sizeof(mag100));
            stars[i].mag = mag100 / 100.0f;
            stars[i].tint = p[10] < TINT_COUNT ? p[10] : 1;
        }
        star_count = n;
    }
    p = sky_find_section(data, bytes, SECTION_LINES, &n, NULL);
    if (p && (segments = heap_caps_malloc(n * sizeof(segment_t), MALLOC_CAP_SPIRAM))) {
        for (uint32_t i = 0; i < n; i++, p += LINE_RECORD_BYTES) {
            sky_radec_vector(sky_read_f32(p), sky_read_f32(p + 4), segments[i].a);
            sky_radec_vector(sky_read_f32(p + 8), sky_read_f32(p + 12), segments[i].b);
        }
        segment_count = n;
    }
    sky_objects_load(data, bytes);
    strcpy(loaded_id, id);
    ESP_LOGI(TAG, "Catalogue %s: %u stars, %u line segments", id, (unsigned)star_count,
             (unsigned)segment_count);
}

// ---- Drawing ----

// A curve through local directions at (alt, az) steps, as the app's drawCurve: a step that
// jumps across the screen is the curve passing behind the view, and isn't drawn.
static void draw_altaz_curve(lv_layer_t *layer, lv_draw_line_dsc_t *line, const sky_frame_t *f, float alt0,
                             float az0, float alt_step, float az_step, int steps)
{
    float last_x = 0, last_y = 0;
    bool last_ok = false;
    for (int i = 0; i <= steps; i++) {
        float alt = (alt0 + i * alt_step) * SKY_DEG_TO_RAD, az = (az0 + i * az_step) * SKY_DEG_TO_RAD;
        float local[3] = {cosf(alt) * sinf(az), cosf(alt) * cosf(az), sinf(alt)};
        float x, y;
        bool ok = sky_project_local(f, local, &x, &y);
        if (ok && last_ok && fabsf(x - last_x) + fabsf(y - last_y) < SIZE_PX) {
            line->p1.x = last_x;
            line->p1.y = last_y;
            line->p2.x = x;
            line->p2.y = y;
            lv_draw_line(layer, line);
        }
        last_x = x;
        last_y = y;
        last_ok = ok;
    }
}

// The alt-az grid, under everything else (the app's drawGrid).
static void draw_grid(lv_layer_t *layer, const sky_frame_t *f, bool night)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(sky_for_night(night ? COLOR_GRID_NIGHT : COLOR_GRID_DAY, night));
    line.width = 1;
    for (size_t i = 0; i < sizeof(GRID_ALTITUDES_DEG) / sizeof(GRID_ALTITUDES_DEG[0]); i++) {
        draw_altaz_curve(layer, &line, f, GRID_ALTITUDES_DEG[i], 0, 0, 3, 120);
    }
    for (int az = 0; az < 360; az += GRID_AZIMUTH_STEP_DEG) {
        draw_altaz_curve(layer, &line, f, 0, (float)az, 5, 0, 18);
    }
}

// Outside the eyepiece's circle goes black, with a thin edge (the app's drawEyepieceMask).
static void draw_eyepiece_mask(const sky_frame_t *f, float field_deg, bool night)
{
    if (field_deg <= 0) return;
    float radius = sky_radius_px(f, field_deg);
    uint32_t edge = sky_for_night(COLOR_EYEPIECE_EDGE, night);
    for (int py = 0; py < SIZE_PX; py++) {
        float dy = py + 0.5f - SKY_CENTRE_PX;
        for (int px = 0; px < SIZE_PX; px++) {
            float dx = px + 0.5f - SKY_CENTRE_PX;
            float d = sqrtf(dx * dx + dy * dy) - radius;
            if (d > 1) {
                pixels[py * SIZE_PX + px] = 0;
            } else if (d > -1) {
                blend(&pixels[py * SIZE_PX + px], edge, 1 - fabsf(d)); // the edge, a pixel wide
            }
        }
    }
}

static void draw_lines(lv_layer_t *layer, const sky_frame_t *f, bool night)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(sky_for_night(night ? COLOR_LINE_NIGHT : COLOR_LINE_DAY, night));
    line.width = 2;
    line.round_start = line.round_end = 1;
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
        line.opa = a_below && b_below ? (lv_opa_t)(LV_OPA_COVER * SKY_BELOW_HORIZON_OPACITY) : LV_OPA_COVER;
        line.p1.x = ax;
        line.p1.y = ay;
        line.p2.x = bx;
        line.p2.y = by;
        lv_draw_line(layer, &line);
    }
}

static void draw_stars(const sky_frame_t *f, bool night)
{
    // Brightest first, so drawing stops at the limit, as the app's map does.
    for (size_t i = 0; i < star_count; i++) {
        const star_t *s = &stars[i];
        if (s->mag > f->view->star_limit_mag) break;
        float x, y;
        bool below;
        if (sky_far_off(f, s->v) || !sky_project(f, s->v, &x, &y, &below)) continue;
        if (x < -8 || y < -8 || x > SIZE_PX + 8 || y > SIZE_PX + 8) continue;
        float radius = fmaxf(0.8f, 3.4f - s->mag * 0.42f) * STAR_SCALE;
        // Faint stars fade as well as shrink.
        float alpha = fminf(1, fmaxf(0.55f, 1.15f - floorf(s->mag) * 0.1f));
        if (below && f->fade_below) alpha *= SKY_BELOW_HORIZON_OPACITY;
        draw_disc(x, y, radius, sky_for_night(STAR_TINTS[s->tint], night), alpha);
    }
}

// The Moon, planets and asteroids, over the stars: a disc of their real size, but never
// smaller than a bright star, in the app's colour for the solar system.
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
        float min_px = (moon ? 6.0f : 3.0f) * STAR_SCALE;
        float radius = fmaxf(min_px, sky_radius_px(f, o->size_arcmin / 60));
        if (x < -radius || y < -radius || x > SIZE_PX + radius || y > SIZE_PX + radius) continue;
        float alpha = below && f->fade_below ? SKY_BELOW_HORIZON_OPACITY : 1;
        uint32_t color = sky_for_night(moon ? COLOR_MOON : SKY_COLOR_SOLAR_SYSTEM, night);
        if (isnan(o->phase)) {
            draw_disc(x, y, radius, color, alpha);
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
        draw_phase(x, y, radius, dir_x, dir_y, o->phase, color, alpha);
    }
}

bool sky_draw_create(lv_obj_t *screen)
{
    pixels = heap_caps_malloc(SIZE_PX * SIZE_PX * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!pixels) {
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
    fill(SKY_COLOR_SKY);
    return true;
}

void sky_draw_clear(bool night)
{
    if (!canvas) return;
    fill(sky_for_night(SKY_COLOR_SKY, night));
    lv_obj_invalidate(canvas);
}

void sky_draw_frame(const puck_view_msg_t *view, bool night, float eyepiece_field_deg)
{
    if (!canvas) return;
    load_catalog();
    int64_t started = esp_timer_get_time();
    fill(sky_for_night(SKY_COLOR_SKY, night));
    if (view->fov_deg > 0 && view->fov_deg < 360) {
        sky_frame_t f = sky_frame(view);
        moving_count = moving_get(moving, moving_sun);
        lv_layer_t layer;
        // The eyepiece view shows no grid and no constellation lines, as the app's.
        if (f.fade_below) {
            lv_canvas_init_layer(canvas, &layer);
            draw_grid(&layer, &f, night);
            draw_lines(&layer, &f, night);
            lv_canvas_finish_layer(canvas, &layer);
        }
        draw_stars(&f, night);
        draw_moving(&f, night);
        if (!f.fade_below) draw_eyepiece_mask(&f, eyepiece_field_deg, night);
        lv_canvas_init_layer(canvas, &layer);
        sky_objects_draw(&layer, &f, night, moving, moving_count);
        lv_canvas_finish_layer(canvas, &layer);
    }
    lv_obj_invalidate(canvas);
    // How long drawing takes, now and then: at 30 views a second there are 33 ms for it all.
    static unsigned frames;
    static int64_t total_us;
    total_us += esp_timer_get_time() - started;
    if (++frames == 300) {
        ESP_LOGI(TAG, "Drawing the sky: %u us a frame", (unsigned)(total_us / frames));
        frames = 0;
        total_us = 0;
    }
}
