// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// Writing straight into the sky canvas's pixels: see sky_pixels.h.

#include "sky_pixels.h"

#include <math.h>

#define SIZE_PX SKY_SIZE_PX

// The dark side of a phased disc, so the whole disc still shows faintly.
#define DARK_SIDE_ALPHA 0.15f

uint16_t *sky_pixels;

// Where row y crosses the round screen: [*first, *end). A pixel more each side, for the
// screen's soft edge.
static void row_span(int y, int *first, int *end)
{
    float dy = y + 0.5f - SKY_CENTRE_PX;
    float r = SKY_CENTRE_PX + 1;
    float half = r * r - dy * dy > 0 ? sqrtf(r * r - dy * dy) : 0;
    int a = (int)floorf(SKY_CENTRE_PX - half), b = (int)ceilf(SKY_CENTRE_PX + half);
    *first = a < 0 ? 0 : a & ~1; // even, for two pixels a write
    *end = b > SIZE_PX ? SIZE_PX : b;
}

void sky_fill(uint32_t rgb)
{
    // Two pixels a write: the buffer is in PSRAM, where each write costs.
    uint16_t one = sky_rgb565(rgb);
    uint32_t two = one * 0x00010001u;
    for (int y = 0; y < SIZE_PX; y++) {
        int first, end;
        row_span(y, &first, &end);
        uint16_t *row = sky_pixels + y * SIZE_PX;
        // Rows start on even pixels (SIZE_PX is even), so first stays 4-byte aligned.
        uint32_t *p = (uint32_t *)(row + first);
        int pairs = (end - first) / 2;
        for (int i = 0; i < pairs; i++) p[i] = two;
        if ((end - first) & 1) row[end - 1] = one;
    }
}

void sky_disc(float x, float y, float radius, uint32_t rgb, float alpha)
{
    int x0 = (int)floorf(x - radius - 1), x1 = (int)ceilf(x + radius + 1);
    int y0 = (int)floorf(y - radius - 1), y1 = (int)ceilf(y + radius + 1);
    if (x1 < 0 || y1 < 0 || x0 >= SIZE_PX || y0 >= SIZE_PX) return;
    uint16_t c = sky_rgb565(rgb);
    for (int py = y0 < 0 ? 0 : y0; py <= y1 && py < SIZE_PX; py++) {
        for (int px = x0 < 0 ? 0 : x0; px <= x1 && px < SIZE_PX; px++) {
            float dx = px + 0.5f - x, dy = py + 0.5f - y;
            float cover = radius + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cover > 0) sky_blend565(&sky_pixels[py * SIZE_PX + px], c, (uint32_t)(alpha * fminf(cover, 1) * 32 + 0.5f));
        }
    }
}

void sky_phase(float x, float y, float radius, float sun_x, float sun_y, float lit, uint32_t rgb, float alpha)
{
    // The terminator is half an ellipse whose width shrinks to nothing at half phase.
    float k = fmaxf(0, fminf(1, lit));
    int x0 = (int)floorf(x - radius - 1), x1 = (int)ceilf(x + radius + 1);
    int y0 = (int)floorf(y - radius - 1), y1 = (int)ceilf(y + radius + 1);
    for (int py = y0 < 0 ? 0 : y0; py <= y1 && py < SIZE_PX; py++) {
        for (int px = x0 < 0 ? 0 : x0; px <= x1 && px < SIZE_PX; px++) {
            float dx = px + 0.5f - x, dy = py + 0.5f - y;
            float cover = radius + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cover <= 0) continue;
            // Along the Sun's direction (u) and across it (v), in the disc's radii.
            float u = (dx * sun_x + dy * sun_y) / radius, v = (dy * sun_x - dx * sun_y) / radius;
            float half_chord = sqrtf(fmaxf(0, 1 - v * v));
            bool lit_here = u > -(2 * k - 1) * half_chord;
            sky_blend(&sky_pixels[py * SIZE_PX + px], rgb, alpha * fminf(cover, 1) * (lit_here ? 1 : DARK_SIDE_ALPHA));
        }
    }
}

// Narrows [*lo, *hi] to the x where lo_v <= a + b x <= hi_v (a line's along or across
// distance, which changes linearly along a row).
static void clip_linear(float a, float b, float lo_v, float hi_v, float *lo, float *hi)
{
    if (fabsf(b) < 1e-6f) {
        if (a < lo_v || a > hi_v) *hi = *lo - 1; // the whole row is outside
        return;
    }
    float x0 = (lo_v - a) / b, x1 = (hi_v - a) / b;
    if (x0 > x1) {
        float t = x0;
        x0 = x1;
        x1 = t;
    }
    if (x0 > *lo) *lo = x0;
    if (x1 < *hi) *hi = x1;
}

