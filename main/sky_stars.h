// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The sky map's stars: the catalogue's main stars, brightest first, and in the eyepiece view
// the faint stars where it looks, read straight from flash (the app's drawStars, starsNear).
// Written straight into the canvas's pixels (sky_pixels.h).

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sky_common.h"

/** The puck's pixels are smaller than a phone's CSS pixels: stars (and the discs and guides
 *  drawn with them) are drawn this much bigger, to look the same size at arm's length. */
#define SKY_STAR_SCALE 1.5f

/** Takes our copy of the main stars, and where the faint stars' bands start, from a newly
 *  kept catalogue. */
void sky_stars_load(const uint8_t *catalog, size_t bytes);

/** The stars down to the view's limit; the faint ones too when faint is true. */
void sky_stars_draw(const sky_frame_t *f, bool night, bool faint);

/** How many main and faint stars the catalogue has, for the log. */
size_t sky_stars_count(size_t *faint);
