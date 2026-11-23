// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The menu's look: see menu_chip.h.

#include "menu_chip.h"

#include <math.h>
#include <string.h>

#include "board.h"
#include "fonts.h"
#include "sky_icons.h"

// The day's colours, from the app's theme (theme.ts): its dark sky, a dark blue tint for the
// chips, its light blue as the accent.
#define DAY_PAGE 0x05070F
#define DAY_CHIP 0x1C2438
#define DAY_CHIP_PRESSED 0x2E3B5C
#define DAY_ACCENT 0xA9CBFF
#define DAY_TEXT 0xE8EBF5
#define DAY_SUB 0xB8C4E0
#define DAY_TRACK 0x3A4560
// Night: full-strength red on black and dark red, never grey (the app's AGENTS.md).
#define NIGHT_PAGE 0x000000
#define NIGHT_CHIP 0x2A0000
#define NIGHT_CHIP_PRESSED 0x500000
#define NIGHT_RED 0xFF0000
#define NIGHT_TRACK 0x500000

// Sizes, for the round 466 px screen read at arm's length.
#define CENTRE_PX (BOARD_SCREEN_PX / 2)
#define CHIP_HEIGHT_PX 84
#define CHIP_GAP_PX 12
#define CHIP_MAX_WIDTH_PX 380
#define CHIP_MIN_WIDTH_PX 150
#define EDGE_MARGIN_PX 10 // between a chip's corners and the screen's edge
#define DISC_PX 56
#define SWITCH_WIDTH_PX 64
#define SWITCH_HEIGHT_PX 36
// The title, along the top edge, as the main screen's readout (screen.c).
#define TITLE_RADIUS_PX 198
#define TITLE_SPAN_DEG 150
#define TITLE_BAND_PX 46
// The level bar, as the main screen's distance bar: empty on the left, full on the right.
#define LEVEL_BAR_WIDTH_PX 280
#define LEVEL_BAR_HEIGHT_PX 24
#define LEVEL_BAR_TOUCH_PX 24 // taken as on the bar this far around it, for fingers
// The app's line icons in a chip's disc: as big as the font's icons (32), their disc 7 of the
// 24 grid, with lines 2.5 wide, as the phone's lists (about 1.5 of 24).
#define DRAWN_ICON_DISC_PX (32.0f * 7 / 24)
#define DRAWN_ICON_LINE_PX 2.5f
// How high and which way, on the right of a target's chip, as the phone's badge: green from
// 30° up, amber below (the app's success and warning colours).
#define BADGE_HIGH 0x6FCF9F
#define BADGE_LOW 0xFFB454
#define BADGE_PAD_X 10
#define BADGE_PAD_Y 4

static lv_style_t style_page, style_chip, style_chip_pressed, style_disc, style_big, style_sub, style_title;
static lv_style_t style_bubble, style_bubble_on, style_badge_high, style_badge_low;
static lv_style_t style_band, style_track, style_track_on, style_knob, style_knob_on, style_level, style_level_fill;

