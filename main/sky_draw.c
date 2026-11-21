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
// The faint stars, for the eyepiece view: sorted by declination band, then right ascension,
// read straight from flash where the view looks (docs/puck.md, section 7).
#define SECTION_FAINT 7
#define FAINT_BAND_DEG 2
#define FAINT_BANDS (180 / FAINT_BAND_DEG)
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
// Where each faint star band starts among the faint stars, and the end (FAINT_BANDS + 1).
static uint32_t faint_band_first[FAINT_BANDS + 1];
static uint32_t faint_count;

// The Moon and planets this frame: their discs here, their labels in sky_objects.c.
static moving_object_t moving[MOVING_MAX];
static size_t moving_count;
static float moving_sun[3]; // the Sun's J2000 vector, which lit sides face

// ---- The layer ----

void sky_layer_step(lv_layer_t *layer)
{
    static unsigned queued;
    if (++queued < SKY_LAYER_BATCH) return;
    queued = 0;
    lv_canvas_finish_layer(canvas, layer);
    lv_canvas_init_layer(canvas, layer);
}

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
    // Two pixels a write: the buffer is in PSRAM, where each write costs.
    uint32_t two = rgb565(rgb) * 0x00010001u;
    uint32_t *p = (uint32_t *)pixels;
    for (size_t i = 0; i < SIZE_PX * SIZE_PX / 2; i++) p[i] = two;
    if ((SIZE_PX * SIZE_PX) & 1) pixels[SIZE_PX * SIZE_PX - 1] = (uint16_t)two;
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
    // The faint stars stay in flash: only where each band starts is worked out here.
    faint_count = 0;
    p = sky_find_section(data, bytes, SECTION_FAINT, &n, NULL);
    if (p) {
        int band = 0;
        for (uint32_t i = 0; i < n; i++) {
            int b = (int)((sky_read_f32(p + i * STAR_RECORD_BYTES + 4) + 90) / FAINT_BAND_DEG);
            if (b >= FAINT_BANDS) b = FAINT_BANDS - 1;
            while (band <= b) faint_band_first[band++] = i;
        }
        while (band <= FAINT_BANDS) faint_band_first[band++] = n;
        faint_count = n;
    }
    sky_objects_load(data, bytes);
    strcpy(loaded_id, id);
    ESP_LOGI(TAG, "Catalogue %s: %u stars, %u faint stars, %u line segments", id, (unsigned)star_count,
             (unsigned)faint_count, (unsigned)segment_count);
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
            sky_layer_step(layer);
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
    float radius = sky_radius_px(f, field_deg) + 8 * STAR_SCALE;
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    sky_draw_chevrons(&layer, SKY_CENTRE_PX + sinf(angle) * radius, SKY_CENTRE_PX - cosf(angle) * radius, angle,
                      v->target_dist_deg, night);
    lv_canvas_finish_layer(canvas, &layer);
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
        sky_layer_step(layer);
    }
}

// One star drawn: its size, tint and opacity as the app's (drawStars).
static void draw_star(const sky_frame_t *f, const float v[3], float mag, uint8_t tint, bool night)
{
    float x, y;
    bool below;
    if (sky_far_off(f, v) || !sky_project(f, v, &x, &y, &below)) return;
    if (x < -8 || y < -8 || x > SIZE_PX + 8 || y > SIZE_PX + 8) return;
    float radius = fmaxf(0.8f, 3.4f - mag * 0.42f) * STAR_SCALE;
    // Faint stars fade as well as shrink.
    float alpha = fminf(1, fmaxf(0.55f, 1.15f - floorf(mag) * 0.1f));
    if (below && f->fade_below) alpha *= SKY_BELOW_HORIZON_OPACITY;
    draw_disc(x, y, radius, sky_for_night(STAR_TINTS[tint < TINT_COUNT ? tint : 1], night), alpha);
}

static void draw_stars(const sky_frame_t *f, bool night)
{
    // Brightest first, so drawing stops at the limit, as the app's map does.
    for (size_t i = 0; i < star_count; i++) {
        const star_t *star = &stars[i];
        if (star->mag > f->view->star_limit_mag) break;
        draw_star(f, star->v, star->mag, star->tint, night);
    }
}

// The first faint star in [first, end) whose right ascension is at least ra (they are sorted).
static uint32_t faint_first_from(const uint8_t *records, uint32_t first, uint32_t end, float ra)
{
    while (first < end) {
        uint32_t mid = first + (end - first) / 2;
        if (sky_read_f32(records + mid * STAR_RECORD_BYTES) < ra) {
            first = mid + 1;
        } else {
            end = mid;
        }
    }
    return first;
}

