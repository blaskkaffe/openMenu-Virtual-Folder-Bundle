/* ui_list: navigation of the game list, see ui_list.h. */
#include <string.h>

#include <backend/gd_list.h>

#include "ui_list.h"

static int cursor;
static int top;

int
uil_is_folder(const gd_item* item) {
    return item && !strncmp(item->disc, "DIR", 3);
}

int
uil_count(void) {
    return list_length();
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
    return list_item_get(index);
}

void
uil_reset(void) {
    cursor = 0;
    top = 0;
}

static void
move_cursor(int delta) {
    int count = list_length();
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
    const gd_item* item = list_item_get(cursor);

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
            if (uil_is_folder(item)) {
                list_folder_enter(item->name, cursor);
                cursor = 0;
                top = 0;
                return UIL_REDRAW;
            }
            *launch = item;
            return UIL_LAUNCH;
        case BTN_B:
            if (!list_folder_is_root()) {
                int restored = list_folder_go_back();
                cursor = restored < 0 ? 0 : restored;
                top = cursor > UIL_VISIBLE / 2 ? cursor - UIL_VISIBLE / 2 : 0;
                move_cursor(0);
                return UIL_REDRAW;
            }
            return UIL_EXIT;
        default: return UIL_NONE;
    }
}
