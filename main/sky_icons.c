// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The planets' icons: see sky_icons.h.

#include "sky_icons.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "json_read.h"
#include "sky_pixels.h"

static const char *TAG = "sky_icons";

#define ICONS_MAX 16
#define ICON_BODY_BYTES 24
#define ICON_LINES_MAX 12
#define ICON_POINTS_MAX 256 // the asteroid, the busiest, has about 125
// The app's icon grid (target-icons.ts): 24 across, its disc 7 in radius about the centre.
#define ICON_CENTRE 12.0f
#define ICON_DISC_RADIUS 7.0f
// As the app draws them (draw-targets.ts), in the puck's pixels (1.5 a phone's CSS pixel):
// lines 1.6 wide over a dark edge 3 wider, and dashes of 3 with gaps of 4.
#define PX_SCALE 1.5f
#define LINE_PX (1.6f * PX_SCALE)
#define EDGE_PX ((1.6f + 3) * PX_SCALE)
#define DASH_PX (3 * PX_SCALE)
#define GAP_PX (4 * PX_SCALE)
#define COLOR_EDGE 0x05070F

#define LINE_DASHED 1
#define LINE_FILLED 2

typedef struct {
    char body[ICON_BODY_BYTES];
    uint8_t line_count;
    uint8_t line_flags[ICON_LINES_MAX];
    uint16_t line_start[ICON_LINES_MAX + 1]; // each line's first point, and the end
    int16_t points[ICON_POINTS_MAX][2];      // in tenths of the grid
} icon_t;

static icon_t *icons; // in PSRAM: about 17 KB
static size_t icon_count;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

// Reads the numbers of "p":[…] from p, into icon from its next point. Returns where the
// array ends, or NULL.
static const char *read_points(const char *p, icon_t *icon)
{
    uint16_t n = icon->line_start[icon->line_count];
    while (*p && *p != ']') {
        char *end;
        float x = strtof(p, &end);
        if (end == p) return NULL;
        p = end + (*end == ',');
        float y = strtof(p, &end);
        if (end == p) return NULL;
        p = end + (*end == ',');
        if (n < ICON_POINTS_MAX) {
            icon->points[n][0] = (int16_t)lroundf(x * 10);
            icon->points[n][1] = (int16_t)lroundf(y * 10);
            n++;
        }
    }
    icon->line_start[icon->line_count + 1] = n;
    return *p == ']' ? p + 1 : NULL;
}

void sky_icons_parse(const char *json)
{
    static icon_t parsed; // the link task's: too big for its stack
    memset(&parsed, 0, sizeof(parsed));
    if (!json_string(json, "body", parsed.body, sizeof(parsed.body))) return;
    const char *p = strstr(json, "\"paths\":[");
    while (p && parsed.line_count < ICON_LINES_MAX && (p = strstr(p, "\"p\":[")) != NULL) {
        p = read_points(p + 5, &parsed);
        if (!p) break;
        // Its flags follow its points, before the line's closing brace.
        const char *close = strchr(p, '}');
        size_t len = close ? (size_t)(close - p) : 0;
        uint8_t flags = 0;
        if (len && memmem(p, len, "\"dashed\":true", 13)) flags |= LINE_DASHED;
        if (len && memmem(p, len, "\"fill\":true", 11)) flags |= LINE_FILLED;
        parsed.line_flags[parsed.line_count++] = flags;
    }
    if (!icons) icons = heap_caps_calloc(ICONS_MAX, sizeof(icon_t), MALLOC_CAP_SPIRAM);
    if (!icons || parsed.line_count == 0) return;
    taskENTER_CRITICAL(&lock);
    size_t slot = 0;
    while (slot < icon_count && strcmp(icons[slot].body, parsed.body) != 0) slot++;
    if (slot < ICONS_MAX) {
        icons[slot] = parsed;
        if (slot == icon_count) icon_count++;
    }
    taskEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Icon for %s: %u lines, %u points", parsed.body, parsed.line_count,
             parsed.line_start[parsed.line_count]);
}

// The dashes and gaps scale with the lines: the app's [1.5, 2] on its grid, doubled.
#define DASH_PER_LINE (DASH_PX / LINE_PX)
#define GAP_PER_LINE (GAP_PX / LINE_PX)

// A line, dashed: dashes and gaps carry on from one step to the next.
static void dashed_line(const float (*xy)[2], int n, float width, float dash, float gap, bool edge,
                        sky_icons_stroke_fn stroke, void *ctx)
{
    float phase = 0; // how far along the pattern
    for (int i = 1; i < n; i++) {
        float dx = xy[i][0] - xy[i - 1][0], dy = xy[i][1] - xy[i - 1][1];
        float length = sqrtf(dx * dx + dy * dy);
        for (float at = 0; at < length;) {
            float in_pattern = fmodf(phase + at, dash + gap);
            bool on = in_pattern < dash;
            float step = fminf(length - at, on ? dash - in_pattern : dash + gap - in_pattern);
            if (step <= 0) break;
            if (on) {
                float a = at / length, b = (at + step) / length;
                stroke(xy[i - 1][0] + dx * a, xy[i - 1][1] + dy * a, xy[i - 1][0] + dx * b, xy[i - 1][1] + dy * b,
                       width, edge, ctx);
            }
            at += step;
        }
        phase += length;
    }
}

