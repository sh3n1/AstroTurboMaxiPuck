// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The words the puck shows once linked, in the app's language, from its `texts` message
// (the app's docs/puck.md): the firmware holds no translations. Before the first link, and
// on the screens before linking, it falls back to English.

#pragma once

#include <stddef.h>

#define TEXTS_COMPASS_BYTES 12
#define TEXTS_CREDITS_BYTES 1024

/** The compass points, north, east, south and west. */
typedef enum { TEXTS_NORTH, TEXTS_EAST, TEXTS_SOUTH, TEXTS_WEST } texts_compass_t;

/** Reads a `texts` message and keeps its words. */
void texts_parse(const char *json);

/** The letter for a compass point, copied into out (TEXTS_COMPASS_BYTES). */
void texts_compass(texts_compass_t point, char *out);

/** The data credits, one per line, copied into out (TEXTS_CREDITS_BYTES); "" until sent. */
void texts_credits(char *out);
