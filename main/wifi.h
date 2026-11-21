// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Joins the Wi-Fi network set in menuconfig, and keeps trying when the connection drops.

#pragma once

#include "esp_err.h"

typedef enum {
    WIFI_NOT_SET,    // no Wi-Fi name in menuconfig
    WIFI_CONNECTING, // trying to join, or rejoining after losing it
    WIFI_CONNECTED,  // joined, with an IP address
} wifi_state_t;

/**
 * Called on every change of state. ip is the puck's address as text, such as
 * "192.168.1.23", when connected, and an empty string otherwise. Runs in the Wi-Fi event task:
 * keep it short, and lock LVGL before drawing.
 */
typedef void (*wifi_state_cb_t)(wifi_state_t state, const char *ip);

/** Starts joining the network. Calls on_change at once with the first state. */
esp_err_t wifi_start(wifi_state_cb_t on_change);
