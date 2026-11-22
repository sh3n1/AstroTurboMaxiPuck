// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The screen's brightness, set in the menu (Settings, Brightness) and kept in NVS. Only the
// LVGL task sends it to the screen (main.c), so it never crosses a frame being sent.

#pragma once

#include <stdint.h>

#define BRIGHTNESS_MIN_PERCENT 10 // never quite off: the screen would look dead
#define BRIGHTNESS_STEP_PERCENT 10

/** Reads the kept brightness. Call once, after NVS is ready (wifi_start does that). */
void brightness_load(void);

/** The brightness, in percent. */
int brightness_percent(void);

/** The brightness for board_set_brightness(), 0 to 255. */
uint8_t brightness_level(void);

/** Sets the brightness, within BRIGHTNESS_MIN_PERCENT and 100. */
void brightness_set_percent(int percent);

/** Keeps it in NVS, if it changed since it was last kept: once the user is done. */
void brightness_save(void);
