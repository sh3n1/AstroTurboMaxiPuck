// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The app's words: see texts.h. Written by the link task on core 0, read by the screen on
// core 1, so each side copies under the lock.

#include "texts.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "json_read.h"

// English until the app sends its own.
static char compass[4][TEXTS_COMPASS_BYTES] = {"N", "E", "S", "W"};
static char credits[TEXTS_CREDITS_BYTES];
// The readout's templates, with {alt}, {az} and {field} where the numbers go.
static char alt_az[TEXTS_READOUT_BYTES] = "Alt {alt}° Az {az}°";
static char field[TEXTS_READOUT_BYTES] = "FOV {field}°";
// The menu's words, in texts_word_t's order: in English until the app sends its own.
static char words[TEXTS_WORD_COUNT][TEXTS_WORD_BYTES] = {
    "Menu", "Sensors", "Sky map", "Eyepiece view", "Field ring", "Lists", "Alignment", "Zoom", "Night",
    "Settings", "Brightness", "Wi-Fi", "Client", "Access point", "Meade LX200 Repeater", "On", "Off"};
static const char *const WORD_KEYS[TEXTS_WORD_COUNT] = {
    "menu", "sensors", "viewSky", "viewEyepiece", "viewRing", "lists", "alignment", "zoom", "night",
    "settings", "brightness", "wifi", "wifiClient", "wifiAp", "lx200", "on", "off"};
// The "words" object, at most this long.
#define WORDS_JSON_BYTES 1536
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

// Reads the "words" object, whose keys are WORD_KEYS: inside its braces only, so its keys
// match nothing else in the message.
static void parse_words(const char *json)
{
    const char *start = strstr(json, "\"words\":{");
    const char *end = start ? strchr(start, '}') : NULL;
    if (!start || !end || (size_t)(end - start) >= WORDS_JSON_BYTES) return;
    static char inside[WORDS_JSON_BYTES]; // only the link task parses
    memcpy(inside, start, (size_t)(end - start));
    inside[end - start] = '\0';
    for (int i = 0; i < TEXTS_WORD_COUNT; i++) {
        char word[TEXTS_WORD_BYTES];
        if (!json_string(inside, WORD_KEYS[i], word, sizeof(word)) || !word[0]) continue;
        taskENTER_CRITICAL(&lock);
        memcpy(words[i], word, sizeof(word));
        taskEXIT_CRITICAL(&lock);
    }
}

void texts_parse(const char *json)
{
    parse_words(json);
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
    char new_alt_az[TEXTS_READOUT_BYTES], new_field[TEXTS_READOUT_BYTES];
    taskENTER_CRITICAL(&lock);
    memcpy(new_alt_az, alt_az, sizeof(new_alt_az));
    memcpy(new_field, field, sizeof(new_field));
    taskEXIT_CRITICAL(&lock);
    json_string(json, "altAz", new_alt_az, sizeof(new_alt_az));
    json_string(json, "field", new_field, sizeof(new_field));

    taskENTER_CRITICAL(&lock);
    memcpy(compass, points, sizeof(compass));
    memcpy(credits, new_credits, sizeof(credits));
    memcpy(alt_az, new_alt_az, sizeof(alt_az));
    memcpy(field, new_field, sizeof(field));
    taskEXIT_CRITICAL(&lock);
}

void texts_compass(texts_compass_t point, char *out)
{
    taskENTER_CRITICAL(&lock);
    memcpy(out, compass[point & 3], TEXTS_COMPASS_BYTES);
    taskEXIT_CRITICAL(&lock);
}

// Writes a template into out, each {name} replaced by its number, to one decimal.
static void fill(const char *template, const char *const names[], const float values[], int count, char *out)
{
    size_t at = 0;
    while (*template && at + 1 < TEXTS_READOUT_BYTES) {
        int found = -1;
        for (int i = 0; i < count && found < 0; i++) {
            size_t length = strlen(names[i]);
            if (strncmp(template, names[i], length) == 0) found = i;
        }
        if (found >= 0) {
            int n = snprintf(out + at, TEXTS_READOUT_BYTES - at, "%.1f", values[found]);
            if (n > 0) at += (size_t)n < TEXTS_READOUT_BYTES - at ? (size_t)n : TEXTS_READOUT_BYTES - at - 1;
            template += strlen(names[found]);
        } else {
            out[at++] = *template++;
        }
    }
    out[at] = '\0';
}

void texts_alt_az(float alt_deg, float az_deg, char *out)
{
    char template[TEXTS_READOUT_BYTES];
    taskENTER_CRITICAL(&lock);
    memcpy(template, alt_az, sizeof(template));
    taskEXIT_CRITICAL(&lock);
    static const char *const names[] = {"{alt}", "{az}"};
    const float values[] = {alt_deg, az_deg};
    fill(template, names, values, 2, out);
}

void texts_field(float field_deg, char *out)
{
    char template[TEXTS_READOUT_BYTES];
    taskENTER_CRITICAL(&lock);
    memcpy(template, field, sizeof(template));
    taskEXIT_CRITICAL(&lock);
    static const char *const names[] = {"{field}"};
    const float values[] = {field_deg};
    fill(template, names, values, 1, out);
}

void texts_word(texts_word_t word, char *out)
{
    taskENTER_CRITICAL(&lock);
    memcpy(out, words[word < TEXTS_WORD_COUNT ? word : 0], TEXTS_WORD_BYTES);
    taskEXIT_CRITICAL(&lock);
}

void texts_credits(char *out)
{
    taskENTER_CRITICAL(&lock);
    memcpy(out, credits, TEXTS_CREDITS_BYTES);
    taskEXIT_CRITICAL(&lock);
}