void chip_styles_init(void)
{
    static bool done;
    if (done) return; // the zoom's buttons use the bubbles too: whoever comes first
    done = true;
    lv_style_t *all[] = {&style_page, &style_chip, &style_chip_pressed, &style_disc, &style_big, &style_sub,
                         &style_title, &style_band, &style_track, &style_track_on, &style_knob, &style_knob_on,
                         &style_level, &style_level_fill, &style_bubble, &style_bubble_on,
                         &style_badge_high, &style_badge_low};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) lv_style_init(all[i]);

    lv_style_set_bg_opa(&style_page, LV_OPA_COVER);
    lv_style_set_bg_opa(&style_chip, LV_OPA_COVER);
    lv_style_set_radius(&style_chip, LV_RADIUS_CIRCLE);
    lv_style_set_pad_left(&style_chip, (CHIP_HEIGHT_PX - DISC_PX) / 2);
    lv_style_set_pad_right(&style_chip, 22);
    lv_style_set_pad_column(&style_chip, 16);
    lv_style_set_bg_opa(&style_bubble, LV_OPA_COVER);
    lv_style_set_radius(&style_bubble, LV_RADIUS_CIRCLE);
    lv_style_set_text_font(&style_bubble, &material_symbols_48);
    lv_style_set_bg_opa(&style_disc, LV_OPA_COVER);
    lv_style_set_radius(&style_disc, LV_RADIUS_CIRCLE);
    lv_style_set_text_font(&style_disc, &material_symbols_32);
    lv_style_set_text_font(&style_big, &noto_sans_28);
    lv_style_set_text_font(&style_sub, &noto_sans_20);
    lv_style_set_text_font(&style_title, &noto_sans_28);
    lv_style_set_arc_width(&style_band, TITLE_BAND_PX);
    lv_style_set_arc_rounded(&style_band, true);
    lv_style_set_arc_opa(&style_band, LV_OPA_90);
    lv_style_set_bg_opa(&style_track, LV_OPA_COVER);
    lv_style_set_bg_opa(&style_track_on, LV_OPA_COVER);
    lv_style_set_bg_opa(&style_knob, LV_OPA_COVER);
    lv_style_set_bg_opa(&style_knob_on, LV_OPA_COVER);
    lv_style_set_pad_all(&style_knob, -4); // the knob a little inside the track
    lv_style_set_bg_opa(&style_level, LV_OPA_COVER);
    lv_style_set_radius(&style_level, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&style_level_fill, LV_OPA_COVER);
    lv_style_set_radius(&style_level_fill, LV_RADIUS_CIRCLE);
    lv_style_t *badges[] = {&style_badge_high, &style_badge_low};
    for (int i = 0; i < 2; i++) {
        lv_style_set_bg_opa(badges[i], LV_OPA_COVER);
        lv_style_set_radius(badges[i], LV_RADIUS_CIRCLE);
        lv_style_set_pad_hor(badges[i], BADGE_PAD_X);
        lv_style_set_pad_ver(badges[i], BADGE_PAD_Y);
        lv_style_set_text_font(badges[i], &noto_sans_20);
    }
    chip_set_night(false);
}

void chip_set_night(bool night)
{
    lv_color_t page = lv_color_hex(night ? NIGHT_PAGE : DAY_PAGE);
    lv_color_t chip = lv_color_hex(night ? NIGHT_CHIP : DAY_CHIP);
    lv_color_t accent = lv_color_hex(night ? NIGHT_RED : DAY_ACCENT);
    lv_color_t text = lv_color_hex(night ? NIGHT_RED : DAY_TEXT);
    lv_style_set_bg_color(&style_page, page);
    lv_style_set_bg_color(&style_chip, chip);
    lv_style_set_text_color(&style_chip, text);
    lv_style_set_bg_color(&style_chip_pressed, lv_color_hex(night ? NIGHT_CHIP_PRESSED : DAY_CHIP_PRESSED));
    // The icon dark on its bright disc.
    // A bubble: the icon bright on the dark tint; when on, dark on the bright accent.
    lv_style_set_bg_color(&style_bubble, chip);
    lv_style_set_text_color(&style_bubble, accent);
    lv_style_set_bg_color(&style_bubble_on, accent);
    lv_style_set_text_color(&style_bubble_on, page);
    lv_style_set_bg_color(&style_disc, accent);
    lv_style_set_text_color(&style_disc, page);
    lv_style_set_text_color(&style_big, text);
    lv_style_set_text_color(&style_sub, lv_color_hex(night ? NIGHT_RED : DAY_SUB));
    lv_style_set_text_color(&style_title, accent);
    lv_style_set_arc_color(&style_band, page);
    lv_style_set_bg_color(&style_track, lv_color_hex(night ? NIGHT_TRACK : DAY_TRACK));
    lv_style_set_bg_color(&style_track_on, accent);
    lv_style_set_bg_color(&style_knob, text);
    lv_style_set_bg_color(&style_knob_on, night ? page : text);
    lv_style_set_bg_color(&style_level, chip);
    lv_style_set_bg_color(&style_level_fill, accent);
    // Badges: dark words on green or amber; at night, on red, or red words in a red ring
    // when low, so no word is ever grey.
    lv_style_set_bg_color(&style_badge_high, lv_color_hex(night ? NIGHT_RED : BADGE_HIGH));
    lv_style_set_text_color(&style_badge_high, page);
    lv_style_set_bg_color(&style_badge_low, night ? chip : lv_color_hex(BADGE_LOW));
    lv_style_set_text_color(&style_badge_low, night ? accent : page);
    lv_style_set_border_width(&style_badge_low, night ? 2 : 0);
    lv_style_set_border_color(&style_badge_low, accent);
    lv_obj_report_style_change(NULL);
}

