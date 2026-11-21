// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Reading the app's JSON messages: see json_read.h.

#include "json_read.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Where the value after "key": starts, or NULL.
static const char *value_of(const char *json, const char *key)
{
    char pattern[40];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    return p ? p + strlen(pattern) : NULL;
}

// Writes a code point as UTF-8 into out at *o, if it fits.
static void put_utf8(unsigned code, char *out, size_t out_len, size_t *o)
{
    char bytes[3];
    size_t n = 0;
    if (code < 0x80) {
        bytes[n++] = (char)code;
    } else if (code < 0x800) {
        bytes[n++] = (char)(0xC0 | code >> 6);
        bytes[n++] = (char)(0x80 | (code & 0x3F));
    } else {
        bytes[n++] = (char)(0xE0 | code >> 12);
        bytes[n++] = (char)(0x80 | (code >> 6 & 0x3F));
        bytes[n++] = (char)(0x80 | (code & 0x3F));
    }
    if (*o + n >= out_len) return; // keep whole characters only
    memcpy(out + *o, bytes, n);
    *o += n;
}

// Handles the escapes JSON.stringify writes; other characters, Greek letters among them,
// come as they are, in UTF-8.
bool json_string(const char *json, const char *key, char *out, size_t out_len)
{
    const char *p = value_of(json, key);
    if (!p || *p != '"' || out_len == 0) return false;
    p++;
    size_t o = 0;
    while (*p && *p != '"') {
        unsigned code = (unsigned char)*p++;
        if (code == '\\') {
            char e = *p++;
            switch (e) {
            case 'n': code = '\n'; break;
            case 't': code = '\t'; break;
            case 'r': code = '\r'; break;
            case 'b': code = '\b'; break;
            case 'f': code = '\f'; break;
            case 'u':
                if (sscanf(p, "%4x", &code) != 1) return false;
                p += 4;
                break;
            case '\0': return false;
            default: code = (unsigned char)e; break; // \" \\ \/
            }
            put_utf8(code, out, out_len, &o);
        } else if (o + 1 < out_len) {
            out[o++] = (char)code; // a byte as it is, part of UTF-8 or not
        }
    }
    out[o] = '\0';
    return *p == '"';
}

bool json_uint(const char *json, const char *key, uint32_t *value)
{
    const char *p = value_of(json, key);
    if (!p) return false;
    char *end;
    unsigned long long n = strtoull(p, &end, 10);
    if (end == p || n > UINT32_MAX) return false;
    *value = (uint32_t)n;
    return true;
}

bool json_float(const char *json, const char *key, float *value)
{
    const char *p = value_of(json, key);
    if (!p) return false;
    char *end;
    float f = strtof(p, &end);
    if (end == p) return false; // null, or not a number
    *value = f;
    return true;
}

bool json_bool(const char *json, const char *key, bool *value)
{
    const char *p = value_of(json, key);
    if (!p) return false;
    if (strncmp(p, "true", 4) == 0) {
        *value = true;
    } else if (strncmp(p, "false", 5) == 0) {
        *value = false;
    } else {
        return false;
    }
    return true;
}
