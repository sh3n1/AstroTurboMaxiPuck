// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The puck's own zoom (the app's docs/puck.md, "The puck's zoom"): its sky map shows a field
// of its own, apart from the phone's, centred where the phone points. Only in the phone's
// eyepiece view does it follow the phone's field. The menu's Zoom shows +, −, ✓ and ✗ over
// the map: ✓ keeps the new field, ✗ goes back to the one before. The rules (limits, step,
// where it starts, how faint the stars go) are the app's, sent in `mapRules`.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "puck_protocol.h"

/** Makes the four buttons, hidden. In the LVGL task, or with LVGL locked. */
void zoom_create(lv_obj_t *screen);

/** Reads the app's `mapRules` message. From any task. */
void zoom_set_rules(const char *json);

/** Shows the buttons (the menu's Zoom). In the LVGL task. */
void zoom_open(void);

/** Whether the buttons show: taps elsewhere on the map then do nothing. */
bool zoom_active(void);

/** Changes each time the field changes, so the screen knows to draw again. */
uint32_t zoom_revision(void);

/** Puts the puck's own field and its star limit into a view from the app, unless the phone
 *  shows its eyepiece view, whose field the puck follows. */
void zoom_apply(puck_view_msg_t *view);

