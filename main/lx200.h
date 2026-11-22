// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The Meade LX200 repeater (docs/screen-layout.md, Settings; the app's docs/puck.md, "LX200"):
// the puck answers LX200 commands over TCP on port 4030, so planetarium apps (Stellarium,
// SkySafari) show where the telescope points and can send it a GoTo. It only repeats: the
// phone sends the pointing (`pointing`), and a GoTo goes back to it (`goto`), which makes
// the nearest object the target. On or off from the menu, kept in NVS.

#pragma once

#include <stdbool.h>

#define LX200_PORT 4030

/** Starts the server if it was left on. After the Wi-Fi has started (NVS is ready). */
void lx200_start(void);

/** Whether the repeater is on. */
bool lx200_enabled(void);

/** Switches it on or off, keeps that, and tells the phone. From the menu. */
void lx200_set_enabled(bool on);

/** Where the telescope points, on the equator and equinox of the date. From the link task. */
void lx200_set_pointing(float ra_deg, float dec_deg);
