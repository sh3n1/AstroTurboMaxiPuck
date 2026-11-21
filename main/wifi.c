// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Wi-Fi in station mode: the puck joins the user's network, the same one as the phone.

#include "wifi.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "wifi";

// After losing the network, wait this long before trying again, so a router that is
// restarting isn't flooded with attempts.
#define RETRY_DELAY_MS 3000

static wifi_state_cb_t state_cb;
static TimerHandle_t retry_timer;

static void retry(TimerHandle_t timer)
{
    esp_wifi_connect();
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "Not connected (reason %d), trying again", event->reason);
        state_cb(WIFI_CONNECTING, "");
        xTimerStart(retry_timer, 0);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        char ip[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip));
        ESP_LOGI(TAG, "Connected, address %s", ip);
        state_cb(WIFI_CONNECTED, ip);
    }
}

esp_err_t wifi_start(wifi_state_cb_t on_change)
{
    state_cb = on_change;
    if (strlen(CONFIG_PUCK_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "No Wi-Fi name set: see AstroTurboMaxiPuck in menuconfig");
        state_cb(WIFI_NOT_SET, "");
        return ESP_OK;
    }

    // The Wi-Fi driver keeps its calibration data in NVS. Erase and retry if the NVS
    // partition is from another layout or a newer ESP-IDF.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "NVS");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta();

    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL),
                        TAG, "Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL),
                        TAG, "IP events");
    retry_timer = xTimerCreate("wifi_retry", pdMS_TO_TICKS(RETRY_DELAY_MS), pdFALSE, NULL, retry);

    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, CONFIG_PUCK_WIFI_SSID, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, CONFIG_PUCK_WIFI_PASSWORD, sizeof(config.sta.password));
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "config");
    // No power saving: the link to the phone needs low latency more than battery.
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "power save");

    ESP_LOGI(TAG, "Joining \"%s\"", CONFIG_PUCK_WIFI_SSID);
    state_cb(WIFI_CONNECTING, "");
    return esp_wifi_start();
}