// The faint stars where the view looks, down to its limit: only the bands and right
// ascensions near its centre are read, as the app's starsNear does.
static void draw_faint_stars(const sky_frame_t *f, bool night)
{
    size_t bytes;
    const uint8_t *data = catalog_data(&bytes); // NULL while a new catalogue is written
    uint32_t n;
    const uint8_t *records = data && faint_count ? sky_find_section(data, bytes, SECTION_FAINT, &n, NULL) : NULL;
    if (!records || n != faint_count) return;

    // The view's centre in J2000: the view's forward turned back.
    const float *q = f->sky_then_view;
    const float back[4] = {q[0], -q[1], -q[2], -q[3]};
    const float forward[3] = {0, 0, -1};
    float c[3];
    sky_rotate(back, forward, c);
    float dec_c = asinf(fmaxf(-1, fminf(1, c[2]))) * SKY_RAD_TO_DEG;
    float ra_c = atan2f(c[1], c[0]) * SKY_RAD_TO_DEG;
    if (ra_c < 0) ra_c += 360;
    float radius = f->view->fov_deg / 2 + 1; // the round screen, and a margin for refraction

    int b0 = (int)((dec_c - radius + 90) / FAINT_BAND_DEG), b1 = (int)((dec_c + radius + 90) / FAINT_BAND_DEG);
    if (b0 < 0) b0 = 0;
    if (b1 >= FAINT_BANDS) b1 = FAINT_BANDS - 1;
    int16_t limit = (int16_t)(f->view->star_limit_mag * 100);
    for (int b = b0; b <= b1; b++) {
        // Lines of right ascension crowd together towards the poles: cover more of them there.
        float low = -90.0f + b * FAINT_BAND_DEG, high = low + FAINT_BAND_DEG;
        float widest = fminf(89.9f, fmaxf(fabsf(low), fabsf(high)));
        float ra_radius = fabsf(dec_c) + radius >= 89.9f ? 180 : fminf(180, radius / cosf(widest * SKY_DEG_TO_RAD));
        // One right ascension range, or two where it wraps past 0°.
        float ranges[2][2];
        int range_count = 1;
        if (ra_radius >= 180) {
            ranges[0][0] = 0;
            ranges[0][1] = 360;
        } else if (ra_c - ra_radius < 0 || ra_c + ra_radius > 360) {
            ranges[0][0] = fmodf(ra_c - ra_radius + 360, 360);
            ranges[0][1] = 360;
            ranges[1][0] = 0;
            ranges[1][1] = fmodf(ra_c + ra_radius, 360);
            range_count = 2;
        } else {
            ranges[0][0] = ra_c - ra_radius;
            ranges[0][1] = ra_c + ra_radius;
        }
        for (int r = 0; r < range_count; r++) {
            uint32_t end = faint_band_first[b + 1];
            for (uint32_t i = faint_first_from(records, faint_band_first[b], end, ranges[r][0]); i < end; i++) {
                const uint8_t *rec = records + i * STAR_RECORD_BYTES;
                float ra = sky_read_f32(rec);
                if (ra > ranges[r][1]) break;
                int16_t mag100;
                memcpy(&mag100, rec + 8, sizeof(mag100));
                if (mag100 > limit) continue;
                float v[3];
                sky_radec_vector(ra, sky_read_f32(rec + 4), v);
                draw_star(f, v, mag100 / 100.0f, rec[10], night);
            }
        }
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

// How long each step of a frame takes, on average, logged every 100 frames: to see where
// the time goes (at 30 views a second there are 33 ms for it all).
enum { STEP_FILL, STEP_LINES, STEP_STARS, STEP_MOVING, STEP_TOP, STEP_COUNT };
static const char *STEP_NAMES[STEP_COUNT] = {"fill", "grid+lines", "stars", "moon+planets", "objects+labels"};
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
    fill(sky_for_night(SKY_COLOR_SKY, night));
    t = step_done(STEP_FILL, t);
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
        t = step_done(STEP_LINES, t);
        draw_stars(&f, night);
        // The eyepiece view shows the faint stars too, as the app's.
        if (!f.fade_below) draw_faint_stars(&f, night);
        t = step_done(STEP_STARS, t);
        draw_moving(&f, night);
        t = step_done(STEP_MOVING, t);
        lv_canvas_init_layer(canvas, &layer);
        sky_objects_draw(&layer, &f, night, moving, moving_count);
        lv_canvas_finish_layer(canvas, &layer);
        // The eyepiece view: black outside its circle, over everything, as the app draws it
        // last, then the guide to a target outside it.
        if (!f.fade_below) {
            draw_eyepiece_mask(&f, eyepiece_field_deg, night);
            draw_eyepiece_guide(&f, eyepiece_field_deg, night);
        }
        t = step_done(STEP_TOP, t);
    }
    lv_obj_invalidate(canvas);
    static unsigned frames;
    static int64_t total_us;
    total_us += esp_timer_get_time() - started;
    if (++frames == 100) {
        ESP_LOGI(TAG, "Drawing the sky: %u us a frame: %s %u, %s %u, %s %u, %s %u, %s %u",
                 (unsigned)(total_us / frames), STEP_NAMES[0], (unsigned)(step_us[0] / frames), STEP_NAMES[1],
                 (unsigned)(step_us[1] / frames), STEP_NAMES[2], (unsigned)(step_us[2] / frames), STEP_NAMES[3],
                 (unsigned)(step_us[3] / frames), STEP_NAMES[4], (unsigned)(step_us[4] / frames));
        frames = 0;
        total_us = 0;
        memset(step_us, 0, sizeof(step_us));
    }
}