lv_obj_t *chip_page_create(lv_obj_t *screen)
{
    lv_obj_t *page = lv_obj_create(screen);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_add_style(page, &style_page, 0);
    lv_obj_set_scrollable(page, false);
    lv_obj_set_clickable(page, true);
    return page;
}

lv_obj_t *chip_label(lv_obj_t *parent, bool big)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_add_style(label, big ? &style_big : &style_sub, 0);
    return label;
}

// Each chip as wide as the circle allows where it is, so the column follows the round
// screen, as a watch's: narrower towards the top and bottom.
static void fit_to_circle(lv_obj_t *list)
{
    const float r = CENTRE_PX - EDGE_MARGIN_PX;
    uint32_t count = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *chip = lv_obj_get_child(list, i);
        lv_area_t a;
        lv_obj_get_coords(chip, &a);
        // The chip's edge furthest from the middle, where the circle is narrowest.
        float d = fmaxf(fabsf((float)(a.y1 - CENTRE_PX)), fabsf((float)(a.y2 - CENTRE_PX)));
        int width = d < r ? (int)(2 * sqrtf(r * r - d * d)) : 0;
        if (width > CHIP_MAX_WIDTH_PX) width = CHIP_MAX_WIDTH_PX;
        if (width < CHIP_MIN_WIDTH_PX) width = CHIP_MIN_WIDTH_PX;
        if (lv_obj_get_width(chip) != width) lv_obj_set_width(chip, width);
    }
}

static void on_scroll(lv_event_t *event)
{
    fit_to_circle(lv_event_get_target_obj(event));
}

lv_obj_t *chip_list_create(lv_obj_t *page, int bottom_px)
{
    // It ends at bottom_px, above the close button: chips are cut off there, never over or
    // under the button. Still the screen's middle that they snap to and shrink around.
    lv_obj_t *list = lv_obj_create(page);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), bottom_px);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, CHIP_GAP_PX, 0);
    // Room above the first chip and below the last, so each can come to the middle.
    lv_obj_set_style_pad_top(list, CENTRE_PX - CHIP_HEIGHT_PX / 2, 0);
    lv_obj_set_style_pad_bottom(list, bottom_px - CENTRE_PX - CHIP_HEIGHT_PX / 2, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(list, on_scroll, LV_EVENT_SCROLL, NULL);
    return list;
}

