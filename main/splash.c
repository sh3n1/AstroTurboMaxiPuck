// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The start-up screen: see splash.h. The stars' places, colours, sizes and timings are the
// landing page's (src/landing/styles.css, ".sky"), in the puck's pixels.

#include "splash.h"

#include <stdint.h>

#define SCREEN_PX 466
// The landing page's colours: its sky, text, muted text, accent and warning.
#define COLOR_SKY 0x05070F
#define COLOR_TEXT 0xE8EBF5
#define COLOR_MUTED 0xA8B0C6
#define COLOR_ACCENT 0x8FB7FF
#define COLOR_WARNING 0xFFB454
// The stars twinkle between these opacities (its @keyframes twinkle: 0.25 to 0.8).
#define TWINKLE_LOW (LV_OPA_COVER / 4)
#define TWINKLE_HIGH (LV_OPA_COVER * 8 / 10)
// A shooting star: 140 by 2 pixels, tilted 18°, across the sky in the first 8 % of its 13 s,
// the first after 3 s (its .shooting-star and @keyframes shoot).
#define SHOOT_LENGTH_PX 140
#define SHOOT_WIDTH_PX 2
#define SHOOT_TILT_DECIDEG 180
#define SHOOT_PERIOD_MS 13000
#define SHOOT_DELAY_MS 3000
// The logo floats up and back down this far, every 6 s (its .logo's float).
#define LOGO_FLOAT_PX 10
#define LOGO_FLOAT_MS 3000

extern const lv_image_dsc_t logo_image; // images/logo.c, made by tools/make_logo.py

// One layer of stars: a tile of a few, repeated across the screen (its background-size).
typedef struct {
    int16_t x, y;
    uint8_t size;
    uint32_t color;
} star_t;

static const star_t FAR_STARS[] = {
    {20, 30, 2, COLOR_TEXT}, {140, 80, 2, COLOR_MUTED}, {90, 160, 2, COLOR_TEXT},
    {210, 210, 2, COLOR_MUTED}, {250, 40, 2, COLOR_TEXT},
};
static const star_t NEAR_STARS[] = {
    {60, 120, 3, COLOR_TEXT}, {330, 60, 3, COLOR_ACCENT}, {200, 300, 4, COLOR_TEXT}, {400, 250, 3, COLOR_WARNING},
};
#define FAR_TILE_W 280
#define FAR_TILE_H 260
#define NEAR_TILE_W 460
#define NEAR_TILE_H 380
#define STARS_MAX 48

typedef struct {
    lv_obj_t *dots[STARS_MAX];
    int count;
} layer_t;

static lv_obj_t *sky;
static layer_t far_layer, near_layer;
static lv_obj_t *shooting_star;

// The layer's stars, at this opacity: each its own, so no layer needs drawing apart.
static void set_layer_opa(void *layer, int32_t opa)
{
    layer_t *l = layer;
    for (int i = 0; i < l->count; i++) lv_obj_set_style_bg_opa(l->dots[i], (lv_opa_t)opa, 0);
}

