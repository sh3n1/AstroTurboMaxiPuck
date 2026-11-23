// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The phone's lists: see lists.h.

#include "lists.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "json_read.h"

#define ITEM_MAX_BYTES 256 // one item's JSON: its id, name and line under it

static lists_t *held;  // in PSRAM: about 16 KB, which the internal RAM can't spare
static SemaphoreHandle_t lock;

// Made on first use, from whichever core comes first: the link starts before the menu.
static void ensure_made(void)
{
    static portMUX_TYPE making = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&making);
    bool first = lock == NULL;
    if (first) lock = xSemaphoreCreateMutex();
    taskEXIT_CRITICAL(&making);
    if (first) held = heap_caps_calloc(1, sizeof(lists_t), MALLOC_CAP_SPIRAM);
}

// The next {...} after p, as a string of its own in out, or NULL at the array's end. A name
// can hold '}' or ']', so those count only outside strings.
static const char *next_item(const char *p, char *out, size_t out_len)
{
    while (*p == ' ' || *p == ',') p++;
    if (*p != '{') return NULL;
    const char *start = p;
    bool in_string = false;
    for (; *p; p++) {
        if (in_string) {
            if (*p == '\\' && p[1]) p++;
            else if (*p == '"') in_string = false;
        } else if (*p == '"') {
            in_string = true;
        } else if (*p == '}') {
            size_t len = (size_t)(p - start + 1);
            if (len >= out_len) {
                out[0] = '\0'; // too long: skipped, the rest still read
            } else {
                memcpy(out, start, len);
                out[len] = '\0';
            }
            return p + 1;
        }
    }
    return NULL;
}

// Where the array under "key":[ starts, or NULL.
static const char *array_of(const char *json, const char *key)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\":[", key);
    const char *p = strstr(json, pattern);
    return p ? p + strlen(pattern) : NULL;
}

void lists_parse_lists(const char *json)
{
    ensure_made();
    if (!held) return;
    const char *p = array_of(json, "lists");
    if (!p) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t n = 0;
    char item[ITEM_MAX_BYTES];
    while (n < LISTS_MAX && (p = next_item(p, item, sizeof(item))) != NULL) {
        lists_item_t *list = &held->lists[n];
        uint32_t count = 0;
        if (!json_string(item, "id", list->id, sizeof(list->id)) ||
            !json_string(item, "name", list->name, sizeof(list->name))) {
            continue;
        }
        json_uint(item, "count", &count); // how many targets it has, as the phone's Lists page
        snprintf(list->sub, sizeof(list->sub), "%u", (unsigned)count);
        list->icon[0] = list->badge[0] = '\0';
        list->high = false;
        n++;
    }
    held->list_count = n;
    held->version++;
    xSemaphoreGive(lock);
}

// Reads a "key":[{"id","name","sub",…}] array into items; how many were read.
static size_t parse_items(const char *array, lists_item_t *items, size_t max)
{
    size_t n = 0;
    char item[ITEM_MAX_BYTES];
    const char *p = array;
    while (n < max && (p = next_item(p, item, sizeof(item))) != NULL) {
        lists_item_t *it = &items[n];
        if (!json_string(item, "id", it->id, sizeof(it->id)) || !json_string(item, "name", it->name, sizeof(it->name))) {
            continue;
        }
        if (!json_string(item, "sub", it->sub, sizeof(it->sub))) it->sub[0] = '\0';
        if (!json_string(item, "icon", it->icon, sizeof(it->icon))) it->icon[0] = '\0';
        // How high and which way, as the phone's badge: "42° SE".
        float alt;
        char dir[8] = "";
        it->badge[0] = '\0';
        if (json_float(item, "alt", &alt) && json_string(item, "dir", dir, sizeof(dir))) {
            snprintf(it->badge, sizeof(it->badge), "%d° %s", (int)alt, dir);
        }
        it->high = false;
        json_bool(item, "good", &it->high);
        n++;
    }
    return n;
}

void lists_parse_targets(const char *json)
{
    ensure_made();
    if (!held) return;
    // The list's own id and name come before its targets, which have theirs too.
    const char *p = array_of(json, "targets");
    if (!p) return;
    char head[ITEM_MAX_BYTES];
    size_t head_len = (size_t)(p - json);
    if (head_len >= sizeof(head)) return;
    memcpy(head, json, head_len);
    head[head_len] = '\0';
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!json_string(head, "id", held->targets_of, sizeof(held->targets_of)) ||
        !json_string(head, "name", held->targets_name, sizeof(held->targets_name))) {
        held->targets_of[0] = '\0';
    }
    held->target_count = parse_items(p, held->targets, LISTS_TARGETS_MAX);
    // "more" is the message's own: the targets have none.
    uint32_t more = 0;
    json_uint(json, "more", &more);
    held->more = more;
    held->version++;
    xSemaphoreGive(lock);
}

void lists_parse_align(const char *json)
{
    ensure_made();
    if (!held) return;
    // The page's own words come before the stars, which have names too.
    const char *p = array_of(json, "stars");
    if (!p) return;
    char head[3 * LISTS_NAME_BYTES + 96];
    size_t head_len = (size_t)(p - json);
    if (head_len >= sizeof(head)) return;
    memcpy(head, json, head_len);
    head[head_len] = '\0';
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!json_string(head, "current", held->align_current, sizeof(held->align_current))) held->align_current[0] = '\0';
    if (!json_string(head, "remove", held->align_remove, sizeof(held->align_remove))) held->align_remove[0] = '\0';
    if (!json_string(head, "removeAgain", held->align_remove_again, sizeof(held->align_remove_again))) {
        held->align_remove_again[0] = '\0';
    }
    held->align_count = parse_items(p, held->align_stars, LISTS_ALIGN_MAX);
    held->version++;
    xSemaphoreGive(lock);
}

void lists_forget_targets(void)
{
    ensure_made();
    if (!held) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    held->targets_of[0] = '\0';
    held->target_count = 0;
    held->more = 0;
    held->version++;
    xSemaphoreGive(lock);
}

const lists_t *lists_lock(void)
{
    ensure_made();
    if (!held) return NULL;
    xSemaphoreTake(lock, portMAX_DELAY);
    return held;
}

void lists_unlock(void)
{
    xSemaphoreGive(lock);
}

unsigned lists_version(void)
{
    ensure_made();
    return held ? held->version : 0;
}
