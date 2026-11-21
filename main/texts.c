// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The app's words: see texts.h. Written by the link task on core 0, read by the screen on
// core 1, so each side copies under the lock.

#include "texts.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "json_read.h"

// English until the app sends its own.
static char compass[4][TEXTS_COMPASS_BYTES] = {"N", "E", "S", "W"};
static char credits[TEXTS_CREDITS_BYTES];
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

void texts_parse(const char *json)
{
    // {"t":"texts","compass":{"n":"N","e":"E","s":"S","w":"O"},"credits":"…\n…"}
    char points[4][TEXTS_COMPASS_BYTES];
    memcpy(points, compass, sizeof(points));
    const char *start = strstr(json, "\"compass\":{");
    const char *end = start ? strchr(start, '}') : NULL;
    if (start && end && (size_t)(end - start) < 160) {
        // Read inside the compass's braces only, so its one-letter keys match nothing else.
        char inside[160];
        memcpy(inside, start, (size_t)(end - start));
        inside[end - start] = '\0';
        static const char *keys[4] = {"n", "e", "s", "w"};
        for (int i = 0; i < 4; i++) {
            char letter[TEXTS_COMPASS_BYTES];
            if (json_string(inside, keys[i], letter, sizeof(letter)) && letter[0]) strcpy(points[i], letter);
        }
    }
    static char new_credits[TEXTS_CREDITS_BYTES];
    if (!json_string(json, "credits", new_credits, sizeof(new_credits))) new_credits[0] = '\0';

    taskENTER_CRITICAL(&lock);
    memcpy(compass, points, sizeof(compass));
    memcpy(credits, new_credits, sizeof(credits));
    taskEXIT_CRITICAL(&lock);
}

void texts_compass(texts_compass_t point, char *out)
{
    taskENTER_CRITICAL(&lock);
    memcpy(out, compass[point & 3], TEXTS_COMPASS_BYTES);
    taskEXIT_CRITICAL(&lock);
}

void texts_credits(char *out)
{
    taskENTER_CRITICAL(&lock);
    memcpy(out, credits, TEXTS_CREDITS_BYTES);
    taskEXIT_CRITICAL(&lock);
}
