// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The log over the Wi-Fi, for field tests (docs/field-tests.md): in a build with
// CONFIG_PUCK_FIELD_LOG, every log line also goes out as a UDP broadcast on port 4031, so the
// laptop's collector reads the puck in the hand with no cable and no address to set. Off in
// every other build: the functions do nothing.

#pragma once

#include <stdbool.h>
#include <stddef.h>

/** Starts the sending task. After log_history_start; lines before the Wi-Fi wait a little. */
void field_log_start(void);

/** Whether the puck has an address on a network: lines wait until it has. */
void field_log_network_up(bool up);

/** One line for the Wi-Fi. Called from the log's output, under its lock: never blocks. */
void field_log_line(const char *text, size_t length);
