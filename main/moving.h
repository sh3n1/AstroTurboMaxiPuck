// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// What moves on the map: the Moon, the planets and the bright asteroids, from the app's
// `objects` message (the app's docs/puck.md). The link writes them on core 0, the sky map
// reads a copy on core 1.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MOVING_MAX 24
#define MOVING_LABEL_BYTES 24

typedef struct {
    float v[3];           // J2000 unit vector, in the catalogue's frame (topocentric for the Moon)
    float mag;            // NAN when unknown
    float size_arcmin;    // 0 when unknown
    float phase;          // the lit fraction, 0 to 1; NAN when not given
    uint8_t kind;         // as in the catalogue (docs/puck.md, "Kinds")
    uint8_t show_below;   // shown when the field is under this many degrees; 255 always
    char label[MOVING_LABEL_BYTES]; // what the map writes beside it, UTF-8
} moving_object_t;

/** Reads an `objects` message and keeps what it holds. Returns how many objects. */
size_t moving_parse(const char *json);

/** A copy of the objects, up to MOVING_MAX, and the Sun's J2000 vector. */
size_t moving_get(moving_object_t *out, float sun[3]);

/** Forgets them, as when the link is lost. */
void moving_clear(void);
