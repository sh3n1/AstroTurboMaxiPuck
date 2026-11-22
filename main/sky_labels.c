// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The sky map's labels, rendered once and kept: see sky_labels.h.

#include "sky_labels.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fonts.h"
#include "sky_pixels.h"

static const char *TAG = "labels";

#define LABEL_FONT (&noto_sans_20) // with Greek, for star names
// As many labels a frame as fit without overlapping, up to this many.
#define MAX_LABELS 48
// The dark backing behind each label, so it stays readable over lines and stars, as the
// app's dark outline does: this colour, this opaque, its corners rounded this much.
#define COLOR_BACKING 0x05070F
#define BACKING_ALPHA 0.6f
#define BACKING_RADIUS 4
#define BACKING_MARGIN 2
// The kept texts: a table this big, emptied when it is three quarters full or holds this
// many bytes of masks. A whole sky's labels are a few hundred.
#define CACHE_SLOTS 2048
#define CACHE_MAX_BYTES (2 * 1024 * 1024)

typedef struct {
    uint32_t hash;  // 0: an empty slot
    char *text;
    uint16_t w, h, stride;
    uint8_t *mask;  // NULL until first drawn
} entry_t;

typedef struct {
    float left, top, right, bottom;
} box_t;

typedef struct {
    entry_t *entry;
    int x, y;       // top left
    uint16_t color; // RGB565
    float alpha;
} placed_t;

static lv_obj_t *text_canvas;
static entry_t *cache; // CACHE_SLOTS, in PSRAM
static size_t cache_count, cache_bytes;

static box_t boxes[MAX_LABELS];
static placed_t placed[MAX_LABELS];
static size_t placed_count;

bool sky_labels_create(lv_obj_t *screen)
{
    cache = heap_caps_calloc(CACHE_SLOTS, sizeof(entry_t), MALLOC_CAP_SPIRAM);
    if (!cache) return false;
    text_canvas = lv_canvas_create(screen);
    lv_obj_set_hidden(text_canvas, true);
    return true;
}

// FNV-1a; never 0, which marks an empty slot.
static uint32_t hash_text(const char *text)
{
    uint32_t h = 2166136261u;
    while (*text) h = (h ^ (uint8_t)*text++) * 16777619u;
    return h ? h : 1;
}

static void empty_cache(void)
{
    for (size_t i = 0; i < CACHE_SLOTS; i++) {
        heap_caps_free(cache[i].text);
        heap_caps_free(cache[i].mask);
    }
    memset(cache, 0, CACHE_SLOTS * sizeof(entry_t));
    cache_count = cache_bytes = 0;
}

