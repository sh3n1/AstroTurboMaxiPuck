// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// What the sky map's parts (sky_draw.c, sky_objects.c) share: turning and projecting J2000
// directions onto the puck's screen as the app's map does (the app's docs/puck.md, "Drawing
// on the puck"), its colours, and reading the catalogue. Small, so kept here as inline
// functions.

#pragma once

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "puck_protocol.h"

// ---- The app's colours (sky-style.ts, theme.ts) ----

#define SKY_COLOR_SKY 0x05070F
#define SKY_COLOR_SOLAR_SYSTEM 0xFFD479

/** Night mode turns everything red by its brightness, as the app does. */
static inline uint32_t sky_for_night(uint32_t rgb, bool night)
{
    if (!night) return rgb;
    float lum = 0.2126f * (rgb >> 16 & 0xFF) + 0.7152f * (rgb >> 8 & 0xFF) + 0.0722f * (rgb & 0xFF);
    return (uint32_t)(lum > 255 ? 255 : lum) << 16;
}

// ---- Reading the catalogue (docs/puck.md, "Format") ----

#define SKY_CATALOG_MAGIC 0x434D5441 // "ATMC"

static inline float sky_read_f32(const uint8_t *p)
{
    float f;
    memcpy(&f, p, sizeof(f));
    return f;
}

static inline uint32_t sky_read_u32(const uint8_t *p)
{
    uint32_t n;
    memcpy(&n, p, sizeof(n));
    return n;
}

/** A section of the catalogue: its records, how many, and its size in bytes. NULL when
 *  the catalogue has none of that type. */
static inline const uint8_t *sky_find_section(const uint8_t *data, size_t bytes, uint32_t type,
                                              uint32_t *count, uint32_t *size)
{
    uint16_t sections;
    memcpy(&sections, data + 6, sizeof(sections));
    for (uint32_t i = 0; i < sections && 8 + (i + 1) * 16 <= bytes; i++) {
        const uint8_t *entry = data + 8 + i * 16;
        uint32_t offset = sky_read_u32(entry + 4), n = sky_read_u32(entry + 8), s = sky_read_u32(entry + 12);
        if (sky_read_u32(entry) == type && offset + s <= bytes) {
            *count = n;
            if (size) *size = s;
            return data + offset;
        }
    }
    return NULL;
}

// ---- Turning and projecting ----

#define SKY_SIZE_PX BOARD_SCREEN_PX
#define SKY_CENTRE_PX (SKY_SIZE_PX / 2.0f)
#define SKY_DEG_TO_RAD 0.017453292f
#define SKY_RAD_TO_DEG 57.29578f
// What is below the horizon is drawn at this opacity on the sky map (BELOW_HORIZON_OPACITY).
#define SKY_BELOW_HORIZON_OPACITY 0.35f
// Things this far behind the view are not drawn (the app's project()).
#define SKY_BEHIND_LIMIT -0.2f

/** One frame's view, worked out once. */
typedef struct {
    const puck_view_msg_t *view;
    float scale;        // pixels per projected unit
    float mirror_x, mirror_y;
    bool fade_below;    // the sky map fades what is below the horizon; the eyepiece doesn't
    // Things further than this from the centre (as the cosine of the angle) are off the round
    // screen. Checked before refraction, which is the slow part, with a margin for its lift.
    float visible_cos;
    float sky_then_view[4]; // the two rotations as one quaternion, for the quick check
} sky_frame_t;

static inline void sky_radec_vector(float ra_deg, float dec_deg, float v[3])
{
    float ra = ra_deg * SKY_DEG_TO_RAD, dec = dec_deg * SKY_DEG_TO_RAD;
    v[0] = cosf(dec) * cosf(ra);
    v[1] = cosf(dec) * sinf(ra);
    v[2] = sinf(dec);
}

