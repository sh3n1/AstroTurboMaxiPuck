// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The start-up screen: the app's logo in the middle, floating gently, over the landing page's
// sky: two layers of twinkling stars and now and then a shooting star (the app's
// src/landing/styles.css). Shown from start-up until the first QR code.

#pragma once

#include "lvgl.h"

/** Shows it over the map, under everything else. In the LVGL task, LVGL locked. */
void splash_create(lv_obj_t *screen);

/** Stops its animations and removes it. LVGL locked; does nothing once gone. */
void splash_hide(void);
