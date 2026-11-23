// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The player, as a watch's media controls (docs/screen-layout.md, "Player"): a tap on the
// map, off the reticle, dims it and shows, in the menu's bubbles, the list being observed
// through and the target at the top, the previous and next buttons left and right, the menu
// in the middle, and ✗ at the bottom. The reticle and the edges' readouts hide meanwhile. It
// goes again after a while without a tap.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/** Makes it, hidden. In the LVGL task, after the screen's parts and before the menu. */
void player_create(lv_obj_t *screen);

/** Shows it, with the list's line and the target's name ("" for none). */
void player_open(void);

/** Hides it. */
void player_close(void);

/** Whether it shows. */
bool player_active(void);

/** One more each time it opens or closes, so the screen knows to show again what it hid. */
uint32_t player_revision(void);

/** The list's line ("" when not observing through a list) and the target's name. */
void player_set_text(const char *list, const char *target);

/** Call on every refresh: it closes after a while without a tap. */
void player_tick(void);
