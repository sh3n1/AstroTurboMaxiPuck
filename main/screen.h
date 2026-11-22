// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The puck's main screen, over the sky map (docs/screen-layout.md): the readout along the
// top edge; the reticle in the centre, green on target, pulsing while the app plate solves;
// and, in the bottom third, media-player style, the target's name, the previous, Menu and
// next buttons, a bar for how close the target is, and its distance in degrees. The bottom
// third shows after a tap on the map, and goes again after a while without one. The
// guidance chevrons are drawn with the sky (sky_objects.c). Day colours or night's red.

#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "puck_protocol.h"

/** Makes the screen's parts, hidden until the first view. With LVGL locked. */
void screen_create(lv_obj_t *screen);

/**
 * Shows a view: the readout, the reticle sized for its field, and the target's name and
 * distance. target_name: "" for none. eyepiece_field_deg: the eyepiece's true field, which
 * sets when the target counts as reached, and the ring view's one ring; 0 until known.
 */
void screen_show(const puck_view_msg_t *view, const char *target_name, float eyepiece_field_deg);

/** Hides everything but the map, as before the first view or after the link is lost. */
void screen_hide(void);

/** Call on every refresh: the reticle pulses while the app plate solves, as the app's. */
void screen_tick(bool solving);

/** The day's colours or night's full-strength red. */
void screen_set_night(bool night);

/** A tap on the map, not on the reticle: shows the bottom third for a while. */
void screen_touched(void);

/** True when a tap at (x, y) is on the reticle, where it starts a plate solve. */
bool screen_is_reticle(int x, int y);
