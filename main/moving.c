// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// What moves on the map: see moving.h.

#include "moving.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "json_read.h"

#define DEG_TO_RAD 0.017453292f
// One item of the list, as the app writes it, is well under this.
#define ITEM_MAX_BYTES 512

static moving_object_t objects[MOVING_MAX];
static size_t count;
static float sun[3];
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

static void radec_vector(float ra_deg, float dec_deg, float v[3])
{
    float ra = ra_deg * DEG_TO_RAD, dec = dec_deg * DEG_TO_RAD;
    v[0] = cosf(dec) * cosf(ra);
    v[1] = cosf(dec) * sinf(ra);
    v[2] = sinf(dec);
}

// One item: {"id":"Moon","label":"Moon","kind":10,"raDeg":..,"decDeg":..,"mag":..,...}
static bool parse_item(const char *item, moving_object_t *o)
{
    float ra, dec;
    uint32_t kind = 0, show_below = 255;
    if (!json_float(item, "raDeg", &ra) || !json_float(item, "decDeg", &dec)) return false;
    radec_vector(ra, dec, o->v);
    if (!json_string(item, "label", o->label, sizeof(o->label))) o->label[0] = '\0';
    json_uint(item, "kind", &kind);
    json_uint(item, "showBelowFieldDeg", &show_below);
    o->kind = kind > 255 ? 0 : (uint8_t)kind;
    o->show_below = show_below > 255 ? 255 : (uint8_t)show_below;
    if (!json_float(item, "mag", &o->mag)) o->mag = NAN;
    if (!json_float(item, "sizeArcmin", &o->size_arcmin)) o->size_arcmin = 0;
    if (!json_float(item, "phase", &o->phase)) o->phase = NAN;
    return true;
}

size_t moving_parse(const char *json)
{
    moving_object_t parsed[MOVING_MAX];
    size_t n = 0;
    float sun_v[3] = {1, 0, 0};

    // "sun":{"raDeg":..,"decDeg":..}: read inside its braces only, as items have raDeg too.
    const char *s = strstr(json, "\"sun\":{");
    if (s) {
        const char *end = strchr(s, '}');
        char sun_text[96];
        size_t len = end ? (size_t)(end - s) : 0;
        if (len > 0 && len < sizeof(sun_text)) {
            memcpy(sun_text, s, len);
            sun_text[len] = '\0';
            float ra, dec;
            if (json_float(sun_text, "raDeg", &ra) && json_float(sun_text, "decDeg", &dec)) {
                radec_vector(ra, dec, sun_v);
            }
        }
    }

    // "items":[{...},{...}]: each item is flat, so it ends at its first '}'.
    const char *p = strstr(json, "\"items\":[");
    while (p && n < MOVING_MAX) {
        const char *open = strchr(p, '{');
        if (!open) break;
        const char *close = strchr(open, '}');
        if (!close) break;
        size_t len = (size_t)(close - open + 1);
        p = close + 1;
        if (len >= ITEM_MAX_BYTES) continue;
        char item[ITEM_MAX_BYTES];
        memcpy(item, open, len);
        item[len] = '\0';
        if (parse_item(item, &parsed[n])) n++;
        // The list ends at its ']'.
        while (*p == ',' || *p == ' ') p++;
        if (*p == ']') break;
    }

    taskENTER_CRITICAL(&lock);
    memcpy(objects, parsed, n * sizeof(parsed[0]));
    count = n;
    memcpy(sun, sun_v, sizeof(sun));
    taskEXIT_CRITICAL(&lock);
    return n;
}

size_t moving_get(moving_object_t *out, float sun_out[3])
{
    taskENTER_CRITICAL(&lock);
    size_t n = count;
    memcpy(out, objects, n * sizeof(objects[0]));
    memcpy(sun_out, sun, sizeof(sun));
    taskEXIT_CRITICAL(&lock);
    return n;
}

void moving_clear(void)
{
    taskENTER_CRITICAL(&lock);
    count = 0;
    taskEXIT_CRITICAL(&lock);
}
