// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The networks the puck can join: see wifi_networks.h.

#include "wifi_networks.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "wifi_networks";

// All of them in one NVS entry, as an array: about 500 bytes.
#define NVS_SPACE "wifi"
#define KEY_NETWORKS "networks"
// The single network kept before the list (2026-11-22), taken into it once.
#define KEY_OLD_SSID "ssid"
#define KEY_OLD_PASSWORD "pass"

static wifi_network_t networks[WIFI_NETWORKS_KEPT];
static size_t count;

static esp_err_t keep(void)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_SPACE, NVS_READWRITE, &nvs), TAG, "NVS");
    esp_err_t err = nvs_set_blob(nvs, KEY_NETWORKS, networks, count * sizeof(networks[0]));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

void wifi_networks_load(void)
{
    nvs_handle_t nvs;
    count = 0;
    if (nvs_open(NVS_SPACE, NVS_READWRITE, &nvs) != ESP_OK) return;
    size_t bytes = sizeof(networks);
    if (nvs_get_blob(nvs, KEY_NETWORKS, networks, &bytes) == ESP_OK) count = bytes / sizeof(networks[0]);
    for (size_t i = 0; i < count; i++) {
        networks[i].ssid[sizeof(networks[i].ssid) - 1] = '\0';
        networks[i].password[sizeof(networks[i].password) - 1] = '\0';
    }
    // The network kept before the list, if any: into it, and the old entries gone.
    wifi_network_t old = {0};
    size_t len = sizeof(old.ssid);
    if (nvs_get_str(nvs, KEY_OLD_SSID, old.ssid, &len) == ESP_OK && old.ssid[0]) {
        len = sizeof(old.password);
        nvs_get_str(nvs, KEY_OLD_PASSWORD, old.password, &len);
        nvs_erase_key(nvs, KEY_OLD_SSID);
        nvs_erase_key(nvs, KEY_OLD_PASSWORD);
        nvs_commit(nvs);
        nvs_close(nvs);
        wifi_networks_add(old.ssid, old.password);
        return;
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "%u networks kept", (unsigned)count);
}

size_t wifi_networks_count(void)
{
    return count;
}

const wifi_network_t *wifi_networks_get(size_t i)
{
    return i < count ? &networks[i] : NULL;
}

const wifi_network_t *wifi_networks_find(const char *ssid)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(networks[i].ssid, ssid) == 0) return &networks[i];
    }
    return NULL;
}

// Removes the network at i, closing the gap.
static void remove_at(size_t i)
{
    memmove(&networks[i], &networks[i + 1], (count - i - 1) * sizeof(networks[0]));
    count--;
}

esp_err_t wifi_networks_add(const char *ssid, const char *password)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(networks[i].ssid, ssid) == 0) {
            remove_at(i);
            break;
        }
    }
    if (count == WIFI_NETWORKS_KEPT) count--; // the oldest goes
    memmove(&networks[1], &networks[0], count * sizeof(networks[0]));
    memset(&networks[0], 0, sizeof(networks[0]));
    strlcpy(networks[0].ssid, ssid, sizeof(networks[0].ssid));
    strlcpy(networks[0].password, password, sizeof(networks[0].password));
    count++;
    ESP_LOGI(TAG, "Network kept: \"%s\" (%u in all)", ssid, (unsigned)count);
    return keep();
}

esp_err_t wifi_networks_forget(const char *ssid)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(networks[i].ssid, ssid) == 0) {
            remove_at(i);
            ESP_LOGI(TAG, "Network forgotten: \"%s\"", ssid);
            return keep();
        }
    }
    return ESP_ERR_NOT_FOUND;
}
