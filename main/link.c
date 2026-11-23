// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The WebRTC link to the app: see link.h.

#include "link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "catalog.h"
#include "json_read.h"
#include "align.h"
#include "lists.h"
#include "lx200.h"
#include "sky_icons.h"
#include "sky_objects.h"
#include "moving.h"
#include "peer.h"
#include "texts.h"
#include "puck_protocol.h"
#include "view_state.h"
#include "zoom.h"

static const char *TAG = "link";

// The QR code's text starts with this, so a wrong code is told apart at once. AFP2: the
// payload in base32, all capitals and digits, which a QR code holds in its compact
// alphanumeric mode (qr_view.h). The app also reads AFP1, the first firmware's base64url.
#define QR_PREFIX "AFP2:"
#define SECRET_BYTES 16
#define FINGERPRINT_BYTES 32 // SHA-256
// IPv4 (4) + port (2) + ICE ufrag (4) + ICE password (24) + fingerprint + secret
#define QR_PAYLOAD_BYTES (4 + 2 + 4 + 24 + FINGERPRINT_BYTES + SECRET_BYTES)
// libpeer makes ICE credentials of exactly these lengths.
#define UFRAG_CHARS 4
#define PWD_CHARS 24
// Once the data channel is open, the app must send its secret within this time.
#define AUTH_TIMEOUT_US (5 * 1000 * 1000)
// Once the app has been heard, the data channel must open within this time, or pairing
// starts again with a new QR code: a handshake with an address that doesn't answer never
// ends on its own.
#define CONNECT_TIMEOUT_US (20 * 1000 * 1000)
// The biggest control message read: `objects`, the Moon and planets, is the biggest, at a
// few KB. Read into one static buffer: messages only come in the link task.
#define CONTROL_MAX_BYTES 16384 // libpeer's SCTP_MAX_MESSAGE_SIZE: a list of 60 targets is about 7 KB
// Keeping the link alive (the app's docs/puck.md, "Keeping the link alive"): each side
// sends at least one message a second, and silence for 4 s means the link is lost.
#define PING_INTERVAL_US (1000 * 1000)
#define SILENCE_LIMIT_US (4 * 1000 * 1000)
// While a catalogue comes: catalogAck is said again this often, and with no new part for
// this long the transfer is given up.
#define CATALOG_ACK_REPEAT_US (500 * 1000)
#define CATALOG_STALL_US (15 * 1000 * 1000)
// Commands waiting for the link task to send them, and the biggest one.
#define COMMAND_QUEUE_LENGTH 8
#define COMMAND_MAX_BYTES 160 // a play command holds a list's UUID and a target's id
// Packets read at most each time round the link task's loop, before it rests a millisecond.
// One a millisecond was too few: the catalogue comes in bursts of dozens of packets, and the
// rest were dropped and resent seconds later, holding up everything behind them.
#define PACKETS_PER_ROUND 16
// The link task runs on core 0, with the Wi-Fi, leaving core 1 to drawing.
#define LINK_TASK_CORE 0
#define LINK_TASK_STACK 16384

static link_callbacks_t ui;
static PeerConnection *connection;
static uint8_t secret[SECRET_BYTES];
static char secret_text[32];          // the secret in base64url, as the app sends it
static bool authenticated;
static int64_t channel_open_time;     // 0 until the data channel opens
static int64_t peer_found_time;       // 0 until the app is first heard
static volatile bool restart_needed;  // set in libpeer's callbacks, acted on by the task
static const char *volatile restart_reason; // why, for the record of losses below
// The pairing code (address, port, ICE credentials, certificate and secret) stays the same
// until the puck restarts, so the app can link again with the code it read, without a new
// scan. A new one only at start-up, and after a wrong secret: someone tried an old code.
static volatile bool new_code_needed = true;

// The links lost so far, and the last one's reason, repeated in the log every 5 s: a loss
// happens when no one is watching the log, and its own line is soon gone.
static unsigned losses;
static const char *last_loss_reason = "";
static int64_t last_loss_us;
static int64_t last_loss_lasted_us;
static int64_t paired_us; // when the app sent its secret
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

