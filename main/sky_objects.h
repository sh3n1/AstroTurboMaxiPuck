// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The sky map's top layer, over the stars: the horizon and its compass points, the
// deep-sky objects' markers, the labels (planets, objects, bright stars) and the
// constellation names, as the app's map draws them (draw-targets.ts, draw-sky.ts).

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "moving.h"
#include "sky_common.h"

/** Takes our copy of the objects, their labels and the constellation names from a newly
 *  kept catalogue. */
void sky_objects_load(const uint8_t *catalog, size_t bytes);

/** A trail of three chevrons from (x, y) towards angle (radians, clockwise from up), that
 *  brightens outwards, spread apart the farther away the target is (the app's drawChevrons). */
void sky_draw_chevrons(float x, float y, float angle, float distance_deg, bool night);

/** Draws the top layer into the canvas's pixels, and places its labels, which
 *  sky_labels_draw() draws after. moving: the Moon and planets, for their labels. */
void sky_objects_draw(const sky_frame_t *f, bool night,
                      const moving_object_t *moving, size_t moving_count);
