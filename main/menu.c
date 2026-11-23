// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The menu: see menu.h, and docs/screen-layout.md for what each item does. The main page is
// a honeycomb of bubbles, as a watch's app launcher; the pages within it are columns of
// chips, made afresh when each opens, from the app's words of the moment. The look is
// menu_chip.c's.

#include "menu.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "brightness.h"
#include "fonts.h"
#include "icons.h"
#include "link.h"
#include "lists.h"
#include "lx200.h"
#include "menu_lists.h"
#include "menu_chip.h"
#include "texts.h"
#include "wifi.h"
#include "zoom.h"

#define BACK_PX 72
#define BACK_BOTTOM_PX 16 // between the back button and the screen's bottom edge
#define LIST_GAP_PX 8     // between the column's cut-off and the back button
// The main menu's bubbles, in a honeycomb of rows of 3, 2 and 3, a little above the middle,
// clear of the title and the close button.
#define BUBBLE_PX 96
#define BUBBLE_STEP_X_PX 112
#define BUBBLE_STEP_Y_PX 97 // the step across times √3/2: a honeycomb
#define BUBBLES_RAISE_PX 13
// The Brightness page: its icon, the value with − and + either side, and the bar under them.
#define STEP_PX 80
#define STEP_OFFSET_PX 130
#define LEVEL_ICON_Y (-80)
#define LEVEL_VALUE_Y (-10)
#define LEVEL_BAR_Y 70

typedef enum { PAGE_MAIN, PAGE_SETTINGS, PAGE_WIFI, PAGE_BRIGHTNESS, PAGE_LISTS, PAGE_LIST_TARGETS, PAGE_ALIGN } page_t;
typedef enum {
    DO_SENSORS,
    DO_VIEW,
    DO_LISTS,
    DO_ALIGNMENT,
    DO_ZOOM,
    DO_NIGHT,
    DO_SETTINGS,
    DO_LX200,
    DO_BRIGHTNESS,
    DO_WIFI,
    DO_WIFI_CLIENT,
    DO_WIFI_AP,
    DO_DIMMER,
    DO_BRIGHTER,
} action_t;

static lv_obj_t *page;     // the whole menu, over the map
static lv_obj_t *list;     // the chips of the page shown (not the main page)
static lv_obj_t *bubbles;  // the main page's bubbles
static lv_obj_t *title;    // along the top edge
static lv_obj_t *back_icon;
static lv_obj_t *level_box; // the Brightness page's ring, value and buttons
static lv_obj_t *level_bar;
static lv_obj_t *level_label;
static page_t shown_page;
static void (*switch_night)(void);
static chip_t brightness_chip;
// The app's state as it last sent it, for the bubbles that show it.
static view_state_t app;

// The main page, in the order of docs/screen-layout.md: each bubble's icon, its word (shown
// along the top edge while it's pressed), and what it does.
typedef struct {
    const char *icon;
    texts_word_t word;
    action_t action;
} bubble_item_t;
enum { BUBBLE_SENSORS, BUBBLE_VIEW, BUBBLE_LISTS, BUBBLE_ALIGNMENT, BUBBLE_ZOOM, BUBBLE_NIGHT, BUBBLE_SETTINGS,
       BUBBLE_COUNT };
static const bubble_item_t BUBBLES[BUBBLE_COUNT] = {
    {ICON_SENSORS, TEXTS_SENSORS, DO_SENSORS}, {ICON_VIEW_EYEPIECE, TEXTS_VIEW_EYEPIECE, DO_VIEW},
    {ICON_LISTS, TEXTS_LISTS, DO_LISTS},       {ICON_ALIGNMENT, TEXTS_ALIGNMENT, DO_ALIGNMENT},
    {ICON_ZOOM, TEXTS_ZOOM, DO_ZOOM},          {ICON_NIGHT, TEXTS_NIGHT, DO_NIGHT},
    {ICON_SETTINGS, TEXTS_SETTINGS, DO_SETTINGS},
};
// Where each goes, in steps from the honeycomb's middle: rows of 2, 3 and 2.
static const float BUBBLE_AT[BUBBLE_COUNT][2] = {
    {-0.5f, -1}, {0.5f, -1}, {-1, 0}, {0, 0}, {1, 0}, {-0.5f, 1}, {0.5f, 1},
};
static lv_obj_t *bubble[BUBBLE_COUNT];
static const texts_word_t VIEW_WORDS[] = {TEXTS_VIEW_SKY, TEXTS_VIEW_EYEPIECE, TEXTS_VIEW_RING};
static const char *const VIEW_ICONS[] = {ICON_VIEW_SKY, ICON_VIEW_EYEPIECE, ICON_VIEW_RING};

