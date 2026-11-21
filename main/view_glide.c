// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Smooth movement on the puck: see view_glide.h.

#include "view_glide.h"

#include <math.h>

// How quickly the shown view catches up, in ms: after this time about two thirds of the way
// is covered (the app's GLIDE_MS).
#define GLIDE_MS 60.0f
// Closer than these the glide is finished: about 0.001° of direction (half the angle, in
// radians, as a quaternion's difference measures it), and a 0.01 % change of field.
#define SETTLED_HALF_ANGLE 1e-5f
#define SETTLED_FIELD_RATIO 1e-4f

static float quaternion_dot(const float a[4], const float b[4])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

bool view_glide_step(puck_view_msg_t *shown, const puck_view_msg_t *newest, float elapsed_ms)
{
    // Nothing shown yet, a different view, or a field that makes no sense: no gliding.
    bool snap = shown->type != PUCK_VIEW_TYPE || shown->flags != newest->flags || !(shown->fov_deg > 0) ||
                !(newest->fov_deg > 0);
    if (snap) {
        *shown = *newest;
        return false;
    }
    float fraction = 1 - expf(-fmaxf(0, elapsed_ms) / GLIDE_MS);

    // The direction: the shortest way between the two quaternions (q and -q turn alike),
    // mixed and made unit length again, which is close enough to the true arc for one step.
    float target[4];
    float sign = quaternion_dot(shown->view, newest->view) < 0 ? -1.0f : 1.0f;
    float remaining = 0;
    for (int i = 0; i < 4; i++) {
        target[i] = sign * newest->view[i];
        remaining = fmaxf(remaining, fabsf(target[i] - shown->view[i]));
    }
    float field_ratio = logf(newest->fov_deg / shown->fov_deg);
    if (remaining < SETTLED_HALF_ANGLE && fabsf(field_ratio) < SETTLED_FIELD_RATIO) {
        *shown = *newest;
        return false;
    }

    float mixed[4], length = 0;
    for (int i = 0; i < 4; i++) {
        mixed[i] = shown->view[i] + (target[i] - shown->view[i]) * fraction;
        length += mixed[i] * mixed[i];
    }
    length = sqrtf(length);
    // The field, by ratio, so zooming in and out glide alike.
    float fov = shown->fov_deg * expf(field_ratio * fraction);

    *shown = *newest;
    if (length > 1e-6f) {
        for (int i = 0; i < 4; i++) shown->view[i] = mixed[i] / length;
    }
    shown->fov_deg = fov;
    return true;
}
