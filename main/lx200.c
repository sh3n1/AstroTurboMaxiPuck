// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The Meade LX200 repeater: see lx200.h. The commands are those of Meade's "LX200 Command
// Set" that planetarium apps use: where the telescope points, a target and a GoTo, and the
// questions they ask on connecting, answered with harmless values.

#include "lx200.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "link.h"
#include "lwip/sockets.h"
#include "nvs.h"

static const char *TAG = "lx200";

#define NVS_SPACE "puck"
#define NVS_KEY "lx200"
#define TASK_STACK (4 * 1024)
#define TASK_PRIORITY 2
#define WAIT_MS 500        // how often the task looks whether it was switched off
#define COMMAND_MAX 32
#define ACK 0x06           // asks the mount's alignment: answered "A", alt-azimuth

static volatile bool enabled;
static TaskHandle_t task;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static float pointing_ra = 0, pointing_dec = 0; // degrees, of the date
// The client's: its precision (high by default, :U# toggles) and the target it set.
static bool high_precision = true;
static float target_ra = NAN, target_dec = NAN;

static void reply(int client, const char *text)
{
    send(client, text, strlen(text), 0);
}

// Hours or degrees as whole units, minutes and seconds, rounded to the second.
static void split(float value, int *whole, int *minutes, int *seconds)
{
    long total = lroundf(fabsf(value) * 3600);
    *whole = (int)(total / 3600);
    *minutes = (int)(total / 60 % 60);
    *seconds = (int)(total % 60);
}

// "HH:MM:SS", "HH:MM.T", or degrees "sDD*MM:SS", "sDD*MM'SS", "sDD*MM": any separators.
static bool parse_sexagesimal(const char *text, float *value)
{
    while (*text == ' ') text++;
    float sign = 1;
    if (*text == '-' || *text == '+') {
        if (*text == '-') sign = -1;
        text++;
    }
    char *end;
    float parts[3] = {0, 0, 0};
    for (int i = 0; i < 3; i++) {
        parts[i] = strtof(text, &end);
        if (end == text) {
            if (i == 0) return false;
            break;
        }
        text = end;
        // Past the separator: ':', '*', '\'', or the degree sign's bytes.
        while (*text && *text != '.' && (*text < '0' || *text > '9')) text++;
        if (*text == '.') break; // "MM.T": its tenths are in the float already
    }
    *value = sign * (parts[0] + parts[1] / 60 + parts[2] / 3600);
    return true;
}

static void answer_position(int client, bool ra)
{
    float ra_deg, dec_deg;
    taskENTER_CRITICAL(&lock);
    ra_deg = pointing_ra;
    dec_deg = pointing_dec;
    taskEXIT_CRITICAL(&lock);
    char text[24];
    int a, b, c;
    if (ra) {
        split(ra_deg / 15, &a, &b, &c);
        if (high_precision) snprintf(text, sizeof(text), "%02d:%02d:%02d#", a % 24, b, c);
        else snprintf(text, sizeof(text), "%02d:%02d.%d#", a % 24, b, c / 6);
    } else {
        split(dec_deg, &a, &b, &c);
        char sign = dec_deg < 0 ? '-' : '+';
        if (high_precision) snprintf(text, sizeof(text), "%c%02d*%02d'%02d#", sign, a, b, c);
        else snprintf(text, sizeof(text), "%c%02d*%02d#", sign, a, b);
    }
    reply(client, text);
}

// A GoTo: the phone makes the nearest object the target. "0" is Meade's "slewing"; "1" and a
// reason, its "below the horizon", is the only refusal planetariums show.
static void go_to(int client)
{
    if (isnan(target_ra) || isnan(target_dec)) {
        reply(client, "1No target set#");
        return;
    }
    char command[96];
    snprintf(command, sizeof(command), "{\"t\":\"goto\",\"raDeg\":%.4f,\"decDeg\":%.4f}", target_ra, target_dec);
    if (link_send_command(command)) {
        ESP_LOGI(TAG, "GoTo RA %.3f, Dec %.3f", target_ra, target_dec);
        reply(client, "0");
    } else {
        reply(client, "1No phone linked#");
    }
}