void sky_line(float x0, float y0, float x1, float y1, float width, uint32_t rgb, float alpha)
{
    float half = width / 2, reach = half + 1; // a pixel more, for the soft edge
    if (fmaxf(x0, x1) < -reach || fminf(x0, x1) > SIZE_PX + reach || fmaxf(y0, y1) < -reach ||
        fminf(y0, y1) > SIZE_PX + reach) {
        return;
    }
    float dx = x1 - x0, dy = y1 - y0, length = sqrtf(dx * dx + dy * dy);
    float ux = length > 1e-6f ? dx / length : 1, uy = length > 1e-6f ? dy / length : 0;
    uint16_t c = sky_rgb565(rgb);
    int top = (int)floorf(fminf(y0, y1) - reach), bottom = (int)ceilf(fmaxf(y0, y1) + reach);
    if (top < 0) top = 0;
    if (bottom > SIZE_PX - 1) bottom = SIZE_PX - 1;
    for (int py = top; py <= bottom; py++) {
        float cy = py + 0.5f;
        // The row's part within reach of the line: the band along it, between its ends, joined
        // with the round ends' circles (the shape is convex, so that's one span).
        float lo = -1e9f, hi = 1e9f;
        clip_linear((cy - y0) * uy - x0 * ux, ux, -reach, length + reach, &lo, &hi);  // along
        clip_linear((cy - y0) * ux + x0 * uy, -uy, -reach, reach, &lo, &hi);          // across
        if (hi < lo) continue;
        int first = (int)floorf(lo), last = (int)ceilf(hi);
        if (first < 0) first = 0;
        if (last > SIZE_PX - 1) last = SIZE_PX - 1;
        uint16_t *row = sky_pixels + py * SIZE_PX;
        for (int px = first; px <= last; px++) {
            float qx = px + 0.5f - x0, qy = cy - y0;
            float t = fmaxf(0, fminf(length, qx * ux + qy * uy));
            float ex = qx - t * ux, ey = qy - t * uy;
            float cover = half + 0.5f - sqrtf(ex * ex + ey * ey);
            if (cover > 0) sky_blend565(&row[px], c, (uint32_t)(alpha * fminf(cover, 1) * 32 + 0.5f));
        }
    }
}

void sky_ring(float x, float y, float radius, float width, uint32_t rgb, float alpha, float dash_px)
{
    float half = width / 2;
    float outer = radius + half + 1, inner = radius - half - 1;
    if (x + outer < 0 || x - outer > SIZE_PX || y + outer < 0 || y - outer > SIZE_PX) return;
    // Dashes: a whole number of them round the circle, each followed by a gap as long.
    int dashes = dash_px > 0 ? (int)(2 * (float)M_PI * radius / (2 * dash_px)) : 0;
    if (dashes < 4) dashes = 0;
    float per_dash = dashes ? dashes / (float)M_PI : 0; // dash-and-gap pairs per radian, times 2
    uint16_t c = sky_rgb565(rgb);
    int top = (int)floorf(y - outer), bottom = (int)ceilf(y + outer);
    if (top < 0) top = 0;
    if (bottom > SIZE_PX - 1) bottom = SIZE_PX - 1;
    for (int py = top; py <= bottom; py++) {
        float dy = py + 0.5f - y;
        if (fabsf(dy) > outer) continue;
        // Only the ring's two spans of the row, outside the inner circle.
        float xo = sqrtf(outer * outer - dy * dy);
        float xi = inner > 0 && fabsf(dy) < inner ? sqrtf(inner * inner - dy * dy) : 0;
        float spans[2][2] = {{x - xo, x - xi}, {x + xi, x + xo}};
        uint16_t *row = sky_pixels + py * SIZE_PX;
        int done = -1; // the last pixel blended, so none is blended twice where the spans meet
        for (int s = 0; s < 2; s++) {
            int first = (int)floorf(spans[s][0]), last = (int)ceilf(spans[s][1]);
            if (first <= done) first = done + 1;
            done = last;
            if (first < 0) first = 0;
            if (last > SIZE_PX - 1) last = SIZE_PX - 1;
            for (int px = first; px <= last; px++) {
                float dx = px + 0.5f - x;
                float cover = half + 0.5f - fabsf(sqrtf(dx * dx + dy * dy) - radius);
                if (cover <= 0) continue;
                if (dashes) {
                    float turn = (atan2f(dy, dx) + (float)M_PI) * per_dash; // in half dash pairs
                    if ((int)turn & 1) continue; // a gap
                }
                sky_blend565(&row[px], c, (uint32_t)(alpha * fminf(cover, 1) * 32 + 0.5f));
            }
        }
    }
}

void sky_mask_outside(float radius, uint32_t edge_rgb)
{
    for (int py = 0; py < SIZE_PX; py++) {
        int first, end;
        row_span(py, &first, &end);
        float dy = py + 0.5f - SKY_CENTRE_PX;
        // Well inside the circle nothing changes: skip that part of the row.
        float inner = radius - 2;
        float half = inner > 0 && inner * inner > dy * dy ? sqrtf(inner * inner - dy * dy) : -1;
        int skip_first = (int)ceilf(SKY_CENTRE_PX - half), skip_end = (int)floorf(SKY_CENTRE_PX + half);
        for (int px = first; px < end; px++) {
            if (half > 0 && px >= skip_first && px < skip_end) px = skip_end;
            if (px >= end) break;
            float dx = px + 0.5f - SKY_CENTRE_PX;
            float d = sqrtf(dx * dx + dy * dy) - radius;
            if (d > 1) {
                sky_pixels[py * SIZE_PX + px] = 0;
            } else if (d > -1) {
                sky_blend(&sky_pixels[py * SIZE_PX + px], edge_rgb, 1 - fabsf(d)); // the edge
            }
        }
    }
}
