// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The puck's Wi-Fi: see wifi.h.

#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "wifi_networks.h"
#include "wifi_setup.h"

static const char *TAG = "wifi";

// After losing the network, wait this long before looking again, so a router that is
// restarting isn't flooded with attempts.
#define RETRY_DELAY_MS 3000
#define SCAN_RECORDS_MAX 20
// No kept network joined this long after start-up: setup opens, to choose another.
#define JOIN_GIVE_UP_MS (60 * 1000)
// Setup with no phone on it this long: back to trying the saved network.
#define SETUP_GIVE_UP_MS (5 * 60 * 1000)
#define RESTART_DELAY_MS 1500
#define OWN_PASSWORD_CHARS 10
#define OWN_MAX_PHONES 2

// What is kept in NVS.
#define NVS_SPACE "wifi"
#define KEY_OWN "own"          // 1: make its own network
#define KEY_SETUP "setup"      // 1: open setup at the next start
#define KEY_OWN_PASSWORD "own_pass"

typedef enum { MODE_STATION, MODE_OWN, MODE_SETUP } puck_wifi_mode_t;

static wifi_state_cb_t state_cb;
static puck_wifi_mode_t mode;
static TimerHandle_t retry_timer, give_up_timer, restart_timer;
static bool ever_joined;
static char address[16]; // the puck's IP address, "" until it has one
static wifi_network_t fallback; // menuconfig's network, for development, after the kept ones
static unsigned blind_tries;    // none seen: each tried in turn, as a hidden one isn't seen
static char own_name[33];
static char own_password[OWN_PASSWORD_CHARS + 1];
static const char OWN_IP[] = "192.168.4.1"; // ESP-IDF's access point's own address

static void restart(TimerHandle_t timer)
{
    esp_restart();
}

void wifi_restart_soon(void)
{
    xTimerStart(restart_timer, 0);
}

static void keep_u8(const char *key, uint8_t value)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_SPACE, NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_u8(nvs, key, value);
    nvs_commit(nvs);
    nvs_close(nvs);
}

static uint8_t kept_u8(const char *key)
{
    nvs_handle_t nvs;
    uint8_t value = 0;
    if (nvs_open(NVS_SPACE, NVS_READONLY, &nvs) != ESP_OK) return 0;
    nvs_get_u8(nvs, key, &value);
    nvs_close(nvs);
    return value;
}

static bool kept_string(const char *key, char *out, size_t out_len)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_SPACE, NVS_READONLY, &nvs) != ESP_OK) return false;
    size_t len = out_len;
    bool ok = nvs_get_str(nvs, key, out, &len) == ESP_OK;
    nvs_close(nvs);
    return ok;
}

bool wifi_own_network(void)
{
    return mode == MODE_OWN;
}

void wifi_set_own_network(bool on)
{
    keep_u8(KEY_OWN, on ? 1 : 0);
    ESP_LOGI(TAG, "Own network %s: restarting", on ? "on" : "off");
    wifi_restart_soon();
}

void wifi_open_setup(void)
{
    keep_u8(KEY_SETUP, 1);
    ESP_LOGI(TAG, "Setup asked for: restarting");
    wifi_restart_soon();
}

const char *wifi_address(void)
{
    return address;
}

const char *wifi_own_name(void)
{
    return own_name;
}

void wifi_join_qr(char *out, size_t out_len)
{
    // The name and password are letters and digits only, so need no escaping.
    snprintf(out, out_len, "WIFI:T:WPA;S:%s;P:%s;;", own_name, own_password);
}

