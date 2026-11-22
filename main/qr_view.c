// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The pairing QR code: see qr_view.h.

#include "qr_view.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "src/libs/qrcode/qrcodegen.h" // LVGL's copy of Nayuki's QR encoder (MIT)

static const char *TAG = "qr";

// The light border a QR code needs around it, in squares (the standard asks for 4).
#define QUIET_MODULES 4
#define DARK 0x0000  // RGB565 black
#define LIGHT 0xFFFF // RGB565 white

static lv_obj_t *canvas;
static lv_draw_buf_t draw_buf;
static uint16_t *pixels;
static int size;
static uint8_t *qr, *temp; // the encoder's buffers, in PSRAM

lv_obj_t *qr_view_create(lv_obj_t *parent, int size_px)
{
    size = size_px;
    pixels = heap_caps_malloc(size * size * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    qr = heap_caps_malloc(qrcodegen_BUFFER_LEN_MAX, MALLOC_CAP_SPIRAM);
    temp = heap_caps_malloc(qrcodegen_BUFFER_LEN_MAX, MALLOC_CAP_SPIRAM);
    if (!pixels || !qr || !temp) {
        ESP_LOGE(TAG, "No memory for the QR code");
        return NULL;
    }
    lv_draw_buf_init(&draw_buf, size, size, LV_COLOR_FORMAT_RGB565, size * sizeof(uint16_t), pixels,
                     size * size * sizeof(uint16_t));
    canvas = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(canvas, &draw_buf);
    lv_obj_set_clickable(canvas, false);
    lv_obj_set_hidden(canvas, true);
    return canvas;
}

bool qr_view_show(const char *text)
{
    if (!canvas) return false;
    // The lowest error correction, at the smallest version the text fits.
    if (!qrcodegen_encodeText(text, temp, qr, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                              qrcodegen_Mask_AUTO, false)) {
        ESP_LOGE(TAG, "The text doesn't fit a QR code");
        return false;
    }
    int modules = qrcodegen_getSize(qr);
    // Whole pixels a square, as big as fits with the quiet zone; centred.
    int square = size / (modules + 2 * QUIET_MODULES);
    int offset = (size - square * modules) / 2;
    for (int i = 0; i < size * size; i++) pixels[i] = LIGHT;
    for (int my = 0; my < modules; my++) {
        for (int mx = 0; mx < modules; mx++) {
            if (!qrcodegen_getModule(qr, mx, my)) continue;
            for (int y = 0; y < square; y++) {
                uint16_t *row = pixels + (offset + my * square + y) * size + offset + mx * square;
                for (int x = 0; x < square; x++) row[x] = DARK;
            }
        }
    }
    ESP_LOGI(TAG, "%d by %d squares of %d px", modules, modules, square);
    lv_obj_invalidate(canvas);
    return true;
}
