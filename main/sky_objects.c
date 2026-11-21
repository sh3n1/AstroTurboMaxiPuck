// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The sky map's top layer: see sky_objects.h.

#include "sky_objects.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
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

// Sizes, as the app's (draw-targets.ts), in the puck's pixels: its pixels are smaller than a
// phone's CSS pixels (see STAR_SCALE in sky_draw.c).
#define PX_SCALE 1.5f
#define MIN_SIZE_CIRCLE_PX (6 * PX_SCALE)
#define MAX_SIZE_CIRCLE_MAG 6.0f
#define MARKER_RADIUS_PX (4 * PX_SCALE)
#define LABEL_GAP_PX (9 * PX_SCALE)
// Labels: as many as fit without overlapping, up to this many, which keeps the frame quick.
#define MAX_LABELS 48
#define LABEL_FONT (&lv_font_montserrat_20)
// Size circles are dashed, as the app's ([3, 3] on a canvas): this long a dash and gap.
#define DASH_PX (3 * PX_SCALE)
#define MAX_DASHES 120
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
static name_t *names;
static size_t name_count;
static char *strings;     // our copy of the catalogue's strings
static size_t strings_bytes;

// The room the labels drawn so far took, so later ones don't overlap them.
typedef struct {
    float left, top, right, bottom;
} box_t;
static box_t boxes[MAX_LABELS];
static size_t box_count;

static const char *string_at(uint32_t offset)
{
    return offset < strings_bytes ? strings + offset : "";
}

