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
#include "freertos/queue.h"
#include "freertos/task.h"
#include "catalog.h"
#include "peer.h"
#include "puck_protocol.h"
#include "view_state.h"

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
// The biggest control message read; control messages are small JSON texts.
#define CONTROL_MAX_BYTES 512
// Keeping the link alive (the app's docs/puck.md, "Keeping the link alive"): each side
// sends at least one message a second, and silence for 4 s means the link is lost.
#define PING_INTERVAL_US (1000 * 1000)
#define SILENCE_LIMIT_US (4 * 1000 * 1000)
// Commands waiting for the link task to send them, and the biggest one.
#define COMMAND_QUEUE_LENGTH 8
#define COMMAND_MAX_BYTES 96
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
static QueueHandle_t commands;        // from the screen, sent by the link task
static int64_t last_heard_us;         // when the app's last message came, once paired
static int64_t last_sent_us;          // when we last sent the app anything
static bool hello_needed;             // puckHello to send, once paired

static void send_control(char *json);

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

// Writes a code point as UTF-8 into out at *o, if it fits.
static void put_utf8(unsigned code, char *out, size_t out_len, size_t *o)
{
    char bytes[3];
    size_t n = 0;
    if (code < 0x80) {
        bytes[n++] = (char)code;
    } else if (code < 0x800) {
        bytes[n++] = (char)(0xC0 | code >> 6);
        bytes[n++] = (char)(0x80 | (code & 0x3F));
    } else {
        bytes[n++] = (char)(0xE0 | code >> 12);
        bytes[n++] = (char)(0x80 | (code >> 6 & 0x3F));
        bytes[n++] = (char)(0x80 | (code & 0x3F));
    }
    if (*o + n >= out_len) return; // keep whole characters only
    memcpy(out + *o, bytes, n);
    *o += n;
}

// Reads the string after "key": in a JSON text, such as "name":"Crab Nebula", into out.
// Handles the escapes JSON.stringify writes; other characters, Greek letters among them,
// come as they are, in UTF-8. Our messages are small and flat, so no JSON library is needed.
static bool json_string(const char *json, const char *key, char *out, size_t out_len)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *p = strstr(json, pattern);
    if (!p || out_len == 0) return false;
    p += strlen(pattern);
    size_t o = 0;
    while (*p && *p != '"') {
        unsigned code = (unsigned char)*p++;
        if (code == '\\') {
            char e = *p++;
            switch (e) {
            case 'n': code = '\n'; break;
            case 't': code = '\t'; break;
            case 'r': code = '\r'; break;
            case 'b': code = '\b'; break;
            case 'f': code = '\f'; break;
            case 'u':
                if (sscanf(p, "%4x", &code) != 1) return false;
                p += 4;
                break;
            case '\0': return false;
            default: code = (unsigned char)e; break; // \" \\ \/
            }
            put_utf8(code, out, out_len, &o);
        } else if (o + 1 < out_len) {
            out[o++] = (char)code; // a byte as it is, part of UTF-8 or not
        }
    }
    out[o] = '\0';
    return *p == '"';
}

// Reads a whole number after "key": in a JSON text, such as "bytes":523000. Up to 2^32 - 1,
// for a CRC.
static bool json_uint(const char *json, const char *key, uint32_t *value)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return false;
    p += strlen(pattern);
    char *end;
    unsigned long long n = strtoull(p, &end, 10);
    if (end == p || n > UINT32_MAX) return false;
    *value = (uint32_t)n;
    return true;
}

// Reads true or false after "key": in a JSON text, such as "night":true.
static bool json_bool(const char *json, const char *key, bool *value)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return false;
    p += strlen(pattern);
    if (strncmp(p, "true", 4) == 0) {
        *value = true;
    } else if (strncmp(p, "false", 5) == 0) {
        *value = false;
    } else {
        return false;
    }
    return true;
}

// A binary view, from the view channel. See puck_protocol.h and the app's docs/puck.md.
static void on_view(const char *message, size_t length)
{
    puck_view_msg_t view;
    if (length < sizeof(view)) return;
    memcpy(&view, message, sizeof(view));
    // Unknown types and versions are ignored, so the app can add new ones.
    if (view.type != PUCK_VIEW_TYPE || view.version != PUCK_VIEW_VERSION) return;
    view_state_set_view(&view);
    // How many views arrive: about 30 a second while the map moves, 1 when still.
    static unsigned views;
    if (++views % 300 == 0) {
        ESP_LOGI(TAG, "%u views received (Alt %.1f, Az %.1f, FOV %.1f)", views, view.alt_deg,
                 view.az_deg, view.fov_deg);
    }
}

