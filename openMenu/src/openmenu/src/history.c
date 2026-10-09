/* history: see history.h. */
#include <stdint.h>
#include <string.h>

#include <backend/gd_item.h>
#include <backend/gd_list.h>
#include <openmenu_savefile.h>
#include <openmenu_settings.h>

#include "history.h"

/* Push the game to the front of the recent list. Returns 1 if the list changed. */
static int
recent_update(const gd_item* disc) {
    if (sf_recently_played[0] == RECENTLY_PLAYED_OFF) {
        return 0;
    }

    unsigned int hash = gd_item_recent_hash(disc);
    if (sf_recent_games_get(0) == hash) {
        return 0;
    }

    /* If it was played before, pull it out of the list; otherwise the oldest falls off. */
    int shift_from = sf_recent_games_slots - 1;
    for (int i = 1; i < sf_recent_games_slots; i++) {
        if (sf_recent_games_get(i) == hash) {
            shift_from = i;
            break;
        }
    }
    for (int i = shift_from; i > 0; i--) {
        sf_recent_games_set(i, sf_recent_games_get(i - 1));
    }
    sf_recent_games_set(0, hash);
    return 1;
}

/* Remember game, product code and folder. Returns 1 if anything changed. */
static int
last_game_update(const gd_item* disc) {
    if (sf_remember_last_game[0] != REMEMBER_LAST_GAME_ON) {
        return 0;
    }

    list_view view;
    unsigned int folder = 0;
    list_view_get(&view);
    if (view.kind == LIST_VIEW_FOLDER && view.folder_path && view.folder_path[0]) {
        folder = list_folder_path_hash(view.folder_path);
    }
    unsigned int game = gd_item_recent_hash(disc);

    if (sf_u32_read(sf_last_game) == game && sf_u32_read(sf_last_game_folder) == folder
        && !strncmp((const char*)sf_last_game_product, disc->product, sf_last_game_product_length - 1)) {
        return 0;
    }

    sf_u32_write(sf_last_game, game);
    sf_u32_write(sf_last_game_folder, folder);
    strncpy((char*)sf_last_game_product, disc->product, sf_last_game_product_length - 1);
    sf_last_game_product[sf_last_game_product_length - 1] = '\0';
    return 1;
}

void
history_record(const gd_item* disc) {
    if (!disc || !strncmp(disc->disc, "DIR", 3)) {
        return;
    }
    int changed = recent_update(disc);
    changed |= last_game_update(disc);
    if (changed) {
        savefile_save(); /* best effort, the launch goes ahead either way */
    }
}

/* Row of the game in the shown list; any disc of the same set counts (Compact hides some). */
static int
row_for(const gd_item* item) {
    int row = list_index_of(item);
    if (row < 0 && item->product[0]) {
        row = list_index_of_product(item->product);
    }
    return row;
}

int
history_restore(void) {
    unsigned int hash = sf_u32_read(sf_last_game);
    if (sf_remember_last_game[0] != REMEMBER_LAST_GAME_ON || !hash) {
        return -1;
    }

    const gd_item* item = list_find_by_hash(hash);
    if (!item) {
        item = list_find_by_product((const char*)sf_last_game_product);
    }
    if (!item) {
        return -1;
    }

    char path[LIST_FOLDER_PATH_MAX];
    int found = 0;
    unsigned int saved = sf_u32_read(sf_last_game_folder);
    if (saved && list_folder_path_by_hash(saved, path, sizeof(path))) {
        found = list_folder_contains(path, item);
    }
    if (!found) { /* the folder is gone: try where the game says it lives */
        strncpy(path, item->folder, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        found = list_folder_contains(path, item);
    }
    if (found && path[0]) {
        list_folder_enter_path(path);
    }

    int row = row_for(item);
    if (row < 0) {
        list_set_folder_root();
    }
    return row;
}

int
history_recent_count(void) {
    return sf_recently_played[0] == RECENTLY_PLAYED_OFF ? 0 : list_recent_count();
}

const gd_item*
history_recent(int index) {
    int n = 0;
    gd_item** e = list_recent_entries(&n);
    return index >= 0 && index < n ? e[index] : NULL;
}
