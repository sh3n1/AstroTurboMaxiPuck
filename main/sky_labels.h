// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The sky map's labels. LVGL renders a label's text anew each time it is drawn, which took
// most of a frame's time; here each text is rendered once, by LVGL, into a small mask of
// coverage (A8, in PSRAM), kept, and copied into the canvas's pixels in the colour wanted
// each frame. A frame's labels are placed first, where they don't overlap one already
// placed (the app's drawLabel), and drawn together once the layer under them is finished.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/** Makes the hidden canvas texts are rendered on. With LVGL locked. */
bool sky_labels_create(lv_obj_t *screen);

/** A new frame: no labels placed yet. */
void sky_labels_begin(void);

/** Places a label with its left edge at x (its middle, when centred), centred on y, unless
 *  it would overlap one already placed or the frame has all it can take. rgb is the text's
 *  colour, alpha (0 to 1) its opacity and its dark backing's. True when placed. */
bool sky_labels_place(const char *text, float x, float y, uint32_t rgb, float alpha, bool centred);

/** True when the frame can take no more labels. */
bool sky_labels_full(void);

/** Draws the labels placed this frame into the canvas's pixels. */
void sky_labels_draw(void);