chip_t chip_add(lv_obj_t *list, const char *icon, const char *text, const char *sub, bool toggle,
                lv_event_cb_t on_click, void *data)
{
    chip_t c = {0};
    c.chip = lv_obj_create(list);
    lv_obj_remove_style_all(c.chip);
    lv_obj_add_style(c.chip, &style_chip, 0);
    lv_obj_add_style(c.chip, &style_chip_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(c.chip, CHIP_MAX_WIDTH_PX, CHIP_HEIGHT_PX);
    lv_obj_set_flex_flow(c.chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c.chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(c.chip, false); // a drag on it scrolls the column
    lv_obj_set_clickable(c.chip, true);
    lv_obj_add_event_cb(c.chip, on_click, LV_EVENT_CLICKED, data);

    lv_obj_t *disc = lv_obj_create(c.chip);
    lv_obj_remove_style_all(disc);
    lv_obj_add_style(disc, &style_disc, 0);
    lv_obj_set_size(disc, DISC_PX, DISC_PX);
    lv_obj_set_clickable(disc, false);
    c.icon = lv_label_create(disc);
    lv_label_set_text(c.icon, icon);
    lv_obj_center(c.icon);

    // The word, and its state under it.
    lv_obj_t *words = lv_obj_create(c.chip);
    lv_obj_remove_style_all(words);
    lv_obj_set_flex_grow(words, 1);
    lv_obj_set_height(words, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(words, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_clickable(words, false);
    c.label = chip_label(words, true);
    lv_obj_set_width(c.label, LV_PCT(100));
    lv_label_set_long_mode(c.label, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(c.label, text);
    if (sub) {
        c.sub = chip_label(words, false);
        lv_obj_set_width(c.sub, LV_PCT(100));
        lv_label_set_long_mode(c.sub, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(c.sub, sub);
    }

    if (toggle) {
        c.toggle = lv_switch_create(c.chip);
        lv_obj_remove_style_all(c.toggle);
        lv_obj_set_size(c.toggle, SWITCH_WIDTH_PX, SWITCH_HEIGHT_PX);
        lv_obj_set_style_radius(c.toggle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(c.toggle, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_radius(c.toggle, LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_add_style(c.toggle, &style_track, LV_PART_MAIN);
        lv_obj_add_style(c.toggle, &style_track_on, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_style(c.toggle, &style_knob, LV_PART_KNOB);
        lv_obj_add_style(c.toggle, &style_knob_on, LV_PART_KNOB | LV_STATE_CHECKED);
        lv_obj_set_clickable(c.toggle, false); // the whole chip switches it
    }
    return c;
}

void chip_list_reset(lv_obj_t *list)
{
    lv_obj_update_layout(list);
    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    lv_obj_update_layout(list);
    fit_to_circle(list);
}

void chip_list_fit(lv_obj_t *list)
{
    lv_obj_update_layout(list);
    fit_to_circle(list);
}

// Draws the app's icon named in the event's user data in the disc, in the disc's icon colour.
typedef struct {
    lv_layer_t *layer;
    lv_draw_line_dsc_t line;
} disc_draw_t;

static void stroke_in_disc(float x0, float y0, float x1, float y1, float width, bool edge, void *ctx)
{
    disc_draw_t *d = ctx;
    d->line.width = (int32_t)lroundf(fmaxf(1, width));
    d->line.p1.x = (lv_value_precise_t)x0;
    d->line.p1.y = (lv_value_precise_t)y0;
    d->line.p2.x = (lv_value_precise_t)x1;
    d->line.p2.y = (lv_value_precise_t)y1;
    lv_draw_line(d->layer, &d->line);
}

static void on_disc_draw(lv_event_t *event)
{
    lv_obj_t *disc = lv_event_get_target_obj(event);
    const char *name = lv_event_get_user_data(event);
    lv_area_t area;
    lv_obj_get_coords(disc, &area);
    disc_draw_t d = {.layer = lv_event_get_layer(event)};
    lv_draw_line_dsc_init(&d.line);
    d.line.color = lv_obj_get_style_text_color(disc, LV_PART_MAIN);
    d.line.round_start = d.line.round_end = 1;
    float cx = (area.x1 + area.x2 + 1) / 2.0f, cy = (area.y1 + area.y2 + 1) / 2.0f;
    sky_icons_trace(name, cx, cy, DRAWN_ICON_DISC_PX, DRAWN_ICON_LINE_PX, 0, stroke_in_disc, &d);
}

static void on_disc_delete(lv_event_t *event)
{
    lv_free(lv_event_get_user_data(event));
}

bool chip_set_drawn_icon(const chip_t *chip, const char *name)
{
    if (!chip->icon || !sky_icons_has(name)) return false;
    lv_obj_t *disc = lv_obj_get_parent(chip->icon);
    lv_label_set_text(chip->icon, "");
    char *copy = lv_strdup(name);
    lv_obj_add_event_cb(disc, on_disc_draw, LV_EVENT_DRAW_MAIN_END, copy);
    lv_obj_add_event_cb(disc, on_disc_delete, LV_EVENT_DELETE, copy);
    return true;
}

void chip_set_badge(const chip_t *chip, const char *text, bool high)
{
    lv_obj_t *badge = lv_label_create(chip->chip);
    lv_obj_add_style(badge, high ? &style_badge_high : &style_badge_low, 0);
    lv_label_set_text(badge, text);
    lv_obj_set_clickable(badge, false);
}

void chip_set_sub(const chip_t *chip, const char *text)
{
    if (chip->sub && strcmp(lv_label_get_text(chip->sub), text) != 0) lv_label_set_text(chip->sub, text);
}

void chip_set_on(const chip_t *chip, bool on)
{
    if (chip->toggle) lv_obj_set_state(chip->toggle, LV_STATE_CHECKED, on);
}

lv_obj_t *chip_bubble(lv_obj_t *parent, int size_px, const char *icon, lv_event_cb_t on_click, void *data)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &style_bubble, 0);
    lv_obj_add_style(b, &style_bubble_on, LV_STATE_CHECKED);
    lv_obj_add_style(b, &style_chip_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(b, size_px, size_px);
    lv_obj_set_scrollable(b, false);
    lv_obj_set_clickable(b, true);
    lv_obj_add_event_cb(b, on_click, LV_EVENT_ALL, data);
    lv_obj_t *label = lv_label_create(b);
    lv_label_set_text(label, icon);
    lv_obj_center(label);
    return b;
}

void chip_bubble_set_on(lv_obj_t *bubble, bool on)
{
    lv_obj_set_state(bubble, LV_STATE_CHECKED, on);
}

lv_obj_t *chip_round_button(lv_obj_t *parent, int size_px, const char *icon, lv_event_cb_t on_click, void *data)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &style_chip, 0);
    lv_obj_add_style(b, &style_chip_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_size(b, size_px, size_px);
    lv_obj_set_scrollable(b, false);
    lv_obj_set_clickable(b, true);
    lv_obj_add_event_cb(b, on_click, LV_EVENT_CLICKED, data);
    lv_obj_t *label = lv_label_create(b);
    lv_obj_set_style_text_font(label, &material_symbols_32, 0);
    lv_label_set_text(label, icon);
    lv_obj_center(label);
    return b;
}

lv_obj_t *chip_title_create(lv_obj_t *page)
{
    // A dark band behind it, so chips scrolling under it don't muddle it.
    lv_obj_t *band = lv_arc_create(page);
    lv_obj_set_size(band, BOARD_SCREEN_PX, BOARD_SCREEN_PX);
    lv_obj_center(band);
    lv_arc_set_bg_angles(band, 270 - TITLE_SPAN_DEG / 2 - 8, 270 + TITLE_SPAN_DEG / 2 + 8);
    lv_obj_remove_style(band, NULL, LV_PART_KNOB);
    lv_obj_remove_style(band, NULL, LV_PART_INDICATOR);
    lv_obj_add_style(band, &style_band, LV_PART_MAIN);
    lv_obj_set_style_arc_width(band, 0, LV_PART_INDICATOR);
    lv_obj_set_clickable(band, false);

    lv_obj_t *title = lv_arclabel_create(page);
    lv_obj_set_size(title, BOARD_SCREEN_PX, BOARD_SCREEN_PX);
    lv_obj_add_style(title, &style_title, 0);
    lv_arclabel_set_radius(title, TITLE_RADIUS_PX);
    lv_arclabel_set_angle_start(title, 270 - TITLE_SPAN_DEG / 2);
    lv_arclabel_set_angle_size(title, TITLE_SPAN_DEG);
    lv_arclabel_set_dir(title, LV_ARCLABEL_DIR_CLOCKWISE);
    lv_arclabel_set_text_horizontal_align(title, LV_ARCLABEL_TEXT_ALIGN_CENTER);
    lv_arclabel_set_text_vertical_align(title, LV_ARCLABEL_TEXT_ALIGN_TRAILING);
    lv_arclabel_set_text(title, "");
    lv_obj_set_clickable(title, false);
    lv_obj_center(title);
    return title;
}

lv_obj_t *chip_level_bar_create(lv_obj_t *parent, int min, int max)
{
    // A slider with no knob: the fill alone shows the level, and a drag along it sets it.
    lv_obj_t *bar = lv_slider_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LEVEL_BAR_WIDTH_PX, LEVEL_BAR_HEIGHT_PX);
    lv_slider_set_range(bar, min, max);
    lv_obj_add_style(bar, &style_level, LV_PART_MAIN);
    lv_obj_add_style(bar, &style_level_fill, LV_PART_INDICATOR);
    lv_obj_set_ext_click_area(bar, LEVEL_BAR_TOUCH_PX);
    return bar;
}
