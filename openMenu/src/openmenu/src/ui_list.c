/* ui_list: navigation of the game list, see ui_list.h. */
#include <string.h>

#include <stdlib.h>

#include <backend/gd_list.h>
#include <openmenu_settings.h>

#include "ui_list.h"

static int cursor;
static int top;

/* Multi-disc "Compact" mode: discs 2 and up are left out of the list (vis[] holds the indexes
 * of the rows that remain), and starting a set opens a chooser with its discs (chooser = 1). */
static int* vis;
static int vis_count;
static int vis_valid;
static int chooser;
static int saved_cursor, saved_top;

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
    if (chooser) {
        return list_multidisc_length();
    }
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
uil_in_chooser(void) {
    return chooser;
}

/* Number of discs in the set the item belongs to (1 when it is not part of one). */
int
uil_disc_total(const gd_item* item) {
    return item && item->product[0] != '\0' && strncmp(item->disc, "DIR", 3) ? gd_item_disc_total(item->disc) : 1;
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
    if (chooser) {
        return index >= 0 && index < list_multidisc_length() ? list_get_multidisc()[index] : NULL;
    }
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
    chooser = 0;
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
    chooser = 0;
    vis_valid = 0;
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
        case BTN_LEFT: move_cursor(-UIL_VISIBLE); return UIL_REDRAW;
        case BTN_RIGHT: move_cursor(UIL_VISIBLE); return UIL_REDRAW;
        case BTN_A:
        case BTN_START:
            if (!item) {
                return UIL_NONE;
            }
            if (!chooser && uil_is_folder(item)) {
                list_folder_enter(item->name, real_index(cursor));
                cursor = 0;
                top = 0;
                vis_valid = 0;
                return UIL_REDRAW;
            }
            if (!chooser && is_set(item)) {
                if (sf_multidisc_grouping[0] == MULTIDISC_GROUPING_SAME_FOLDER && !list_folder_is_root()) {
                    list_set_multidisc_in_folder(item->product);
                } else {
                    list_set_multidisc(item->product);
                }
                saved_cursor = cursor;
                saved_top = top;
                chooser = 1;
                cursor = 0;
                top = 0;
                return UIL_REDRAW;
            }
            *launch = item;
            return UIL_LAUNCH;
        case BTN_B:
            if (chooser) {
                chooser = 0;
                cursor = saved_cursor;
                top = saved_top;
                return UIL_REDRAW;
            }
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