// The own network's name from the puck's MAC address, and its password, made once and kept,
// so a phone that joined it once joins again by itself.
static void make_own_identity(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(own_name, sizeof(own_name), "AstroPuck-%02X%02X", mac[4], mac[5]);
    if (kept_string(KEY_OWN_PASSWORD, own_password, sizeof(own_password)) &&
        strlen(own_password) == OWN_PASSWORD_CHARS) {
        return;
    }
    // No 0/O or 1/l/I: it may be typed by hand.
    static const char letters[] = "abcdefghijkmnpqrstuvwxyz23456789";
    for (int i = 0; i < OWN_PASSWORD_CHARS; i++) own_password[i] = letters[esp_random() % (sizeof(letters) - 1)];
    own_password[OWN_PASSWORD_CHARS] = '\0';
    nvs_handle_t nvs;
    if (nvs_open(NVS_SPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, KEY_OWN_PASSWORD, own_password);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

// The networks the puck may join: the kept ones, then menuconfig's if it isn't among them.
static size_t candidate_count(void)
{
    return wifi_networks_count() + (fallback.ssid[0] ? 1 : 0);
}

static const wifi_network_t *candidate(size_t i)
{
    return i < wifi_networks_count() ? wifi_networks_get(i) : &fallback;
}

// Looks for the networks around; joining follows on WIFI_EVENT_SCAN_DONE.
static void start_scan(void)
{
    if (esp_wifi_scan_start(NULL, false) != ESP_OK) xTimerStart(retry_timer, 0);
}

static void retry(TimerHandle_t timer)
{
    start_scan();
}

static void join(const wifi_network_t *network)
{
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, network->ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, network->password, sizeof(config.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &config);
    ESP_LOGI(TAG, "Joining \"%s\"", network->ssid);
    esp_wifi_connect();
}

// The scan is done: joins the strongest kept network in range. If none is seen, each is
// tried in turn, as a hidden network doesn't show in a scan.
static void join_best(void)
{
    uint16_t n = SCAN_RECORDS_MAX;
    wifi_ap_record_t *found = calloc(SCAN_RECORDS_MAX, sizeof(*found));
    const wifi_network_t *best = NULL;
    int best_rssi = -1000;
    if (found && esp_wifi_scan_get_ap_records(&n, found) == ESP_OK) {
        for (int i = 0; i < n; i++) {
            for (size_t c = 0; c < candidate_count(); c++) {
                if (strcmp(candidate(c)->ssid, (const char *)found[i].ssid) == 0 && found[i].rssi > best_rssi) {
                    best = candidate(c);
                    best_rssi = found[i].rssi;
                }
            }
        }
    } else {
        esp_wifi_clear_ap_list(); // frees the scan's records
    }
    free(found);
    if (!best) {
        best = candidate(blind_tries++ % candidate_count());
        ESP_LOGW(TAG, "No kept network in range: trying \"%s\" anyway", best->ssid);
    }
    join(best);
}

// Not joined in time (station), or no one came (setup): try the other.
static void give_up(TimerHandle_t timer)
{
    if (mode == MODE_STATION && !ever_joined) {
        ESP_LOGW(TAG, "The network wasn't joined in time: opening setup");
        wifi_open_setup();
    } else if (mode == MODE_SETUP) {
        ESP_LOGW(TAG, "No one came to setup: trying the saved network again");
        wifi_restart_soon();
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (mode == MODE_STATION) start_scan();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        if (mode == MODE_STATION) join_best(); // setup's page scans for itself
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (mode != MODE_STATION) return; // setup only scans
        const wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "Not connected (reason %d), trying again", event->reason);
        address[0] = '\0';
        state_cb(WIFI_CONNECTING, "");
        xTimerStart(retry_timer, 0);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        char ip[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip));
        strlcpy(address, ip, sizeof(address));
        ESP_LOGI(TAG, "Connected, address %s", ip);
        ever_joined = true;
        xTimerStop(give_up_timer, 0);
        state_cb(WIFI_CONNECTED, ip);
    } else if (base == WIFI_EVENT && (id == WIFI_EVENT_AP_STACONNECTED || id == WIFI_EVENT_AP_STADISCONNECTED)) {
        wifi_sta_list_t phones;
        int count = esp_wifi_ap_get_sta_list(&phones) == ESP_OK ? phones.num : 0;
        ESP_LOGI(TAG, "A phone %s our network (%d on it)", id == WIFI_EVENT_AP_STACONNECTED ? "joined" : "left", count);
        if (mode == MODE_SETUP) {
            if (count > 0) xTimerStop(give_up_timer, 0);
            else xTimerStart(give_up_timer, 0);
            state_cb(count > 0 ? WIFI_SETUP_JOINED : WIFI_SETUP_WAITING, OWN_IP);
        } else {
            state_cb(count > 0 ? WIFI_OWN_JOINED : WIFI_OWN_WAITING, OWN_IP);
        }
    }
}

