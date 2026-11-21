// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The WebRTC link to the app: see link.h.

#include "link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "peer.h"

static const char *TAG = "link";

// The QR code's text starts with this, so a wrong code is told apart at once.
#define QR_PREFIX "AFP1:"
#define SECRET_BYTES 16
#define FINGERPRINT_BYTES 32 // SHA-256
// IPv4 (4) + port (2) + ICE ufrag (4) + ICE password (24) + fingerprint + secret
#define QR_PAYLOAD_BYTES (4 + 2 + 4 + 24 + FINGERPRINT_BYTES + SECRET_BYTES)
// libpeer makes ICE credentials of exactly these lengths.
#define UFRAG_CHARS 4
#define PWD_CHARS 24
// Once the data channel is open, the app must send its secret within this time.
#define AUTH_TIMEOUT_US (5 * 1000 * 1000)
// The link task runs on core 0, with the Wi-Fi, leaving core 1 to drawing.
#define LINK_TASK_CORE 0
#define LINK_TASK_STACK 16384

static link_callbacks_t ui;
static PeerConnection *connection;
static uint8_t secret[SECRET_BYTES];
static char secret_text[32];          // the secret in base64url, as the app sends it
static bool authenticated;
static int64_t channel_open_time;     // 0 until the data channel opens
static volatile bool restart_needed;  // set in libpeer's callbacks, acted on by the task

// Bytes as base64url without padding, as the app's pairing codes use.
static void base64url(const uint8_t *in, size_t len, char *out)
{
    static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = in[i] << 16 | (i + 1 < len ? in[i + 1] << 8 : 0) | (i + 2 < len ? in[i + 2] : 0);
        out[o++] = abc[n >> 18 & 63];
        out[o++] = abc[n >> 12 & 63];
        if (i + 1 < len) out[o++] = abc[n >> 6 & 63];
        if (i + 2 < len) out[o++] = abc[n & 63];
    }
    out[o] = '\0';
}

// The QR text from libpeer's details and the secret. See the app's docs/puck.md.
static bool make_qr_text(const PeerLiteInfo *info, char *out, size_t out_len)
{
    uint8_t payload[QR_PAYLOAD_BYTES];
    uint8_t *p = payload;
    unsigned ip[4];
    if (sscanf(info->ip, "%u.%u.%u.%u", &ip[0], &ip[1], &ip[2], &ip[3]) != 4) return false;
    if (strlen(info->ufrag) != UFRAG_CHARS || strlen(info->pwd) != PWD_CHARS) return false;
    for (int i = 0; i < 4; i++) *p++ = (uint8_t)ip[i];
    *p++ = info->port >> 8;
    *p++ = info->port & 0xFF;
    memcpy(p, info->ufrag, UFRAG_CHARS);
    p += UFRAG_CHARS;
    memcpy(p, info->pwd, PWD_CHARS);
    p += PWD_CHARS;
    // The fingerprint comes as "AB:CD:...": hex pairs with colons.
    const char *f = info->fingerprint;
    for (int i = 0; i < FINGERPRINT_BYTES; i++, f += 3) {
        unsigned byte;
        if (sscanf(f, "%2x", &byte) != 1) return false;
        *p++ = (uint8_t)byte;
    }
    memcpy(p, secret, SECRET_BYTES);

    if (out_len < strlen(QR_PREFIX) + (QR_PAYLOAD_BYTES * 4 + 2) / 3 + 1) return false;
    strcpy(out, QR_PREFIX);
    base64url(payload, QR_PAYLOAD_BYTES, out + strlen(QR_PREFIX));
    return true;
}

// Reads a number after "key": in a JSON text, such as "altDeg":12.5. Only for this test's
// pose messages, which the app writes in a fixed way; the real link will be binary.
static bool json_number(const char *json, const char *key, float *value)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *at = strstr(json, pattern);
    if (!at) return false;
    char *end;
    *value = strtof(at + strlen(pattern), &end);
    return end != at + strlen(pattern);
}

