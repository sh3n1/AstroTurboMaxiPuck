// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The player over the map: see player.h. Its colours are the menu's (menu_chip.c), so they
// follow night mode with it.

#include "player.h"

#include <string.h>

#include "fonts.h"
#include "icons.h"
#include "link.h"
#include "menu.h"
#include "menu_chip.h"

// Sizes for the round 466 px screen read at arm's length; centres from the middle.
#define LIST_Y_PX (-140)
#define TARGET_Y_PX (-100)
#define TEXT_WIDTH_PX 300
#define MENU_PX 124
#define STEP_PX 92
#define STEP_X_PX 142
#define CLOSE_PX 76
#define CLOSE_BOTTOM_PX 22
#define DIM_OPA LV_OPA_80     // the map shows dimly behind
#define NO_LIST_OPA LV_OPA_30 // previous and next, when there is no list to step through
#define SHOW_MS 15000         // it goes after this long without a tap

typedef enum { DO_PREVIOUS, DO_MENU, DO_NEXT, DO_CLOSE } action_t;

static lv_obj_t *page, *list_label, *target_label, *steps[2];
static bool active;
static uint32_t revision;
static uint32_t since;

// A bubble hears every event: taps only. Each keeps it showing a while longer.
static void on_button(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    since = lv_tick_get();
    switch ((action_t)(intptr_t)lv_event_get_user_data(event)) {
    case DO_PREVIOUS:
        link_send_command("{\"t\":\"step\",\"by\":-1}");
        break;
    case DO_NEXT:
        link_send_command("{\"t\":\"step\",\"by\":1}");
        break;
    case DO_MENU:
        player_close();
        menu_open();
        break;
    case DO_CLOSE:
        player_close();
        break;
    }
}

static lv_obj_t *bubble(int size_px, const char *icon, const lv_font_t *font, action_t action)
{
    lv_obj_t *b = chip_bubble(page, size_px, icon, on_button, (void *)(intptr_t)action);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), font, 0);
    return b;
}

static lv_obj_t *line(bool big, int y_px)
{
    lv_obj_t *label = chip_label(page, big);
    lv_obj_set_width(label, TEXT_WIDTH_PX);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, y_px);
    lv_label_set_text(label, "");
    return label;
}

void player_create(lv_obj_t *screen)
{
    chip_styles_init();
    // The menu's page colour, not quite opaque: the map dims behind. Taps on it stay here.
    page = chip_page_create(screen);
    lv_obj_set_style_bg_opa(page, DIM_OPA, 0);

    list_label = line(false, LIST_Y_PX);
    target_label = line(true, TARGET_Y_PX);

    steps[0] = bubble(STEP_PX, ICON_SKIP_PREVIOUS, &material_symbols_48, DO_PREVIOUS);
    lv_obj_align(steps[0], LV_ALIGN_CENTER, -STEP_X_PX, 0);
    steps[1] = bubble(STEP_PX, ICON_SKIP_NEXT, &material_symbols_48, DO_NEXT);
    lv_obj_align(steps[1], LV_ALIGN_CENTER, STEP_X_PX, 0);
    // The menu, big in the middle and filled, as the watch's play button.
    lv_obj_t *menu = bubble(MENU_PX, ICON_MENU, &material_symbols_48, DO_MENU);
    lv_obj_center(menu);
    chip_bubble_set_on(menu, true);
    lv_obj_t *close = bubble(CLOSE_PX, ICON_CLOSE, &material_symbols_32, DO_CLOSE);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -CLOSE_BOTTOM_PX);

    lv_obj_set_hidden(page, true);
}

void player_open(void)
{
    if (!page) return;
    since = lv_tick_get();
    if (active) return;
    active = true;
    revision++;
    lv_obj_set_hidden(page, false);
}

void player_close(void)
{
    if (!active) return;
    active = false;
    revision++;
    lv_obj_set_hidden(page, true);
}

bool player_active(void)
{
    return active;
}

uint32_t player_revision(void)
{
    return revision;
}

void player_set_text(const char *list, const char *target)
{
    if (!page) return;
    if (strcmp(lv_label_get_text(list_label), list) != 0) lv_label_set_text(list_label, list);
    if (strcmp(lv_label_get_text(target_label), target) != 0) lv_label_set_text(target_label, target);
    // Previous and next step through a list: faint, and deaf, without one.
    bool has_list = list[0] != '\0';
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_opa(steps[i], has_list ? LV_OPA_COVER : NO_LIST_OPA, 0);
        lv_obj_set_clickable(steps[i], has_list);
    }
}

void player_tick(void)
{
    if (active && lv_tick_elaps(since) > SHOW_MS) player_close();
}
