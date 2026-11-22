// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The Waveshare ESP32-S3-Touch-AMOLED-1.75 board: its round screen and touch, set up for LVGL.

#pragma once

#include "esp_err.h"
#include "lvgl.h"

/** The screen is round, 466 px across. */
#define BOARD_SCREEN_PX 466

/**
 * Starts the screen, the touch and LVGL. LVGL then runs in its own task on core 1, so any
 * LVGL call from elsewhere must be wrapped in lvgl_port_lock() and lvgl_port_unlock().
 * Returns the LVGL display, or NULL on failure.
 */
lv_display_t *board_start(void);

/** Sets the screen brightness, from 0 (off) to 255 (full). */
esp_err_t board_set_brightness(uint8_t level);

/** Switches the display on. It starts off and dark, so nothing shows before the first frame:
 *  call once LVGL has drawn and sent it (main.c, on_first_frame). The brightness is still 0. */
esp_err_t board_screen_on(void);
