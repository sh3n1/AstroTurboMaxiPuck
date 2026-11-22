// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The sky map's top layer: see sky_objects.h.

#include "sky_objects.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "sky_labels.h"
#include "sky_pixels.h"
#include "texts.h"

static const char *TAG = "sky";

// The app's colours (sky-style.ts, theme.ts), day and night. Night turns them all red by
// brightness after (sky_for_night), so night's are brighter, to stay visible.
#define COLOR_DEEP_SKY 0x8FB7FF
#define COLOR_STAR_LABEL_DAY 0xA8B0C6
#define COLOR_STAR_LABEL_NIGHT 0xE8EBF5
#define COLOR_SIZE_CIRCLE_DAY 0x4C6AA6
#define COLOR_SIZE_CIRCLE_NIGHT 0x7F9BD2
#define COLOR_NAME_DAY 0x55709F
#define COLOR_NAME_NIGHT 0xB4C6E8
#define COLOR_HORIZON_DAY 0x3F8F6A
#define COLOR_HORIZON_NIGHT 0x6FCF9F
#define COLOR_CARDINAL 0x6FCF9F
#define COLOR_OUTLINE 0x05070F
// The selected target's crosshair: the green the rings turn on target (THEME.success).
#define COLOR_ON_TARGET 0x6FCF9F
// The guidance chevrons (THEME.guidance), and their shape and spacing (draw-targets.ts,
// reticle.ts).
#define COLOR_GUIDE 0xA9CBFF
#define CHEVRON_HEIGHT_PX (8 * PX_SCALE)
#define CHEVRON_HALF_WIDTH_PX (11 * PX_SCALE)
#define CHEVRON_WIDTH_PX (3.5f * PX_SCALE)
#define CHEVRON_MIN_SPACING_PX 3.0f
#define CHEVRON_MAX_SPACING_PX 18.0f
#define CHEVRON_SPACING_PX_PER_DEG 0.5f
static const float CHEVRON_FADE[] = {0.35f, 0.65f, 1.0f};
// The guide in the normal view (the app's drawPointer): chevrons this far outside the
// reticle's outer ring, and a dotted line from a little further out to the target once it is
// on the screen and far enough from the rings.
#define POINTER_GAP_PX (24 * PX_SCALE)
#define POINTER_CHEVRON_PX (4 * PX_SCALE)
#define POINTER_LINE_START_PX (34 * PX_SCALE)
#define POINTER_LINE_MIN_PX (40 * PX_SCALE)
#define POINTER_DOT_PX (2 * PX_SCALE)
#define POINTER_DOT_GAP_PX (5 * PX_SCALE)
#define POINTER_LINE_WIDTH_PX (1.5f * PX_SCALE)
// The reticle's outer ring, 4° across (the app's RETICLE_RINGS_DEG).
#define RETICLE_OUTER_DEG 4.0f

// Sizes, as the app's (draw-targets.ts), in the puck's pixels: its pixels are smaller than a
// phone's CSS pixels (see STAR_SCALE in sky_draw.c).
#define PX_SCALE 1.5f
#define MIN_SIZE_CIRCLE_PX (6 * PX_SCALE)
#define MAX_SIZE_CIRCLE_MAG 6.0f
#define MARKER_RADIUS_PX (4 * PX_SCALE)
#define LABEL_GAP_PX (9 * PX_SCALE)
// Size circles are dashed, as the app's ([3, 3] on a canvas): this long a dash and gap.
#define DASH_PX (3 * PX_SCALE)
// The crosshair (the app's drawTargetMarker): strokes from the gap out this far.
#define CROSSHAIR_GAP_PX (6 * PX_SCALE)
#define CROSSHAIR_LENGTH_PX (10 * PX_SCALE)

// The catalogue's layout (docs/puck.md, "Format").
#define SECTION_NAMES 3
#define SECTION_OBJECTS 4
#define SECTION_STRINGS 5
#define NAME_RECORD_BYTES 12
#define OBJECT_RECORD_BYTES 20
#define NO_MAGNITUDE 32767
#define KIND_STAR 5
#define KIND_DOUBLE_STAR 6
#define KIND_PLANET 7 // to KIND_MOON: the solar system
#define KIND_MOON 10
// The selected target's label: this far beyond its marker, as the app's (15).
#define TARGET_LABEL_GAP_PX (15 * PX_SCALE)

