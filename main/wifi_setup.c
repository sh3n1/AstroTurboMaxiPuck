// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The setup page: see wifi_setup.h.

#include "wifi_setup.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "wifi.h"
#include "wifi_networks.h"

static const char *TAG = "wifi_setup";

#define NETWORKS_MAX 20
#define BODY_MAX_BYTES 400 // the name and password, each up to three times longer encoded

// Big text and buttons, dark, as the puck's screen: read at arm's length, often at night.
static const char PAGE_HEAD[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>AstroTurboMaxiPuck</title><style>"
    "body{margin:0;padding:16px;background:#05070f;color:#e8ebf5;font:20px system-ui,sans-serif}"
    "h1{font-size:26px;margin:8px 0 16px}"
    "button,input{box-sizing:border-box;width:100%;min-height:56px;margin:6px 0;padding:0 16px;"
    "border-radius:14px;font:inherit}"
    "button{border:0;background:#1c2438;color:#e8ebf5;text-align:left}"
    "input{border:2px solid #1c2438;background:#000;color:#e8ebf5}"
    "button.save{background:#a9cbff;color:#05070f;text-align:center;font-weight:600;margin-top:16px}"
    "label{display:block;margin-top:18px}h2{font-size:20px;margin:22px 0 4px}"
    ".kept{display:flex;gap:8px;align-items:center}.kept span{flex:1;overflow-wrap:anywhere}"
    ".kept button{width:auto;text-align:center}"
    "</style></head><body><h1>Wi-Fi for the puck</h1>";

static const char PAGE_FORM[] =
    "<form method=post action=/save>"
    "<label for=ssid>Network</label><input id=ssid name=ssid maxlength=32 required>"
    "<label for=pass>Password</label><input id=pass name=pass type=password maxlength=63>"
    "<button class=save type=submit>Save and join</button></form>"
    "<script>document.querySelectorAll('button[data-ssid]').forEach(b=>b.onclick=()=>{"
    "ssid.value=b.dataset.ssid;pass.focus()})</script></body></html>";

// Writes text with HTML's special characters escaped: network names can hold anything.
static void send_escaped(httpd_req_t *req, const char *text)
{
    char out[8 * 33];
    size_t o = 0;
    for (const char *p = text; *p && o + 7 < sizeof(out); p++) {
        const char *entity = *p == '&' ? "&amp;" : *p == '<' ? "&lt;" : *p == '>' ? "&gt;"
                           : *p == '"' ? "&quot;" : *p == '\'' ? "&#39;" : NULL;
        if (entity) {
            strcpy(out + o, entity);
            o += strlen(entity);
        } else {
            out[o++] = *p;
        }
    }
    out[o] = '\0';
    httpd_resp_send_chunk(req, out, o);
}

// How strong a network is, as bars, from its signal in dBm.
static const char *bars(int rssi)
{
    return rssi > -55 ? "▂▄▆█" : rssi > -67 ? "▂▄▆" : rssi > -78 ? "▂▄" : "▂";
}

// The networks kept, each with a button to forget it.
static void send_kept(httpd_req_t *req)
{
    size_t count = wifi_networks_count();
    if (count == 0) return;
    char heading[64];
    snprintf(heading, sizeof(heading), "<h2>Kept (%u of %d)</h2>", (unsigned)count, WIFI_NETWORKS_KEPT);
    httpd_resp_send_chunk(req, heading, HTTPD_RESP_USE_STRLEN);
    for (size_t i = 0; i < count; i++) {
        const char *name = wifi_networks_get(i)->ssid;
        httpd_resp_send_chunk(req, "<form class=kept method=post action=/forget><span>", HTTPD_RESP_USE_STRLEN);
        send_escaped(req, name);
        httpd_resp_send_chunk(req, "</span><input type=hidden name=ssid value=\"", HTTPD_RESP_USE_STRLEN);
        send_escaped(req, name);
        httpd_resp_send_chunk(req, "\"><button type=submit>Forget</button></form>", HTTPD_RESP_USE_STRLEN);
    }
}

// The page: the networks kept, then those the puck sees, strongest first, each once; a tap
// on one fills the form, where another can be typed too. The puck joins the strongest kept
// one in range.
static esp_err_t on_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, PAGE_HEAD, HTTPD_RESP_USE_STRLEN);
    send_kept(req);
    httpd_resp_send_chunk(req, "<h2>Around</h2>", HTTPD_RESP_USE_STRLEN);
    wifi_ap_record_t *found = calloc(NETWORKS_MAX, sizeof(*found));
    uint16_t count = NETWORKS_MAX;
    if (found && esp_wifi_scan_start(NULL, true) == ESP_OK && esp_wifi_scan_get_ap_records(&count, found) == ESP_OK) {
        for (int i = 0; i < count; i++) {
            const char *name = (const char *)found[i].ssid;
            bool seen = !name[0];
            for (int j = 0; j < i && !seen; j++) seen = strcmp(name, (const char *)found[j].ssid) == 0;
            if (seen) continue;
            httpd_resp_send_chunk(req, "<button type=button data-ssid=\"", HTTPD_RESP_USE_STRLEN);
            send_escaped(req, name);
            httpd_resp_send_chunk(req, "\">", HTTPD_RESP_USE_STRLEN);
            send_escaped(req, name);
            char signal[48];
            snprintf(signal, sizeof(signal), " &nbsp;%s%s</button>", bars(found[i].rssi),
                     found[i].authmode == WIFI_AUTH_OPEN ? " (open)" : "");
            httpd_resp_send_chunk(req, signal, HTTPD_RESP_USE_STRLEN);
        }
    } else {
        httpd_resp_send_chunk(req, "<p>No networks found: type its name.</p>", HTTPD_RESP_USE_STRLEN);
    }
    free(found);
    httpd_resp_send_chunk(req, PAGE_FORM, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

// Decodes a form's value in place: '+' is a space, and %XX a byte.
static void url_decode(char *text)
{
    char *out = text;
    for (char *p = text; *p; p++) {
        if (*p == '+') {
            *out++ = ' ';
        } else if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            char hex[3] = {p[1], p[2], '\0'};
            *out++ = (char)strtol(hex, NULL, 16);
            p += 2;
        } else {
            *out++ = *p;
        }
    }
    *out = '\0';
}

