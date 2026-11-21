// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The sky map: see sky_draw.h. Each step follows the app's map (sky-canvas.component.ts,
// draw-sky.ts, sky-style.ts) so both show the same sky.

#include "sky_draw.h"

#include <math.h>
#include <string.h>

#include "board.h"
#include "catalog.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "sky";

#define SIZE_PX BOARD_SCREEN_PX
#define CENTRE_PX (SIZE_PX / 2.0f)
#define DEG_TO_RAD 0.017453292f
#define RAD_TO_DEG 57.29578f

// The app's colours (sky-style.ts).
#define COLOR_SKY 0x05070F
#define COLOR_LINE_DAY 0x2A4170
#define COLOR_LINE_NIGHT 0x4F6FAE
static const uint32_t STAR_TINTS[] = {0xB9D0FF, 0xF4F6FF, 0xFFF3D6, 0xFFD2A1, 0xFFB08A};
#define TINT_COUNT (sizeof(STAR_TINTS) / sizeof(STAR_TINTS[0]))
// What is below the horizon is drawn at this opacity on the sky map (BELOW_HORIZON_OPACITY).
#define BELOW_HORIZON_OPACITY 0.35f
// The puck's pixels are smaller than a phone's CSS pixels: stars are drawn this much bigger,
// to look the same size at arm's length.
#define STAR_SCALE 1.5f
// Things this far behind the view are not drawn (the app's project()).
#define BEHIND_LIMIT -0.2f

// The catalogue's layout (docs/puck.md, "Format").
#define CATALOG_MAGIC 0x434D5441 // "ATMC"
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

// ---- Colours ----

static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