void sky_objects_load(const uint8_t *catalog, size_t bytes)
{
    heap_caps_free(objects);
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

static bool overlaps(const box_t *a, const box_t *b)
{
    return a->left < b->right && b->left < a->right && a->top < b->bottom && b->top < a->bottom;
}

// Writes a label with its left edge at x, centred on y, unless it would overlap one already
// written. A dark outline keeps it readable over lines and stars, as the app's drawLabel.
static bool draw_label(lv_layer_t *layer, const char *text, float x, float y, uint32_t rgb, float alpha,
                       bool centred)
{
    if (!text[0] || box_count >= MAX_LABELS) return false;
    lv_point_t size;
    lv_text_get_size(&size, text, LABEL_FONT, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (centred) x -= size.x / 2.0f;
    box_t box = {x, y - size.y / 2.0f, x + size.x, y + size.y / 2.0f};
    if (box.right < 0 || box.left > SKY_SIZE_PX || box.bottom < 0 || box.top > SKY_SIZE_PX) return false;
    for (size_t i = 0; i < box_count; i++) {
        if (overlaps(&box, &boxes[i])) return false;
    }
    boxes[box_count++] = box;

    lv_draw_label_dsc_t label;
    lv_draw_label_dsc_init(&label);
    label.font = LABEL_FONT;
    label.text = text;
    label.opa = (lv_opa_t)(LV_OPA_COVER * alpha);
    // A dark backing, rounded, so the text stays readable over lines and stars, as the app's
    // dark outline does. Drawing the text four more times, offset, as an outline, made labels
    // two thirds of the frame's time.
    lv_draw_rect_dsc_t back;
    lv_draw_rect_dsc_init(&back);
    back.bg_color = lv_color_hex(COLOR_OUTLINE);
    back.bg_opa = (lv_opa_t)(LV_OPA_60 * alpha);
    back.radius = 4;
    lv_area_t back_area = {(int32_t)box.left - 2, (int32_t)box.top, (int32_t)box.right + 2, (int32_t)box.bottom};
    lv_draw_rect(layer, &back, &back_area);
    sky_layer_step(layer);
    label.color = lv_color_hex(rgb);
    lv_area_t area = {(int32_t)box.left, (int32_t)box.top, (int32_t)box.right, (int32_t)box.bottom};
    lv_draw_label(layer, &label, &area);
    sky_layer_step(layer);
    return true;
}

static void draw_ring(lv_layer_t *layer, float x, float y, float radius, uint32_t rgb, float alpha)
{
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = (int32_t)x;
    arc.center.y = (int32_t)y;
    arc.radius = (uint16_t)(radius < 2 ? 2 : radius);
    arc.start_angle = 0;
    arc.end_angle = 360;
    arc.width = 2;
    arc.color = lv_color_hex(rgb);
    arc.opa = (lv_opa_t)(LV_OPA_COVER * alpha);
    lv_draw_arc(layer, &arc);
    sky_layer_step(layer);
}

// A dashed circle: as many short arcs as fit, up to MAX_DASHES.
static void draw_dashed_ring(lv_layer_t *layer, float x, float y, float radius, uint32_t rgb, float alpha)
{
    int dashes = (int)(2 * (float)M_PI * radius / (2 * DASH_PX));
    if (dashes < 4) {
        draw_ring(layer, x, y, radius, rgb, alpha);
        return;
    }
    if (dashes > MAX_DASHES) dashes = MAX_DASHES;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = (int32_t)x;
    arc.center.y = (int32_t)y;
    arc.radius = (uint16_t)radius;
    arc.width = 1;
    arc.color = lv_color_hex(rgb);
    arc.opa = (lv_opa_t)(LV_OPA_COVER * alpha);
    float step = 360.0f / dashes;
    for (int i = 0; i < dashes; i++) {
        arc.start_angle = i * step;
        arc.end_angle = i * step + step / 2;
        lv_draw_arc(layer, &arc);
        sky_layer_step(layer);
    }
}

// The selected target: four short rounded strokes round a gap, in the on-target green, with
// a dark edge first so it stands out over stars and lines (the app's drawTargetMarker). Its
// place comes from the view's distance and direction to the target, so it needs no position
// of its own.
static void draw_target(lv_layer_t *layer, const sky_frame_t *f, bool night)
{
    const puck_view_msg_t *v = f->view;
    if (isnan(v->target_dist_deg) || isnan(v->target_dir_deg)) return;
    float r = sky_radius_px(f, 2 * v->target_dist_deg);
    float dir = v->target_dir_deg * SKY_DEG_TO_RAD; // clockwise from up, on the screen
    float x = SKY_CENTRE_PX + r * sinf(dir), y = SKY_CENTRE_PX - r * cosf(dir);
    if (x < -CROSSHAIR_LENGTH_PX || y < -CROSSHAIR_LENGTH_PX || x > SKY_SIZE_PX + CROSSHAIR_LENGTH_PX ||
        y > SKY_SIZE_PX + CROSSHAIR_LENGTH_PX) {
        return; // off the screen: the chevrons point to it (task 11)
    }
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.round_start = line.round_end = 1;
    static const int8_t arms[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (int pass = 0; pass < 2; pass++) {
        line.color = lv_color_hex(pass == 0 ? COLOR_OUTLINE : sky_for_night(COLOR_ON_TARGET, night));
        line.width = (int32_t)(pass == 0 ? 6 * PX_SCALE : 3 * PX_SCALE);
        for (int i = 0; i < 4; i++) {
            line.p1.x = x + arms[i][0] * CROSSHAIR_GAP_PX;
            line.p1.y = y + arms[i][1] * CROSSHAIR_GAP_PX;
            line.p2.x = x + arms[i][0] * (CROSSHAIR_GAP_PX + CROSSHAIR_LENGTH_PX);
            line.p2.y = y + arms[i][1] * (CROSSHAIR_GAP_PX + CROSSHAIR_LENGTH_PX);
            lv_draw_line(layer, &line);
            sky_layer_step(layer);
        }
    }
}

void sky_draw_chevrons(lv_layer_t *layer, float x, float y, float angle, float distance_deg, bool night)
{
    // Wide gaps mean the target is far away, so move quickly; tight gaps mean slow down.
    float spacing = fminf(CHEVRON_MAX_SPACING_PX,
                          fmaxf(CHEVRON_MIN_SPACING_PX, CHEVRON_MIN_SPACING_PX + distance_deg * CHEVRON_SPACING_PX_PER_DEG)) *
                    PX_SCALE;
    float s = sinf(angle), c = cosf(angle);
    // A point at (across, along) in the chevrons' own frame, where along runs outwards.
    #define CHEVRON_X(across, along) (x + (across) * c + (along) * s)
    #define CHEVRON_Y(across, along) (y + (across) * s - (along) * c)
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(sky_for_night(COLOR_GUIDE, night));
    line.width = (int32_t)CHEVRON_WIDTH_PX;
    line.round_start = line.round_end = 1;
    for (int i = 0; i < 3; i++) {
        float tip = i * spacing + CHEVRON_HEIGHT_PX; // the tip, this far out
        float base = tip - CHEVRON_HEIGHT_PX;
        line.opa = (lv_opa_t)(LV_OPA_COVER * CHEVRON_FADE[i]);
        for (int side = -1; side <= 1; side += 2) {
            line.p1.x = CHEVRON_X(side * CHEVRON_HALF_WIDTH_PX, base);
            line.p1.y = CHEVRON_Y(side * CHEVRON_HALF_WIDTH_PX, base);
            line.p2.x = CHEVRON_X(0, tip);
            line.p2.y = CHEVRON_Y(0, tip);
            lv_draw_line(layer, &line);
            sky_layer_step(layer);
        }
    }
    #undef CHEVRON_X
    #undef CHEVRON_Y
}

// ---- The parts ----

// The horizon's line, with the compass points written under it (the app's drawHorizon).
static void draw_horizon(lv_layer_t *layer, const sky_frame_t *f, bool night)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(sky_for_night(night ? COLOR_HORIZON_NIGHT : COLOR_HORIZON_DAY, night));
    line.width = 2;
    float last_x = 0, last_y = 0;
    bool last_ok = false;
    for (int az = 0; az <= 360; az += 2) {
        // Altitude 0, as it appears: no refraction to add.
        float local[3] = {sinf(az * SKY_DEG_TO_RAD), cosf(az * SKY_DEG_TO_RAD), 0};
        float x, y;
        bool ok = sky_project_local(f, local, &x, &y);
        // A step that jumps across the screen is the line passing behind the view.
        if (ok && last_ok && fabsf(x - last_x) + fabsf(y - last_y) < SKY_SIZE_PX) {
            line.p1.x = last_x;
            line.p1.y = last_y;
            line.p2.x = x;
            line.p2.y = y;
            lv_draw_line(layer, &line);
            sky_layer_step(layer);
        }
        last_x = x;
        last_y = y;
        last_ok = ok;
    }
    // The letters in the app's language (texts.c). Kept until the layer is drawn: LVGL
    // reads a label's text only then.
    static char letters[4][TEXTS_COMPASS_BYTES];
    for (int i = 0; i < 4; i++) {
        texts_compass((texts_compass_t)i, letters[i]);
        float az = i * 90 * SKY_DEG_TO_RAD; // north, east, south, west
        float local[3] = {sinf(az), cosf(az), 0};
        float x, y;
        if (sky_project_local(f, local, &x, &y)) {
            draw_label(layer, letters[i], x, y + 22 * PX_SCALE, sky_for_night(COLOR_CARDINAL, night), 1, true);
        }
    }
}

// The Moon's and planets' labels, beside their discs (sky_draw.c draws the discs).
static void draw_moving_labels(lv_layer_t *layer, const sky_frame_t *f, bool night,
                               const moving_object_t *moving, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const moving_object_t *o = &moving[i];
        if (o->show_below != 255 && f->view->fov_deg >= o->show_below) continue;
        float x, y;
        bool below;
        if (!sky_project(f, o->v, &x, &y, &below)) continue;
        float disc = fmaxf(7 * PX_SCALE, sky_radius_px(f, o->size_arcmin / 60));
        float alpha = below && f->fade_below ? SKY_BELOW_HORIZON_OPACITY : 1;
        draw_label(layer, o->label, x + disc + 3 * PX_SCALE, y, sky_for_night(SKY_COLOR_SOLAR_SYSTEM, night), alpha,
                   false);
    }
}

// The catalogue's objects shown at this field: a marker (or their size, for the bright and
// the Messier objects) and their label. Those above the horizon first, so their labels win.
static void draw_catalog_objects(lv_layer_t *layer, const sky_frame_t *f, bool night, bool above_pass)
{
    for (size_t i = 0; i < object_count && box_count < MAX_LABELS; i++) {
        const object_t *o = &objects[i];
        if (o->show_below != 255 && f->view->fov_deg >= o->show_below) continue;
        if (sky_far_off(f, o->v)) continue;
        float x, y;
        bool below;
        if (!sky_project(f, o->v, &x, &y, &below)) continue;
        if (below == above_pass) continue;
        if (x < -40 || y < -40 || x > SKY_SIZE_PX + 40 || y > SKY_SIZE_PX + 40) continue;

        float alpha = below && f->fade_below ? SKY_BELOW_HORIZON_OPACITY : 1;
        bool star = o->kind == KIND_STAR || o->kind == KIND_DOUBLE_STAR;
        uint32_t color = star ? (night ? COLOR_STAR_LABEL_NIGHT : COLOR_STAR_LABEL_DAY) : COLOR_DEEP_SKY;
        bool shows_size = o->messier || o->mag <= MAX_SIZE_CIRCLE_MAG;
        float size_radius = shows_size && o->size_deg > 0 ? sky_radius_px(f, o->size_deg) : 0;
        float marker = size_radius;
        if (size_radius >= MIN_SIZE_CIRCLE_PX) {
            draw_dashed_ring(layer, x, y, size_radius,
                             sky_for_night(night ? COLOR_SIZE_CIRCLE_NIGHT : COLOR_SIZE_CIRCLE_DAY, night), alpha);
        } else if (!star) {
            // Stars are already drawn as dots: only other objects need a small marker.
            draw_ring(layer, x, y, MARKER_RADIUS_PX, sky_for_night(color, night), alpha);
            marker = MARKER_RADIUS_PX;
        }
        draw_label(layer, string_at(o->label), x + fmaxf(LABEL_GAP_PX, marker + 3 * PX_SCALE), y,
                   sky_for_night(color, night), alpha, false);
    }
}

// Constellation names go last, and give way to every other label (the app's
// drawConstellationNames). Not in the eyepiece view.
static void draw_names(lv_layer_t *layer, const sky_frame_t *f, bool night)
{
    for (size_t i = 0; i < name_count; i++) {
        if (sky_far_off(f, names[i].v)) continue;
        float x, y;
        bool below;
        if (!sky_project(f, names[i].v, &x, &y, &below)) continue;
        float alpha = below ? SKY_BELOW_HORIZON_OPACITY : 1;
        draw_label(layer, string_at(names[i].label), x, y, sky_for_night(night ? COLOR_NAME_NIGHT : COLOR_NAME_DAY, night),
                   alpha, true);
    }
}

void sky_objects_draw(lv_layer_t *layer, const sky_frame_t *f, bool night,
                      const moving_object_t *moving, size_t moving_count)
{
    box_count = 0;
    draw_horizon(layer, f, night); // in the eyepiece view too, as the app's
    draw_moving_labels(layer, f, night, moving, moving_count);
    draw_catalog_objects(layer, f, night, true);
    draw_catalog_objects(layer, f, night, false);
    if (f->fade_below) draw_names(layer, f, night);
    draw_target(layer, f, night);
}