// The puck's own network, on its own (MODE_OWN) or beside the station, which setup uses
// only to see the networks around.
static esp_err_t start_own_network(wifi_mode_t wifi_mode)
{
    wifi_config_t config = {
        .ap = {
            .channel = 1,
            .max_connection = OWN_MAX_PHONES,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strlcpy((char *)config.ap.ssid, own_name, sizeof(config.ap.ssid));
    config.ap.ssid_len = strlen(own_name);
    strlcpy((char *)config.ap.password, own_password, sizeof(config.ap.password));
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(wifi_mode), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), TAG, "own network");
    ESP_LOGI(TAG, "Own network \"%s\" for %s", own_name, mode == MODE_SETUP ? "setup" : "the phone");
    return ESP_OK;
}

static esp_err_t start_station(void)
{
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_LOGI(TAG, "Looking for %u kept networks", (unsigned)candidate_count());
    xTimerStart(give_up_timer, 0);
    return ESP_OK;
}

esp_err_t wifi_start(wifi_state_cb_t on_change)
{
    state_cb = on_change;
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
    esp_netif_create_default_wifi_ap();
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL),
                        TAG, "Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL),
                        TAG, "IP events");
    retry_timer = xTimerCreate("wifi_retry", pdMS_TO_TICKS(RETRY_DELAY_MS), pdFALSE, NULL, retry);
    give_up_timer = xTimerCreate("wifi_give_up", pdMS_TO_TICKS(JOIN_GIVE_UP_MS), pdFALSE, NULL, give_up);
    restart_timer = xTimerCreate("wifi_restart", pdMS_TO_TICKS(RESTART_DELAY_MS), pdFALSE, NULL, restart);
    make_own_identity();

    // The networks to join: those saved from the setup page, then menuconfig's.
    wifi_networks_load();
    if (!wifi_networks_find(CONFIG_PUCK_WIFI_SSID)) {
        strlcpy(fallback.ssid, CONFIG_PUCK_WIFI_SSID, sizeof(fallback.ssid));
        strlcpy(fallback.password, CONFIG_PUCK_WIFI_PASSWORD, sizeof(fallback.password));
    }
    bool none = candidate_count() == 0;
    // Setup asked for is opened once: the next start is as before.
    bool setup = kept_u8(KEY_SETUP);
    if (setup) keep_u8(KEY_SETUP, 0);
    mode = kept_u8(KEY_OWN) ? MODE_OWN : (setup || none) ? MODE_SETUP : MODE_STATION;

    switch (mode) {
    case MODE_STATION:
        ESP_RETURN_ON_ERROR(start_station(), TAG, "station");
        state_cb(WIFI_CONNECTING, "");
        break;
    case MODE_OWN:
        ESP_RETURN_ON_ERROR(start_own_network(WIFI_MODE_AP), TAG, "own network");
        break;
    case MODE_SETUP:
        if (none) state_cb(WIFI_NOT_SET, "");
        ESP_RETURN_ON_ERROR(start_own_network(WIFI_MODE_APSTA), TAG, "setup network");
        xTimerChangePeriod(give_up_timer, pdMS_TO_TICKS(SETUP_GIVE_UP_MS), 0);
        break;
    }
    // No power saving: the link to the phone needs low latency more than battery.
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "power save");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    if (mode != MODE_STATION) strlcpy(address, OWN_IP, sizeof(address));
    if (mode == MODE_OWN) state_cb(WIFI_OWN_WAITING, OWN_IP);
    if (mode == MODE_SETUP) {
        ESP_RETURN_ON_ERROR(wifi_setup_start(), TAG, "setup page");
        state_cb(WIFI_SETUP_WAITING, OWN_IP);
    }
    return ESP_OK;
}
