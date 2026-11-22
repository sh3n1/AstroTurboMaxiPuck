// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The puck's Wi-Fi, in one of three modes (docs/screen-layout.md, "Settings → Wi-Fi"):
// - joining the user's network (station): the strongest in range of those kept in NVS
//   (wifi_networks.h), or else menuconfig's;
// - its own network (access point), for the field without a router, switched on in the menu;
// - setup: its own network for a moment, and a web page to choose the network to join
//   (wifi_setup.c). Opened from the menu, and by itself when no network is set or the
//   saved one can't be joined.
// Changing mode restarts the puck: the new mode is kept in NVS until then.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef enum {
    WIFI_NOT_SET,       // no network to join (setup follows)
    WIFI_CONNECTING,    // trying to join, or rejoining after losing it
    WIFI_CONNECTED,     // joined, with an IP address
    WIFI_OWN_WAITING,   // its own network is up, with no phone on it yet
    WIFI_OWN_JOINED,    // a phone has joined its own network
    WIFI_SETUP_WAITING, // setup: its own network is up, with no phone on it yet
    WIFI_SETUP_JOINED,  // setup: a phone has joined, and can open the page
} wifi_state_t;

/**
 * Called on every change of state. ip is the puck's address as text, such as
 * "192.168.1.23", when it has one, and an empty string otherwise. Runs in the Wi-Fi event task:
 * keep it short, and lock LVGL before drawing.
 */
typedef void (*wifi_state_cb_t)(wifi_state_t state, const char *ip);

/** Starts the Wi-Fi in the mode kept in NVS. Calls on_change at once with the first state. */
esp_err_t wifi_start(wifi_state_cb_t on_change);

/** Whether the puck makes its own network (the menu's AP switch). */
bool wifi_own_network(void);

/** Switches the puck's own network on or off: kept, then the puck restarts. */
void wifi_set_own_network(bool on);

/** Opens setup: kept, then the puck restarts into it. */
void wifi_open_setup(void);

/** The puck's own network's name, such as "AstroPuck-3F2A", short enough for the screen. */
const char *wifi_own_name(void);

/** The QR code that joins the puck's own network: "WIFI:T:WPA;S:<name>;P:<password>;;". */
void wifi_join_qr(char *out, size_t out_len);

/** Restarts the puck in a moment, so a web page's answer can still go out first. */
void wifi_restart_soon(void);
