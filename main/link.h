// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The WebRTC link to the app, paired with one QR code that the puck shows and the phone
// scans. The puck can't read a code, so libpeer's lite pairing is used: the QR holds the
// puck's address, ICE credentials, certificate fingerprint and a fresh secret, the app builds
// the puck's answer from it, and the app's first message must carry the secret.
// The QR's format is in the app's docs/puck.md.

#pragma once

#include <stdbool.h>

#include "esp_err.h"

/** What the link tells the screen. Called from the link task: keep short, lock LVGL. */
typedef struct {
    /** Show this QR code text for pairing (a new one after every lost or refused link).
     *  after_loss: a link to the app was there and is lost. */
    void (*show_pairing)(const char *qr_text, bool after_loss);
    /** A short state to show, such as "Linked". */
    void (*show_state)(const char *text);
} link_callbacks_t;

// What the app sends once linked (the view, the target's name, its state) goes to
// view_state.h, for the screen to read on its own core.

/** Sends a small JSON command to the app, such as {"t":"step","by":1}, from any task. It is
 *  queued and sent by the link task; dropped (false) when not linked or the queue is full. */
bool link_send_command(const char *json);

/** Starts the link task, which shows a QR code and waits for the app. Call once the Wi-Fi
 *  is connected. */
esp_err_t link_start(const link_callbacks_t *callbacks);
