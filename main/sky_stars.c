// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The sky map's stars: see sky_stars.h.

#include "sky_stars.h"

#include <math.h>
#include <string.h>

#include "catalog.h"
#include "esp_heap_caps.h"
#include "sky_pixels.h"

// The app's star colours (sky-style.ts), by the catalogue's tint index.
static const uint32_t STAR_TINTS[] = {0xB9D0FF, 0xF4F6FF, 0xFFF3D6, 0xFFD2A1, 0xFFB08A};
#define TINT_COUNT (sizeof(STAR_TINTS) / sizeof(STAR_TINTS[0]))

// The catalogue's layout (docs/puck.md, "Format").
#define SECTION_STARS 1
// The faint stars, for the eyepiece view: sorted by declination band, then right ascension,
// read straight from flash where the view looks (docs/puck.md, section 7).
#define SECTION_FAINT 7
#define FAINT_BAND_DEG 2
#define FAINT_BANDS (180 / FAINT_BAND_DEG)
#define STAR_RECORD_BYTES 12

typedef struct {
    float v[3];      // J2000 unit vector
    float mag;
    uint8_t tint;    // index in STAR_TINTS
} star_t;

// Our own copy of the main stars, ready to turn.
static star_t *stars;
static size_t star_count;
// Where each faint star band starts among the faint stars, and the end (FAINT_BANDS + 1).
static uint32_t faint_band_first[FAINT_BANDS + 1];
static uint32_t faint_count;

void sky_stars_load(const uint8_t *data, size_t bytes)
{
    heap_caps_free(stars);
    stars = NULL;
    star_count = 0;

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
}

size_t sky_stars_count(size_t *faint)
{
    if (faint) *faint = faint_count;
    return star_count;
}

// One star drawn: its size, tint and opacity as the app's (drawStars).
static void draw_star(const sky_frame_t *f, const float v[3], float mag, uint8_t tint, bool night)
{
    float x, y;
    bool below;
    if (sky_far_off(f, v) || !sky_project(f, v, &x, &y, &below)) return;
    if (x < -8 || y < -8 || x > SKY_SIZE_PX + 8 || y > SKY_SIZE_PX + 8) return;
    float radius = fmaxf(0.8f, 3.4f - mag * 0.42f) * SKY_STAR_SCALE;
    // Faint stars fade as well as shrink.
    float alpha = fminf(1, fmaxf(0.55f, 1.15f - floorf(mag) * 0.1f));
    if (below && f->fade_below) alpha *= SKY_BELOW_HORIZON_OPACITY;
    sky_disc(x, y, radius, sky_for_night(STAR_TINTS[tint < TINT_COUNT ? tint : 1], night), alpha);
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

void sky_stars_draw(const sky_frame_t *f, bool night, bool faint)
{
    // Brightest first, so drawing stops at the limit, as the app's map does.
    for (size_t i = 0; i < star_count; i++) {
        const star_t *star = &stars[i];
        if (star->mag > f->view->star_limit_mag) break;
        draw_star(f, star->v, star->mag, star->tint, night);
    }
    if (faint) draw_faint_stars(f, night);
}
