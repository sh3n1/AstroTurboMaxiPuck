// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The pairing QR code, drawn here rather than with LVGL's lv_qrcode, which always uses
// medium error correction and byte mode. This uses the lowest error correction (a screen is
// never torn or smudged) and lets the QR encoder pick its compact alphanumeric mode for a
// text of capitals, digits and a few signs (the base32 code from link.c): fewer, bigger
// squares, which a phone reads more easily off a bright screen in the dark.

#pragma once

#include <stdbool.h>

#include "lvgl.h"

/** Makes the QR code's square, size_px wide, hidden. With LVGL locked. */
lv_obj_t *qr_view_create(lv_obj_t *parent, int size_px);

/** Draws text as a QR code, as big as the square allows, with its quiet zone. */
bool qr_view_show(const char *text);

/** Night mode: the light squares full-strength red instead of white, as all of night mode. */
void qr_view_set_night(bool night);