typedef struct {
    float v[3];
    float mag;            // 99 when unknown
    float size_deg;       // 0 when unknown
    uint32_t label;       // offset in strings
    uint8_t kind;
    uint8_t show_below;   // 255: always
    bool messier;         // labelled M and a number: the app shows their size circle
} object_t;

typedef struct {
    float v[3];
    uint32_t label;
} name_t;

static object_t *objects;
static size_t object_count;
// The objects by the field they show below, widest first (255, always, first of all), to find
// quickly those the field shows; and those, in the catalogue's order, which is the order of
// their labels' priority. Remade only when the field crosses one of the objects' limits.
static uint32_t *by_limit;
static uint32_t *shown;
static size_t shown_count;
static bool shown_ready;
// The objects shown this frame that land on the screen, projected once.
#define MAX_CANDIDATES 1024
typedef struct {
    uint32_t index;
    float x, y;
    bool below;
} candidate_t;
static candidate_t candidates[MAX_CANDIDATES];
static name_t *names;
static size_t name_count;
static char *strings;     // our copy of the catalogue's strings
static size_t strings_bytes;
// The selected target, from the link (sky_objects_set_target), and this frame's copy.
static portMUX_TYPE target_lock = portMUX_INITIALIZER_UNLOCKED;
static char target_label_in[SKY_TARGET_LABEL_BYTES];
static uint8_t target_kind_in;
static float target_size_in;
static char target_label[SKY_TARGET_LABEL_BYTES];

void sky_objects_set_target(const char *label, uint8_t kind, float size_arcmin)
{
    taskENTER_CRITICAL(&target_lock);
    strlcpy(target_label_in, label, sizeof(target_label_in));
    target_kind_in = kind;
    target_size_in = size_arcmin;
    taskEXIT_CRITICAL(&target_lock);
}

// Whether a label is the selected target's, already written (draw_target_label).
static bool is_target(const char *label)
{
    return target_label[0] && strcmp(label, target_label) == 0;
}

static const char *string_at(uint32_t offset)
{
    return offset < strings_bytes ? strings + offset : "";
}

// For qsort: widest limit first, then the catalogue's order.
static int by_limit_widest_first(const void *a, const void *b)
{
    uint32_t i = *(const uint32_t *)a, j = *(const uint32_t *)b;
    if (objects[i].show_below != objects[j].show_below) return objects[j].show_below - objects[i].show_below;
    return i < j ? -1 : i > j;
}

static int ascending(const void *a, const void *b)
{
    uint32_t i = *(const uint32_t *)a, j = *(const uint32_t *)b;
    return i < j ? -1 : i > j;
}

