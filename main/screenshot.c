// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Screenshots over the USB cable: see screenshot.h.

#include "screenshot.h"

#include <stdio.h>

#include "board.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "log_history.h"
#include "menu.h"
#include "player.h"
#include "screen.h"

static const char *TAG = "screenshot";

// The serial driver's buffers, in internal RAM, which is scarce: a row's line goes out in a
// few pieces. A log line from another task can fall inside one, and that row is lost
// (tools/screenshot.py fills it in).
#define TX_BUFFER_BYTES 1024
#define RX_BUFFER_BYTES 256
// LVGL draws the snapshot in this task: the menu's pages need a big stack (in PSRAM).
#define TASK_STACK (16 * 1024)
#define TASK_PRIORITY 2

// The driver takes no more than its buffer holds in one write, so longer text goes in pieces.
#define SEND_PIECE_BYTES 512

static void send(const char *text, size_t length)
{
    while (length > 0) {
        size_t n = length < SEND_PIECE_BYTES ? length : SEND_PIECE_BYTES;
        usb_serial_jtag_write_bytes(text, n, portMAX_DELAY);
        text += n;
        length -= n;
    }
}

static void take_and_send(void)
{
    const int size = BOARD_SCREEN_PX;
    uint32_t stride = lv_draw_buf_width_to_stride(size, LV_COLOR_FORMAT_RGB565);
    uint8_t *pixels = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, stride * size, MALLOC_CAP_SPIRAM);
    char *line = heap_caps_malloc(16 + size * 4 + 2, MALLOC_CAP_SPIRAM);
    if (!pixels || !line) {
        ESP_LOGE(TAG, "No memory for a screenshot");
        heap_caps_free(pixels);
        heap_caps_free(line);
        return;
    }
    lv_draw_buf_t buf;
    lv_draw_buf_init(&buf, size, size, LV_COLOR_FORMAT_RGB565, stride, pixels, stride * size);
    lvgl_port_lock(0);
    lv_result_t taken = lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565, &buf);
    lvgl_port_unlock();
    if (taken != LV_RESULT_OK) {
        ESP_LOGE(TAG, "The snapshot failed");
    } else {
        static const char hex[] = "0123456789abcdef";
        int n = snprintf(line, 32, "SHOT BEGIN %d %d\n", size, size);
        send(line, n);
        for (int y = 0; y < size; y++) {
            n = snprintf(line, 16, "R%d ", y);
            const uint8_t *row = pixels + y * stride;
            for (int i = 0; i < size * 2; i++) {
                line[n++] = hex[row[i] >> 4];
                line[n++] = hex[row[i] & 15];
            }
            line[n++] = '\n';
            send(line, n);
        }
        send("SHOT END\n", 9);
    }
    heap_caps_free(pixels);
    heap_caps_free(line);
}

// The cable's commands that change the screen, in the LVGL task: "M" opens the menu, "B" its
// Brightness page, "T" acts as a tap on the map, so the player shows.
static void act(void *command)
{
    switch ((char)(intptr_t)command) {
    case 'M':
        menu_open();
        break;
    case 'B':
        menu_open_brightness();
        break;
    case 'T':
        player_open();
        break;
    }
}

static void listen(void *arg)
{
    for (;;) {
        char c;
        if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1) continue;
        if (c == 'S') {
            take_and_send();
        } else if (c == 'L') {
            log_history_dump(send);
        } else if (c == 'M' || c == 'B' || c == 'T') {
            // Done in the LVGL task, as a tap would be: this task's small stack can't hold
            // the menu's layout work.
            lvgl_port_lock(0);
            lv_async_call(act, (void *)(intptr_t)c);
            lvgl_port_unlock();
        }
    }
}

void screenshot_start(void)
{
    usb_serial_jtag_driver_config_t config = {.tx_buffer_size = TX_BUFFER_BYTES, .rx_buffer_size = RX_BUFFER_BYTES};
    if (usb_serial_jtag_driver_install(&config) != ESP_OK) {
        ESP_LOGE(TAG, "No serial driver: no screenshots");
        return;
    }
    // The log goes through the driver too from now on, so both share the port in turn.
    usb_serial_jtag_vfs_use_driver();
    // Its stack in PSRAM: internal RAM is kept for the Wi-Fi and the link.
    xTaskCreatePinnedToCoreWithCaps(listen, "screenshot", TASK_STACK, NULL, TASK_PRIORITY, NULL, 0, MALLOC_CAP_SPIRAM);
}