// One command, between ':' and '#'.
static void handle(int client, const char *c)
{
    float value;
    if (strcmp(c, "GR") == 0) {
        answer_position(client, true);
    } else if (strcmp(c, "GD") == 0) {
        answer_position(client, false);
    } else if (strcmp(c, "U") == 0) {
        high_precision = !high_precision;
    } else if (strncmp(c, "Sr", 2) == 0) {
        bool ok = parse_sexagesimal(c + 2, &value);
        if (ok) target_ra = fmodf(value * 15 + 360, 360);
        reply(client, ok ? "1" : "0");
    } else if (strncmp(c, "Sd", 2) == 0) {
        bool ok = parse_sexagesimal(c + 2, &value) && fabsf(value) <= 90;
        if (ok) target_dec = value;
        reply(client, ok ? "1" : "0");
    } else if (strcmp(c, "MS") == 0) {
        go_to(client);
    } else if (strcmp(c, "CM") == 0) {
        reply(client, "Coordinates matched.        #"); // a sync: nothing to change here
    } else if (strcmp(c, "D") == 0) {
        reply(client, "#"); // not slewing: the phone guides by hand
    } else if (strcmp(c, "GVP") == 0) {
        reply(client, "AstroTurboMaxiPuck#");
    } else if (strcmp(c, "GVN") == 0 || strcmp(c, "GVD") == 0 || strcmp(c, "GVT") == 0) {
        reply(client, "1.0#");
    } else if (strcmp(c, "Gt") == 0) {
        reply(client, "+00*00#"); // the site and time stay on the phone
    } else if (strcmp(c, "Gg") == 0) {
        reply(client, "+000*00#");
    } else if (strcmp(c, "GG") == 0) {
        reply(client, "+00#");
    } else if (strcmp(c, "GL") == 0) {
        reply(client, "00:00:00#");
    } else if (strcmp(c, "GC") == 0) {
        reply(client, "01/01/26#");
    } else if (c[0] == 'S' && (c[1] == 'g' || c[1] == 't' || c[1] == 'L' || c[1] == 'C' || c[1] == 'G')) {
        reply(client, "1"); // the site and time sent: taken, and left to the phone
    }
    // Anything else, such as the hand controller's moves (:Mn#, :Q#, :RS#): no answer.
}

// Reads what the client sent, command by command.
static bool serve(int client)
{
    static char command[COMMAND_MAX];
    static size_t length;
    static bool in_command;
    char buffer[64];
    int n = recv(client, buffer, sizeof(buffer), 0);
    if (n <= 0) return false;
    for (int i = 0; i < n; i++) {
        char ch = buffer[i];
        if (ch == ACK && !in_command) {
            reply(client, "A");
        } else if (ch == ':' && !in_command) {
            // A command starts; within one, ':' is part of it, as in ":Sr18:36:56#".
            in_command = true;
            length = 0;
        } else if (ch == '#' && in_command) {
            command[length] = '\0';
            in_command = false;
            handle(client, command);
        } else if (in_command && length + 1 < COMMAND_MAX) {
            command[length++] = ch;
        }
    }
    return true;
}

static void server_task(void *arg)
{
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(LX200_PORT), .sin_addr.s_addr = INADDR_ANY};
    if (listener < 0 || bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listener, 1) != 0) {
        ESP_LOGE(TAG, "Couldn't listen on port %d", LX200_PORT);
        if (listener >= 0) close(listener);
        task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Listening on port %d", LX200_PORT);
    int client = -1;
    while (enabled) {
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(listener, &ready);
        if (client >= 0) FD_SET(client, &ready);
        struct timeval wait = {.tv_sec = 0, .tv_usec = WAIT_MS * 1000};
        int top = client > listener ? client : listener;
        if (select(top + 1, &ready, NULL, NULL, &wait) <= 0) continue;
        if (FD_ISSET(listener, &ready)) {
            // One planetarium at a time: a new one takes over.
            int next = accept(listener, NULL, NULL);
            if (next >= 0) {
                if (client >= 0) close(client);
                client = next;
                high_precision = true;
                ESP_LOGI(TAG, "A planetarium connected");
            }
        }
        if (client >= 0 && FD_ISSET(client, &ready) && !serve(client)) {
            ESP_LOGI(TAG, "The planetarium left");
            close(client);
            client = -1;
        }
    }
    if (client >= 0) close(client);
    close(listener);
    ESP_LOGI(TAG, "Stopped");
    task = NULL;
    vTaskDelete(NULL);
}

static void start_task(void)
{
    if (task) return;
    // On core 0, with the Wi-Fi and the link; its stack in PSRAM, which internal RAM can't spare.
    xTaskCreatePinnedToCoreWithCaps(server_task, "lx200", TASK_STACK, NULL, TASK_PRIORITY, &task, 0,
                                    MALLOC_CAP_SPIRAM);
}

void lx200_start(void)
{
    nvs_handle_t nvs;
    uint8_t on = 0;
    if (nvs_open(NVS_SPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, NVS_KEY, &on);
        nvs_close(nvs);
    }
    enabled = on;
    if (enabled) start_task();
}

bool lx200_enabled(void)
{
    return enabled;
}

void lx200_set_enabled(bool on)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_SPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY, on);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    enabled = on;
    if (on) start_task(); // the task stops by itself when switched off, within WAIT_MS
    link_send_command(on ? "{\"t\":\"lx200\",\"on\":true}" : "{\"t\":\"lx200\",\"on\":false}");
}

void lx200_set_pointing(float ra_deg, float dec_deg)
{
    taskENTER_CRITICAL(&lock);
    pointing_ra = ra_deg;
    pointing_dec = dec_deg;
    taskEXIT_CRITICAL(&lock);
}
