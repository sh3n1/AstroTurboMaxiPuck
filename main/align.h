// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Guided alignment (docs/screen-layout.md, "Alignment"; the app's docs/puck.md, "Alignment"),
// as the app's Align page: once a star is chosen in the menu, a page over the map says to
// centre it in the eyepiece, by eye, with no guidance (the phone isn't aligned yet), and shows
// a big round Align button in the middle and ✗ at the bottom. Align asks the phone to plate
// solve; the phone answers how it went (`alignStatus`), with the words to show.

#pragma once

#include <stdbool.h>

#include "lvgl.h"

/** Makes the page, hidden. In the LVGL task. */
void align_create(lv_obj_t *screen);

void align_set_night(bool night);

/** Shows the page, for the star just chosen. In the LVGL task. */
void align_open(void);

/** Whether the alignment's buttons are over the map. */
bool align_active(void);

/** Reads `{"t":"alignStatus","step","text"}`. From the link task: shown by align_update. */
void align_set_status(const char *json);

/** Shows the newest status: from the LVGL task, each frame. */
void align_update(void);

/** Hides the buttons, as when the link goes. In the LVGL task. */
void align_close(void);
