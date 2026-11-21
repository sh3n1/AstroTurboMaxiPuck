// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The WebRTC link to the app, paired with one QR code that the puck shows and the phone
// scans. The puck can't read a code, so libpeer's lite pairing is used: the QR holds the
// puck's address, ICE credentials, certificate fingerprint and a fresh secret, the app builds
// the puck's answer from it, and the app's first message must carry the secret.
// The QR's format is in the app's docs/puck.md.

#pragma once

#include "esp_err.h"

/** What the link tells the screen. Called from the link task: keep short, lock LVGL. */
typedef struct {
    /** Show this QR code text for pairing (a new one after every lost or refused link). */
    void (*show_pairing)(const char *qr_text);
    /** A short state to show, such as "Linked". */
    void (*show_state)(const char *text);
    /** Where the telescope points, from the app's pose messages. */
    void (*show_pose)(float alt_deg, float az_deg);
} link_callbacks_t;

/** Starts the link task, which shows a QR code and waits for the app. Call once the Wi-Fi
 *  is connected. */
esp_err_t link_start(const link_callbacks_t *callbacks);