static void show_page(page_t page);

static const char *on_off(bool on)
{
    static char word[TEXTS_WORD_BYTES];
    texts_word(on ? TEXTS_ON : TEXTS_OFF, word);
    return word;
}

// As the app's view tile: the view a tap switches to, not the one showing.
static int next_view(void)
{
    return (app.view_mode + 1) % 3;
}

// The app's state on the bubbles that show it: filled bright when on.
static void show_app_state(void)
{
    chip_bubble_set_on(bubble[BUBBLE_SENSORS], app.sensors_on);
    chip_bubble_set_on(bubble[BUBBLE_NIGHT], app.night);
    lv_obj_t *view_icon = lv_obj_get_child(bubble[BUBBLE_VIEW], 0);
    if (strcmp(lv_label_get_text(view_icon), VIEW_ICONS[next_view()]) != 0) {
        lv_label_set_text(view_icon, VIEW_ICONS[next_view()]);
    }
}

// The title along the top edge: the page's name, or while a bubble is pressed, its word and
// its state, as a watch's launcher has no words under its icons.
static void show_title(void)
{
    char text[2 * TEXTS_WORD_BYTES + 4];
    texts_word(TEXTS_MENU, text);
    for (int i = 0; i < BUBBLE_COUNT; i++) {
        if (!lv_obj_has_state(bubble[i], LV_STATE_PRESSED)) continue;
        texts_word(i == BUBBLE_VIEW ? VIEW_WORDS[next_view()] : BUBBLES[i].word, text);
        if (i == BUBBLE_SENSORS || i == BUBBLE_NIGHT) {
            bool on = i == BUBBLE_SENSORS ? app.sensors_on : app.night;
            size_t n = strlen(text);
            snprintf(text + n, sizeof(text) - n, ": %s", on_off(on));
        }
    }
    lv_arclabel_set_text(title, text);
}

static void show_level(void)
{
    lv_label_set_text_fmt(level_label, "%d %%", brightness_percent());
    lv_slider_set_value(level_bar, brightness_percent(), LV_ANIM_OFF);
}

// A page made in a chip's own click would delete that chip inside its event: wait for the
// event to end.
static void show_page_later(void *next)
{
    show_page((page_t)(intptr_t)next);
}

static void open_later(page_t next)
{
    lv_async_call(show_page_later, (void *)(intptr_t)next);
}

static void open_list_targets(void)
{
    open_later(PAGE_LIST_TARGETS);
}

static void on_item(lv_event_t *event)
{
    switch ((action_t)(intptr_t)lv_event_get_user_data(event)) {
    case DO_SENSORS:
        // The app answers with its state, which moves the switch.
        link_send_command(app.sensors_on ? "{\"t\":\"sensors\",\"on\":false}" : "{\"t\":\"sensors\",\"on\":true}");
        break;
    case DO_VIEW:
        link_send_command("{\"t\":\"view\",\"next\":true}");
        break;
    case DO_ZOOM:
        menu_close();
        zoom_open();
        break;
    case DO_NIGHT:
        switch_night();
        break;
    case DO_SETTINGS:
        open_later(PAGE_SETTINGS);
        break;
    case DO_BRIGHTNESS:
        open_later(PAGE_BRIGHTNESS);
        break;
    case DO_WIFI:
        open_later(PAGE_WIFI);
        break;
    case DO_DIMMER:
        brightness_set_percent(brightness_percent() - BRIGHTNESS_STEP_PERCENT);
        show_level();
        break;
    case DO_BRIGHTER:
        brightness_set_percent(brightness_percent() + BRIGHTNESS_STEP_PERCENT);
        show_level();
        break;
    case DO_LISTS:
        // Asked afresh each time, as the lists follow the sky; shown as they come.
        link_send_command("{\"t\":\"lists\"}");
        open_later(PAGE_LISTS);
        break;
    case DO_ALIGNMENT:
        // Asked afresh each time, as the stars move; shown as they come.
        link_send_command("{\"t\":\"alignStars\"}");
        open_later(PAGE_ALIGN);
        break;
    case DO_LX200:
        lx200_set_enabled(!lx200_enabled());
        open_later(PAGE_SETTINGS); // shows the switch and the address anew
        break;
    case DO_WIFI_CLIENT:
        // Restarts into setup: the puck's own network and its web page (wifi.h).
        wifi_open_setup();
        break;
    case DO_WIFI_AP:
        // Restarts with or without the puck's own network.
        wifi_set_own_network(!wifi_own_network());
        break;
    }
}

