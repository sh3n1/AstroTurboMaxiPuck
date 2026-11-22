// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The menu (docs/screen-layout.md), opened by the bottom third's Menu button: a page over the
// whole screen with a title along the top edge, a honeycomb of bubbles (the pages within it:
// a column of big items that scrolls), and a round button at the bottom to go back (to the
// map, or from a page within it). Its words come from the app
// (texts.h); its look is menu_chip.h's.

#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "view_state.h"

/** Makes the menu, hidden. night_switch is called by the Night item. With LVGL locked. */
void menu_create(lv_obj_t *screen, void (*night_switch)(void));

/** Opens the menu at its first page. In the LVGL task. */
void menu_open(void);

/** Opens it at its Brightness page, for a screenshot over the cable (screenshot.c). */
void menu_open_brightness(void);

/** Closes it, back to the map. In the LVGL task. */
void menu_close(void);

/** Whether the menu shows: the map isn't drawn meanwhile. */
bool menu_active(void);

/** The day's colours or night's red. */
void menu_set_night(bool night);

/** The app's state, for the chips that show it (sensors, view, night). In the LVGL task. */
void menu_show_state(const view_state_t *state);
