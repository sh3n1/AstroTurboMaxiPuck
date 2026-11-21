// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The messages between the puck and the app, as the app's docs/puck.md defines them. Change
// that file first, then both sides.

#pragma once

#include <stdint.h>

// The data channels, told apart by label.
#define PUCK_CHANNEL_CONTROL "astroturbomaxisolver"
#define PUCK_CHANNEL_VIEW "astroturbomaxisolver-view"
#define PUCK_CHANNEL_CATALOG "astroturbomaxisolver-catalog"

// The puck's screen, for puckHello.
#define PUCK_SCREEN_PX 466
#define PUCK_PROTOCOL_VERSION 1

// The binary `view`: where the app's map points, about 30 a second. Little-endian, as the
// ESP32-S3 is, so it is copied from the message as it comes.
#define PUCK_VIEW_TYPE 1
#define PUCK_VIEW_VERSION 1
#define PUCK_VIEW_FLAG_MIRROR_X 0x01
#define PUCK_VIEW_FLAG_MIRROR_Y 0x02
#define PUCK_VIEW_FLAG_EYEPIECE 0x04
#define PUCK_VIEW_FLAG_RING 0x08     // the sky map shows the eyepiece's field as its one ring

typedef struct {
    uint8_t type;            // PUCK_VIEW_TYPE
    uint8_t version;         // PUCK_VIEW_VERSION
    uint16_t seq;            // counts up, wrapping: an older one is late and dropped
    uint32_t time_ms;        // the app's clock, since the link opened
    float sky[4];            // quaternion (w, x, y, z): J2000 to local (east, north, up)
    float view[4];           // quaternion: local to view (right, up, back)
    float fov_deg;           // the angle the puck's diameter covers
    float alt_deg;           // the view's centre, as it appears
    float az_deg;
    float target_dist_deg;   // NaN when there is no target
    float target_dir_deg;    // clockwise from up on the screen; NaN when no target
    float star_limit_mag;    // the faintest stars to draw
    uint8_t flags;           // PUCK_VIEW_FLAG_*
    uint8_t reserved[3];
} puck_view_msg_t;

_Static_assert(sizeof(puck_view_msg_t) == 68, "the view is 68 bytes in docs/puck.md");
