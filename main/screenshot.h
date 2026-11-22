// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Screenshots over the USB cable, for working on the screen's layout from the computer:
// send "S" on the puck's serial port and it answers with the whole screen, a row a line, in
// hex (RGB565), between "SHOT BEGIN width height" and "SHOT END". tools/screenshot.py turns
// that into a PNG. "T" acts as a tap on the map, to show the bottom third. "L" sends the log's last lines
// (log_history.h). "M" opens the menu, "B" its
// Brightness page.

#pragma once

/** Starts listening on the serial port. Call once, after the screen is made. */
void screenshot_start(void);
