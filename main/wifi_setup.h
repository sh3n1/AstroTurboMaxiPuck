// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The setup page, served by the puck on its own network while in setup (wifi.h), at
// http://192.168.4.1/: the networks kept (wifi_networks.h), each with Forget; those the
// puck sees; and a form for one to keep and its password. Saving restarts the puck, which
// then joins the strongest kept network in range. In English, as everything shown before
// a link to the phone.

#pragma once

#include "esp_err.h"

/** Starts the web server. The Wi-Fi must be started, in setup. */
esp_err_t wifi_setup_start(void);
