// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// A first WebRTC test between a phone and the puck: see link_test.h.

#include "link_test.h"

#include <string.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "peer.h"

static const char *TAG = "link";

// The test page, built into the firmware (see EMBED_TXTFILES in main/CMakeLists.txt).
extern const char test_page_start[] asm("_binary_link_test_page_html_start");
extern const char test_page_end[] asm("_binary_link_test_page_html_end");

// An offer from a browser is a few kilobytes; anything far bigger isn't one.
#define MAX_OFFER_BYTES 8192
// The link task runs on core 0, with the Wi-Fi, leaving core 1 to drawing.
#define LINK_TASK_CORE 0
#define LINK_TASK_STACK 16384

static link_test_state_cb_t state_cb;
// The one connection, replaced by each new offer. libpeer isn't thread-safe, so the web
// server and the link task only touch it while holding this lock.
static PeerConnection *connection;
static SemaphoreHandle_t connection_lock;
static int messages_echoed;

// Runs inside peer_connection_loop, so the lock is already held.
static void on_message(char *message, size_t length, void *user_data, uint16_t stream_id)
{
    peer_connection_datachannel_send_sid(connection, message, length, stream_id);
    if (++messages_echoed % 50 == 0) {
        ESP_LOGI(TAG, "%d messages echoed", messages_echoed);
    }
}

static void on_open(void *user_data)
{
    ESP_LOGI(TAG, "Data channel open");
    state_cb("Link open");
}

static void on_close(void *user_data)
{
    ESP_LOGI(TAG, "Data channel closed");
    state_cb("Link closed");
}

static void on_connection_state(PeerConnectionState state, void *user_data)
{
    ESP_LOGI(TAG, "Connection %s", peer_connection_state_to_string(state));
    if (state == PEER_CONNECTION_CHECKING) state_cb("Link connecting");
    if (state == PEER_CONNECTION_FAILED) state_cb("Link failed");
}

// Keeps the connection going: ICE checks, the DTLS handshake, SCTP, messages.
static void link_task(void *arg)
{
    for (;;) {
        xSemaphoreTake(connection_lock, portMAX_DELAY);
        if (connection) peer_connection_loop(connection);
        xSemaphoreGive(connection_lock);
        vTaskDelay(1); // 1 ms (FreeRTOS ticks at 1 kHz)
    }
}

static esp_err_t serve_page(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, test_page_start, test_page_end - test_page_start);
}

// Takes the browser's offer and sends back the answer.
static esp_err_t take_offer(httpd_req_t *request)
{
    if (request->content_len == 0 || request->content_len > MAX_OFFER_BYTES) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Not an offer");
    }
    char *offer = calloc(1, request->content_len + 1);
    if (!offer) return httpd_resp_send_500(request);
    for (size_t received = 0; received < request->content_len;) {
        int got = httpd_req_recv(request, offer + received, request->content_len - received);
        if (got <= 0) {
            free(offer);
            return ESP_FAIL;
        }
        received += got;
    }

    xSemaphoreTake(connection_lock, portMAX_DELAY);
    if (connection) peer_connection_destroy(connection);
    PeerConfiguration config = {
        .datachannel = DATA_CHANNEL_STRING,
        // No ICE servers: the phone and the puck are on the same network.
    };
    connection = peer_connection_create(&config);
    peer_connection_oniceconnectionstatechange(connection, on_connection_state);
    peer_connection_ondatachannel(connection, on_message, on_open, on_close);
    messages_echoed = 0;

    // The answer first, which gathers the puck's own addresses, then the offer, which pairs
    // them with the phone's.
    const char *answer = peer_connection_create_answer(connection);
    peer_connection_set_remote_description(connection, offer, SDP_TYPE_OFFER);
    free(offer);
    ESP_LOGI(TAG, "Answer ready");

    httpd_resp_set_type(request, "application/sdp");
    esp_err_t err = httpd_resp_sendstr(request, answer);
    xSemaphoreGive(connection_lock);
    return err;
}

esp_err_t link_test_start(link_test_state_cb_t on_state)
{
    state_cb = on_state;
    connection_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(peer_init() == 0, ESP_FAIL, TAG, "libpeer init");

    httpd_handle_t server;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = LINK_TASK_CORE;
    config.stack_size = 16384; // making the answer creates the DTLS certificate
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "web server");
    const httpd_uri_t page = {.uri = "/", .method = HTTP_GET, .handler = serve_page};
    const httpd_uri_t offer = {.uri = "/offer", .method = HTTP_POST, .handler = take_offer};
    httpd_register_uri_handler(server, &page);
    httpd_register_uri_handler(server, &offer);

    xTaskCreatePinnedToCore(link_task, "link", LINK_TASK_STACK, NULL, 5, NULL, LINK_TASK_CORE);
    state_cb("Open the page");
    ESP_LOGI(TAG, "Test page ready");
    return ESP_OK;
}
