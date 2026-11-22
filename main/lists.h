// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The phone's lists for the menu's Lists page, one list's targets, and the stars to align
// on, from the app's `lists`, `listTargets` and `alignStars` messages (the app's
// docs/puck.md, "Lists" and "Alignment"). The link writes them on core 0; the menu reads
// them on core 1, between lists_lock and lists_unlock.

#pragma once

#include <stddef.h>

#define LISTS_MAX 24          // lists offered: the collections, the user's own, two catalogues
#define LISTS_TARGETS_MAX 60  // as the app's PUCK_LIST_TARGETS_MAX
#define LISTS_ID_BYTES 48     // a user list's id is a UUID (36 characters)
#define LISTS_NAME_BYTES 64
#define LISTS_SUB_BYTES 24
#define LISTS_ALIGN_MAX 6 // the selected target and the 4 easiest stars

/** A list or a target: its id, its name, and the line under it (a count, a designation). */
typedef struct {
    char id[LISTS_ID_BYTES];
    char name[LISTS_NAME_BYTES];
    char sub[LISTS_SUB_BYTES];
} lists_item_t;

typedef struct {
    lists_item_t lists[LISTS_MAX];
    size_t list_count;
    /** The list whose targets these are, and its name, or "" before any came. */
    char targets_of[LISTS_ID_BYTES];
    char targets_name[LISTS_NAME_BYTES];
    lists_item_t targets[LISTS_TARGETS_MAX];
    size_t target_count;
    /** How many more targets are up than were sent. */
    unsigned more;
    /** The Alignment page: what the phone is aligned on ("" if nothing), the Remove chip's
     *  words before and after a first tap, and the stars to align on. */
    char align_current[LISTS_NAME_BYTES];
    char align_remove[LISTS_NAME_BYTES];
    char align_remove_again[LISTS_NAME_BYTES];
    lists_item_t align_stars[LISTS_ALIGN_MAX];
    size_t align_count;
    /** One more each time either changes, so the menu knows to show them again. */
    unsigned version;
} lists_t;

/** Reads `{"t":"lists","lists":[{"id","name","count"}]}`. */
void lists_parse_lists(const char *json);

/** Reads `{"t":"listTargets","id","name","targets":[{"id","name","sub"}],"more"}`. */
void lists_parse_targets(const char *json);

/** Reads `{"t":"alignStars","current","remove","removeAgain","stars":[{"id","name","sub"}]}`. */
void lists_parse_align(const char *json);

/** Forgets the targets, so a list opened shows none until the phone's answer comes. */
void lists_forget_targets(void);

/** The lists, held until lists_unlock: read them quickly, as the link waits meanwhile. */
const lists_t *lists_lock(void);
void lists_unlock(void);

/** The version now, without holding the lists. */
unsigned lists_version(void);
