// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The menu's Lists pages: see menu_lists.h, and docs/screen-layout.md.

#include "menu_lists.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "align.h"
#include "esp_log.h"
#include "icons.h"
#include "link.h"
#include "lists.h"
#include "menu_chip.h"

// A chip takes about 15 ms to make, so 60 at once held the screen for a second. The first
// ones fill the screen and come at once; the rest follow a few at each turn of LVGL's loop.
#define FIRST_CHIPS 5
#define CHIPS_PER_TURN 3
#define TURN_MS 10

static const char *TAG = "menu";

static void (*open_targets)(void);
static void (*close_menu)(void);
static lv_obj_t *column;
static lv_timer_t *adder;
typedef enum { SHOW_LISTS, SHOW_TARGETS, SHOW_ALIGN } showing_t;
static showing_t showing;
static bool remove_asked; // Remove tapped once: a second tap removes the alignment
static chip_t remove_chip;
static size_t next_chip;    // the next list or target to add
static unsigned shown;      // the version of the phone's lists shown (lists.h)
static char chosen_list[LISTS_ID_BYTES];
static char chosen_name[LISTS_NAME_BYTES];

// Ids go into JSON as they are: one with a quote or a backslash in it can't be sent.
static bool json_safe(const char *id)
{
    return id[0] && !strpbrk(id, "\"\\");
}

// A list chosen: its targets are asked for, and shown as they come.
static void on_list_chosen(lv_event_t *event)
{
    size_t i = (size_t)(intptr_t)lv_event_get_user_data(event);
    const lists_t *lists = lists_lock();
    if (!lists) return;
    // Only from the lists shown: they may have changed since.
    bool ok = lists->version == shown && i < lists->list_count && json_safe(lists->lists[i].id);
    if (ok) {
        strcpy(chosen_list, lists->lists[i].id);
        strcpy(chosen_name, lists->lists[i].name);
    }
    lists_unlock();
    if (!ok) return;
    lists_forget_targets();
    char command[96];
    snprintf(command, sizeof(command), "{\"t\":\"listTargets\",\"id\":\"%s\"}", chosen_list);
    link_send_command(command);
    open_targets();
}

// A target chosen: it becomes the target, and the list plays from it; back to the map.
static void on_target_chosen(lv_event_t *event)
{
    size_t i = (size_t)(intptr_t)lv_event_get_user_data(event);
    char target[LISTS_ID_BYTES];
    const lists_t *lists = lists_lock();
    if (!lists) return;
    bool ok = lists->version == shown && i < lists->target_count && json_safe(lists->targets[i].id) &&
              strcmp(lists->targets_of, chosen_list) == 0;
    if (ok) strcpy(target, lists->targets[i].id);
    lists_unlock();
    if (!ok) return;
    char command[160];
    snprintf(command, sizeof(command), "{\"t\":\"play\",\"list\":\"%s\",\"target\":\"%s\"}", chosen_list, target);
    link_send_command(command);
    close_menu();
}

static void on_nothing(lv_event_t *event)
{
}

// A star chosen to align on: it becomes the target, and ✗ and Align show over the map.
static void on_star_chosen(lv_event_t *event)
{
    size_t i = (size_t)(intptr_t)lv_event_get_user_data(event);
    char star[LISTS_ID_BYTES];
    const lists_t *lists = lists_lock();
    if (!lists) return;
    bool ok = lists->version == shown && i < lists->align_count && json_safe(lists->align_stars[i].id);
    if (ok) strcpy(star, lists->align_stars[i].id);
    lists_unlock();
    if (!ok) return;
    char command[96];
    snprintf(command, sizeof(command), "{\"t\":\"alignChoose\",\"id\":\"%s\"}", star);
    link_send_command(command);
    close_menu();
    align_open();
}

// Remove: a first tap asks again (the phone's words), a second removes the alignment; the
// phone answers with the page anew.
static void on_remove(lv_event_t *event)
{
    if (remove_asked) {
        link_send_command("{\"t\":\"alignRemove\"}");
        return;
    }
    remove_asked = true;
    const lists_t *lists = lists_lock();
    if (!lists) return;
    lv_label_set_text(remove_chip.label, lists->align_remove_again);
    lists_unlock();
}