static void on_item(lv_event_t *event);

// A bubble: its word along the top edge while pressed, and its action on a tap.
static void on_bubble(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        show_title();
    } else if (code == LV_EVENT_CLICKED) {
        on_item(event);
    }
}

static void on_level_dragged(lv_event_t *event)
{
    brightness_set_percent(lv_slider_get_value(level_bar));
    lv_label_set_text_fmt(level_label, "%d %%", brightness_percent());
}

// Back: from a page within the menu to the one above, from the first page to the map.
static void go_back(void)
{
    if (shown_page == PAGE_BRIGHTNESS) brightness_save();
    switch (shown_page) {
    case PAGE_MAIN:
        menu_close();
        break;
    case PAGE_SETTINGS:
        open_later(PAGE_MAIN);
        break;
    case PAGE_WIFI:
    case PAGE_BRIGHTNESS:
        open_later(PAGE_SETTINGS);
        break;
    case PAGE_LISTS:
    case PAGE_ALIGN:
        open_later(PAGE_MAIN);
        break;
    case PAGE_LIST_TARGETS:
        open_later(PAGE_LISTS);
        break;
    }
}

static void on_back(lv_event_t *event)
{
    go_back();
}

// A swipe to the right goes back too, as on a watch.
static void on_gesture(lv_event_t *event)
{
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) go_back();
}

static chip_t add(const char *icon, texts_word_t word, bool has_state, bool toggle, action_t action)
{
    char text[TEXTS_WORD_BYTES];
    texts_word(word, text);
    return chip_add(list, icon, text, has_state ? "" : NULL, toggle, on_item, (void *)(intptr_t)action);
}

static void show_page(page_t next)
{
    if (!page) return;
    shown_page = next;
    menu_lists_stop();
    lv_obj_clean(list);
    char text[LISTS_NAME_BYTES]; // a word, or a list's name
    switch (next) {
    case PAGE_MAIN:
        // The bubbles stay made; their states are the app's (show_app_state).
        texts_word(TEXTS_MENU, text);
        break;
    case PAGE_SETTINGS: {
        texts_word(TEXTS_SETTINGS, text);
        brightness_chip = add(ICON_BRIGHTNESS, TEXTS_BRIGHTNESS, true, false, DO_BRIGHTNESS);
        char level[8];
        snprintf(level, sizeof(level), "%d %%", brightness_percent());
        chip_set_sub(&brightness_chip, level);
        add(ICON_WIFI, TEXTS_WIFI, false, false, DO_WIFI);
        // While on, the address to give the planetarium app, else "Off".
        chip_t lx200 = add(ICON_LX200, TEXTS_LX200, true, true, DO_LX200);
        char where[32];
        snprintf(where, sizeof(where), "%s:%d", wifi_address(), LX200_PORT);
        chip_set_sub(&lx200, lx200_enabled() && wifi_address()[0] ? where : on_off(lx200_enabled()));
        chip_set_on(&lx200, lx200_enabled());
        break;
    }
    case PAGE_WIFI: {
        texts_word(TEXTS_WIFI, text);
        add(ICON_WIFI_CLIENT, TEXTS_WIFI_CLIENT, false, false, DO_WIFI_CLIENT);
        chip_t ap = add(ICON_WIFI_AP, TEXTS_WIFI_AP, true, true, DO_WIFI_AP);
        chip_set_sub(&ap, on_off(wifi_own_network()));
        chip_set_on(&ap, wifi_own_network());
        break;
    }
    case PAGE_BRIGHTNESS:
        texts_word(TEXTS_BRIGHTNESS, text);
        show_level();
        break;
    case PAGE_LISTS:
        texts_word(TEXTS_LISTS, text);
        menu_lists_show(list);
        break;
    case PAGE_LIST_TARGETS:
        snprintf(text, sizeof(text), "%s", menu_lists_chosen_name());
        menu_lists_show_targets(list);
        break;
    case PAGE_ALIGN:
        texts_word(TEXTS_ALIGNMENT, text);
        menu_lists_show_align(list);
        break;
    }
    lv_obj_set_hidden(bubbles, next != PAGE_MAIN);
    lv_obj_set_hidden(list, next == PAGE_MAIN || next == PAGE_BRIGHTNESS);
    lv_obj_set_hidden(level_box, next != PAGE_BRIGHTNESS);
    lv_arclabel_set_text(title, text);
    // A cross to close the menu, an arrow to go back up.
    lv_label_set_text(back_icon, next == PAGE_MAIN ? ICON_CLOSE : ICON_BACK);
    chip_list_reset(list);
    show_app_state();
}

