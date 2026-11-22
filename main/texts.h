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
#define TEXTS_READOUT_BYTES 64
#define TEXTS_WORD_BYTES 48

/** The compass points, north, east, south and west. */
typedef enum { TEXTS_NORTH, TEXTS_EAST, TEXTS_SOUTH, TEXTS_WEST } texts_compass_t;

/** The menu's words, in the message's "words" object, under the keys in texts.c. */
typedef enum {
    TEXTS_MENU,
    TEXTS_SENSORS,
    TEXTS_VIEW_SKY,
    TEXTS_VIEW_EYEPIECE,
    TEXTS_VIEW_RING,
    TEXTS_LISTS,
    TEXTS_ALIGNMENT,
    TEXTS_ZOOM,
    TEXTS_NIGHT,
    TEXTS_SETTINGS,
    TEXTS_BRIGHTNESS,
    TEXTS_WIFI,
    TEXTS_WIFI_CLIENT,
    TEXTS_WIFI_AP,
    TEXTS_LX200,
    TEXTS_ON,
    TEXTS_OFF,
    TEXTS_WORD_COUNT
} texts_word_t;

/** Reads a `texts` message and keeps its words. */
void texts_parse(const char *json);

/** The letter for a compass point, copied into out (TEXTS_COMPASS_BYTES). */
void texts_compass(texts_compass_t point, char *out);

/** The top edge's readout, as the app writes it: "Alt 45.2° Az 180.3°" and "FOV 20°" in
 *  English, "Campo 20°" in Spanish, into out (TEXTS_READOUT_BYTES). */
void texts_alt_az(float alt_deg, float az_deg, char *out);
void texts_field(float field_deg, char *out);

/** One of the menu's words, copied into out (TEXTS_WORD_BYTES). */
void texts_word(texts_word_t word, char *out);

/** The data credits, one per line, copied into out (TEXTS_CREDITS_BYTES); "" until sent. */
void texts_credits(char *out);
