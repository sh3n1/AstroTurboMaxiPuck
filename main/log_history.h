// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The log's last lines, kept in PSRAM: a lost link or another fault happens when no one is
// watching the serial port, and its lines would be gone. Send "L" on the serial port (see
// screenshot.h) and the puck answers with them, between "LOG BEGIN" and "LOG END".

#pragma once

#include <stddef.h>

/** Starts keeping the log's lines. Call first thing, so the start-up is kept too. */
void log_history_start(void);

/** Hands the kept lines to send(), oldest first, in pieces. */
void log_history_dump(void (*send)(const char *text, size_t length));