// Runs inside peer_connection_loop.
static void on_message(char *message, size_t length, void *user_data, uint16_t stream_id)
{
    char text[512];
    if (length >= sizeof(text)) {
        // Not a message this test reads; logged, to see messages put together from pieces.
        ESP_LOGI(TAG, "Message of %u bytes on stream %u", (unsigned)length, stream_id);
        return;
    }
    memcpy(text, message, length);
    text[length] = '\0';

    if (!authenticated) {
        // Only the auth message counts before the secret is known to be right.
        if (!strstr(text, "\"t\":\"auth\"")) return;
        char expected[64];
        snprintf(expected, sizeof(expected), "\"secret\":\"%s\"", secret_text);
        if (strstr(text, expected)) {
            authenticated = true;
            ESP_LOGI(TAG, "App paired");
            ui.show_state("Linked");
        } else {
            ESP_LOGW(TAG, "Wrong secret: dropping the link");
            restart_needed = true;
        }
        return;
    }

    // The app's pose: {"t":"pose",...,"pointing":{"altDeg":..,"azDeg":..},...}
    if (strstr(text, "\"t\":\"pose\"")) {
        const char *pointing = strstr(text, "\"pointing\":");
        float alt, az;
        if (pointing && json_number(pointing, "altDeg", &alt) && json_number(pointing, "azDeg", &az)) {
            ui.show_pose(alt, az);
        }
        // How many poses arrive: about 20 a second while the phone moves, 1 when still.
        static int poses;
        if (++poses % 100 == 0) ESP_LOGI(TAG, "%d poses received", poses);
    }
}

static void on_open(void *user_data)
{
    ESP_LOGI(TAG, "Data channel open, waiting for the secret");
    channel_open_time = esp_timer_get_time();
}

static void on_close(void *user_data)
{
    ESP_LOGI(TAG, "Data channel closed");
    restart_needed = true;
}

static void on_connection_state(PeerConnectionState state, void *user_data)
{
    ESP_LOGI(TAG, "Connection %s", peer_connection_state_to_string(state));
    if (state == PEER_CONNECTION_FAILED || state == PEER_CONNECTION_DISCONNECTED ||
        state == PEER_CONNECTION_CLOSED) {
        restart_needed = true;
    } else if (state == PEER_CONNECTION_CONNECTED) {
        ui.show_state("Connecting...");
    }
}

// A fresh connection, credentials and secret, and its QR code on screen.
static bool start_pairing(void)
{
    if (connection) peer_connection_destroy(connection);
    PeerConfiguration config = {.datachannel = DATA_CHANNEL_STRING};
    connection = peer_connection_create(&config);
    if (!connection) return false;
    peer_connection_oniceconnectionstatechange(connection, on_connection_state);
    peer_connection_ondatachannel(connection, on_message, on_open, on_close);

    PeerLiteInfo info;
    if (peer_connection_start_lite(connection, &info) != 0) return false;
    esp_fill_random(secret, sizeof(secret));
    base64url(secret, sizeof(secret), secret_text);
    authenticated = false;
    channel_open_time = 0;
    restart_needed = false;

    char qr_text[160];
    if (!make_qr_text(&info, qr_text, sizeof(qr_text))) {
        ESP_LOGE(TAG, "Couldn't make the QR code");
        return false;
    }
    ESP_LOGI(TAG, "Pairing on %s:%u", info.ip, info.port);
    ui.show_pairing(qr_text);
    return true;
}

static void link_task(void *arg)
{
    while (!start_pairing()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    for (;;) {
        peer_connection_loop(connection);

        bool auth_late = channel_open_time && !authenticated &&
                         esp_timer_get_time() - channel_open_time > AUTH_TIMEOUT_US;
        if (auth_late) ESP_LOGW(TAG, "No secret in time: dropping the link");
        if (restart_needed || auth_late) {
            while (!start_pairing()) {
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }
        vTaskDelay(1); // 1 ms (FreeRTOS ticks at 1 kHz)
    }
}

esp_err_t link_start(const link_callbacks_t *callbacks)
{
    ui = *callbacks;
    ESP_RETURN_ON_FALSE(peer_init() == 0, ESP_FAIL, TAG, "libpeer init");
    BaseType_t ok = xTaskCreatePinnedToCore(link_task, "link", LINK_TASK_STACK, NULL, 5, NULL,
                                            LINK_TASK_CORE);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}
