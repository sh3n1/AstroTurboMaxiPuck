// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Reading values from the app's small JSON messages (the app's docs/puck.md, "Control
// messages"). They are flat and written in a fixed way by JSON.stringify, so finding
// "key": is enough, and no JSON library is needed. Each reads the first match of the key in
// the text given: to read inside one item of a list, pass that item alone.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** The string after "key":, such as "name":"Crab Nebula", into out (UTF-8, cut to fit). */
bool json_string(const char *json, const char *key, char *out, size_t out_len);

/** A whole number from 0 to 2^32 - 1, such as "bytes":523000 or a CRC. */
bool json_uint(const char *json, const char *key, uint32_t *value);

/** A number, such as "raDeg":83.633. False for null or a missing key. */
bool json_float(const char *json, const char *key, float *value);

/** true or false, such as "night":true. */
bool json_bool(const char *json, const char *key, bool *value);
