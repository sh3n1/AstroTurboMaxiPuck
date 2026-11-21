// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Writing straight into the sky canvas's pixels (RGB565, in PSRAM): quicker than LVGL for
// what a frame draws thousands of times (stars) or the whole screen of (the fill, the
// eyepiece mask), and for labels already rendered once (sky_labels.c).

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sky_common.h"

/** The canvas's pixels, SKY_SIZE_PX × SKY_SIZE_PX, row by row (sky_draw.c owns them). */
extern uint16_t *sky_pixels;

static inline uint16_t sky_rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

/** Blends colour c (RGB565) over a pixel by a (0 to 32), in integers: the green field is
 *  moved to the upper half so all three are worked at once. */
static inline void sky_blend565(uint16_t *pixel, uint16_t c, uint32_t a)
{
    if (a == 0) return;
    if (a >= 32) {
        *pixel = c;
        return;
    }
    uint32_t d = *pixel;
    d = (d | d << 16) & 0x07E0F81F;
    uint32_t s = (c | (uint32_t)c << 16) & 0x07E0F81F;
    d = (d + (((s - d) * a) >> 5)) & 0x07E0F81F;
    *pixel = (uint16_t)(d | d >> 16);
}

/** Blends a colour over a pixel by alpha (0 to 1). */
static inline void sky_blend(uint16_t *pixel, uint32_t rgb, float alpha)
{
    if (alpha <= 0) return;
    sky_blend565(pixel, sky_rgb565(rgb), (uint32_t)(alpha * 32 + 0.5f));
}

/** The whole round screen in one colour (the square's corners aren't seen, so not filled). */
void sky_fill(uint32_t rgb);

/** A round dot with a soft edge, centred at (x, y). */
void sky_disc(float x, float y, float radius, uint32_t rgb, float alpha);

/** A disc lit on one side, as the Moon or Venus: (sun_x, sun_y) is the screen direction
 *  towards the Sun, and lit the lit fraction (the app's phaseIcon). */
void sky_phase(float x, float y, float radius, float sun_x, float sun_y, float lit, uint32_t rgb, float alpha);

/** A straight line this wide, with round ends and soft edges. */
void sky_line(float x0, float y0, float x1, float y1, float width, uint32_t rgb, float alpha);

/** A circle's outline this wide, with soft edges; dashed when dash_px > 0 (dashes and gaps
 *  that long, about, fitted to a whole number round the circle). */
void sky_ring(float x, float y, float radius, float width, uint32_t rgb, float alpha, float dash_px);

/** Black outside a circle of this radius at the centre, with a pixel-wide edge (the app's
 *  drawEyepieceMask). */
void sky_mask_outside(float radius, uint32_t edge_rgb);
