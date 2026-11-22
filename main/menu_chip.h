// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The menu's look, in the style of a watch (Wear OS, watchOS): bubbles for the main menu,
// as a watch's app launcher, and for the pages within it "chips", wide rounded items
// filled in a dark tint, each with a round icon, its word and, under it, its state; a column
// of them that snaps an item to the middle and narrows them to fit the round screen; round
// buttons; and a title written along the top edge. Shared styles, so night mode recolours
// everything at once (docs/screen-layout.md, "Menu").

#pragma once

#include <stdbool.h>

#include "lvgl.h"

/** One item of the column. */
typedef struct {
    lv_obj_t *chip;   // the whole item, which takes the tap
    lv_obj_t *label;  // its word
    lv_obj_t *sub;    // its state under the word ("On", "Sky map"), or NULL
    lv_obj_t *toggle; // its switch, or NULL
    lv_obj_t *icon;   // the icon's label, for icons that change (the view)
} chip_t;

/** Makes the styles. Call once, before anything else here. */
void chip_styles_init(void);

/** The day's colours or night's full-strength red. */
void chip_set_night(bool night);

/** The page behind everything: the whole screen, opaque, keeping its taps. */
lv_obj_t *chip_page_create(lv_obj_t *screen);

/** The column of chips, over the whole page, scrolling up and down. */
lv_obj_t *chip_list_create(lv_obj_t *page);

/** An item: icon (icons.h), word, and a state line (sub, or NULL for none) and a switch
 *  (toggle) if wanted. on_click gets data as its user data. */
chip_t chip_add(lv_obj_t *list, const char *icon, const char *text, const char *sub, bool toggle,
                lv_event_cb_t on_click, void *data);

/** Back at the top, each chip fitted to the screen's circle: after adding the chips. */
void chip_list_reset(lv_obj_t *list);

/** Each chip fitted to the screen's circle where the column is: after adding chips to it. */
void chip_list_fit(lv_obj_t *list);

/**
 * Draws the app's line icon by this name (sky_icons.h) in the chip's disc, in place of its
 * font icon. False if the app sent none by that name: the font icon stays.
 */
bool chip_set_drawn_icon(const chip_t *chip, const char *name);

/** How high a target is and which way to face ("42° SE"), on the chip's right, as the
 *  phone's lists: green when high enough to see well, amber when low. */
void chip_set_badge(const chip_t *chip, const char *text, bool high);

/** A chip's state line and switch. */
void chip_set_sub(const chip_t *chip, const char *text);
void chip_set_on(const chip_t *chip, bool on);

/** A bubble of the main menu, as a watch's app launcher: a round button with a big icon
 *  (icons.h, one of those in material_symbols_48). Filled bright when on (chip_bubble_set_on). */
lv_obj_t *chip_bubble(lv_obj_t *parent, int size_px, const char *icon, lv_event_cb_t on_click, void *data);
void chip_bubble_set_on(lv_obj_t *bubble, bool on);

/** A round button with an icon (icons.h). */
lv_obj_t *chip_round_button(lv_obj_t *parent, int size_px, const char *icon, lv_event_cb_t on_click, void *data);

/** The page's title, written along the top edge. */
lv_obj_t *chip_title_create(lv_obj_t *page);

/** A bar showing a level, for the Brightness page: empty on the left, full on the right, as
 *  the main screen's distance bar. A drag along it sets the level. */
lv_obj_t *chip_level_bar_create(lv_obj_t *parent, int min, int max);

/** Text in the menu's style: big (the words) or small (the states). */
lv_obj_t *chip_label(lv_obj_t *parent, bool big);