// A JSON message on the control channel, once the app is known.
static void on_control(const char *text)
{
    // The target's name: {"t":"targetInfo","id":"M1","name":"Crab Nebula"}, id null for none.
    if (strstr(text, "\"t\":\"targetInfo\"")) {
        char name[VIEW_STATE_NAME_BYTES];
        if (!json_string(text, "name", name, sizeof(name))) name[0] = '\0';
        view_state_set_target(name);
        ESP_LOGI(TAG, "Target: %s", name[0] ? name : "(none)");
    }
    // What the puck shows besides the map: {"t":"state","night":false,"solving":false,...}
    else if (strstr(text, "\"t\":\"state\"")) {
        bool night = false, solving = false;
        json_bool(text, "night", &night);
        json_bool(text, "solving", &solving);
        view_state_set_app_state(night, solving);
        ESP_LOGI(TAG, "State: night %d, solving %d", night, solving);
    }
    else if (strstr(text, "\"t\":\"catalogOk\"")) {
        ESP_LOGI(TAG, "The app says our catalogue is current");
    }
}

// A message on the catalogue channel: catalogStart as text, then the parts. Both come on
// this one ordered channel, so a part can never arrive before its catalogStart.
static void on_catalog(const char *message, size_t length)
{
    if (catalog_state(NULL, NULL) == CATALOG_RECEIVING) {
        catalog_add_part((const uint8_t *)message, length);
        return;
    }
    // {"t":"catalogStart","id":..,"bytes":..,"parts":..,"crc32":..}
    char text[CONTROL_MAX_BYTES];
    if (length >= sizeof(text)) return;
    memcpy(text, message, length);
    text[length] = '\0';
    if (!strstr(text, "\"t\":\"catalogStart\"")) return;
    char id[CATALOG_ID_CHARS + 2];
    uint32_t bytes, crc;
    if (json_string(text, "id", id, sizeof(id)) && json_uint(text, "bytes", &bytes) &&
        json_uint(text, "crc32", &crc)) {
        catalog_begin(id, bytes, crc); // a refusal is answered from the link task
    }
}

// Answers the app once a catalogue is kept or has failed.
static void answer_catalog(void)
{
    char text[96];
    switch (catalog_state(NULL, NULL)) {
    case CATALOG_DONE:
        snprintf(text, sizeof(text), "{\"t\":\"catalogDone\",\"id\":\"%s\"}", catalog_kept_id());
        break;
    case CATALOG_FAILED:
        snprintf(text, sizeof(text), "{\"t\":\"catalogFailed\",\"reason\":\"%s\"}", catalog_failure());
        break;
    default:
        return;
    }
    ESP_LOGI(TAG, "Sending %s", text);
    send_control(text);
    catalog_acknowledge();
}

// Our hello, once paired: what we are, and which catalogue we hold.
static void send_hello(void)
{
    char text[128];
    const char *id = catalog_kept_id();
    snprintf(text, sizeof(text),
             "{\"t\":\"puckHello\",\"version\":%d,\"screenPx\":%d,\"catalog\":%s%s%s}",
             PUCK_PROTOCOL_VERSION, PUCK_SCREEN_PX, id ? "\"" : "", id ? id : "null", id ? "\"" : "");
    ESP_LOGI(TAG, "Sending %s", text);
    send_control(text);
}

bool link_send_command(const char *json)
{
    if (!commands || !authenticated || strlen(json) >= COMMAND_MAX_BYTES) return false;
    char item[COMMAND_MAX_BYTES];
    strcpy(item, json);
    return xQueueSend(commands, item, 0) == pdTRUE;
}

// Sends a JSON text on the control channel. Only from the link task: libpeer is only used
// from there.
static void send_control(char *json)
{
    uint16_t sid;
    if (!authenticated || peer_connection_lookup_sid(connection, PUCK_CHANNEL_CONTROL, &sid) != 0) {
        return; // not linked any more: dropped
    }
    peer_connection_datachannel_send_sid(connection, json, strlen(json), sid);
    last_sent_us = esp_timer_get_time();
}

// Sends the commands the screen queued.
static void send_commands(void)
{
    char item[COMMAND_MAX_BYTES];
    while (xQueueReceive(commands, item, 0) == pdTRUE) {
        ESP_LOGI(TAG, "Sending %s", item);
        send_control(item);
    }
}

// Once paired: a ping when we've been quiet for a second, so the app knows we're here, and
// true when the app has been silent too long.
static bool link_silent(void)
{
    if (!authenticated) return false;
    int64_t now = esp_timer_get_time();
    if (now - last_sent_us > PING_INTERVAL_US) {
        static char ping[] = "{\"t\":\"ping\"}";
        send_control(ping);
    }
    return now - last_heard_us > SILENCE_LIMIT_US;
}

