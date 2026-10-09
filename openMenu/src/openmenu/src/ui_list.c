/* ui_list: navigation of the game list, see ui_list.h. */
#include <string.h>

#include <stdlib.h>

#include <backend/gd_list.h>
#include <openmenu_settings.h>

#include "ui_list.h"

static int cursor;
static int top;

/* Multi-disc "Compact" mode: discs 2 and up are left out of the list (vis[] holds the indexes
 * of the rows that remain). Left/Right on a set picks its disc, A starts the picked disc. */
static int* vis;
static int vis_count;
static int vis_valid;

#define MAX_SET_DISCS 10
static const gd_item* members[MAX_SET_DISCS]; /* discs of the set under the cursor, in disc order */
static int member_count;
static const gd_item* members_of; /* the list row `members` was built for */
static int disc_idx;              /* picked disc of that set */

static int
is_compact_hidden(const gd_item* it) {
    /* the shared list adds a "Recently played" folder row; recents have their own popup here */
    if (it && !strcmp(it->product, "RCNT")) {
        return 1;
    }
    return it && sf_multidisc[0] == MULTIDISC_HIDE && it->product[0] != '\0' && strncmp(it->disc, "DIR", 3)
           && gd_item_disc_num(it->disc) > 1 && gd_item_disc_total(it->disc) > 1;
}

static int
is_set(const gd_item* it) {
    return it && sf_multidisc[0] == MULTIDISC_HIDE && it->product[0] != '\0' && strncmp(it->disc, "DIR", 3)
           && gd_item_disc_total(it->disc) > 1;
}

static void
rebuild(void) {
    int n = list_length();
    free(vis);
    vis = n > 0 ? malloc((size_t)n * sizeof(int)) : NULL;
    vis_count = 0;
    for (int i = 0; vis && i < n; i++) {
        if (!is_compact_hidden(list_item_get(i))) {
            vis[vis_count++] = i;
        }
    }
    vis_valid = 1;
}

static int
count_now(void) {
    if (!vis_valid) {
        rebuild();
    }
    return vis_count;
}

int
uil_is_folder(const gd_item* item) {
    return item && !strncmp(item->disc, "DIR", 3);
}

int
uil_count(void) {
    return count_now();
}

int
uil_top(void) {
    return top;
}

int
uil_cursor(void) {
    return cursor;
}

const gd_item*
uil_item(int index) {
    if (!vis_valid) {
        rebuild();
    }
    return index >= 0 && index < vis_count ? list_item_get(vis[index]) : NULL;
}

/* Index into the real list of a visible row (folder entry and restore need it). */
static int
real_index(int index) {
    if (!vis_valid) {
        rebuild();
    }
    return index >= 0 && index < vis_count ? vis[index] : index;
}

static void move_cursor(int delta);

/* Put the cursor on a row of the real list (as returned by the list functions). */
void
uil_goto_real(int row) {
    vis_valid = 0;
    rebuild();
    cursor = 0;
    for (int i = 0; i < vis_count; i++) {
        if (vis[i] == row) {
            cursor = i;
        }
    }
    top = cursor > UIL_VISIBLE / 2 ? cursor - UIL_VISIBLE / 2 : 0;
    move_cursor(0);
}

void
uil_reset(void) {
    cursor = 0;
    top = 0;
    vis_valid = 0;
    members_of = NULL;
}

/* Collect the discs of the set `item` belongs to (disc 1 first). */
static void
load_members(const gd_item* item) {
    if (members_of == item) {
        return;
    }
    members_of = item;
    disc_idx = 0;
    member_count = 0;
    if (sf_multidisc_grouping[0] == MULTIDISC_GROUPING_SAME_FOLDER && !list_folder_is_root()) {
        list_set_multidisc_in_folder(item->product);
    } else {
        list_set_multidisc(item->product);
    }
    const gd_item** all = list_get_multidisc();
    int n = list_multidisc_length();
    for (int i = 0; i < n && member_count < MAX_SET_DISCS; i++) {
        const gd_item* d = all[i];
        int at = member_count++;
        while (at > 0 && gd_item_disc_num(members[at - 1]->disc) > gd_item_disc_num(d->disc)) {
            members[at] = members[at - 1];
            at--;
        }
        members[at] = d;
    }
    if (member_count == 0) {
        members[member_count++] = item;
    }
}