// Fills a closed line, row by row: where each row crosses its edges, filled between each
// pair (even-odd), so a crescent fills as well as a round spot.
static void fill_rows(const float (*xy)[2], int n, bool edge, sky_icons_stroke_fn stroke, void *ctx)
{
    float top = xy[0][1], bottom = xy[0][1];
    for (int k = 1; k < n; k++) {
        top = fminf(top, xy[k][1]);
        bottom = fmaxf(bottom, xy[k][1]);
    }
    float crossings[ICON_POINTS_MAX];
    for (float row = floorf(top) + 0.5f; row < bottom; row += 1) {
        int count = 0;
        for (int k = 1; k < n; k++) {
            float y0 = xy[k - 1][1], y1 = xy[k][1];
            if ((y0 <= row) == (y1 <= row)) continue;
            crossings[count++] = xy[k - 1][0] + (row - y0) / (y1 - y0) * (xy[k][0] - xy[k - 1][0]);
        }
        // Few crossings: a simple sort.
        for (int i = 1; i < count; i++) {
            for (int j = i; j > 0 && crossings[j - 1] > crossings[j]; j--) {
                float t = crossings[j];
                crossings[j] = crossings[j - 1];
                crossings[j - 1] = t;
            }
        }
        for (int i = 0; i + 1 < count; i += 2) stroke(crossings[i], row, crossings[i + 1], row, 1, edge, ctx);
    }
}

// The icon by this name, copied, so the link can change the table meanwhile. In the LVGL
// task only, which draws both the map and the menu.
static const icon_t *find(const char *name)
{
    static icon_t icon;
    bool found = false;
    taskENTER_CRITICAL(&lock);
    for (size_t i = 0; i < icon_count && !found; i++) {
        if (strcmp(icons[i].body, name) == 0) {
            icon = icons[i];
            found = true;
        }
    }
    taskEXIT_CRITICAL(&lock);
    return found ? &icon : NULL;
}

bool sky_icons_has(const char *name)
{
    bool found = false;
    taskENTER_CRITICAL(&lock);
    for (size_t i = 0; i < icon_count && !found; i++) found = strcmp(icons[i].body, name) == 0;
    taskEXIT_CRITICAL(&lock);
    return found;
}

bool sky_icons_trace(const char *name, float x, float y, float disc_px, float line_px, float edge_px,
                     sky_icons_stroke_fn stroke, void *ctx)
{
    const icon_t *icon = find(name);
    if (!icon) return false;
    // From the grid's tenths to the screen, about (x, y).
    float scale = disc_px / ICON_DISC_RADIUS / 10;
    static float xy[ICON_POINTS_MAX][2];
    for (int pass = edge_px > 0 ? 0 : 1; pass < 2; pass++) {
        bool edge = pass == 0;
        float width = edge ? edge_px : line_px;
        for (int l = 0; l < icon->line_count; l++) {
            int first = icon->line_start[l], n = icon->line_start[l + 1] - first;
            if (n < 2) continue;
            for (int k = 0; k < n; k++) {
                xy[k][0] = x + (icon->points[first + k][0] - ICON_CENTRE * 10) * scale;
                xy[k][1] = y + (icon->points[first + k][1] - ICON_CENTRE * 10) * scale;
            }
            if ((icon->line_flags[l] & LINE_FILLED) && !edge) fill_rows(xy, n, edge, stroke, ctx);
            if (icon->line_flags[l] & LINE_DASHED) {
                dashed_line(xy, n, width, DASH_PER_LINE * line_px, GAP_PER_LINE * line_px, edge, stroke, ctx);
            } else {
                for (int k = 1; k < n; k++) stroke(xy[k - 1][0], xy[k - 1][1], xy[k][0], xy[k][1], width, edge, ctx);
            }
        }
    }
    return true;
}

typedef struct {
    uint32_t rgb;
    float alpha;
} map_colour_t;

static void stroke_on_map(float x0, float y0, float x1, float y1, float width, bool edge, void *ctx)
{
    const map_colour_t *c = ctx;
    sky_line(x0, y0, x1, y1, width, edge ? COLOR_EDGE : c->rgb, c->alpha);
}

bool sky_icons_draw(const char *body, float x, float y, float disc_px, uint32_t rgb, float alpha)
{
    map_colour_t colour = {rgb, alpha};
    return sky_icons_trace(body, x, y, disc_px, LINE_PX, EDGE_PX, stroke_on_map, &colour);
}