// The kept entry for a text, with its size measured; a new one when it isn't kept yet.
static entry_t *find(const char *text)
{
    uint32_t h = hash_text(text);
    size_t i = h % CACHE_SLOTS;
    while (cache[i].hash) {
        if (cache[i].hash == h && strcmp(cache[i].text, text) == 0) return &cache[i];
        i = (i + 1) % CACHE_SLOTS;
    }
    size_t length = strlen(text);
    char *copy = heap_caps_malloc(length + 1, MALLOC_CAP_SPIRAM);
    if (!copy) return NULL;
    memcpy(copy, text, length + 1);
    lv_point_t size;
    lv_text_get_size(&size, text, LABEL_FONT, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    entry_t *e = &cache[i];
    *e = (entry_t){.hash = h, .text = copy, .w = (uint16_t)size.x, .h = (uint16_t)size.y};
    cache_count++;
    return e;
}

// Renders an entry's text into its mask, by LVGL, the first time it is drawn.
static bool render(entry_t *e)
{
    if (e->mask) return true;
    if (e->w == 0 || e->h == 0) return false;
    uint32_t stride = lv_draw_buf_width_to_stride(e->w, LV_COLOR_FORMAT_A8);
    e->mask = heap_caps_aligned_calloc(LV_DRAW_BUF_ALIGN, 1, stride * e->h, MALLOC_CAP_SPIRAM);
    if (!e->mask) return false;
    e->stride = (uint16_t)stride;
    cache_bytes += stride * e->h;

    lv_draw_buf_t buf;
    lv_draw_buf_init(&buf, e->w, e->h, LV_COLOR_FORMAT_A8, stride, e->mask, stride * e->h);
    lv_canvas_set_draw_buf(text_canvas, &buf);
    lv_layer_t layer;
    lv_canvas_init_layer(text_canvas, &layer);
    lv_draw_label_dsc_t label;
    lv_draw_label_dsc_init(&label);
    label.font = LABEL_FONT;
    label.text = e->text;
    label.color = lv_color_white();
    lv_area_t area = {0, 0, e->w - 1, e->h - 1};
    lv_draw_label(&layer, &label, &area);
    lv_canvas_finish_layer(text_canvas, &layer);
    return true;
}

void sky_labels_begin(void)
{
    placed_count = 0;
    // Emptied between frames only, as placed labels point into it. A frame adds at most
    // MAX_LABELS, so there is always room for one.
    if (cache_count + MAX_LABELS > CACHE_SLOTS * 3 / 4 || cache_bytes > CACHE_MAX_BYTES) {
        ESP_LOGI(TAG, "Emptying the kept labels (%u, %u bytes)", (unsigned)cache_count, (unsigned)cache_bytes);
        empty_cache();
    }
}

bool sky_labels_full(void)
{
    return placed_count >= MAX_LABELS;
}

static bool overlaps(const box_t *a, const box_t *b)
{
    return a->left < b->right && b->left < a->right && a->top < b->bottom && b->top < a->bottom;
}

bool sky_labels_place(const char *text, float x, float y, uint32_t rgb, float alpha, bool centred)
{
    if (!text[0] || placed_count >= MAX_LABELS || !cache) return false;
    entry_t *e = find(text);
    if (!e) return false;
    if (centred) x -= e->w / 2.0f;
    box_t box = {x, y - e->h / 2.0f, x + e->w, y + e->h / 2.0f};
    if (box.right < 0 || box.left > SKY_SIZE_PX || box.bottom < 0 || box.top > SKY_SIZE_PX) return false;
    for (size_t i = 0; i < placed_count; i++) {
        if (overlaps(&box, &boxes[i])) return false;
    }
    boxes[placed_count] = box;
    placed[placed_count++] = (placed_t){e, (int)box.left, (int)box.top, sky_rgb565(rgb), alpha};
    return true;
}

// True when (px, py) of a w × h box is cut off by its rounded corners.
static bool outside_corner(int px, int py, int w, int h)
{
    int cx = px < BACKING_RADIUS ? BACKING_RADIUS : px >= w - BACKING_RADIUS ? w - 1 - BACKING_RADIUS : px;
    int cy = py < BACKING_RADIUS ? BACKING_RADIUS : py >= h - BACKING_RADIUS ? h - 1 - BACKING_RADIUS : py;
    int dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy > BACKING_RADIUS * BACKING_RADIUS;
}

static void draw_one(const placed_t *p)
{
    entry_t *e = p->entry;
    if (!render(e)) return;

    // The backing, a little wider than the text.
    int bx = p->x - BACKING_MARGIN, bw = e->w + 2 * BACKING_MARGIN, bh = e->h;
    uint16_t back = sky_rgb565(COLOR_BACKING);
    uint32_t back_a = (uint32_t)(BACKING_ALPHA * p->alpha * 32 + 0.5f);
    for (int j = 0; j < bh; j++) {
        int py = p->y + j;
        if (py < 0 || py >= SKY_SIZE_PX) continue;
        uint16_t *row = sky_pixels + py * SKY_SIZE_PX;
        for (int i = 0; i < bw; i++) {
            int px = bx + i;
            if (px < 0 || px >= SKY_SIZE_PX || outside_corner(i, j, bw, bh)) continue;
            sky_blend565(&row[px], back, back_a);
        }
    }

    // The text, by its coverage.
    uint32_t text_a = (uint32_t)(p->alpha * 32 + 0.5f);
    for (int j = 0; j < e->h; j++) {
        int py = p->y + j;
        if (py < 0 || py >= SKY_SIZE_PX) continue;
        const uint8_t *m = e->mask + j * e->stride;
        uint16_t *row = sky_pixels + py * SKY_SIZE_PX;
        for (int i = 0; i < e->w; i++) {
            int px = p->x + i;
            if (!m[i] || px < 0 || px >= SKY_SIZE_PX) continue;
            sky_blend565(&row[px], p->color, (m[i] * text_a + 127) / 255);
        }
    }
}

void sky_labels_draw(void)
{
    for (size_t i = 0; i < placed_count; i++) draw_one(&placed[i]);
}