// Reads a form's body into body (BODY_MAX_BYTES + 1); false if it didn't come.
static bool read_body(httpd_req_t *req, char *body)
{
    int len = req->content_len < BODY_MAX_BYTES ? req->content_len : BODY_MAX_BYTES;
    int got = 0;
    while (got < len) {
        int n = httpd_req_recv(req, body + got, len - got);
        if (n <= 0) return false;
        got += n;
    }
    body[got] = '\0';
    return true;
}

// Forgets a kept network, and shows the page again.
static esp_err_t on_forget(httpd_req_t *req)
{
    char body[BODY_MAX_BYTES + 1];
    if (!read_body(req, body)) return ESP_FAIL;
    char ssid[3 * 32 + 1] = "";
    httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
    url_decode(ssid);
    wifi_networks_forget(ssid);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t on_save(httpd_req_t *req)
{
    char body[BODY_MAX_BYTES + 1];
    if (!read_body(req, body)) return ESP_FAIL;
    char ssid[3 * 32 + 1] = "", password[3 * 63 + 1] = ""; // encoded: up to three bytes each
    httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(body, "pass", password, sizeof(password));
    url_decode(ssid);
    url_decode(password);
    // WPA's rules: a name of 1 to 32 bytes, and no password (open) or one of 8 to 63.
    size_t pass_len = strlen(password);
    bool ok = ssid[0] && strlen(ssid) <= 32 && (pass_len == 0 || (pass_len >= 8 && pass_len <= 63));
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, PAGE_HEAD, HTTPD_RESP_USE_STRLEN);
    if (!ok) {
        httpd_resp_send_chunk(req, "<p>The password must be 8 to 63 characters, or empty for an open network.</p>"
                                   "<p><a href=/ style='color:#a9cbff'>Back</a></p>", HTTPD_RESP_USE_STRLEN);
    } else if (wifi_networks_add(ssid, password) != ESP_OK) {
        httpd_resp_send_chunk(req, "<p>Couldn't keep it. Try again.</p>", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send_chunk(req, "<p>Kept <b>", HTTPD_RESP_USE_STRLEN);
        send_escaped(req, ssid);
        httpd_resp_send_chunk(req, "</b>. The puck now restarts and joins the strongest kept network in "
                                   "range.</p><p>Put this phone back on that network too, then scan the "
                                   "puck's code in the app.</p>", HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_send_chunk(req, "</body></html>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    if (ok) wifi_restart_soon();
    return ESP_OK;
}

esp_err_t wifi_setup_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192; // the scan's records and the page's pieces
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) return err;
    static const httpd_uri_t page = {.uri = "/", .method = HTTP_GET, .handler = on_page};
    static const httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = on_save};
    static const httpd_uri_t forget = {.uri = "/forget", .method = HTTP_POST, .handler = on_forget};
    httpd_register_uri_handler(server, &page);
    httpd_register_uri_handler(server, &save);
    httpd_register_uri_handler(server, &forget);
    ESP_LOGI(TAG, "Setup page on http://192.168.4.1/");
    return ESP_OK;
}