// Turns v by the unit quaternion q (w, x, y, z), as the app's tests do (puck-view.spec.ts).
static inline void sky_rotate(const float q[4], const float v[3], float out[3])
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
static inline void sky_refract(float v[3])
{
    float alt = asinf(fmaxf(-1, fminf(1, v[2]))) * SKY_RAD_TO_DEG;
    float hd = alt < -1 ? -1 : alt;
    float lift = 1.02f / tanf((hd + 10.3f / (hd + 5.11f)) * SKY_DEG_TO_RAD) / 60;
    if (alt < -1) lift *= (alt + 90) / 89;
    float raised = (alt + lift) * SKY_DEG_TO_RAD;
    float across = sqrtf(v[0] * v[0] + v[1] * v[1]);
    if (across < 1e-6f) return;
    float k = cosf(raised) / across;
    v[0] *= k;
    v[1] *= k;
    v[2] = sinf(raised);
}

// The quaternion that turns by b, then by a.
static inline void sky_quat_mul(const float a[4], const float b[4], float out[4])
{
    out[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
    out[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
    out[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
    out[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
}

/** The frame for a view. */
static inline sky_frame_t sky_frame(const puck_view_msg_t *view)
{
    sky_frame_t f = {
        .view = view,
        // The puck's diameter shows fovDeg: its radius is 2 tan(fov / 4) projected units.
        .scale = SKY_CENTRE_PX / (2 * tanf(view->fov_deg / 4 * SKY_DEG_TO_RAD)),
        .mirror_x = view->flags & PUCK_VIEW_FLAG_MIRROR_X ? -1 : 1,
        .mirror_y = view->flags & PUCK_VIEW_FLAG_MIRROR_Y ? -1 : 1,
        .fade_below = !(view->flags & PUCK_VIEW_FLAG_EYEPIECE),
    };
    // The screen's edge is fovDeg / 2 from the centre; 1° more covers refraction's lift
    // (under 0.6°) and the biggest star's size. Wide views check nothing.
    float edge_deg = view->fov_deg / 2 + 1;
    f.visible_cos = edge_deg < 170 ? cosf(edge_deg * SKY_DEG_TO_RAD) : -2;
    sky_quat_mul(view->view, view->sky, f.sky_then_view);
    return f;
}

/** Off the round screen for sure: far from the view's centre, refraction or not. */
static inline bool sky_far_off(const sky_frame_t *f, const float j2000[3])
{
    float v[3];
    sky_rotate(f->sky_then_view, j2000, v);
    return -v[2] < f->visible_cos;
}

/** A local (east, north, up) vector, already refracted, to the screen. False when too far
 *  behind to draw. */
static inline bool sky_project_local(const sky_frame_t *f, const float local[3], float *px, float *py)
{
    float v[3];
    sky_rotate(f->view->view, local, v);
    float forward = -v[2]; // the view frame is right, up, back
    if (forward < SKY_BEHIND_LIMIT) return false;
    float k = 2 / (1 + forward) * f->scale;
    *px = SKY_CENTRE_PX + f->mirror_x * k * v[0];
    *py = SKY_CENTRE_PX - f->mirror_y * k * v[1];
    return true;
}

/** A J2000 vector to the screen. False when too far behind to draw. below: under the horizon. */
static inline bool sky_project(const sky_frame_t *f, const float j2000[3], float *px, float *py, bool *below)
{
    float local[3];
    sky_rotate(f->view->sky, j2000, local);
    sky_refract(local);
    *below = local[2] < 0;
    return sky_project_local(f, local, px, py);
}

/** On target: within half the eyepiece's true field, as the app's isOnTarget; never while
 *  that field isn't known (0) or there is no target. */
static inline bool sky_on_target(const puck_view_msg_t *v, float eyepiece_field_deg)
{
    return eyepiece_field_deg > 0 && !isnan(v->target_dist_deg) && v->target_dist_deg < eyepiece_field_deg / 2;
}

/** Radius on screen, in pixels, of something this many degrees across. */
static inline float sky_radius_px(const sky_frame_t *f, float diameter_deg)
{
    return 2 * tanf(diameter_deg / 4 * SKY_DEG_TO_RAD) * f->scale;
}
