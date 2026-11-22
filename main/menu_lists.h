// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The menu's Lists pages: the phone's lists, then one list's targets (lists.h), as chips in
// the menu's column; and the Alignment page, the stars to align on, made the same way. A
// long list is built a few chips at a time, so the first show at once and the rest follow
// while the column can already be scrolled and tapped.

#pragma once

#include <stdbool.h>

#include "lvgl.h"

/** What a chosen list or target leads to: the targets' page, and back to the map. */
void menu_lists_init(void (*open_targets)(void), void (*close_menu)(void));

/** The lists' chips, in the column. */
void menu_lists_show(lv_obj_t *column);

/** The chosen list's targets' chips, in the column. */
void menu_lists_show_targets(lv_obj_t *column);

/** The Alignment page's chips: what the phone is aligned on, Remove, and the stars. */
void menu_lists_show_align(lv_obj_t *column);

/** Stops adding chips: call before the column is cleaned or hidden. */
void menu_lists_stop(void);

/** Whether the phone's answer has changed what the page shows, so it should be shown again. */
bool menu_lists_changed(void);

/** The chosen list's name, for the targets' page's title. */
const char *menu_lists_chosen_name(void);
