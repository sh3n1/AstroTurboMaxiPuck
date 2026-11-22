// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The networks the puck can join, each with its password, kept in NVS: up to
// WIFI_NETWORKS_KEPT, the latest saved first. Saved from the setup page (wifi_setup.c);
// the puck joins the strongest of them in range (wifi.c).

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define WIFI_NETWORKS_KEPT 5

typedef struct {
    char ssid[33];     // WPA's longest name, 32 bytes
    char password[65]; // and longest password, 64
} wifi_network_t;

/** Reads the networks kept. Call once, after nvs_flash_init. */
void wifi_networks_load(void);

/** How many are kept, and each, the latest saved first. */
size_t wifi_networks_count(void);
const wifi_network_t *wifi_networks_get(size_t i);

/** The one with this name, or NULL. */
const wifi_network_t *wifi_networks_find(const char *ssid);

/** Keeps a network first, replacing one of the same name; the oldest goes past the limit. */
esp_err_t wifi_networks_add(const char *ssid, const char *password);

/** Forgets the network with this name. */
esp_err_t wifi_networks_forget(const char *ssid);
