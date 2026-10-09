/*
 * history: the recently played list and the remembered last game. Both live in the save file;
 * the shared game list code resolves the stored hashes back to games.
 */
#pragma once

#include <backend/gd_item.h>

/* Call right before a game starts: moves it to the front of the recent list and remembers it
 * (with the folder it was started from) as the last game. Saves if anything changed. */
void history_record(const gd_item* disc);

/* The first time the game browser opens: go to the folder of the remembered last game.
 * Returns its row in the shown list, or -1 when there is nothing to restore. */
int history_restore(void);

/* Recently played games, newest first, limited by the setting. NULL entries never occur. */
int history_recent_count(void);
const gd_item* history_recent(int index);