int
uil_disc_total(const gd_item* item) {
    return is_set(item) ? gd_item_disc_total(item->disc) : 1;
}

int
uil_disc_index(void) {
    const gd_item* it = uil_item(cursor);
    if (!is_set(it)) {
        return 0;
    }
    load_members(it);
    return disc_idx;
}

void
uil_set_cursor(int index) {
    move_cursor(index - cursor);
}

/* Jump to the next game whose title starts with the letter (digits: any title starting with a
 * digit); from the cursor on, wrapping around. Returns 1 if the cursor moved. */
int
uil_jump_to_letter(int c) {
    int n = count_now();
    for (int i = 1; i <= n; i++) {
        int at = (cursor + i) % n;
        const gd_item* it = uil_item(at);
        if (!it) {
            continue;
        }
        const char* name = it->name;
        while (*name == '[' || *name == ' ') { /* a folder is written [Name] */
            name++;
        }
        int first = *name;
        if (first >= 'a' && first <= 'z') {
            first -= 'a' - 'A';
        }
        if (first == c || (c >= '0' && c <= '9' && first >= '0' && first <= '9' && first == c)) {
            move_cursor(at - cursor);
            return 1;
        }
    }
    return 0;
}

static void
move_cursor(int delta) {
    int count = count_now();
    if (count <= 0) {
        return;
    }
    cursor += delta;
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > count - 1) {
        cursor = count - 1;
    }
    if (cursor < top) {
        top = cursor;
    }
    if (cursor >= top + UIL_VISIBLE) {
        top = cursor - UIL_VISIBLE + 1;
    }
}

uil_result
uil_button(button_t btn, const gd_item** launch) {
    const gd_item* item = uil_item(cursor);

    switch (btn) {
        case BTN_UP: move_cursor(-1); return UIL_REDRAW;
        case BTN_DOWN: move_cursor(1); return UIL_REDRAW;
        case BTN_PAGE_UP: move_cursor(-UIL_VISIBLE); return UIL_REDRAW;
        case BTN_PAGE_DOWN: move_cursor(UIL_VISIBLE); return UIL_REDRAW;
        case BTN_LEFT:
        case BTN_RIGHT:
            if (is_set(item)) { /* pick a disc of the set */
                load_members(item);
                int next = disc_idx + (btn == BTN_LEFT ? -1 : 1);
                if (next >= 0 && next < member_count) {
                    disc_idx = next;
                    return UIL_REDRAW;
                }
                return UIL_NONE;
            }
            move_cursor(btn == BTN_LEFT ? -UIL_VISIBLE : UIL_VISIBLE);
            return UIL_REDRAW;
        case BTN_A:
        case BTN_START:
            if (!item) {
                return UIL_NONE;
            }
            if (uil_is_folder(item)) {
                list_folder_enter(item->name, real_index(cursor));
                cursor = 0;
                top = 0;
                vis_valid = 0;
                return UIL_REDRAW;
            }
            if (is_set(item)) {
                load_members(item);
                *launch = members[disc_idx < member_count ? disc_idx : 0];
                return UIL_LAUNCH;
            }
            *launch = item;
            return UIL_LAUNCH;
        case BTN_B:
            if (!list_folder_is_root()) {
                int restored = list_folder_go_back();
                vis_valid = 0;
                rebuild();
                cursor = 0;
                for (int i = 0; i < vis_count; i++) {
                    if (vis[i] == restored) {
                        cursor = i;
                    }
                }
                top = cursor > UIL_VISIBLE / 2 ? cursor - UIL_VISIBLE / 2 : 0;
                move_cursor(0);
                return UIL_REDRAW;
            }
            return UIL_EXIT;
        default: return UIL_NONE;
    }
}