void sky_objects_load(const uint8_t *catalog, size_t bytes)
{
    heap_caps_free(objects);
    heap_caps_free(by_limit);
    heap_caps_free(shown);
    by_limit = shown = NULL;
    shown_count = 0;
    shown_ready = false;
    heap_caps_free(names);
    heap_caps_free(strings);
    objects = NULL;
    names = NULL;
    strings = NULL;
    object_count = name_count = strings_bytes = 0;

    uint32_t n, size;
    const uint8_t *p = sky_find_section(catalog, bytes, SECTION_STRINGS, &n, &size);
    if (p && size > 0 && (strings = heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM))) {
        memcpy(strings, p, size);
        strings[size] = '\0'; // a label cut short still ends
        strings_bytes = size;
    }
    p = sky_find_section(catalog, bytes, SECTION_OBJECTS, &n, NULL);
    if (p && (objects = heap_caps_malloc(n * sizeof(object_t), MALLOC_CAP_SPIRAM))) {
        for (uint32_t i = 0; i < n; i++, p += OBJECT_RECORD_BYTES) {
            object_t *o = &objects[i];
            sky_radec_vector(sky_read_f32(p), sky_read_f32(p + 4), o->v);
            o->label = sky_read_u32(p + 8);
            int16_t mag100;
            uint16_t size10;
            memcpy(&mag100, p + 12, sizeof(mag100));
            memcpy(&size10, p + 14, sizeof(size10));
            o->mag = mag100 == NO_MAGNITUDE ? 99 : mag100 / 100.0f;
            o->size_deg = size10 / 10.0f / 60;
            o->kind = p[18];
            o->show_below = p[19];
            const char *label = string_at(o->label);
            o->messier = label[0] == 'M' && label[1] >= '0' && label[1] <= '9';
        }
        object_count = n;
        by_limit = heap_caps_malloc(n * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
        shown = heap_caps_malloc(n * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
        if (by_limit && shown) {
            for (uint32_t i = 0; i < n; i++) by_limit[i] = i;
            qsort(by_limit, n, sizeof(uint32_t), by_limit_widest_first);
        }
    }
    p = sky_find_section(catalog, bytes, SECTION_NAMES, &n, NULL);
    if (p && (names = heap_caps_malloc(n * sizeof(name_t), MALLOC_CAP_SPIRAM))) {
        for (uint32_t i = 0; i < n; i++, p += NAME_RECORD_BYTES) {
            sky_radec_vector(sky_read_f32(p), sky_read_f32(p + 4), names[i].v);
            names[i].label = sky_read_u32(p + 8);
        }
        name_count = n;
    }
    ESP_LOGI(TAG, "%u objects, %u constellation names, %u bytes of labels", (unsigned)object_count,
             (unsigned)name_count, (unsigned)strings_bytes);
}

// ---- Drawing helpers ----

// A label, drawn after everything else (sky_labels.c), unless it would overlap one
// already placed.
static void draw_label(const char *text, float x, float y, uint32_t rgb, float alpha, bool centred)
{
    sky_labels_place(text, x, y, rgb, alpha, centred);
}

static void draw_ring(float x, float y, float radius, uint32_t rgb, float alpha)
{
    sky_ring(x, y, radius < 2 ? 2 : radius, 2, rgb, alpha, 0);
}

// A dashed circle, as the app's size circles ([3, 3] on a canvas).
static void draw_dashed_ring(float x, float y, float radius, uint32_t rgb, float alpha)
{
    sky_ring(x, y, radius, 1.5f, rgb, alpha, DASH_PX);
}

// The selected target: four short rounded strokes round a gap, in the on-target green, with
// a dark edge first so it stands out over stars and lines (the app's drawTargetMarker). Its
// place comes from the view's distance and direction to the target, so it needs no position
// of its own.
static void draw_target(const sky_frame_t *f, bool night)
{
    const puck_view_msg_t *v = f->view;
    if (isnan(v->target_dist_deg) || isnan(v->target_dir_deg)) return;
    float r = sky_radius_px(f, 2 * v->target_dist_deg);
    float dir = v->target_dir_deg * SKY_DEG_TO_RAD; // clockwise from up, on the screen
    float x = SKY_CENTRE_PX + r * sinf(dir), y = SKY_CENTRE_PX - r * cosf(dir);
    if (x < -CROSSHAIR_LENGTH_PX || y < -CROSSHAIR_LENGTH_PX || x > SKY_SIZE_PX + CROSSHAIR_LENGTH_PX ||
        y > SKY_SIZE_PX + CROSSHAIR_LENGTH_PX) {
        return; // off the screen: the chevrons point to it (draw_pointer)
    }
    static const int8_t arms[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (int pass = 0; pass < 2; pass++) {
        uint32_t color = pass == 0 ? COLOR_OUTLINE : sky_for_night(COLOR_ON_TARGET, night);
        float width = pass == 0 ? 6 * PX_SCALE : 3 * PX_SCALE;
        for (int i = 0; i < 4; i++) {
            sky_line(x + arms[i][0] * CROSSHAIR_GAP_PX, y + arms[i][1] * CROSSHAIR_GAP_PX,
                     x + arms[i][0] * (CROSSHAIR_GAP_PX + CROSSHAIR_LENGTH_PX),
                     y + arms[i][1] * (CROSSHAIR_GAP_PX + CROSSHAIR_LENGTH_PX), width, color, 1);
        }
    }
}

void sky_draw_chevrons(float x, float y, float angle, float distance_deg, bool night)
{
    // Wide gaps mean the target is far away, so move quickly; tight gaps mean slow down.
    float spacing = fminf(CHEVRON_MAX_SPACING_PX,
                          fmaxf(CHEVRON_MIN_SPACING_PX, CHEVRON_MIN_SPACING_PX + distance_deg * CHEVRON_SPACING_PX_PER_DEG)) *
                    PX_SCALE;
    float s = sinf(angle), c = cosf(angle);
    // A point at (across, along) in the chevrons' own frame, where along runs outwards.
    #define CHEVRON_X(across, along) (x + (across) * c + (along) * s)
    #define CHEVRON_Y(across, along) (y + (across) * s - (along) * c)
    uint32_t color = sky_for_night(COLOR_GUIDE, night);
    for (int i = 0; i < 3; i++) {
        float tip = i * spacing + CHEVRON_HEIGHT_PX; // the tip, this far out
        float base = tip - CHEVRON_HEIGHT_PX;
        for (int side = -1; side <= 1; side += 2) {
            sky_line(CHEVRON_X(side * CHEVRON_HALF_WIDTH_PX, base), CHEVRON_Y(side * CHEVRON_HALF_WIDTH_PX, base),
                     CHEVRON_X(0, tip), CHEVRON_Y(0, tip), CHEVRON_WIDTH_PX, color, CHEVRON_FADE[i]);
        }
    }
    #undef CHEVRON_X
    #undef CHEVRON_Y
}

// The way to the target from the reticle, in the normal view, until it's reached (the app's
// drawPointer). The eyepiece view has its own, round its circle (sky_draw.c).
static void draw_pointer(const sky_frame_t *f, bool night, float eyepiece_field_deg)
{
    const puck_view_msg_t *v = f->view;
    if (!f->fade_below || isnan(v->target_dist_deg) || isnan(v->target_dir_deg)) return;
    if (sky_on_target(v, eyepiece_field_deg)) return;
    float rings_deg = (v->flags & PUCK_VIEW_FLAG_RING) && eyepiece_field_deg > 0 ? eyepiece_field_deg : RETICLE_OUTER_DEG;
    float radius = sky_radius_px(f, rings_deg) + POINTER_GAP_PX;
    float angle = v->target_dir_deg * SKY_DEG_TO_RAD;
    float dx = sinf(angle), dy = -cosf(angle);

    // The target's place, as the crosshair's (draw_target).
    float r = sky_radius_px(f, 2 * v->target_dist_deg);
    float tx = SKY_CENTRE_PX + r * dx, ty = SKY_CENTRE_PX + r * dy;
    bool in_view = tx >= 0 && tx <= SKY_SIZE_PX && ty >= 0 && ty <= SKY_SIZE_PX;
    if (in_view && r > radius + POINTER_LINE_MIN_PX) {
        uint32_t color = sky_for_night(COLOR_GUIDE, night);
        for (float at = radius + POINTER_LINE_START_PX; at < r; at += POINTER_DOT_PX + POINTER_DOT_GAP_PX) {
            float to = fminf(r, at + POINTER_DOT_PX);
            sky_line(SKY_CENTRE_PX + at * dx, SKY_CENTRE_PX + at * dy, SKY_CENTRE_PX + to * dx,
                     SKY_CENTRE_PX + to * dy, POINTER_LINE_WIDTH_PX, color, 1);
        }
    }
    sky_draw_chevrons(SKY_CENTRE_PX + dx * (radius + POINTER_CHEVRON_PX), SKY_CENTRE_PX + dy * (radius + POINTER_CHEVRON_PX),
                      angle, v->target_dist_deg, night);
}

// The selected target's label, first, so it wins any clash, and in full even below the
// horizon (the app's drawTargets), beside its crosshair or its size circle.
static void draw_target_label(const sky_frame_t *f, bool night, uint8_t kind, float size_arcmin)
{
    const puck_view_msg_t *v = f->view;
    if (!target_label[0] || isnan(v->target_dist_deg) || isnan(v->target_dir_deg)) return;
    float r = sky_radius_px(f, 2 * v->target_dist_deg);
    float dir = v->target_dir_deg * SKY_DEG_TO_RAD;
    float x = SKY_CENTRE_PX + r * sinf(dir), y = SKY_CENTRE_PX - r * cosf(dir);
    if (x < -40 || y < -40 || x > SKY_SIZE_PX + 40 || y > SKY_SIZE_PX + 40) return;
    bool solar = kind >= KIND_PLANET && kind <= KIND_MOON;
    bool star = kind == KIND_STAR || kind == KIND_DOUBLE_STAR;
    uint32_t color = solar ? SKY_COLOR_SOLAR_SYSTEM : star ? (night ? COLOR_STAR_LABEL_NIGHT : COLOR_STAR_LABEL_DAY)
                                                           : COLOR_DEEP_SKY;
    float marker = size_arcmin > 0 ? sky_radius_px(f, size_arcmin / 60) : 0;
    // A planet's icon reaches 12 of its 24 grid from its middle, its disc being 7 at 7 pixels.
    if (solar) marker = fmaxf(marker, 12 * PX_SCALE);
    float gap = fmaxf(CROSSHAIR_GAP_PX, marker + 3 * PX_SCALE) + TARGET_LABEL_GAP_PX;
    draw_label(target_label, x + gap, y, sky_for_night(color, night), 1, false);
}

// ---- The parts ----

// The horizon's line, with the compass points written under it (the app's drawHorizon).
static void draw_horizon(const sky_frame_t *f, bool night)
{
    uint32_t color = sky_for_night(night ? COLOR_HORIZON_NIGHT : COLOR_HORIZON_DAY, night);
    float last_x = 0, last_y = 0;
    bool last_ok = false;
    for (int az = 0; az <= 360; az += 2) {
        // Altitude 0, as it appears: no refraction to add.
        float local[3] = {sinf(az * SKY_DEG_TO_RAD), cosf(az * SKY_DEG_TO_RAD), 0};
        float x, y;
        bool ok = sky_project_local(f, local, &x, &y);
        // A step that jumps across the screen is the line passing behind the view.
        if (ok && last_ok && fabsf(x - last_x) + fabsf(y - last_y) < SKY_SIZE_PX) {
            sky_line(last_x, last_y, x, y, 2, color, 1);
        }
        last_x = x;
        last_y = y;
        last_ok = ok;
    }
    // The letters in the app's language (texts.c).
    char letters[4][TEXTS_COMPASS_BYTES];
    for (int i = 0; i < 4; i++) {
        texts_compass((texts_compass_t)i, letters[i]);
        float az = i * 90 * SKY_DEG_TO_RAD; // north, east, south, west
        float local[3] = {sinf(az), cosf(az), 0};
        float x, y;
        if (sky_project_local(f, local, &x, &y)) {
            draw_label(letters[i], x, y + 22 * PX_SCALE, sky_for_night(COLOR_CARDINAL, night), 1, true);
        }
    }
}

// The Moon's and planets' labels, beside their discs (sky_draw.c draws the discs).
static void draw_moving_labels(const sky_frame_t *f, bool night, const moving_object_t *moving, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const moving_object_t *o = &moving[i];
        if (o->show_below != 255 && f->view->fov_deg >= o->show_below) continue;
        float x, y;
        bool below;
        if (!sky_project(f, o->v, &x, &y, &below)) continue;
        float disc = fmaxf(7 * PX_SCALE, sky_radius_px(f, o->size_arcmin / 60));
        if (is_target(o->label)) continue; // written first (draw_target_label)
        float alpha = below && f->fade_below ? SKY_BELOW_HORIZON_OPACITY : 1;
        draw_label(o->label, x + disc + 3 * PX_SCALE, y, sky_for_night(SKY_COLOR_SOLAR_SYSTEM, night), alpha,
                   false);
    }
}

// The objects this field shows, in the catalogue's order: those whose limit is wider than
// the field (or 255, always) lead by_limit, so it's how many of them, put back in order.
static void update_shown(float fov_deg)
{
    if (!by_limit || !shown) return;
    size_t lo = 0, hi = object_count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint8_t limit = objects[by_limit[mid]].show_below;
        if (limit == 255 || fov_deg < limit) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (shown_ready && lo == shown_count) return;
    memcpy(shown, by_limit, lo * sizeof(uint32_t));
    qsort(shown, lo, sizeof(uint32_t), ascending);
    shown_count = lo;
    shown_ready = true;
}

// One object's marker (or its size, for the bright and the Messier objects) and its label.
static void draw_object(const sky_frame_t *f, bool night, const candidate_t *c)
{
    const object_t *o = &objects[c->index];
    // The selected target shows in full, its size circle in its own colour, and its label
    // is written already (draw_target_label), as the app's map does.
    bool selected = is_target(string_at(o->label));
    float alpha = c->below && f->fade_below && !selected ? SKY_BELOW_HORIZON_OPACITY : 1;
    bool star = o->kind == KIND_STAR || o->kind == KIND_DOUBLE_STAR;
    uint32_t color = star ? (night ? COLOR_STAR_LABEL_NIGHT : COLOR_STAR_LABEL_DAY) : COLOR_DEEP_SKY;
    bool shows_size = selected || o->messier || o->mag <= MAX_SIZE_CIRCLE_MAG;
    float size_radius = shows_size && o->size_deg > 0 ? sky_radius_px(f, o->size_deg) : 0;
    float marker = size_radius;
    if (size_radius >= MIN_SIZE_CIRCLE_PX) {
        uint32_t ring = selected ? color : night ? COLOR_SIZE_CIRCLE_NIGHT : COLOR_SIZE_CIRCLE_DAY;
        draw_dashed_ring(c->x, c->y, size_radius, sky_for_night(ring, night), alpha);
    } else if (selected) {
        // The crosshair marks it (draw_target).
    } else if (!star) {
        // Stars are already drawn as dots: only other objects need a small marker.
        draw_ring(c->x, c->y, MARKER_RADIUS_PX, sky_for_night(color, night), alpha);
        marker = MARKER_RADIUS_PX;
    }
    if (selected) return;
    draw_label(string_at(o->label), c->x + fmaxf(LABEL_GAP_PX, marker + 3 * PX_SCALE), c->y,
               sky_for_night(color, night), alpha, false);
}

// The catalogue's objects shown at this field, those above the horizon first, so their
// labels win.
static void draw_catalog_objects(const sky_frame_t *f, bool night)
{
    update_shown(f->view->fov_deg);
    size_t n = 0;
    for (size_t k = 0; k < shown_count && n < MAX_CANDIDATES; k++) {
        const object_t *o = &objects[shown[k]];
        if (sky_far_off(f, o->v)) continue;
        candidate_t *c = &candidates[n];
        if (!sky_project(f, o->v, &c->x, &c->y, &c->below)) continue;
        if (c->x < -40 || c->y < -40 || c->x > SKY_SIZE_PX + 40 || c->y > SKY_SIZE_PX + 40) continue;
        c->index = shown[k];
        n++;
    }
    for (int above_pass = 1; above_pass >= 0; above_pass--) {
        for (size_t i = 0; i < n && !sky_labels_full(); i++) {
            if (candidates[i].below != above_pass) draw_object(f, night, &candidates[i]);
        }
    }
}

// Constellation names go last, and give way to every other label (the app's
// drawConstellationNames). Not in the eyepiece view.
static void draw_names(const sky_frame_t *f, bool night)
{
    for (size_t i = 0; i < name_count; i++) {
        if (sky_far_off(f, names[i].v)) continue;
        float x, y;
        bool below;
        if (!sky_project(f, names[i].v, &x, &y, &below)) continue;
        float alpha = below ? SKY_BELOW_HORIZON_OPACITY : 1;
        draw_label(string_at(names[i].label), x, y, sky_for_night(night ? COLOR_NAME_NIGHT : COLOR_NAME_DAY, night),
                   alpha, true);
    }
}

void sky_objects_draw(const sky_frame_t *f, bool night, float eyepiece_field_deg,
                      const moving_object_t *moving, size_t moving_count)
{
    uint8_t kind;
    float size_arcmin;
    taskENTER_CRITICAL(&target_lock);
    memcpy(target_label, target_label_in, sizeof(target_label));
    kind = target_kind_in;
    size_arcmin = target_size_in;
    taskEXIT_CRITICAL(&target_lock);

    sky_labels_begin();
    draw_target_label(f, night, kind, size_arcmin); // first: it wins every clash
    draw_horizon(f, night); // in the eyepiece view too, as the app's
    draw_moving_labels(f, night, moving, moving_count);
    draw_catalog_objects(f, night);
    if (f->fade_below) draw_names(f, night);
    draw_target(f, night);
    draw_pointer(f, night, eyepiece_field_deg);
}
