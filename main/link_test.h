// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// A first WebRTC test between a phone and the puck. The puck serves a test page at its own
// address; the page sends its offer over HTTP, the puck answers with libpeer, and every
// message on the data channel is echoed back so the page can time the round trip.
// Only a test: the real pairing will be by QR code (see AGENTS.md).

#pragma once

#include "esp_err.h"

/** Called when the link's state changes, with a short text to show, such as "Link open". */
typedef void (*link_test_state_cb_t)(const char *text);

/** Starts the web server and the link task. Call once the Wi-Fi is connected. */
esp_err_t link_test_start(link_test_state_cb_t on_state);
