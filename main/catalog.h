// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The sky catalogue the app sends once (the app's docs/puck.md, "The catalogue"): stars,
// constellation lines and names, and the fixed objects. It is received into PSRAM, checked,
// and written to the "catalog" flash partition by a task of its own, as erasing the flash
// takes seconds and the link must keep running meanwhile. Once kept, it is read straight
// from flash (memory-mapped) for drawing.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define CATALOG_ID_CHARS 16

/** Finds the catalogue kept in flash, if any. Call once at start. */
esp_err_t catalog_init(void);

/** The id of the catalogue kept in flash, or NULL when there is none. */
const char *catalog_kept_id(void);

/** A new catalogue is coming (`catalogStart`). False when it can't be taken (too big, no
 *  memory, or one is still being written). */
bool catalog_begin(const char *id, uint32_t bytes, uint32_t crc32);

/** The next part from the catalogue channel. */
void catalog_add_part(const uint8_t *data, size_t length);

/** Where a transfer is, for the link task to answer the app and the screen to show. */
typedef enum {
    CATALOG_IDLE,       // nothing coming
    CATALOG_RECEIVING,  // parts arriving
    CATALOG_WRITING,    // all here and checked, being written to flash
    CATALOG_DONE,       // written: answer catalogDone, once
    CATALOG_FAILED,     // answer catalogFailed, once
} catalog_state_t;

/** The transfer's state, and how many bytes of how many have come. */
catalog_state_t catalog_state(uint32_t *received, uint32_t *total);

/** Why it failed, for catalogFailed. */
const char *catalog_failure(void);

/** After answering DONE or FAILED: back to IDLE. */
void catalog_acknowledge(void);

/** Drops a transfer that hasn't finished, as when the link is lost. */
void catalog_cancel(void);

/** Gives up a transfer whose parts stopped coming: FAILED, with this reason for the app. */
void catalog_fail_receiving(const char *reason);

/** The kept catalogue, memory-mapped, and its size; NULL when there is none. */
const uint8_t *catalog_data(size_t *bytes);
