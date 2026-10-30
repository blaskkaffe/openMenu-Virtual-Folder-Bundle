#pragma once
#include <stdint.h>

#include "iblue_link.h"

/* A fake adapter that answers like the brctl firmware (protocol 3): items INFO, STATUS, CONFIG,
 * PROFILES, GAME; writes to CONFIG apply at once; operations run when asked. */
extern const iblue_io board_io;

void board_reset(void);
void board_set_present(int on); /* off: the adapter is not there (or hides, no Bluetooth controller) */

/* inspect / drive */
int board_dst(int port, int src);
int board_map_size(int port);
int board_dev(int port);
int board_acc(int port);
int board_pairing(void);
int board_field(int port, int src, int which); /* 0 scale, 1 threshold, 2 deadzone, 3 turbo, 4 algo */
void board_set_dst(int port, int src, int dst);
void board_set_held(int port, uint32_t word0);
void board_set_plugged(int port, int on);
void board_set_connected(int port, int on);
int board_flash_dst(int port, int src); /* the saved configuration */
int board_stat(int which);              /* 0 saves, 1 write calls, 2 read calls, 3 ops */
int board_profile_used(int slot);
int board_profile_dst(int slot, int src);
const char* board_game(void);
int board_game_stat(int which);          /* 0 saves, 1 deletes, 2 a game has its own settings */
void board_corrupt_map_size(int port);   /* put an impossible map size in the live config */