// The Alignment page: a few chips, all at once.
static void add_align(const lists_t *lists)
{
    if (lists->align_current[0]) {
        chip_add(column, ICON_CHECK, lists->align_current, NULL, false, on_nothing, NULL);
        remove_chip = chip_add(column, ICON_CLOSE, lists->align_remove, NULL, false, on_remove, NULL);
    }
    for (size_t i = 0; i < lists->align_count; i++) {
        const lists_item_t *s = &lists->align_stars[i];
        chip_add(column, ICON_STAR, s->name, s->sub, false, on_star_chosen, (void *)(intptr_t)i);
    }
}

// Adds up to `count` more chips; false once all are there. A list shows how many of its
// targets are up under its name; a target its designation, where that isn't its name; and
// after the targets, how many more are up, which the phone didn't send.
static bool add_chips(size_t count)
{
    const lists_t *lists = lists_lock();
    if (!lists) return false;
    // A new answer: the page is shown again from the start (menu_lists_changed).
    bool more_to_add = lists->version == shown;
    if (more_to_add && showing == SHOW_ALIGN) {
        add_align(lists);
        more_to_add = false;
    } else if (more_to_add && showing == SHOW_LISTS) {
        for (size_t end = next_chip + count; next_chip < end && next_chip < lists->list_count; next_chip++) {
            const lists_item_t *l = &lists->lists[next_chip];
            chip_add(column, ICON_LISTS, l->name, l->sub, false, on_list_chosen, (void *)(intptr_t)next_chip);
        }
        more_to_add = next_chip < lists->list_count;
    } else if (more_to_add) {
        bool ours = strcmp(lists->targets_of, chosen_list) == 0;
        size_t total = ours ? lists->target_count : 0;
        for (size_t end = next_chip + count; next_chip < end && next_chip < total; next_chip++) {
            const lists_item_t *t = &lists->targets[next_chip];
            chip_add(column, ICON_TARGET, t->name, t->sub[0] ? t->sub : NULL, false, on_target_chosen,
                     (void *)(intptr_t)next_chip);
        }
        more_to_add = next_chip < total;
        if (!more_to_add && ours && lists->more) {
            char more[16];
            snprintf(more, sizeof(more), "+%u", lists->more);
            chip_add(column, ICON_ADD, more, NULL, false, on_nothing, NULL);
        }
    }
    lists_unlock();
    chip_list_fit(column);
    return more_to_add;
}

static void on_turn(lv_timer_t *timer)
{
    if (add_chips(CHIPS_PER_TURN)) return;
    menu_lists_stop();
    // A page of 60 chips is the menu's biggest: how much of LVGL's memory it leaves.
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    ESP_LOGI(TAG, "LVGL memory: %u%% used, %u KB free", memory.used_pct, (unsigned)(memory.free_size / 1024));
}

static void start(lv_obj_t *list, showing_t what)
{
    menu_lists_stop();
    column = list;
    showing = what;
    remove_asked = false;
    next_chip = 0;
    shown = lists_version();
    if (add_chips(FIRST_CHIPS)) adder = lv_timer_create(on_turn, TURN_MS, NULL);
}

void menu_lists_init(void (*open_targets_page)(void), void (*close)(void))
{
    open_targets = open_targets_page;
    close_menu = close;
}

void menu_lists_show(lv_obj_t *list)
{
    start(list, SHOW_LISTS);
}

void menu_lists_show_targets(lv_obj_t *list)
{
    start(list, SHOW_TARGETS);
}

void menu_lists_show_align(lv_obj_t *list)
{
    start(list, SHOW_ALIGN);
}

void menu_lists_stop(void)
{
    if (adder) lv_timer_delete(adder);
    adder = NULL;
}

bool menu_lists_changed(void)
{
    return lists_version() != shown;
}

const char *menu_lists_chosen_name(void)
{
    return chosen_name;
}