static void make_layer(layer_t *layer, const star_t *stars, int n, int tile_w, int tile_h, uint32_t period_ms,
                       bool reverse)
{
    layer->count = 0;
    for (int ty = 0; ty < SCREEN_PX; ty += tile_h) {
        for (int tx = 0; tx < SCREEN_PX; tx += tile_w) {
            for (int i = 0; i < n && layer->count < STARS_MAX; i++) {
                int x = tx + stars[i].x, y = ty + stars[i].y;
                if (x >= SCREEN_PX || y >= SCREEN_PX) continue;
                lv_obj_t *dot = lv_obj_create(sky);
                lv_obj_remove_style_all(dot);
                lv_obj_set_size(dot, stars[i].size, stars[i].size);
                lv_obj_set_pos(dot, x, y);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(dot, lv_color_hex(stars[i].color), 0);
                layer->dots[layer->count++] = dot;
            }
        }
    }
    // Slowly brighter and dimmer, back and forth, as its ease-in-out twinkle.
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, layer);
    lv_anim_set_exec_cb(&a, set_layer_opa);
    lv_anim_set_values(&a, reverse ? TWINKLE_HIGH : TWINKLE_LOW, reverse ? TWINKLE_LOW : TWINKLE_HIGH);
    lv_anim_set_duration(&a, period_ms);
    lv_anim_set_reverse_duration(&a, period_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

// The shooting star, at a thousandth of its 13 s: in the first 8 % it crosses from left of
// the screen, 12 % down, by 80 % of the width across and 26 % down, fading in by 2 % and
// out by 8 %; then it waits.
static void move_shooting_star(void *obj, int32_t thousandths)
{
    float t = thousandths / 1000.0f;
    if (t >= 0.08f) {
        lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
        return;
    }
    float along = t / 0.08f;
    lv_obj_set_pos(obj, (int32_t)(-0.2f * SCREEN_PX + along * 0.8f * SCREEN_PX),
                   (int32_t)(0.12f * SCREEN_PX + along * 0.26f * SCREEN_PX));
    float opa = t < 0.02f ? t / 0.02f : (0.08f - t) / 0.06f;
    lv_obj_set_style_opa(obj, (lv_opa_t)(opa * LV_OPA_COVER), 0);
}

static void make_shooting_star(void)
{
    shooting_star = lv_obj_create(sky);
    lv_obj_remove_style_all(shooting_star);
    lv_obj_set_size(shooting_star, SHOOT_LENGTH_PX, SHOOT_WIDTH_PX);
    lv_obj_set_style_radius(shooting_star, SHOOT_WIDTH_PX, 0);
    // From nothing at its tail to bright at its head (its linear-gradient).
    lv_obj_set_style_bg_opa(shooting_star, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(shooting_star, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_bg_main_opa(shooting_star, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_color(shooting_star, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_bg_grad_opa(shooting_star, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(shooting_star, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_transform_rotation(shooting_star, SHOOT_TILT_DECIDEG, 0);
    lv_obj_set_style_opa(shooting_star, LV_OPA_TRANSP, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, shooting_star);
    lv_anim_set_exec_cb(&a, move_shooting_star);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, SHOOT_PERIOD_MS);
    lv_anim_set_delay(&a, SHOOT_DELAY_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void float_logo(void *obj, int32_t y)
{
    lv_obj_set_style_translate_y(obj, y, 0);
}

void splash_create(lv_obj_t *screen)
{
    sky = lv_obj_create(screen);
    lv_obj_remove_style_all(sky);
    lv_obj_set_size(sky, SCREEN_PX, SCREEN_PX);
    lv_obj_set_style_bg_color(sky, lv_color_hex(COLOR_SKY), 0);
    lv_obj_set_style_bg_opa(sky, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(sky, false);
    lv_obj_set_clickable(sky, false);
    make_layer(&far_layer, FAR_STARS, sizeof(FAR_STARS) / sizeof(FAR_STARS[0]), FAR_TILE_W, FAR_TILE_H, 7000, false);
    make_layer(&near_layer, NEAR_STARS, sizeof(NEAR_STARS) / sizeof(NEAR_STARS[0]), NEAR_TILE_W, NEAR_TILE_H, 4500,
               true);
    make_shooting_star();

    lv_obj_t *logo = lv_image_create(sky);
    lv_image_set_src(logo, &logo_image);
    lv_obj_center(logo);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, logo);
    lv_anim_set_exec_cb(&a, float_logo);
    lv_anim_set_values(&a, 0, -LOGO_FLOAT_PX);
    lv_anim_set_duration(&a, LOGO_FLOAT_MS);
    lv_anim_set_reverse_duration(&a, LOGO_FLOAT_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void splash_hide(void)
{
    if (!sky) return;
    // The twinkles animate the layers, not objects: stopped here. Deleting the sky stops
    // the logo's and the shooting star's with them.
    lv_anim_delete(&far_layer, NULL);
    lv_anim_delete(&near_layer, NULL);
    lv_obj_delete(sky);
    sky = NULL;
}
