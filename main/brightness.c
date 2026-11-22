// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The screen's brightness: see brightness.h.

#include "brightness.h"

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "brightness";
static const char *NVS_NAMESPACE = "puck";
static const char *NVS_KEY = "brightness";

static volatile int percent = 100;
static int saved_percent = 100; // as kept in NVS

void brightness_load(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return; // nothing kept yet
    uint8_t kept;
    if (nvs_get_u8(nvs, NVS_KEY, &kept) == ESP_OK && kept >= BRIGHTNESS_MIN_PERCENT && kept <= 100) {
        percent = saved_percent = kept;
    }
    nvs_close(nvs);
}

int brightness_percent(void)
{
    return percent;
}

uint8_t brightness_level(void)
{
    return (uint8_t)(percent * 255 / 100);
}

void brightness_set_percent(int new_percent)
{
    if (new_percent < BRIGHTNESS_MIN_PERCENT) new_percent = BRIGHTNESS_MIN_PERCENT;
    if (new_percent > 100) new_percent = 100;
    percent = new_percent;
}

void brightness_save(void)
{
    if (percent == saved_percent) return;
    saved_percent = percent;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGW(TAG, "Couldn't keep the brightness");
        return;
    }
    nvs_set_u8(nvs, NVS_KEY, (uint8_t)percent);
    nvs_commit(nvs);
    nvs_close(nvs);
}
