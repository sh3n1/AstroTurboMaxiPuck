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
 *  sky_labels_draw() draws after. eyepiece_field_deg: the eyepiece's true field (0 until
 *  known), for when the target is reached. moving: the Moon and planets, for their labels. */
#define SKY_TARGET_LABEL_BYTES 24

/**
 * The selected target, from the app's targetInfo: its label as the map writes it ("" for
 * none), its kind (the catalogue's number, for its colour) and its size (0 when unknown).
 * Drawn first and in full, as the app's map does. From the link task.
 */
void sky_objects_set_target(const char *label, uint8_t kind, float size_arcmin);

void sky_objects_draw(const sky_frame_t *f, bool night, float eyepiece_field_deg,
                      const moving_object_t *moving, size_t moving_count);
