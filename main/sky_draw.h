// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The sky map behind the reticle: the catalogue's stars and constellation lines, turned and
// projected for each view the app sends, the same way the app's map draws them (the app's
// docs/puck.md, "Drawing on the puck"). Drawn into one full-screen canvas, in the LVGL task.

#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "puck_protocol.h"

/** Makes the canvas, as the screen's first child so everything else is drawn over it.
 *  False when there's no memory for it. With LVGL locked. */
bool sky_draw_create(lv_obj_t *screen);

/** Draws the sky for a view. Loads the catalogue first if a new one has been kept. */
void sky_draw_frame(const puck_view_msg_t *view, bool night);

/** Just the sky's colour, as before the first view. */
void sky_draw_clear(bool night);
