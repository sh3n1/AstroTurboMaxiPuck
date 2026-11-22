// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The app's own line icons (its target-icons.ts): the planets' for the map and the menu's
// lists (Mercury's craters, Jupiter's belts, Saturn's rings, the Moon and Venus in their
// phase), and the galaxies', nebulae's, double stars' and constellations' for the lists.
// The app sends them as lines of points on a 24 grid (the `icon` message, the app's
// docs/puck.md); the link keeps them on core 0, and they are drawn on core 1, in the LVGL
// task, by name.

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Reads `{"t":"icon","body","paths":[{"p":[x,y,…],"dashed"?,"fill"?}]}` and keeps it. */
void sky_icons_parse(const char *json);

/** Whether the app sent an icon by this name. */
bool sky_icons_has(const char *name);

/**
 * One straight stroke of an icon, this wide, from (x0, y0) to (x1, y1). `edge` says it is the
 * dark edge drawn first under the lines; a filled part comes as rows one pixel high.
 */
typedef void (*sky_icons_stroke_fn)(float x0, float y0, float x1, float y1, float width, bool edge, void *ctx);

/**
 * Traces an icon centred at (x, y), its disc this many pixels in radius: first its dark edge,
 * edge_px wide (none if 0), then its lines, line_px wide. False if there's no icon by this name.
 * In the LVGL task only.
 */
bool sky_icons_trace(const char *name, float x, float y, float disc_px, float line_px, float edge_px,
                     sky_icons_stroke_fn stroke, void *ctx);

/**
 * Draws a body's icon on the map, with a dark edge first as the app's. False if the app sent
 * no icon for it, so a plain disc goes there.
 */
bool sky_icons_draw(const char *body, float x, float y, float disc_px, uint32_t rgb, float alpha);
