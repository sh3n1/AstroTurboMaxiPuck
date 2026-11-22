// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The shared view state: see view_state.h.

#include "view_state.h"

#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static view_state_t state;
// A spinlock works across both cores; it's only held for a copy of about 150 bytes.
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

void view_state_set_view(const puck_view_msg_t *view)
{
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&lock);
    // Seq wraps at 65535: compared as a signed difference, a small step back is late.
    bool late = state.has_view && (int16_t)(view->seq - state.view.seq) <= 0;
    if (!late) {
        state.view = *view;
        state.view_time_us = now;
        state.view_count++;
        state.has_view = true;
    }
    taskEXIT_CRITICAL(&lock);
}

void view_state_set_target(const char *name)
{
    char copy[VIEW_STATE_NAME_BYTES];
    size_t len = strlen(name);
    if (len >= sizeof(copy)) {
        len = sizeof(copy) - 1;
        // Don't cut a character in two: step back over UTF-8 continuation bytes.
        while (len > 0 && ((unsigned char)name[len] & 0xC0) == 0x80) len--;
    }
    memcpy(copy, name, len);
    copy[len] = '\0';

    taskENTER_CRITICAL(&lock);
    memcpy(state.target_name, copy, sizeof(copy));
    taskEXIT_CRITICAL(&lock);
}

void view_state_set_app_state(const view_state_app_t *app)
{
    taskENTER_CRITICAL(&lock);
    state.night = app->night;
    state.solving = app->solving;
    state.sensors_on = app->sensors_on;
    state.view_mode = app->view_mode;
    state.eyepiece_field_deg = app->eyepiece_field_deg;
    taskEXIT_CRITICAL(&lock);
}

void view_state_clear(void)
{
    taskENTER_CRITICAL(&lock);
    state.has_view = false;
    state.target_name[0] = '\0';
    state.solving = false;
    // Night mode stays as it was: the screen mustn't flash bright when the link drops.
    taskEXIT_CRITICAL(&lock);
}

void view_state_get(view_state_t *out)
{
    taskENTER_CRITICAL(&lock);
    *out = state;
    taskEXIT_CRITICAL(&lock);
}