// Night mode turns everything red by its brightness, as the app does.
static uint32_t for_night(uint32_t rgb, bool night)
{
    if (!night) return rgb;
    float lum = 0.2126f * (rgb >> 16 & 0xFF) + 0.7152f * (rgb >> 8 & 0xFF) + 0.0722f * (rgb & 0xFF);
    return (uint32_t)(lum > 255 ? 255 : lum) << 16;
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

// ---- The catalogue ----

static void radec_vector(float ra_deg, float dec_deg, float v[3])
{
    float ra = ra_deg * DEG_TO_RAD, dec = dec_deg * DEG_TO_RAD;
    v[0] = cosf(dec) * cosf(ra);
    v[1] = cosf(dec) * sinf(ra);
    v[2] = sinf(dec);
}

static float read_f32(const uint8_t *p)
{
    float f;
    memcpy(&f, p, sizeof(f));
    return f;
}

static uint32_t read_u32(const uint8_t *p)
{
    uint32_t n;
    memcpy(&n, p, sizeof(n));
    return n;
}

// Finds a section in the catalogue: its records and how many.
static const uint8_t *find_section(const uint8_t *data, size_t bytes, uint32_t type, uint32_t *count)
{
    uint16_t sections;
    memcpy(&sections, data + 6, sizeof(sections));
    for (uint32_t i = 0; i < sections && 8 + (i + 1) * 16 <= bytes; i++) {
        const uint8_t *entry = data + 8 + i * 16;
        uint32_t offset = read_u32(entry + 4), n = read_u32(entry + 8), size = read_u32(entry + 12);
        if (read_u32(entry) == type && offset + size <= bytes) {
            *count = n;
            return data + offset;
        }
    }
    return NULL;
}

// Builds our copy of the stars and lines when a new catalogue has been kept.
static void load_catalog(void)
{
    const char *id = catalog_kept_id();
    if (!id || strcmp(id, loaded_id) == 0) return;
    size_t bytes;
    const uint8_t *data = catalog_data(&bytes);
    if (!data || bytes < 8 || read_u32(data) != CATALOG_MAGIC) return;

    heap_caps_free(stars);
    heap_caps_free(segments);
    stars = NULL;
    segments = NULL;
    star_count = segment_count = 0;

    uint32_t n;
    const uint8_t *p = find_section(data, bytes, SECTION_STARS, &n);
    if (p && (stars = heap_caps_malloc(n * sizeof(star_t), MALLOC_CAP_SPIRAM))) {
        for (uint32_t i = 0; i < n; i++, p += STAR_RECORD_BYTES) {
            radec_vector(read_f32(p), read_f32(p + 4), stars[i].v);
            int16_t mag100;
            memcpy(&mag100, p + 8, sizeof(mag100));
            stars[i].mag = mag100 / 100.0f;
            stars[i].tint = p[10] < TINT_COUNT ? p[10] : 1;
        }
        star_count = n;
    }
    p = find_section(data, bytes, SECTION_LINES, &n);
    if (p && (segments = heap_caps_malloc(n * sizeof(segment_t), MALLOC_CAP_SPIRAM))) {
        for (uint32_t i = 0; i < n; i++, p += LINE_RECORD_BYTES) {
            radec_vector(read_f32(p), read_f32(p + 4), segments[i].a);
            radec_vector(read_f32(p + 8), read_f32(p + 12), segments[i].b);
        }
        segment_count = n;
    }
    strcpy(loaded_id, id);
    ESP_LOGI(TAG, "Catalogue %s: %u stars, %u line segments", id, (unsigned)star_count,
             (unsigned)segment_count);
}

// ---- Turning and projecting ----

// Turns v by the unit quaternion q (w, x, y, z), as the app's tests do (puck-view.spec.ts).
static void rotate(const float q[4], const float v[3], float out[3])
{
    const float w = q[0], x = q[1], y = q[2], z = q[3];
    const float tx = 2 * (y * v[2] - z * v[1]);
    const float ty = 2 * (z * v[0] - x * v[2]);
    const float tz = 2 * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}

// The air lifts what is low in the sky: astronomy-engine's "normal" refraction, as the app
// draws (docs/puck.md, "Drawing on the puck", step 2). In place, on a local vector.
static void refract(float v[3])
{
    float alt = asinf(fmaxf(-1, fminf(1, v[2]))) * RAD_TO_DEG;
    float hd = alt < -1 ? -1 : alt;
    float lift = 1.02f / tanf((hd + 10.3f / (hd + 5.11f)) * DEG_TO_RAD) / 60;
    if (alt < -1) lift *= (alt + 90) / 89;
    float raised = (alt + lift) * DEG_TO_RAD;
    float across = sqrtf(v[0] * v[0] + v[1] * v[1]);
    if (across < 1e-6f) return;
    float k = cosf(raised) / across;
    v[0] *= k;
    v[1] *= k;
    v[2] = sinf(raised);
}

typedef struct {
    const puck_view_msg_t *view;
    float scale;        // pixels per projected unit
    float mirror_x, mirror_y;
    bool fade_below;    // the sky map fades what is below the horizon; the eyepiece doesn't
    // Stars further than this from the centre (as the cosine of the angle) are off the round
    // screen. Checked before refraction, which is the slow part, with a margin for its lift.
    float visible_cos;
} frame_t;

// Off the round screen for sure: far from the view's centre, refraction or not.
static bool far_off(const frame_t *f, const float j2000[3])
{
    float local[3], v[3];
    rotate(f->view->sky, j2000, local);
    rotate(f->view->view, local, v);
    return -v[2] < f->visible_cos;
}

// A J2000 vector to the screen. False when too far behind to draw. below: under the horizon.
static bool project(const frame_t *f, const float j2000[3], float *px, float *py, bool *below)
{
    float local[3], v[3];
    rotate(f->view->sky, j2000, local);
    refract(local);
    *below = local[2] < 0;
    rotate(f->view->view, local, v);
    float forward = -v[2]; // the view frame is right, up, back
    if (forward < BEHIND_LIMIT) return false;
    float k = 2 / (1 + forward) * f->scale;
    *px = CENTRE_PX + f->mirror_x * k * v[0];
    *py = CENTRE_PX - f->mirror_y * k * v[1];
    return true;
}

// ---- Drawing ----

static void fill(uint32_t rgb)
{
    uint16_t c = rgb565(rgb);
    for (size_t i = 0; i < SIZE_PX * SIZE_PX; i++) pixels[i] = c;
}

// A round star with a soft edge, centred at (x, y).
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

static void draw_lines(const frame_t *f, bool night)
{
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(for_night(night ? COLOR_LINE_NIGHT : COLOR_LINE_DAY, night));
    line.width = 2;
    line.round_start = line.round_end = 1;
    for (size_t i = 0; i < segment_count; i++) {
        float ax, ay, bx, by;
        bool a_below, b_below;
        if (!project(f, segments[i].a, &ax, &ay, &a_below) || !project(f, segments[i].b, &bx, &by, &b_below)) {
            continue;
        }
        // Both ends well off the round screen on the same side: nothing to see.
        if ((ax < 0 && bx < 0) || (ay < 0 && by < 0) || (ax > SIZE_PX && bx > SIZE_PX) ||
            (ay > SIZE_PX && by > SIZE_PX)) {
            continue;
        }
        line.opa = f->fade_below && a_below && b_below ? (lv_opa_t)(LV_OPA_COVER * BELOW_HORIZON_OPACITY)
                                                       : LV_OPA_COVER;
        line.p1.x = ax;
        line.p1.y = ay;
        line.p2.x = bx;
        line.p2.y = by;
        lv_draw_line(&layer, &line);
    }
    lv_canvas_finish_layer(canvas, &layer);
}

static void draw_stars(const frame_t *f, bool night)
{
    // Brightest first, so drawing stops at the limit, as the app's map does.
    for (size_t i = 0; i < star_count; i++) {
        const star_t *s = &stars[i];
        if (s->mag > f->view->star_limit_mag) break;
        float x, y;
        bool below;
        if (far_off(f, s->v) || !project(f, s->v, &x, &y, &below)) continue;
        if (x < -8 || y < -8 || x > SIZE_PX + 8 || y > SIZE_PX + 8) continue;
        float radius = fmaxf(0.8f, 3.4f - s->mag * 0.42f) * STAR_SCALE;
        // Faint stars fade as well as shrink.
        float alpha = fminf(1, fmaxf(0.55f, 1.15f - floorf(s->mag) * 0.1f));
        if (below && f->fade_below) alpha *= BELOW_HORIZON_OPACITY;
        draw_disc(x, y, radius, for_night(STAR_TINTS[s->tint], night), alpha);
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
    fill(COLOR_SKY);
    return true;
}

void sky_draw_clear(bool night)
{
    if (!canvas) return;
    fill(for_night(COLOR_SKY, night));
    lv_obj_invalidate(canvas);
}

void sky_draw_frame(const puck_view_msg_t *view, bool night)
{
    if (!canvas) return;
    load_catalog();
    int64_t started = esp_timer_get_time();
    fill(for_night(COLOR_SKY, night));
    if (view->fov_deg > 0 && view->fov_deg < 360) {
        frame_t f = {
            .view = view,
            // The puck's diameter shows fovDeg: its radius is 2 tan(fov / 4) projected units.
            .scale = CENTRE_PX / (2 * tanf(view->fov_deg / 4 * DEG_TO_RAD)),
            .mirror_x = view->flags & PUCK_VIEW_FLAG_MIRROR_X ? -1 : 1,
            .mirror_y = view->flags & PUCK_VIEW_FLAG_MIRROR_Y ? -1 : 1,
            .fade_below = !(view->flags & PUCK_VIEW_FLAG_EYEPIECE),
        };
        // The screen's edge is fovDeg / 2 from the centre; 1° more covers refraction's lift
        // (under 0.6°) and the biggest star's size. Wide views check nothing.
        float edge_deg = view->fov_deg / 2 + 1;
        f.visible_cos = edge_deg < 170 ? cosf(edge_deg * DEG_TO_RAD) : -2;
        // The eyepiece view shows no constellation lines, as the app's.
        if (f.fade_below) draw_lines(&f, night);
        draw_stars(&f, night);
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