// Checks the app's first message, which must be its secret.
static void check_auth(const char *text)
{
    if (!strstr(text, "\"t\":\"auth\"")) return;
    char expected[64];
    snprintf(expected, sizeof(expected), "\"secret\":\"%s\"", secret_text);
    if (strstr(text, expected)) {
        authenticated = true;
        last_heard_us = last_sent_us = esp_timer_get_time();
        hello_needed = true; // sent from the link task, outside libpeer's callback
        ESP_LOGI(TAG, "App paired");
        ui.show_state("Linked");
    } else {
        ESP_LOGW(TAG, "Wrong secret: dropping the link");
        restart_needed = true;
    }
}

// Runs inside peer_connection_loop.
static void on_message(char *message, size_t length, void *user_data, uint16_t stream_id)
{
    // Any message, of any kind, shows the app is still there.
    if (authenticated) last_heard_us = esp_timer_get_time();

    // The view channel carries only binary views, and they count only once the app is known.
    uint16_t view_sid;
    if (peer_connection_lookup_sid(connection, PUCK_CHANNEL_VIEW, &view_sid) == 0 &&
        stream_id == view_sid) {
        if (authenticated) on_view(message, length);
        return;
    }
    // The catalogue channel carries catalogStart and the catalogue's parts, in order.
    uint16_t catalog_sid;
    if (peer_connection_lookup_sid(connection, PUCK_CHANNEL_CATALOG, &catalog_sid) == 0 &&
        stream_id == catalog_sid) {
        if (authenticated) on_catalog(message, length);
        return;
    }

    char text[CONTROL_MAX_BYTES];
    if (length >= sizeof(text)) {
        ESP_LOGW(TAG, "Message of %u bytes on stream %u: too big, skipped", (unsigned)length,
                 stream_id);
        return;
    }
    memcpy(text, message, length);
    text[length] = '\0';

    // Only the auth message counts before the secret is known to be right.
    if (!authenticated) {
        check_auth(text);
    } else {
        on_control(text);
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

// A fresh connection, credentials and secret, and its QR code on screen. after_loss says
// a link to the app was there and has gone, so the screen can say so.
static bool start_pairing(bool after_loss)
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
    hello_needed = false;
    // A catalogue cut short starts again from the beginning next time; an answer not sent
    // is not needed, as the next puckHello says which catalogue is kept.
    catalog_cancel();
    catalog_acknowledge();
    view_state_clear();
    channel_open_time = 0;
    restart_needed = false;

    char qr_text[160];
    if (!make_qr_text(&info, qr_text, sizeof(qr_text))) {
        ESP_LOGE(TAG, "Couldn't make the QR code");
        return false;
    }
    ESP_LOGI(TAG, "Pairing on %s:%u", info.ip, info.port);
    ui.show_pairing(qr_text, after_loss);
    return true;
}

static void link_task(void *arg)
{
    while (!start_pairing(false)) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    for (;;) {
        peer_connection_loop(connection);
        if (hello_needed && authenticated) {
            hello_needed = false;
            send_hello();
        }
        send_commands();
        answer_catalog();

        bool auth_late = channel_open_time && !authenticated &&
                         esp_timer_get_time() - channel_open_time > AUTH_TIMEOUT_US;
        if (auth_late) ESP_LOGW(TAG, "No secret in time: dropping the link");
        bool silent = link_silent();
        if (silent) ESP_LOGW(TAG, "Nothing from the app for 4 s: the link is lost");
        if (restart_needed || auth_late || silent) {
            // Paired until now: the link is lost, rather than refused.
            bool after_loss = authenticated;
            while (!start_pairing(after_loss)) {
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }
        vTaskDelay(1); // 1 ms (FreeRTOS ticks at 1 kHz)
    }
}

esp_err_t link_start(const link_callbacks_t *callbacks)
{
    ui = *callbacks;
    commands = xQueueCreate(COMMAND_QUEUE_LENGTH, COMMAND_MAX_BYTES);
    ESP_RETURN_ON_FALSE(commands, ESP_ERR_NO_MEM, TAG, "command queue");
    ESP_RETURN_ON_FALSE(peer_init() == 0, ESP_FAIL, TAG, "libpeer init");
    BaseType_t ok = xTaskCreatePinnedToCore(link_task, "link", LINK_TASK_STACK, NULL, 5, NULL,
                                            LINK_TASK_CORE);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}
