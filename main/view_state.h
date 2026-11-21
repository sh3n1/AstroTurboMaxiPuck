// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// What the app last told the puck to show: the newest view, the target's name, and the
// app's state (night mode, plate solving). The link
// writes it on core 0 and the screen reads a copy on core 1, so each side only holds the
// lock for a copy.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "puck_protocol.h"

#define VIEW_STATE_NAME_BYTES 64

typedef struct {
    bool has_view;                         // false until the first view after linking
    puck_view_msg_t view;
    int64_t view_time_us;                  // esp_timer time the view arrived
    uint32_t view_count;                   // views taken since start, to tell a new one
    char target_name[VIEW_STATE_NAME_BYTES]; // UTF-8, empty when there is no target
    bool night;                            // the app's night mode
    bool solving;                          // the app is plate solving
} view_state_t;

/** Takes a new view, unless it's older than the last (by its wrapping `seq`). */
void view_state_set_view(const puck_view_msg_t *view);

/** The target's name, or "" for none. Cut to fit, on a whole UTF-8 character. */
void view_state_set_target(const char *name);

/** The app's state message: night mode and plate solving. */
void view_state_set_app_state(bool night, bool solving);

/** Forgets the view, target and state, as when the link is lost. */
void view_state_clear(void);

/** A copy of the state as it is now. */
void view_state_get(view_state_t *out);
