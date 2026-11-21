// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Smooth movement on the puck, as the app's pose-glide.ts on a screen phone. Views come about
// 30 times a second, unevenly spaced by the Wi-Fi, and a frame takes a while to draw: jumping
// to each view as it comes looks stepped. Instead, every frame moves the shown view part of
// the way towards the newest one: its direction (the view quaternion) and its field (so the
// zoom's steps glide too). The rest is taken from the newest view as it is.

#pragma once

#include <stdbool.h>

#include "puck_protocol.h"

/**
 * Moves *shown towards *newest, for a frame elapsed_ms after the last one. A change of view
 * (eyepiece, ring, mirroring) is taken at once. True while there is still some way to go,
 * so the caller draws again even with no new view.
 */
bool view_glide_step(puck_view_msg_t *shown, const puck_view_msg_t *newest, float elapsed_ms);
