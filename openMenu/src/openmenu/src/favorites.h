/*
 * favorites: the games the player has starred. openMenu 1.7.1 has no such list, and OPENMENU.SYS is shared with that
 * menu (it must not grow), so this has a file of its own: the games' identity hashes (gd_item_recent_hash, so a game
 * is still found when the SD card order changes), newest first, at most FAV_MAX. It is kept on the SD card
 * (/sd/OPENMENU/FAVORITES.DAT) when one is mounted, and on a memory card as the VMS file OPENMENU.FAV (3 blocks, a gold
 * icon) so it also survives on a GDEMU console. On start the SD copy wins, else the first card that has the file.
 */
#pragma once

#include <backend/gd_item.h>

#define FAV_MAX 100

/* Read the file; call after the game list is loaded (stored hashes are matched to games). Safe to call again. */
void favorites_init(void);

/* The starred games that are on this card, newest first. */
int favorites_count(void);
const gd_item* favorites_item(int index);
int favorites_is(const gd_item* item);

/* Star or unstar. Returns 1 (now starred), 0 (no longer starred) or -1 (the list is full, nothing changed). The change is
 * saved at once; favorites_save_ok() says whether that worked (it stays in memory either way). */
int favorites_toggle(const gd_item* item);
int favorites_save_ok(void);

/* Settings of this menu that 1.7.1 does not have, kept in the same file (its header has a spare byte) because OPENMENU.SYS is shared with
 * 1.7.1 and must not hold anything it does not know: bit 0 is the Swatch .beat clock, bits 1-3 the background colour (bg_palette.h), bit 4 dark mode. Valid after favorites_init(). Setting saves at once. */
#define FORK_FLAG_BEAT 1
#define FORK_BG_SHIFT 1
#define FORK_BG_MASK 0x0E
#define FORK_FLAG_DARK 0x10
unsigned favorites_flags(void);
void favorites_set_flags(unsigned flags);