// Bytes as base32 (RFC 4648) without padding: capitals and 2 to 7.
static void base32(const uint8_t *in, size_t len, char *out)
{
    static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    size_t o = 0;
    uint32_t value = 0;
    int bits = 0;
    for (size_t i = 0; i < len; i++) {
        value = (value << 8) | in[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out[o++] = abc[(value >> bits) & 31];
        }
        value &= (1u << bits) - 1;
    }
    if (bits > 0) out[o++] = abc[(value << (5 - bits)) & 31];
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

    if (out_len < strlen(QR_PREFIX) + (QR_PAYLOAD_BYTES * 8 + 4) / 5 + 1) return false;
    strcpy(out, QR_PREFIX);
    base32(payload, QR_PAYLOAD_BYTES, out + strlen(QR_PREFIX));
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
    // The target: {"t":"targetInfo","id":"M31","name":"Andromeda Galaxy","designations":
    // "M31 · NGC 224",...}, id null for none. Shown as "M31 · NGC 224  Andromeda Galaxy", or
    // once when they are the same (a planet's).
    if (strstr(text, "\"t\":\"targetInfo\"")) {
        // "list": the list observed through, with where in it ("3/110  Messier"), or "".
        char name[VIEW_STATE_NAME_BYTES], designations[VIEW_STATE_NAME_BYTES], list[VIEW_STATE_NAME_BYTES];
        if (!json_string(text, "name", name, sizeof(name))) name[0] = '\0';
        if (!json_string(text, "designations", designations, sizeof(designations))) designations[0] = '\0';
        if (!json_string(text, "list", list, sizeof(list))) list[0] = '\0';
        if (designations[0] && strcmp(designations, name) != 0) {
            char both[2 * VIEW_STATE_NAME_BYTES + 2];
            snprintf(both, sizeof(both), name[0] ? "%s  %s" : "%s%s", designations, name);
            view_state_set_target(both, list);
        } else {
            view_state_set_target(name, list);
        }
        // For the map: its label, kind and size, to draw it first and in full.
        char label[SKY_TARGET_LABEL_BYTES] = "";
        uint32_t kind = 0;
        float size_arcmin = 0;
        json_string(text, "label", label, sizeof(label));
        json_uint(text, "kind", &kind);
        json_float(text, "sizeArcmin", &size_arcmin); // null leaves it 0
        sky_objects_set_target(label, (uint8_t)kind, size_arcmin);
        ESP_LOGI(TAG, "Target: %s", name[0] ? name : "(none)");
    }
    // What the puck shows besides the map: {"t":"state","night":false,"solving":false,...}
    else if (strstr(text, "\"t\":\"state\"")) {
        view_state_app_t app = {0};
        json_bool(text, "night", &app.night);
        json_bool(text, "solving", &app.solving);
        json_bool(text, "sensorsOn", &app.sensors_on);
        json_float(text, "eyepieceFieldDeg", &app.eyepiece_field_deg);
        char view[16] = "";
        json_string(text, "view", view, sizeof(view));
        app.view_mode = strcmp(view, "eyepiece") == 0 ? VIEW_MODE_EYEPIECE
                        : strcmp(view, "ring") == 0   ? VIEW_MODE_RING
                                                      : VIEW_MODE_SKY;
        view_state_set_app_state(&app);
        ESP_LOGI(TAG, "State: night %d, solving %d, sensors %d, view %s", app.night, app.solving, app.sensors_on,
                 view);
    }
    // The app's record of the link lost before: {"t":"appLog","text":"..."}, kept in the log.
    else if (strstr(text, "\"t\":\"appLog\"")) {
        char line[400];
        if (json_string(text, "text", line, sizeof(line))) ESP_LOGW(TAG, "The app's record of the loss: %s", line);
    }
    // The words to show, in the app's language: {"t":"texts","compass":{...},"credits":".."}
    else if (strstr(text, "\"t\":\"texts\"")) {
        texts_parse(text);
        ESP_LOGI(TAG, "The app's words received");
    }
    // The rules for the puck's own zoom, which are the map's.
    else if (strstr(text, "\"t\":\"mapRules\"")) {
        zoom_set_rules(text);
    }
    // The menu's lists: {"t":"lists","lists":[...]}, and one list's targets.
    else if (strstr(text, "\"t\":\"lists\"")) {
        lists_parse_lists(text);
        ESP_LOGI(TAG, "The app's lists received");
    } else if (strstr(text, "\"t\":\"listTargets\"")) {
        lists_parse_targets(text);
        ESP_LOGI(TAG, "A list's targets received");
    }
    // The menu's Alignment page, and where an alignment from the puck is.
    else if (strstr(text, "\"t\":\"alignStars\"")) {
        lists_parse_align(text);
        ESP_LOGI(TAG, "The stars to align on received");
    } else if (strstr(text, "\"t\":\"alignStatus\"")) {
        align_set_status(text);
    }
    // Where the telescope points, for the LX200 repeater: {"t":"pointing","raDeg":..,"decDeg":..}
    else if (strstr(text, "\"t\":\"pointing\"")) {
        float ra, dec;
        if (json_float(text, "raDeg", &ra) && json_float(text, "decDeg", &dec)) lx200_set_pointing(ra, dec);
    }
    // A planet's icon: {"t":"icon","body":"Saturn","paths":[...]}
    else if (strstr(text, "\"t\":\"icon\"")) {
        sky_icons_parse(text);
    }
    // What moves: {"t":"objects","sun":{...},"items":[...]}
    else if (strstr(text, "\"t\":\"objects\"")) {
        size_t n = moving_parse(text);
        ESP_LOGI(TAG, "%u moving objects", (unsigned)n);
    } else if (strstr(text, "\"t\":\"catalogOk\"")) {
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
    char text[256];
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

// Answers the app as a catalogue comes in: how much has come, so it sends the next parts
// (catalogAck), and once it is kept or has failed.
static void answer_catalog(void)
{
    char text[96];
    static uint32_t acked;
    static int64_t acked_at, progress_at;
    uint32_t received = 0;
    catalog_state_t now = catalog_state(&received, NULL);
    int64_t time_now = esp_timer_get_time();
    if (now == CATALOG_RECEIVING || now == CATALOG_WRITING) {
        if (received != acked) progress_at = time_now;
        // Parts stopped coming: give up, so the app starts again (catalogFailed).
        if (now == CATALOG_RECEIVING && progress_at && time_now - progress_at > CATALOG_STALL_US) {
            ESP_LOGW(TAG, "The catalogue stopped coming at %u bytes: giving up", (unsigned)received);
            catalog_fail_receiving("stalled");
            progress_at = 0;
            return;
        }
        // Said again every so often even with nothing new: what we send is never resent, and
        // a lost catalogAck would leave the app waiting for it, and us for the next parts.
        if (received != acked || time_now - acked_at > CATALOG_ACK_REPEAT_US) {
            acked = received;
            acked_at = time_now;
            snprintf(text, sizeof(text), "{\"t\":\"catalogAck\",\"bytes\":%u}", (unsigned)received);
            send_control(text);
        }
        return;
    }
    acked = 0;
    progress_at = 0;
    switch (now) {
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
    // Whether the LX200 repeater is on, so the phone sends the pointing while it is (lx200.h).
    static char lx200_on[] = "{\"t\":\"lx200\",\"on\":true}";
    if (lx200_enabled()) send_control(lx200_on);
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
        last_heard_us = last_sent_us = paired_us = esp_timer_get_time();
        hello_needed = true; // sent from the link task, outside libpeer's callback
        ESP_LOGI(TAG, "App paired");
        ui.show_state("Linked");
    } else {
        ESP_LOGW(TAG, "Wrong secret: dropping the link");
        restart_reason = "wrong secret";
        new_code_needed = true;
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

    // In PSRAM, made on first use: internal RAM is kept for the Wi-Fi.
    static char *text;
    if (!text) text = heap_caps_malloc(CONTROL_MAX_BYTES, MALLOC_CAP_SPIRAM);
    if (!text) return;
    if (length >= CONTROL_MAX_BYTES) {
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
    restart_reason = "data channel closed";
    restart_needed = true;
}

static void on_connection_state(PeerConnectionState state, void *user_data)
{
    ESP_LOGI(TAG, "Connection %s", peer_connection_state_to_string(state));
    if (state == PEER_CONNECTION_FAILED || state == PEER_CONNECTION_DISCONNECTED ||
        state == PEER_CONNECTION_CLOSED) {
        restart_reason = peer_connection_state_to_string(state);
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
    bool new_code = new_code_needed;
    peer_connection_keep_identity(!new_code);
    PeerConfiguration config = {.datachannel = DATA_CHANNEL_STRING};
    connection = peer_connection_create(&config);
    if (!connection) return false;
    peer_connection_oniceconnectionstatechange(connection, on_connection_state);
    peer_connection_ondatachannel(connection, on_message, on_open, on_close);

    PeerLiteInfo info;
    if (peer_connection_start_lite(connection, &info) != 0) return false;
    if (new_code) {
        esp_fill_random(secret, sizeof(secret));
        base64url(secret, sizeof(secret), secret_text);
        new_code_needed = false;
    }
    authenticated = false;
    hello_needed = false;
    // A catalogue cut short starts again from the beginning next time; an answer not sent
    // is not needed, as the next puckHello says which catalogue is kept.
    catalog_cancel();
    catalog_acknowledge();
    view_state_clear();
    sky_objects_set_target("", 0, 0);
    moving_clear();
    channel_open_time = 0;
    peer_found_time = 0;
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
        // Every packet waiting, up to a limit: the loop returns the size of the one it read.
        for (int i = 0; i < PACKETS_PER_ROUND && peer_connection_loop(connection) > 0; i++) {
        }
        if (hello_needed && authenticated) {
            hello_needed = false;
            send_hello();
        }
        send_commands();
        answer_catalog();

        int64_t now = esp_timer_get_time();
        // Memory, now and then, to catch the internal RAM running short (the Wi-Fi and lwIP
        // need it, and fail to send with "Not enough space" without).
        static int64_t memory_logged;
        if (now - memory_logged > 5 * 1000 * 1000) {
            memory_logged = now;
            ESP_LOGI(TAG, "Free internal %u KB (lowest %u KB, largest block %u KB), PSRAM %u KB",
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
            if (authenticated) {
                uint32_t data_in, sacks_in, sack_cum, next_tsn, heartbeats, write_errors, resent;
                peer_connection_sctp_stats(connection, &data_in, &sacks_in, &sack_cum, &next_tsn, &heartbeats,
                                           &write_errors, &resent);
                ESP_LOGI(TAG, "SCTP: %u chunks in, %u SACKs in (acked to %u, our next %u), %u heartbeats, "
                         "%u write errors, %u resent", (unsigned)data_in, (unsigned)sacks_in, (unsigned)sack_cum,
                         (unsigned)next_tsn, (unsigned)heartbeats, (unsigned)write_errors, (unsigned)resent);
            }
            if (losses) {
                ESP_LOGI(TAG, "Links lost: %u; the last %u s ago, after %u s linked: %s", losses,
                         (unsigned)((now - last_loss_us) / 1000000), (unsigned)(last_loss_lasted_us / 1000000),
                         last_loss_reason);
            }
        }
        bool auth_late = channel_open_time && !authenticated && now - channel_open_time > AUTH_TIMEOUT_US;
        if (auth_late) ESP_LOGW(TAG, "No secret in time: dropping the link");
        if (!peer_found_time && peer_connection_lite_peer_found(connection)) peer_found_time = now;
        bool connect_late = peer_found_time && !channel_open_time && now - peer_found_time > CONNECT_TIMEOUT_US;
        if (connect_late) ESP_LOGW(TAG, "The app was heard, but the link didn't follow: starting again");
        bool silent = link_silent();
        if (silent) ESP_LOGW(TAG, "Nothing from the app for 4 s: the link is lost");
        if (restart_needed || auth_late || connect_late || silent) {
            // Paired until now: the link is lost, rather than refused.
            bool after_loss = authenticated;
            if (after_loss) {
                losses++;
                last_loss_reason = silent ? "nothing from the app for 4 s" : restart_reason ? restart_reason : "?";
                last_loss_us = now;
                last_loss_lasted_us = now - paired_us;
            }
            restart_reason = NULL;
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