// The main page's bubbles, made once.
static void make_bubbles(void)
{
    bubbles = lv_obj_create(page);
    lv_obj_remove_style_all(bubbles);
    lv_obj_set_size(bubbles, LV_PCT(100), LV_PCT(100));
    lv_obj_set_scrollable(bubbles, false);
    lv_obj_set_clickable(bubbles, false);
    for (int i = 0; i < BUBBLE_COUNT; i++) {
        bubble[i] = chip_bubble(bubbles, BUBBLE_PX, BUBBLES[i].icon, on_bubble, (void *)(intptr_t)BUBBLES[i].action);
        lv_obj_align(bubble[i], LV_ALIGN_CENTER, (int)(BUBBLE_AT[i][0] * BUBBLE_STEP_X_PX),
                     (int)(BUBBLE_AT[i][1] * BUBBLE_STEP_Y_PX) - BUBBLES_RAISE_PX);
    }
}

// The Brightness page: the sun's icon, the value with − and + either side, and under them a
// bar that can be dragged.
static void make_level_box(void)
{
    level_box = lv_obj_create(page);
    lv_obj_remove_style_all(level_box);
    lv_obj_set_size(level_box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_scrollable(level_box, false);
    lv_obj_set_clickable(level_box, false);
    lv_obj_t *icon = chip_label(level_box, true);
    lv_obj_set_style_text_font(icon, &material_symbols_32, 0);
    lv_label_set_text(icon, ICON_BRIGHTNESS);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, LEVEL_ICON_Y);
    level_label = chip_label(level_box, true);
    lv_obj_align(level_label, LV_ALIGN_CENTER, 0, LEVEL_VALUE_Y);
    lv_obj_t *dimmer = chip_round_button(level_box, STEP_PX, ICON_REMOVE, on_item, (void *)(intptr_t)DO_DIMMER);
    lv_obj_align(dimmer, LV_ALIGN_CENTER, -STEP_OFFSET_PX, LEVEL_VALUE_Y);
    lv_obj_t *brighter = chip_round_button(level_box, STEP_PX, ICON_ADD, on_item, (void *)(intptr_t)DO_BRIGHTER);
    lv_obj_align(brighter, LV_ALIGN_CENTER, STEP_OFFSET_PX, LEVEL_VALUE_Y);
    level_bar = chip_level_bar_create(level_box, BRIGHTNESS_MIN_PERCENT, 100);
    lv_obj_align(level_bar, LV_ALIGN_CENTER, 0, LEVEL_BAR_Y);
    lv_obj_add_event_cb(level_bar, on_level_dragged, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_hidden(level_box, true);
}

void menu_create(lv_obj_t *screen, void (*night_switch)(void))
{
    switch_night = night_switch;
    chip_styles_init();
    page = chip_page_create(screen);
    lv_obj_add_event_cb(page, on_gesture, LV_EVENT_GESTURE, NULL);
    list = chip_list_create(page, BOARD_SCREEN_PX - BACK_BOTTOM_PX - BACK_PX - LIST_GAP_PX);
    make_bubbles();
    make_level_box();
    title = chip_title_create(page);
    menu_lists_init(open_list_targets, menu_close);
    lv_obj_t *back = chip_round_button(page, BACK_PX, ICON_CLOSE, on_back, NULL);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -BACK_BOTTOM_PX);
    back_icon = lv_obj_get_child(back, 0);
    lv_obj_set_hidden(page, true);
}

void menu_open(void)
{
    show_page(PAGE_MAIN);
    lv_obj_set_hidden(page, false);
}

void menu_open_brightness(void)
{
    show_page(PAGE_BRIGHTNESS);
    lv_obj_set_hidden(page, false);
}

void menu_close(void)
{
    if (!page) return;
    if (shown_page == PAGE_BRIGHTNESS) brightness_save();
    menu_lists_stop();
    lv_obj_set_hidden(page, true);
}

bool menu_active(void)
{
    return page && !lv_obj_is_hidden(page);
}

void menu_set_night(bool night)
{
    chip_set_night(night);
}

void menu_show_state(const view_state_t *state)
{
    // The phone's answer to the Lists pages, shown as it comes.
    bool on_lists = shown_page == PAGE_LISTS || shown_page == PAGE_LIST_TARGETS || shown_page == PAGE_ALIGN;
    if (menu_active() && on_lists && menu_lists_changed()) show_page(shown_page);
    if (state->sensors_on == app.sensors_on && state->view_mode == app.view_mode && state->night == app.night) return;
    app = *state;
    if (bubbles) show_app_state();
}
